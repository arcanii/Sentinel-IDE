// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// Modal, dark-themed "an update is available" offer, raised by our own periodic appcast
// check (see Updater.h::WM_APP_UPDATE_AVAILABLE). Returns true if the user wants it now.
//
// This exists because WinSparkle's equivalent prompt is the one whose install path is
// broken; accepting here routes into checkForUpdates(), which is the path that works.
enum class UpdateChoice { InstallNow, Later, SkipVersion };

UpdateChoice showUpdateAvailableDialog(HWND owner, const std::wstring& newVersion,
                                       const std::wstring& currentVersion);

}  // namespace sentinelide
