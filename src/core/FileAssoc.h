// SPDX-License-Identifier: GPL-3.0-or-later
// Per-user file associations (HKCU\Software\Classes — no admin, effective at once
// via SHChangeNotify). Associates .sntproject and .sentinel with this executable so
// a double-click in Explorer opens the IDE (which already accepts a path on argv).
//
// Note: if the user has explicitly picked another app for an extension (a shell
// "UserChoice"), Windows honors that over our Classes default — by design. For these
// otherwise-unclaimed extensions, the Classes default makes us the double-click handler.
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

namespace sentinelide {

inline std::wstring moduleExePath() {
    wchar_t buf[MAX_PATH] = L""; GetModuleFileNameW(nullptr, buf, MAX_PATH); return buf;
}

// Write a REG_SZ under HKCU\Software\Classes\<subkey> (valueName=nullptr → the default value).
inline bool regWriteSz(const std::wstring& subkey, const wchar_t* valueName, const std::wstring& data) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + subkey).c_str(),
                        0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(h, valueName, 0, REG_SZ, (const BYTE*)data.c_str(),
                            (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

// A ProgID: description, an icon (negative = exe resource id), and the open command.
inline bool registerProgId(const std::wstring& progId, const std::wstring& desc, const std::wstring& exe, int iconResId) {
    bool ok = regWriteSz(progId, nullptr, desc);
    ok = regWriteSz(progId + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\"," + std::to_wstring(-iconResId)) && ok;
    ok = regWriteSz(progId + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"") && ok;
    return ok;
}

// Register .sntproject + .sentinel to open in this exe. exeOut (optional) gets the path.
inline bool registerFileAssociations(std::wstring* exeOut = nullptr) {
    std::wstring exe = moduleExePath();
    if (exeOut) *exeOut = exe;
    bool ok = true;
    ok = registerProgId(L"SentinelIDE.Project", L"Sentinel Project", exe, 100) && ok;  // S2 app icon (res 100)
    ok = registerProgId(L"SentinelIDE.Source",  L"Sentinel Source",  exe, 101) && ok;  // .sentinel file icon (res 101)
    ok = regWriteSz(L".sntproject", nullptr, L"SentinelIDE.Project") && ok;
    ok = regWriteSz(L".sntproject\\OpenWithProgids", L"SentinelIDE.Project", L"") && ok;
    ok = regWriteSz(L".sentinel", nullptr, L"SentinelIDE.Source") && ok;
    ok = regWriteSz(L".sentinel\\OpenWithProgids", L"SentinelIDE.Source", L"") && ok;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

}  // namespace sentinelide
