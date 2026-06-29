// SPDX-License-Identifier: GPL-3.0-or-later
// Entry point. Common Controls v6 + DPI awareness come from the embedded
// manifest (packaging/app.manifest via SentinelIDE.rc).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ui/MainWindow.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    return sentinelide::runApp(hInstance, nCmdShow, pCmdLine);
}
