// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/win32/Updater.h"

#ifdef SENTINELIDE_HAVE_WINSPARKLE

#include <shellapi.h>   // ShellExecuteExW
#include <wininet.h>    // our own appcast poll (see startOwnUpdateTimer) — we launch the update installer ourselves

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

// ---- our own periodic check ------------------------------------------------
// WinSparkle's built-in periodic check is disabled in initUpdater, because when it finds
// something it raises WinSparkle's own prompt — the flow that hands user_run_installer an
// empty path and installs nothing. So we poll the appcast ourselves and, if it names a newer
// version, ask the main window to offer it. Accepting routes into checkForUpdates(), i.e.
// win_sparkle_check_update_with_ui_and_install(), which is the path that actually works.
//
// This does NOT weaken anything: the only decision made here is whether to OFFER an update.
// The download, the Ed25519 signature check against the compiled-in public key, and the
// install are all still WinSparkle's. A tampered feed can at worst make us offer an update
// that WinSparkle then refuses to install.

// Parse "a.b.c.d" into four numbers. Missing components read as 0, so "0.1.6" == "0.1.6.0".
void parseVersion(const char* v, int out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0;
    int i = 0;
    while (v && *v && i < 4) {
        while (*v >= '0' && *v <= '9') { out[i] = out[i] * 10 + (*v - '0'); ++v; }
        if (*v == '.') { ++v; ++i; } else break;
    }
}
bool versionIsNewer(const char* candidate, const char* mine) {
    int a[4], b[4];
    parseVersion(candidate, a);
    parseVersion(mine, b);
    for (int i = 0; i < 4; ++i) { if (a[i] != b[i]) return a[i] > b[i]; }
    return false;
}

// GET the appcast. Small, plain, and failure just means "try again next tick".
bool fetchAppcast(std::string& body) {
    HINTERNET net = InternetOpenW(L"Sentinel-IDE", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return false;
    // RELOAD/NO_CACHE_WRITE or WinINet will happily serve us a cached feed forever.
    const DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                        INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_UI;
    HINTERNET url = InternetOpenUrlA(net, kAppcastUrl, nullptr, 0, flags, 0);
    if (!url) { InternetCloseHandle(net); return false; }
    char buf[2048];
    DWORD got = 0;
    while (InternetReadFile(url, buf, sizeof(buf), &got) && got > 0) {
        body.append(buf, got);
        if (body.size() > 256 * 1024) break;   // a sane cap; the real feed is well under 1 KB
    }
    InternetCloseHandle(url);
    InternetCloseHandle(net);
    return !body.empty();
}

// Pull sparkle:version="..." out of the feed. We publish this file, so a targeted match
// beats dragging in an XML parser.
std::string appcastVersion(const std::string& body) {
    const std::string key = "sparkle:version=\"";
    const size_t a = body.find(key);
    if (a == std::string::npos) return {};
    const size_t b = body.find('"', a + key.size());
    if (b == std::string::npos) return {};
    return body.substr(a + key.size(), b - (a + key.size()));
}

std::atomic<bool> g_timerRunning{false};

void startOwnUpdateTimer(HWND mainWnd) {
    if (g_timerRunning.exchange(true)) return;
    std::thread([mainWnd] {
        // Let the app finish starting before touching the network.
        Sleep(90 * 1000);
        for (;;) {
            std::string body;
            if (fetchAppcast(body)) {
                const std::string ver = appcastVersion(body);
                if (!ver.empty() && versionIsNewer(ver.c_str(), SENTINEL_FILEVERSION_STR)) {
                    logMsg(LogLevel::Info, L"Updater: periodic check found a newer version in the appcast");
                    const std::wstring w(ver.begin(), ver.end());   // ASCII version string
                    PostMessageW(mainWnd, WM_APP_UPDATE_AVAILABLE, 0, (LPARAM)_wcsdup(w.c_str()));
                    return;   // offered once per run; the user decides from here
                }
            }
            Sleep(24 * 60 * 60 * 1000);   // daily thereafter
        }
    }).detach();
}

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
    // Turn WinSparkle's own periodic check OFF. When it finds an update it raises its own
    // prompt, and that prompt's install path is the broken one (empty payload path, installs
    // nothing). We replace it with startOwnUpdateTimer below, which routes an accepted offer
    // through the working entry point instead. Without this the user gets two update prompts,
    // one of which silently does nothing.
    win_sparkle_set_automatic_check_for_updates(0);
    win_sparkle_init();
    g_started = true;
    startOwnUpdateTimer(mainWnd);
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
