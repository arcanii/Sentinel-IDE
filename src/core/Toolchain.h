// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE toolchain discovery — the MSVC "Developer environment" that puts
// link.exe + the import libraries on PATH (what "run from a Developer Command
// Prompt" provides). snc shells out to link.exe; without this, builds fail at link.
// We locate vcvars64.bat, run it, and capture the resulting environment so the
// build child process inherits the linker. Header-only.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "core/Proc.h"   // runCapture

namespace sentinelide {

inline std::wstring firstNonEmptyLine(const std::wstring& s) {
    std::wstring line;
    for (wchar_t c : s) { if (c == L'\n' || c == L'\r') { if (!line.empty()) return line; } else line += c; }
    return line;
}

// Auto-detect vcvars64.bat. Tries vswhere (the supported detector — any edition,
// version, or preview), then well-known install paths. Empty if none found.
inline std::wstring findVcvars() {
    wchar_t pfx86[MAX_PATH] = L"";
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", pfx86, MAX_PATH)) {
        std::wstring vswhere = std::wstring(pfx86) + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (GetFileAttributesW(vswhere.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring out;
            runCapture(L"\"" + vswhere + L"\" -latest -prerelease -products * "
                       L"-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath", L"", out);
            std::wstring inst = firstNonEmptyLine(out);
            if (!inst.empty()) {
                std::wstring vc = inst + L"\\VC\\Auxiliary\\Build\\vcvars64.bat";
                if (GetFileAttributesW(vc.c_str()) != INVALID_FILE_ATTRIBUTES) return vc;
            }
        }
    }
    const wchar_t* roots[] = { L"C:\\Program Files\\Microsoft Visual Studio",
                               L"C:\\Program Files (x86)\\Microsoft Visual Studio" };
    const wchar_t* vers[]  = { L"2022", L"18", L"2019", L"2017", L"17" };
    const wchar_t* eds[]   = { L"Community", L"Professional", L"Enterprise", L"BuildTools", L"Preview" };
    for (auto r : roots) for (auto v : vers) for (auto e : eds) {
        std::wstring vc = std::wstring(r) + L"\\" + v + L"\\" + e + L"\\VC\\Auxiliary\\Build\\vcvars64.bat";
        if (GetFileAttributesW(vc.c_str()) != INVALID_FILE_ATTRIBUTES) return vc;
    }
    return L"";
}

// Run vcvars64.bat and capture the resulting environment as a CREATE_UNICODE_ENVIRONMENT
// block (`K=V\0…\0\0`). Leaves `block` empty on failure. Slow (~1-2s) — cache the result.
inline void captureMsvcEnv(const std::wstring& vcvars, std::vector<wchar_t>& block) {
    block.clear();
    if (vcvars.empty() || GetFileAttributesW(vcvars.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::wstring out;
    runCapture(L"cmd.exe /s /c \"\"" + vcvars + L"\" >nul 2>&1 && set\"", L"", out);
    auto push = [&](const std::wstring& l) {
        if (l.find(L'=') == std::wstring::npos) return;   // skip banners/blank lines
        for (wchar_t c : l) block.push_back(c);
        block.push_back(L'\0');
    };
    std::wstring line;
    for (wchar_t c : out) { if (c == L'\n') { if (!line.empty() && line.back() == L'\r') line.pop_back(); push(line); line.clear(); } else line += c; }
    if (!line.empty()) push(line);
    if (!block.empty()) block.push_back(L'\0');           // terminating double-null
}

}  // namespace sentinelide
