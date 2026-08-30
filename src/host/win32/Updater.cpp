// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/win32/Updater.h"

#ifdef SENTINELIDE_HAVE_WINSPARKLE

#include <shellapi.h>   // ShellExecuteExW — we launch the update installer ourselves

#include <atomic>
#include <string>
#include <thread>
#include <winsparkle.h>

#include "Version.h"
#include "core/Logger.h"

namespace sentinelide {
namespace {

// The appcast feed. Served by raw.githubusercontent.com straight off `main`, so
// publishing a release is "commit the regenerated appcast.xml" — no extra hosting.
// This only resolves because the repo is PUBLIC; against a private repo GitHub
// returns 404 to WinSparkle's unauthenticated GET and every check silently finds
// nothing. See docs/RELEASING.md.
constexpr char kAppcastUrl[] =
    "https://raw.githubusercontent.com/arcanii/Sentinel-IDE/main/appcast.xml";

// Ed25519 public key whose private half signs the installer. WinSparkle refuses any
// download that does not verify against this, which is the ONLY thing standing
// between an update check and running an attacker's binary — the feed is plain
// HTTPS off a CDN, so transport security alone is not the guarantee here.
//
// Generate with `winsparkle-tool.exe generate-key --file <path>\sentinel-ide.key` from the
// WinSparkle 0.9.3 release zip (https://github.com/vslavik/winsparkle/releases); it prints
// the base64 public half to paste here. NOTE the private key is a FILE on disk (0.9.3 does
// not use the Windows credential store, and no longer ships the older generate_keys.exe) —
// keep it outside this repo. See docs/RELEASING.md.
//
// ⚠ THIS VALUE IS NOW LOAD-BEARING FOR EVERY INSTALLED COPY. Changing it orphans every
// client already in the field: they will reject updates signed by any other key, and the
// only recovery is a manual re-install by each user. Rotate only with a deliberate plan.
constexpr char kEdDsaPublicKey[] = "rngndWCYiDprBlzu6ZkzEfnGL1gI0s7k8QwttQuakVQ=";

// True once a real key has been pasted above. Until then we refuse to run any check
// rather than run one that cannot verify a signature — an unconfigured updater that
// silently reports "up to date" is worse than a visibly absent one.
bool haveSigningKey() {
    return kEdDsaPublicKey[0] != '@';
}

HWND g_mainWnd = nullptr;
bool g_started = false;
// Set before the WM_CLOSE below is posted, so the main window can tell an update
// install apart from a user close and skip the unsaved-changes prompt (it would sit
// unanswered until the watchdog killed the process, losing the very edits it asked about).
std::atomic<bool> g_shutdownPending{false};

// WinSparkle asks the app to quit so it can run the downloaded installer, and waits
// on our process handle before starting it. If we do not actually exit, the
// installer cannot overwrite the locked exe and the update fails.
//
// The trap: every modal dialog here runs its own nested `GetMessageW` loop. WM_CLOSE
// -> WM_DESTROY -> PostQuitMessage posts WM_QUIT, the NESTED loop consumes it, and
// runApp's outer loop then blocks forever on a queue that will never see another
// WM_QUIT. The dialogs now re-post it (see the `<= 0` arm in each dialog loop), which
// is the real fix; this watchdog is the backstop for anything that still wedges —
// a stuck worker thread, a system modal, a dialog added later that forgets the arm.
// Ugly, but the alternative failure is a half-applied update.
void onShutdownRequest() {
    logMsg(LogLevel::Info, L"Updater: shutdown requested — closing for update install");
    g_shutdownPending = true;
    if (g_mainWnd) PostMessageW(g_mainWnd, WM_CLOSE, 0, 0);
    std::thread([] {
        Sleep(3000);
        logMsg(LogLevel::Warn, L"Updater: clean exit did not complete — forcing process exit");
        ExitProcess(0);
    }).detach();
}

int onCanShutdown() { return 1; }

// ---- diagnostics ----------------------------------------------------------
// WinSparkle reports failures ONLY through these callbacks. Without them a failed
// update is completely silent: the app cannot know, and the log shows nothing between
// "shutdown requested" and the next launch still on the old version. That silence is
// how a broken install path survived four releases unnoticed.
void onError() {
    logMsg(LogLevel::Error, L"Updater: WinSparkle reported an error (check/download/verify/install failed)");
}
void onDidFindUpdate()    { logMsg(LogLevel::Info,  L"Updater: update found"); }
void onDidNotFindUpdate() { logMsg(LogLevel::Info,  L"Updater: no update available"); }
void onUpdateCancelled()  { logMsg(LogLevel::Info,  L"Updater: update cancelled by user"); }
void onUpdateDismissed()  { logMsg(LogLevel::Info,  L"Updater: update dialog dismissed"); }

// Called once the payload is downloaded and SIGNATURE-VERIFIED, with its path,
// immediately before WinSparkle would execute it. Return 1 for "handled by me",
// 0 for "do your default thing".
//
// WE RUN IT OURSELVES, because WinSparkle's own execute step does not work here:
// it downloads and verifies correctly, then launches nothing and reports no error
// (its error callback never fires). Measured 2026-08-30 — every release v0.1.0..v0.1.4
// offered updates that could never install. Proven by taking WinSparkle's own
// downloaded payload, from its own temp dir, and running it by hand: installs fine,
// byte-identical to the published installer. So the artifact, the feed, the download
// and the Ed25519 verification are all sound; only the launch was broken.
//
// This costs no security: WinSparkle verifies the signature against the compiled-in
// public key BEFORE calling us, and refuses to call us at all if it fails.
int onUserRunInstaller(const wchar_t* path) {
    const std::wstring p = path ? path : L"(null)";
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    const bool exists = path && *path && GetFileAttributesExW(path, GetFileExInfoStandard, &fad);
    unsigned long long size = 0;
    if (exists) size = (static_cast<unsigned long long>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    logMsg(LogLevel::Info, L"Updater: payload ready — [" + p + L"]  exists=" +
           (exists ? L"yes" : L"NO") + L" size=" + std::to_wstring(size));

    if (!exists) {
        // Nothing we can launch. Fall back rather than pretend we handled it.
        logMsg(LogLevel::Error, L"Updater: no usable payload path from WinSparkle — "
                                L"falling back to its (known-broken) handling; update will not install");
        return 0;
    }

    // /SILENT gives a progress window but no wizard pages — the Sparkle-style
    // experience. The install SCOPE must be explicit: the .iss sets
    // PrivilegesRequiredOverridesAllowed=dialog, so without /CURRENTUSER or /ALLUSERS
    // Inno stops on its "Select Setup Install Mode" dialog — and a silent launch
    // sitting on a modal question is just a hang. (Measured: that is exactly where the
    // first version of this fix stalled.) Pick the scope this copy actually lives in,
    // so an update never installs a second copy into the other scope.
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t localAppData[MAX_PATH]{};
    const DWORD ladLen = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    const bool perUser = ladLen > 0 && ladLen < MAX_PATH &&
                         _wcsnicmp(exePath, localAppData, ladLen) == 0;
    const wchar_t* args = perUser ? L"/SILENT /NORESTART /CURRENTUSER"
                                  : L"/SILENT /NORESTART /ALLUSERS";
    logMsg(LogLevel::Info, std::wstring(L"Updater: install scope = ") +
           (perUser ? L"per-user (/CURRENTUSER)" : L"per-machine (/ALLUSERS)"));

    // SEE_MASK_NOASYNC matters because WinSparkle asks us to quit immediately after
    // this returns; without it the shell call can be abandoned as the process dies.
    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOASYNC | SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"open";
    sei.lpFile       = path;
    sei.lpParameters = args;
    sei.nShow        = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        logMsg(LogLevel::Info, L"Updater: installer launched — handing off and quitting");
        return 1;   // handled
    }
    logMsg(LogLevel::Error, L"Updater: ShellExecuteEx failed for the installer, GetLastError=" +
           std::to_wstring(GetLastError()));
    return 0;
}

}  // namespace

bool updaterAvailable() { return haveSigningKey(); }
bool updaterShutdownPending() { return g_shutdownPending.load(); }

void initUpdater(HWND mainWnd) {
    g_mainWnd = mainWnd;
    if (!haveSigningKey()) {
        logMsg(LogLevel::Warn,
               L"Updater: no EdDSA public key compiled in — auto-update disabled. "
               L"See docs/RELEASING.md.");
        return;
    }
    win_sparkle_set_appcast_url(kAppcastUrl);
    // Report marketing.build (e.g. "0.1.0.23") so it compares against the appcast's
    // sparkle:version. The marketing version alone would never advance and every
    // build would look identical to the feed.
    win_sparkle_set_app_details(L"Sentinel", L"Sentinel-IDE", SENTINEL_FILEVERSION_STR_W);
    // Returns 0 if the string is not a valid base64 EdDSA key. Checked, because a
    // mistyped key is otherwise SILENT: WinSparkle would fall back to looking for an
    // "EdDSAPub"/"EDDSA" Windows resource, which this exe does not ship, leaving the
    // updater running with no trust anchor at all. Refuse to start instead.
    if (!win_sparkle_set_eddsa_public_key(kEdDsaPublicKey)) {
        logMsg(LogLevel::Error,
               L"Updater: WinSparkle rejected the EdDSA public key — auto-update disabled. "
               L"It must be the bare base64 line printed by `winsparkle-tool generate-key`. "
               L"See docs/RELEASING.md.");
        return;
    }
    win_sparkle_set_can_shutdown_callback(onCanShutdown);
    win_sparkle_set_shutdown_request_callback(onShutdownRequest);
    // Diagnostics — see the block above. Cheap, and the only way a failed update is
    // ever visible.
    win_sparkle_set_error_callback(onError);
    win_sparkle_set_did_find_update_callback(onDidFindUpdate);
    win_sparkle_set_did_not_find_update_callback(onDidNotFindUpdate);
    win_sparkle_set_update_cancelled_callback(onUpdateCancelled);
    win_sparkle_set_update_dismissed_callback(onUpdateDismissed);
    win_sparkle_set_user_run_installer_callback(onUserRunInstaller);
    win_sparkle_init();   // also runs the periodic background check
    g_started = true;
    logMsg(LogLevel::Info, std::wstring(L"Updater: initialised (") + SENTINEL_FILEVERSION_STR_W + L")");
}

void checkForUpdates(HWND owner) {
    if (!g_started) {
        MessageBoxW(owner,
                    L"Auto-update isn't configured in this build.\n\n"
                    L"No update-signing key was compiled in, so an update could not be "
                    L"verified even if one were found. See docs/RELEASING.md.",
                    L"Sentinel-IDE", MB_OK | MB_ICONINFORMATION);
        return;
    }
    // NOT win_sparkle_check_update_with_ui(). That is the prompt-then-install flow, and
    // in 0.9.3 it calls our user_run_installer callback with an EMPTY path ~0.5s after
    // finding the update — i.e. before it has downloaded anything — and then launches
    // nothing and reports no error. Measured repeatedly on 2026-08-30; it is why every
    // release v0.1.0..v0.1.4 offered updates that could never install.
    //
    // This variant downloads first and hands the callback a real, verified payload,
    // which we then run ourselves. It skips the "do you want to update?" prompt, which
    // is acceptable here because the user reached this by explicitly asking to check.
    win_sparkle_check_update_with_ui_and_install();
}

void shutdownUpdater() {
    if (g_started) win_sparkle_cleanup();
}

}  // namespace sentinelide

#else  // ---- built without WinSparkle (SENTINELIDE_UPDATER=OFF) ----------------

namespace sentinelide {
bool updaterAvailable() { return false; }
bool updaterShutdownPending() { return false; }
void initUpdater(HWND) {}
void checkForUpdates(HWND) {}
void shutdownUpdater() {}
}  // namespace sentinelide

#endif
