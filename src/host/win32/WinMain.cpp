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
#include <shellapi.h>
#include <string>

#include "host/win32/MainWindow.h"
#include "host/win32/SingleInstance.h"

namespace {

// First non-flag argument, resolved to a full path. Only the PATH is handed to a running
// instance: --settings and friends would mean opening a modal in response to an asynchronous
// message, which is the hazard phase 41 exists to avoid. A second launch carrying only flags
// therefore just raises the existing window.
std::wstring firstPathArg() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    std::wstring found;
    for (int i = 1; i < argc && found.empty(); ++i)
        if (argv[i] && argv[i][0] != L'-') found = argv[i];
    LocalFree(argv);
    if (found.empty()) return {};
    wchar_t full[MAX_PATH * 2] = L"";
    if (!GetFullPathNameW(found.c_str(), MAX_PATH * 2, full, nullptr)) return found;
    return full;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    // If an instance is already running, give it our path and get out of the way. Any failure
    // falls through to a normal launch: a second window is a nuisance, a silently swallowed
    // double-click is a bug.
    if (!sentinelide::acquireSingleInstance() &&
        sentinelide::handOffToRunningInstance(firstPathArg()))
        return 0;
    return sentinelide::runApp(hInstance, nCmdShow, pCmdLine);
}
