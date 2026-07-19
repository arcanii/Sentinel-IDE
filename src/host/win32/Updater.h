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

}  // namespace sentinelide
