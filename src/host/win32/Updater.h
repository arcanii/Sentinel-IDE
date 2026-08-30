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

// User-triggered "Check for Updates…" (≡ menu). Shows WinSparkle's own UI.
void checkForUpdates(HWND owner);

// On app exit.
void shutdownUpdater();

// Is auto-update compiled in AND configured with a real signing key? The menu item
// is hidden when false, so we never present a check that cannot verify anything.
bool updaterAvailable();

// Posted to the main window when OUR OWN periodic check finds a newer version in the
// appcast. lParam is a heap wchar_t* with the new version string; the UI must free it.
//
// We run that periodic check ourselves because WinSparkle's built-in one is unusable here:
// it raises WinSparkle's own update prompt, which is the flow that hands us an empty payload
// path and installs nothing (see HANDOVER phase 40). initUpdater therefore turns WinSparkle's
// automatic checking OFF and starts the timer below instead. Nothing security-relevant moves:
// we only decide whether to OFFER, and the download, Ed25519 verification and install still
// go through WinSparkle via checkForUpdates().
constexpr UINT WM_APP_UPDATE_AVAILABLE = WM_APP + 9;

// True once WinSparkle has asked us to quit so it can install an update. The WM_CLOSE
// that arrives then is NOT a user closing the window: WinSparkle is waiting on our
// process handle behind a 3-second force-exit watchdog, so the close path must not stop
// to ask a question it would never get an answer to. Set on a WinSparkle worker thread,
// read on the UI thread.
bool updaterShutdownPending();

}  // namespace sentinelide
