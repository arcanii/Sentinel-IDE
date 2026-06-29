// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE palette — the code embodiment of DESIGN.md (dark-primary, coral
// accent, OS light/dark follow). Non-diagnostic values are sourced from
// SQLTerminal-Win32 Theme.h; the diag-* and trust-verified tokens are
// SentinelIDE additions (see DESIGN.md Colors). Pure GDI COLORREFs.
#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace sentinelide {

struct Theme {
    bool dark;
    COLORREF windowBg;       // deepest surface (editor, window erase)
    COLORREF panelBg;        // panels (tree, problems, output rows)
    COLORREF panelElevBg;    // elevated bands (tab strip, headers, status bar)
    COLORREF altRowBg;       // zebra-stripe / current-line band
    COLORREF hoverBg;        // hover highlight
    COLORREF border;         // hairline separators
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF textMuted;
    COLORREF accent;         // coral — primary action / active / Sentinel-safety signal
    COLORREF accentText;     // text on accent
    COLORREF selectionBg;    // muted coral — selection / active row
    COLORREF selectionText;
    COLORREF synKeyword;
    COLORREF synNumber;
    COLORREF synString;
    COLORREF synComment;
    COLORREF diagError;      // generic compile error
    COLORREF diagWarning;
    COLORREF diagInfo;
    COLORREF diagSecurity;   // Sentinel safety findings (== accent by intent)
    COLORREF trustVerified;  // signing "Signed" state (origin+integrity, not identity)
};

inline Theme makeDarkTheme() {
    Theme t{};
    t.dark = true;
    t.windowBg     = RGB(22, 22, 24);
    t.panelBg      = RGB(26, 26, 28);
    t.panelElevBg  = RGB(32, 32, 35);
    t.altRowBg     = RGB(28, 28, 31);
    t.hoverBg      = RGB(42, 42, 45);
    t.border       = RGB(48, 48, 52);
    t.textPrimary  = RGB(230, 230, 232);
    t.textSecondary= RGB(154, 154, 160);
    t.textMuted    = RGB(106, 106, 112);
    t.accent       = RGB(217, 119, 87);   // Claude coral
    t.accentText   = RGB(40, 18, 10);
    t.selectionBg  = RGB(92, 52, 38);
    t.selectionText= RGB(247, 238, 233);
    t.synKeyword   = RGB(201, 143, 214);
    t.synNumber    = RGB(159, 209, 154);
    t.synString    = RGB(224, 150, 107);
    t.synComment   = RGB(118, 150, 118);
    t.diagError    = RGB(224, 108, 117);  // #E06C75
    t.diagWarning  = RGB(229, 192, 123);  // #E5C07B
    t.diagInfo     = RGB(108, 168, 196);  // #6CA8C4
    t.diagSecurity = RGB(217, 119, 87);   // coral
    t.trustVerified= RGB(127, 179, 122);  // #7FB37A
    return t;
}

inline Theme makeLightTheme() {
    Theme t{};
    t.dark = false;
    t.windowBg     = GetSysColor(COLOR_WINDOW);
    t.panelBg      = GetSysColor(COLOR_WINDOW);
    t.panelElevBg  = RGB(244, 244, 246);
    t.altRowBg     = RGB(245, 245, 245);
    t.hoverBg      = RGB(232, 232, 234);
    t.border       = RGB(214, 214, 218);
    t.textPrimary  = GetSysColor(COLOR_WINDOWTEXT);
    t.textSecondary= RGB(96, 96, 102);
    t.textMuted    = RGB(140, 140, 146);
    t.accent       = RGB(193, 95, 60);
    t.accentText   = RGB(255, 255, 255);
    t.selectionBg  = RGB(250, 232, 224);
    t.selectionText= RGB(74, 27, 12);
    t.synKeyword   = RGB(199, 37, 108);
    t.synNumber    = RGB(128, 0, 128);
    t.synString    = RGB(196, 26, 22);
    t.synComment   = RGB(34, 139, 34);
    t.diagError    = RGB(192, 57, 43);    // #C0392B
    t.diagWarning  = RGB(138, 109, 0);    // #8A6D00
    t.diagInfo     = RGB(44, 110, 155);   // #2C6E9B
    t.diagSecurity = RGB(193, 95, 60);
    t.trustVerified= RGB(46, 125, 50);    // #2E7D32
    return t;
}

// Follow the system light/dark setting; default to DARK (SentinelIDE is dark-primary).
inline bool systemUsesDarkMode() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return true;
    return value == 0;
}

// -1 = follow system, 0 = force light, 1 = force dark.
inline int& themeOverride() { static int mode = -1; return mode; }

inline const Theme& currentTheme() {
    static const Theme dark = makeDarkTheme();
    static const Theme light = makeLightTheme();
    const int o = themeOverride();
    const bool useDark = (o < 0) ? systemUsesDarkMode() : (o == 1);
    return useDark ? dark : light;
}

// Scale a 96-dpi design value to a window's DPI.
inline int dpiScale(int v, UINT dpi) { return MulDiv(v, static_cast<int>(dpi), 96); }

// Dark title bar (Win10 1809+; no-op otherwise).
inline void applyWindowDarkMode(HWND hwnd) {
    const Theme& th = currentTheme();
    BOOL dark = th.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
}

// Force popup / context menus to match the current app theme. uxtheme's dark-mode
// entry points are exported by ordinal only (undocumented but stable since Win10
// 1809 — the standard way apps get dark menus). Every call is guarded, so menus
// simply stay light if the exports are missing. Call at startup and on theme change.
inline void applyMenuDarkMode() {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto flush  = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(currentTheme().dark ? 2 : 3);  // ForceDark / ForceLight
    if (flush) flush();
}

// ---- themed-dialog helpers (for modal dialogs: Settings, etc.) -------------
inline HBRUSH themeBrush(COLORREF c) {
    static COLORREF colors[16]; static HBRUSH brushes[16]; static int count = 0;
    for (int i = 0; i < count; ++i) if (colors[i] == c) return brushes[i];
    HBRUSH b = CreateSolidBrush(c);
    if (count < 16) { colors[count] = c; brushes[count] = b; ++count; }
    return b;
}

// Handle WM_CTLCOLOR{STATIC,EDIT,LISTBOX,BTN}: dark field/background, light text.
inline LRESULT dialogCtlColor(UINT msg, WPARAM wParam) {
    const Theme& th = currentTheme();
    HDC hdc = reinterpret_cast<HDC>(wParam);
    const COLORREF bg = (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) ? th.windowBg : th.panelBg;
    SetTextColor(hdc, th.textPrimary);
    SetBkColor(hdc, bg);
    return reinterpret_cast<LRESULT>(themeBrush(bg));
}

// Theme a popup dialog + its child controls dark (Win10 1809+; no-op otherwise).
inline void applyDialogDarkMode(HWND dlg) {
    const Theme& th = currentTheme();
    BOOL dark = th.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(dlg, 20 /*USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = th.panelElevBg, txt = th.textSecondary, bdr = th.border;
    DwmSetWindowAttribute(dlg, 35 /*CAPTION_COLOR*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(dlg, 36 /*TEXT_COLOR*/, &txt, sizeof(txt));
    DwmSetWindowAttribute(dlg, 34 /*BORDER_COLOR*/, &bdr, sizeof(bdr));
    if (!th.dark) return;
    if (HMODULE ux = GetModuleHandleW(L"uxtheme.dll")) {
        using AllowFn = BOOL(WINAPI*)(HWND, BOOL);
        if (auto allow = reinterpret_cast<AllowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133))))
            allow(dlg, TRUE);
    }
    EnumChildWindows(dlg, [](HWND child, LPARAM) -> BOOL {
        wchar_t cls[64] = L"";
        GetClassNameW(child, cls, 64);
        SetWindowTheme(child, lstrcmpiW(cls, L"COMBOBOX") == 0 ? L"DarkMode_CFD" : L"DarkMode_Explorer", nullptr);
        return TRUE;
    }, 0);
}

}  // namespace sentinelide
