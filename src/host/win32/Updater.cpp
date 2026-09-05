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

// ---- reading the feed ------------------------------------------------------
// Everything the appcast reader answers, in one crossing. The flags NEST — newer
// implies valid implies found — so a caller that consults only `newer` is already
// fail-closed, and `version` is populated only when `valid`.
struct AppcastVerdict {
    bool found = false;      // sparkle:version="…" located (opening AND closing quote)
    bool valid = false;      // ... and what sat between them is a well-formed version
    bool newer = false;      // ... and it is strictly newer than the running build
    std::string version;     // the version — ONLY when valid; empty otherwise
};

#ifdef SENTINELIDE_SENTINEL
// THE APPCAST READER IS SENTINEL — src/sentinel/parsers.sentinel::parse_appcast. It is
// the fifth and last of the IDE's readers to move out of C++, and the only one whose
// bytes arrive off the NETWORK (fetchAppcast below, over deliberately unauthenticated
// HTTPS). One call does the find, the VALIDATION and the compare: the check runs
// hourly and on a menu click, so a single FFI crossing costs nothing and it retires
// all three C++ functions that used to live here — parseVersion, versionIsNewer,
// appcastVersion.
//
// It is not a transcription. Two defects in the code it replaces close by
// construction there, and the long comment above parse_appcast is the record of both:
//   * parseVersion accumulated `out[i] = out[i] * 10 + (*v - '0')` into an `int` over
//     however many digits the feed supplied — UB, and in practice a wrap, so a crafted
//     or corrupt feed could make a LOWER version compare as newer or hide a real one.
//   * appcastVersion returned whatever sat between the quotes, unchecked, up to the
//     ~256 KB fetch cap — and it was then shown, logged, and on "Skip this version"
//     persisted into settings.ini.
//
// The fetch STAYS in C++: fetchAppcast is a thin wrapper over WinINet, so porting it
// would move the FFI boundary without moving any logic. Native fetch, Sentinel parse —
// the same split the other four ports use.
//
// Held to the code that shipped by tests/appcast_xcheck.cpp, which asserts parity with
// the old C++ where the old C++ was right and names every place they deliberately
// disagree (the two defects are exactly those places).
#include "sentinel_parsers.h"   // generated by snc: parse_appcast() + sentinel_free_bytes()
AppcastVerdict readAppcast(const std::string& body, const std::string& mine) {
    AppcastVerdict v;
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_appcast((const uint8_t*)body.data(), (int64_t)body.size(),
                  (const uint8_t*)mine.data(), (int64_t)mine.size(), &out, &olen);
    // Record: [found][valid][newer][len i64 LE][version bytes] — 11 bytes minimum.
    if (out && olen >= 11) {
        v.found = out[0] == 1;
        v.valid = out[1] == 1;
        v.newer = out[2] == 1;
        uint64_t n = 0;
        for (int i = 0; i < 8; i++) n |= (uint64_t)out[3 + i] << (8 * i);
        if (v.valid && n > 0 && (uint64_t)olen >= 11 + n)
            v.version.assign((const char*)out + 11, (size_t)n);
    }
    if (out) sentinel_free_bytes(out);
    return v;
}
#else
// C++ fallback for a snc-less build (parsers.lib absent), as all four earlier ports
// keep one. It is deliberately NOT the code that shipped: restoring that would put the
// overflow this port exists to delete back into a real configuration. It implements
// parse_appcast's rules, and tests/appcast_xcheck.cpp carries a verbatim copy and
// asserts it agrees with Sentinel on every case — so the duplication is pinned by a
// test rather than trusted.

// The version grammar, and the only one accepted: 1..4 components, 1..9 digits each,
// single dots, nothing else. Nine digits caps a component at 999,999,999, so
// acVersionComp's accumulate cannot overflow a long long — and a longer component
// rejects the WHOLE version rather than wrapping or being truncated, because a number
// the feed never stated is exactly what decides an update the wrong way.
bool acVersionValid(const std::string& v) {
    if (v.empty() || v.size() > 39) return false;
    int dots = 0, digits = 0;
    for (char c : v) {
        if (c == '.') {
            if (digits == 0 || ++dots > 3) return false;   // empty component / 5th component
            digits = 0;
        } else if (c >= '0' && c <= '9') {
            if (++digits > 9) return false;                // the overflow bound
        } else {
            return false;                                  // not a digit or a dot
        }
    }
    return digits > 0;                                     // rejects a trailing '.'
}
// Component k (0-based); missing trailing components read as 0, so "0.1.6" == "0.1.6.0".
// PRECONDITION: acVersionValid(v). The `digits < 9` guard is therefore unreachable and
// is written anyway, so this cannot overflow for any input at all.
long long acVersionComp(const std::string& v, int k) {
    int cur = 0, digits = 0;
    long long acc = 0;
    for (char c : v) {
        if (c == '.') { cur++; digits = 0; }
        else if (cur == k && c >= '0' && c <= '9' && digits < 9) { acc = acc * 10 + (c - '0'); digits++; }
    }
    return acc;
}
AppcastVerdict readAppcast(const std::string& body, const std::string& mine) {
    AppcastVerdict v;
    // FIRST match, not highest — see parse_appcast for why. Searching for the name
    // without the quote and then requiring the quote reproduces find()ing the whole
    // `sparkle:version="` literal: an unquoted occurrence is not a match and the scan
    // continues past it, but once the quote is seen the search stops either way.
    const std::string key = "sparkle:version=";
    for (size_t at = body.find(key); at != std::string::npos; at = body.find(key, at + 1)) {
        if (at + key.size() >= body.size() || body[at + key.size()] != '"') continue;
        const size_t s = at + key.size() + 1;
        const size_t q = body.find('"', s);
        if (q != std::string::npos) { v.found = true; v.version.assign(body, s, q - s); }
        break;
    }
    if (!v.found) return v;
    v.valid = acVersionValid(v.version);
    if (!v.valid) { v.version.clear(); return v; }   // an invalid version is never handed back
    if (!acVersionValid(mine)) return v;             // our own version unreadable -> nothing is newer
    for (int i = 0; i < 4; i++) {
        const long long a = acVersionComp(v.version, i), b = acVersionComp(mine, i);
        if (a != b) { v.newer = a > b; break; }
    }
    return v;
}
#endif

// GET the appcast. Small, plain, and failure just means "try again next tick".
bool fetchAppcast(std::string& body) {
    HINTERNET net = InternetOpenW(L"Sentinel-IDE", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) return false;
    // Bound the wait. An unroutable feed took 21.7s to report failure on the default stack
    // timeouts (measured against 10.255.255.1) — the UI stays live and the status bar says
    // "Checking for updates…", so it is not a hang, but nothing in the code bounded it. A
    // manual check is someone standing there waiting; 8s to a verdict beats 22s.
    DWORD toMs = 8000;
    InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &toMs, sizeof(toMs));
    InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &toMs, sizeof(toMs));
    InternetSetOptionW(net, INTERNET_OPTION_SEND_TIMEOUT, &toMs, sizeof(toMs));
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

std::atomic<bool> g_timerRunning{false};

// Check shortly after startup, then hourly. The startup delay is only a settle — it keeps
// the first network call off the window-creation path — not a policy choice. Hourly (rather
// than daily) so a release published mid-session is noticed within the hour; the request is a
// ~1 KB GET, so the cost is negligible either way.
constexpr DWORD kStartupSettleMs = 10 * 1000;
constexpr DWORD kCheckIntervalMs = 60 * 60 * 1000;

void startOwnUpdateTimer(HWND mainWnd) {
    if (g_timerRunning.exchange(true)) return;
    std::thread([mainWnd] {
        Sleep(kStartupSettleMs);
        for (;;) {
            std::string body;
            if (fetchAppcast(body)) {
                // `newer` already implies the version was found AND is well-formed, so
                // there is no separate emptiness test to forget here any more.
                const AppcastVerdict v = readAppcast(body, SENTINEL_FILEVERSION_STR);
                if (v.newer) {
                    logMsg(LogLevel::Info, L"Updater: periodic check found a newer version in the appcast");
                    const std::wstring w(v.version.begin(), v.version.end());   // validated: digits and dots
                    PostMessageW(mainWnd, WM_APP_UPDATE_AVAILABLE, 0, (LPARAM)_wcsdup(w.c_str()));
                    return;   // offered once per run — keep checking, but never nag twice
                }
            }
            Sleep(kCheckIntervalMs);
        }
    }).detach();
}

// ---- the manual check -----------------------------------------------------
// Same fetch as the background poll, three differences, all of them the point:
//   1. it always reports — up to date and "could not reach the feed" are outcomes the user
//      must see, where the background poll is allowed to shrug and retry in an hour;
//   2. it posts wParam 1, which tells the host to ignore [update] skip_version;
//   3. it is guarded, so a double-click cannot start two threads or raise two dialogs.
// It does NOT install anything by itself: that is checkForUpdates(), reached only if the
// user picks Install now in the offer this raises.
std::atomic<bool> g_manualCheckInFlight{false};

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
    // This variant downloads first and hands the callback a real, verified payload, which we
    // then run ourselves. It skips the "do you want to update?" prompt and then REPLACES THE
    // RUNNING APP, which is why it is no longer wired to the menu item: ≡ ▸ Check for Updates…
    // goes to checkForUpdatesInteractive(), and only an explicit "Install now" lands here.
    win_sparkle_check_update_with_ui_and_install();
}

void endInteractiveUpdateCheck() { g_manualCheckInFlight = false; }
bool updaterManualCheckInFlight() { return g_manualCheckInFlight.load(); }

bool checkForUpdatesInteractive(HWND owner) {
    if (!g_started) {
        // Synchronous on purpose: nothing was started, so there is no outcome to wait for,
        // and the caller is already on the UI thread inside its own WM_COMMAND.
        MessageBoxW(owner,
                    L"Auto-update isn't configured in this build.\n\n"
                    L"No update-signing key was compiled in, so an update could not be "
                    L"verified even if one were found. See docs/RELEASING.md.",
                    L"Sentinel-IDE", MB_OK | MB_ICONINFORMATION);
        return true;   // handled, and visibly so — nothing is in flight to collide with
    }
    // Second click while one is running: do nothing at all. Not a queued check, not a second
    // thread — the first one's answer is already on its way and will be shown. The guard is
    // released by endInteractiveUpdateCheck() once the UI has ACTED on that answer, so this
    // also covers the window in which the offer is parked behind a modal.
    if (g_manualCheckInFlight.exchange(true)) {
        logMsg(LogLevel::Info, L"Updater: a manual check is already running — ignoring the second request");
        return false;
    }
    // Post to the MAIN window, not to `owner`. Owner may be the About box, which is modal;
    // routing through the main window means the outcome goes through its uiIsBusy deferral
    // instead of stacking a message box on top of a dialog (the phase-41 defect).
    HWND target = g_mainWnd ? g_mainWnd : owner;
    if (!target) { g_manualCheckInFlight = false; return true; }

    // OFF THE UI THREAD. The GET is small but the network is not ours to promise anything
    // about; blocking the message loop on it would freeze the window, and a DNS black hole
    // would freeze it for the resolver's timeout, not ours.
    std::thread([target] {
        std::string body;
        AppcastVerdict v;
        bool ok = fetchAppcast(body);
        if (ok) {
            v = readAppcast(body, SENTINEL_FILEVERSION_STR);
            // A 200 carrying something that is not our appcast (a GitHub 404 page, a captive
            // portal's login form) is a FAILURE, not "no update" — reporting it as up to date
            // is exactly the silence that hid a broken updater for four releases.
            //
            // A feed that DOES carry sparkle:version but whose value is not a version — junk,
            // an empty string, a 14-digit component — is the same kind of failure, and it used
            // to be neither: the old reader handed those bytes straight to the offer dialog,
            // the log and settings.ini. Say which of the two happened; "unreadable feed" and
            // "feed with a bad version in it" are different problems to go and look at.
            if (!v.valid) {
                ok = false;
                if (v.found)
                    logMsg(LogLevel::Error, L"Updater: the appcast's sparkle:version is not a "
                                            L"well-formed version — refusing to offer it");
            }
        }
        UINT msg = WM_APP_UPDATE_CHECK_RESULT;
        WPARAM wp = kUpdateCheckFailed;
        LPARAM lp = 0;
        if (!ok) {
            logMsg(LogLevel::Error, L"Updater: manual check could not read the appcast "
                                    L"(feed unreachable, or the response was not an appcast)");
        } else if (v.newer) {
            const std::wstring w(v.version.begin(), v.version.end());   // validated: digits and dots
            logMsg(LogLevel::Info, L"Updater: manual check found a newer version — " + w);
            msg = WM_APP_UPDATE_AVAILABLE;
            wp  = 1;                                        // manual: ignore skip_version
            lp  = (LPARAM)_wcsdup(w.c_str());
        } else {
            logMsg(LogLevel::Info, std::wstring(L"Updater: manual check — already current at ")
                                   + SENTINEL_FILEVERSION_STR_W);
            wp = kUpdateCheckUpToDate;
        }
        if (!PostMessageW(target, msg, wp, lp)) {
            // The window went away (shutting down). Free the payload and drop the guard here,
            // because the UI will never reach endInteractiveUpdateCheck() for this one.
            if (lp) free(reinterpret_cast<void*>(lp));
            g_manualCheckInFlight = false;
        }
    }).detach();
    return true;
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
// false, NOT true: the caller sets "Checking for updates…" on a true return and then
// waits for a result that a stubbed-out updater will never post — a latent instance of the
// exact do-nothing bug this whole change exists to kill. Unreachable today (both entry
// points are gated on updaterAvailable(), false in this build), which is precisely why it
// would rot unnoticed.
bool checkForUpdatesInteractive(HWND) { return false; }
void endInteractiveUpdateCheck() {}
void shutdownUpdater() {}
}  // namespace sentinelide

#endif
