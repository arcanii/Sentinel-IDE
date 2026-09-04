// SPDX-License-Identifier: GPL-3.0-or-later
// Auto-update — a thin wrapper over WinSparkle, checking an EdDSA-signed appcast.
//
// Deliberately a three-function surface so a future macOS host can implement the
// same names over Sparkle without the rest of the app knowing which engine ran.
// It lives in host/win32/ rather than core/ because it IS the platform integration
// (window handles, message pumping, process lifetime) — per the repo's rule of not
// scaffolding platform trees until a port actually starts.
//
// Compiled out entirely when SENTINELIDE_HAVE_WINSPARKLE is undefined (CMake option
// SENTINELIDE_UPDATER=OFF), in which case these become no-ops and the menu item is
// hidden — so a build without the vendored DLL still links and runs.
#pragma once
#include <windows.h>

namespace sentinelide {

// Configure the feed + key and start background checks. `mainWnd` receives WM_CLOSE
// when WinSparkle needs the app to quit so it can install an update.
void initUpdater(HWND mainWnd);

// Download and install an update NOW, with no further questions —
// win_sparkle_check_update_with_ui_and_install(). This is what runs once the user has
// answered "Install now"; it is NOT the menu item. See the comment on its definition for
// why the prompt-then-install variant cannot be used instead.
void checkForUpdates(HWND owner);

// User-triggered "Check for Updates…" (≡ menu, About box) — asks BEFORE installing.
//
// Fetches the appcast on a detached thread (a blocking network GET on the UI thread would
// freeze the message loop for as long as the server takes) and reports the outcome by
// POSTING to the main window:
//   * a newer version   -> WM_APP_UPDATE_AVAILABLE with wParam 1, i.e. the same themed
//                          Install now / Later / Skip offer the background poll raises;
//   * already current   -> WM_APP_UPDATE_CHECK_RESULT, wParam kUpdateCheckUpToDate;
//   * fetch/parse fails -> WM_APP_UPDATE_CHECK_RESULT, wParam kUpdateCheckFailed.
//
// ALL THREE are reported, and that is the difference from the background poll: a background
// check may fail silently and try again next tick, but a menu item the user just clicked
// must never appear to do nothing. Nothing is swallowed here.
//
// Returns false when a manual check is ALREADY IN FLIGHT, in which case nothing at all is
// started: no second thread, and — because the guard is not released until the UI has acted
// on the first outcome — no second dialog either. The caller may say so in the status bar.
//
// `owner` is used only for the synchronous "not configured" message box. Every asynchronous
// outcome is posted to the main window instead, so it goes through that window's busy
// deferral and can never stack a second modal on top of an open one.
bool checkForUpdatesInteractive(HWND owner);

// Release the in-flight guard taken by checkForUpdatesInteractive. The UI calls this when it
// has finished ACTING on an outcome, not when the message arrives — otherwise a click landing
// while the offer is still parked behind a modal would queue a second, identical offer.
void endInteractiveUpdateCheck();

// On app exit.
void shutdownUpdater();

// Is auto-update compiled in AND configured with a real signing key? The menu item
// is hidden when false, so we never present a check that cannot verify anything.
bool updaterAvailable();

// Posted to the main window when one of OUR OWN appcast checks finds a newer version.
// lParam is a heap wchar_t* with the new version string; the UI must free it.
//
// wParam says WHICH check found it, and it changes one thing only:
//   0 = the background poll   — honours [update] skip_version, so a skipped release stays quiet;
//   1 = a manual menu check   — IGNORES skip_version, because an explicit request outranks a
//       standing preference. Someone who skipped 0.1.11 and then asks "any updates?" is owed
//       an answer, not silence.
//
// We run that periodic check ourselves because WinSparkle's built-in one is unusable here:
// it raises WinSparkle's own update prompt, which is the flow that hands us an empty payload
// path and installs nothing (see HANDOVER phase 40). initUpdater therefore turns WinSparkle's
// automatic checking OFF and starts the timer below instead. Nothing security-relevant moves:
// we only decide whether to OFFER, and the download, Ed25519 verification and install still
// go through WinSparkle via checkForUpdates().
constexpr UINT WM_APP_UPDATE_AVAILABLE = WM_APP + 9;

// Outcome of a MANUAL check that produced no offer. wParam is one of the two constants
// below; lParam is unused. The "there IS an update" outcome deliberately goes to
// WM_APP_UPDATE_AVAILABLE above instead of here, so the manual path reuses the proven
// offer-and-defer handler rather than growing a second copy of it.
constexpr UINT WM_APP_UPDATE_CHECK_RESULT = WM_APP + 10;
constexpr WPARAM kUpdateCheckUpToDate = 0;
constexpr WPARAM kUpdateCheckFailed   = 1;

// True once WinSparkle has asked us to quit so it can install an update. The WM_CLOSE
// that arrives then is NOT a user closing the window: WinSparkle is waiting on our
// process handle behind a 3-second force-exit watchdog, so the close path must not stop
// to ask a question it would never get an answer to. Set on a WinSparkle worker thread,
// read on the UI thread.
bool updaterShutdownPending();

// True between the moment ≡ ▸ Check for Updates… starts a fetch and the moment the UI has
// acted on its outcome. The host uses it to hold a BACKGROUND find back rather than racing
// the user's own request to the screen — see the WM_APP_UPDATE_AVAILABLE handler. Set and
// cleared on the UI thread's behalf but written from a worker, so it is an atomic.
bool updaterManualCheckInFlight();

}  // namespace sentinelide
