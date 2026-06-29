// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE settings — persisted to %LOCALAPPDATA%\SentinelIDE\settings.ini.
// Header-only (Win32 private-profile API). Holds editor font, theme mode, and
// the logging level + location.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include "core/Logger.h"

namespace sentinelide {

constexpr int kMaxRecents = 10;   // recent-projects list cap

struct Settings {
    std::wstring editorFont = L"Cascadia Code";
    int          themeMode  = -1;                 // -1 follow system, 0 light, 1 dark
    LogLevel     logLevel   = LogLevel::Info;
    std::wstring logFile;                         // computed default if empty
    std::wstring sncPath;                         // optional snc.exe override (else auto-detected)
    std::wstring vcvarsPath;                      // optional vcvars64.bat override (MSVC linker env; else auto-detected)
    bool         lineNumbers = false;             // show the editor line-number gutter
    std::vector<std::wstring> recents;            // recently-opened project folders (most-recent first)
};

// Promote `folder` to the front of the recents list (case-insensitive de-dupe, capped).
inline void addRecent(Settings& s, const std::wstring& folder) {
    if (folder.empty()) return;
    auto& r = s.recents;
    r.erase(std::remove_if(r.begin(), r.end(),
            [&](const std::wstring& x) { return lstrcmpiW(x.c_str(), folder.c_str()) == 0; }), r.end());
    r.insert(r.begin(), folder);
    if ((int)r.size() > kMaxRecents) r.resize(kMaxRecents);
}

inline std::wstring appDataDir() {
    wchar_t p[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p);
    std::wstring r = std::wstring(p) + L"\\SentinelIDE";
    CreateDirectoryW(r.c_str(), nullptr);
    return r;
}
inline std::wstring defaultLogFile() {
    std::wstring d = appDataDir() + L"\\logs";
    CreateDirectoryW(d.c_str(), nullptr);
    return d + L"\\sentinelide.log";
}
inline std::wstring settingsPath() { return appDataDir() + L"\\settings.ini"; }

inline void loadSettings(Settings& s) {
    if (s.logFile.empty()) s.logFile = defaultLogFile();
    const std::wstring path = settingsPath();
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"editor", L"font", s.editorFont.c_str(), buf, 1024, path.c_str()); s.editorFont = buf;
    s.themeMode = GetPrivateProfileIntW(L"ui", L"theme", s.themeMode, path.c_str());
    s.logLevel  = (LogLevel)GetPrivateProfileIntW(L"log", L"level", (int)s.logLevel, path.c_str());
    GetPrivateProfileStringW(L"log", L"file", s.logFile.c_str(), buf, 1024, path.c_str()); s.logFile = buf;
    GetPrivateProfileStringW(L"build", L"snc", L"", buf, 1024, path.c_str()); s.sncPath = buf;
    GetPrivateProfileStringW(L"build", L"vcvars", L"", buf, 1024, path.c_str()); s.vcvarsPath = buf;
    s.lineNumbers = GetPrivateProfileIntW(L"editor", L"line_numbers", s.lineNumbers ? 1 : 0, path.c_str()) != 0;
    s.recents.clear();
    int rc = GetPrivateProfileIntW(L"recents", L"count", 0, path.c_str());
    for (int i = 0; i < rc && i < kMaxRecents; ++i) {
        GetPrivateProfileStringW(L"recents", (L"item" + std::to_wstring(i)).c_str(), L"", buf, 1024, path.c_str());
        if (buf[0]) s.recents.push_back(buf);
    }
}
inline void saveSettings(const Settings& s) {
    const std::wstring path = settingsPath();
    WritePrivateProfileStringW(L"editor", L"font", s.editorFont.c_str(), path.c_str());
    WritePrivateProfileStringW(L"ui", L"theme", std::to_wstring(s.themeMode).c_str(), path.c_str());
    WritePrivateProfileStringW(L"log", L"level", std::to_wstring((int)s.logLevel).c_str(), path.c_str());
    WritePrivateProfileStringW(L"log", L"file", s.logFile.c_str(), path.c_str());
    WritePrivateProfileStringW(L"build", L"snc", s.sncPath.c_str(), path.c_str());
    WritePrivateProfileStringW(L"build", L"vcvars", s.vcvarsPath.c_str(), path.c_str());
    WritePrivateProfileStringW(L"editor", L"line_numbers", s.lineNumbers ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"recents", nullptr, nullptr, path.c_str());   // clear stale itemN first
    WritePrivateProfileStringW(L"recents", L"count", std::to_wstring((int)s.recents.size()).c_str(), path.c_str());
    for (size_t i = 0; i < s.recents.size(); ++i)
        WritePrivateProfileStringW(L"recents", (L"item" + std::to_wstring(i)).c_str(), s.recents[i].c_str(), path.c_str());
}

}  // namespace sentinelide
