// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>

namespace sentinelide {

// Create the main window and run the message loop. Returns the exit code.
// cmdLine may name a folder to open, or a .sentinel file to open (with its
// parent folder as the project root).
int runApp(HINSTANCE hInstance, int nCmdShow, PWSTR cmdLine);

}  // namespace sentinelide
