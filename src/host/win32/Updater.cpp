// SPDX-License-Identifier: GPL-3.0-or-later
#include "host/win32/Updater.h"

#ifdef SENTINELIDE_HAVE_WINSPARKLE

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
// Generate with `generate_keys.exe` from the WinSparkle release zip
// (https://github.com/vslavik/winsparkle/releases); it keeps the private key in the
// Windows credential store and prints the public half. Paste that here.
// The private key must never enter this repo.
constexpr char kEdDsaPublicKey[] = "@@SENTINEL_IDE_ED25519_PUBLIC_KEY@@";

// True once a real key has been pasted above. Until then we refuse to run any check
// rather than run one that cannot verify a signature — an unconfigured updater that
// silently reports "up to date" is worse than a visibly absent one.
bool haveSigningKey() {
    return kEdDsaPublicKey[0] != '@';
}

HWND g_mainWnd = nullptr;
bool g_started = false;

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
    if (g_mainWnd) PostMessageW(g_mainWnd, WM_CLOSE, 0, 0);
    std::thread([] {
        Sleep(3000);
        logMsg(LogLevel::Warn, L"Updater: clean exit did not complete — forcing process exit");
        ExitProcess(0);
    }).detach();
}

int onCanShutdown() { return 1; }

}  // namespace

bool updaterAvailable() { return haveSigningKey(); }

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
               L"It must be the bare base64 line printed by generate_keys.exe. See docs/RELEASING.md.");
        return;
    }
    win_sparkle_set_can_shutdown_callback(onCanShutdown);
    win_sparkle_set_shutdown_request_callback(onShutdownRequest);
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
    win_sparkle_check_update_with_ui();
}

void shutdownUpdater() {
    if (g_started) win_sparkle_cleanup();
}

}  // namespace sentinelide

#else  // ---- built without WinSparkle (SENTINELIDE_UPDATER=OFF) ----------------

namespace sentinelide {
bool updaterAvailable() { return false; }
void initUpdater(HWND) {}
void checkForUpdates(HWND) {}
void shutdownUpdater() {}
}  // namespace sentinelide

#endif
