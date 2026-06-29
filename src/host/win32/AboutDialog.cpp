// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE About — a dark-themed modal (mirrors the other themed dialogs),
// replacing the classic MessageBox. Shows the S2 shield (app icon resource 100,
// drawn via DrawIconEx) plus name / version / tagline.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/AboutDialog.h"
#include "Version.h"     // generated: SENTINEL_VERSION_DISPLAY_W (marketing + auto build number)
#include "Loc.h"         // generated: SENTINEL_LOC_* lines-of-code by language (scripts/loc.ps1)
#include "host/win32/Theme.h"
#include "core/Logger.h"

#include <windows.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace sentinelide {
namespace {

enum { IDC_TITLE = 201, IDC_VER, IDC_TAG, IDC_LINE1, IDC_LINE2, IDC_COPY, IDC_CLOSE };

struct AboutState { bool done = false; HICON icon = nullptr; int pad = 20, iconSz = 72;
                    int capY = 0, badgeY = 0, badgeH = 0; HFONT badgeFont = nullptr; };

// Draw a shields.io-style flat badge [ label | value ] at (x,y). The whole pill is
// rounded (clip region), the label half is dark, the value half is colored, white
// text on both. Returns the badge width so callers can lay a row out left-to-right.
int drawBadge(HDC hdc, int x, int y, int h, const wchar_t* label, const wchar_t* value,
              COLORREF labelBg, COLORREF valueBg, HFONT font) {
    HGDIOBJ of = SelectObject(hdc, font);
    SIZE ls{}, vs{}; GetTextExtentPoint32W(hdc, label, lstrlenW(label), &ls);
    GetTextExtentPoint32W(hdc, value, lstrlenW(value), &vs);
    int padX = (h * 9) / 20;
    int wl = ls.cx + 2 * padX, wv = vs.cx + 2 * padX, w = wl + wv, rad = h / 2;
    HRGN rgn = CreateRoundRectRgn(x, y, x + w + 1, y + h + 1, rad, rad);
    SelectClipRgn(hdc, rgn);
    RECT rl{ x, y, x + wl, y + h };      HBRUSH bl = CreateSolidBrush(labelBg); FillRect(hdc, &rl, bl); DeleteObject(bl);
    RECT rv{ x + wl, y, x + w, y + h };  HBRUSH bv = CreateSolidBrush(valueBg); FillRect(hdc, &rv, bv); DeleteObject(bv);
    SelectClipRgn(hdc, nullptr); DeleteObject(rgn);
    int obk = SetBkMode(hdc, TRANSPARENT); COLORREF oc = SetTextColor(hdc, RGB(255, 255, 255));
    DrawTextW(hdc, label, -1, &rl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(hdc, value, -1, &rv, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetBkMode(hdc, obk); SetTextColor(hdc, oc); SelectObject(hdc, of);
    return w;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<AboutState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, w, l);
        }
        case WM_ERASEBKGND: { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)w, &rc, themeBrush(currentTheme().panelBg)); return 1; }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            if (st && st->icon) DrawIconEx(hdc, st->pad, st->pad, st->icon, st->iconSz, st->iconSz, 0, nullptr, DI_NORMAL);
            if (st && st->badgeY && st->badgeFont) {
                const Theme& th = currentTheme();
                const COLORREF lbl = RGB(0x41, 0x48, 0x52);   // shields-style dark label half
                // caption — ties the total to the Sentinel dogfood when it computed it
                HGDIOBJ of = SelectObject(hdc, st->badgeFont);
                SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, th.textMuted);
                const wchar_t* cap = SENTINEL_LOC_BY_SENTINEL
                    ? L"Lines of code  ·  total counted by loc.sentinel"
                    : L"Lines of code";
                RECT cr{ st->pad, st->capY, 2000, st->capY + st->badgeH }; DrawTextW(hdc, cap, -1, &cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, of);
                // badge row: C++ / Sentinel (the new one) / Build / Total
                std::wstring c = std::to_wstring((long long)SENTINEL_LOC_CPP);
                std::wstring s = std::to_wstring((long long)SENTINEL_LOC_SENTINEL);
                std::wstring b = std::to_wstring((long long)SENTINEL_LOC_BUILD);
                std::wstring t = std::to_wstring((long long)SENTINEL_LOC_TOTAL);
                int x = st->pad, gap = st->pad / 3, h = st->badgeH;
                x += drawBadge(hdc, x, st->badgeY, h, L"C++",      c.c_str(), lbl, th.diagInfo,      st->badgeFont) + gap;
                x += drawBadge(hdc, x, st->badgeY, h, L"Sentinel", s.c_str(), lbl, th.accent,        st->badgeFont) + gap;
                x += drawBadge(hdc, x, st->badgeY, h, L"Build",    b.c_str(), lbl, RGB(0x5a,0x63,0x6e), st->badgeFont) + gap;
                x += drawBadge(hdc, x, st->badgeY, h, L"Total",    t.c_str(), lbl, th.trustVerified, st->badgeFont) + gap;
            }
            EndPaint(hwnd, &ps); return 0;
        }
        case WM_CTLCOLORSTATIC: {
            const Theme& th = currentTheme(); HDC hdc = (HDC)w; int id = GetDlgCtrlID((HWND)l);
            COLORREF fg = th.textSecondary;
            if (id == IDC_TITLE) fg = th.accent;
            else if (id == IDC_VER || id == IDC_COPY) fg = th.textMuted;
            SetTextColor(hdc, fg); SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg);
        }
        case WM_CTLCOLORBTN: return dialogCtlColor(msg, w);
        case WM_COMMAND:
            if (st && (LOWORD(w) == IDC_CLOSE || LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)) st->done = true;
            return 0;
        case WM_CLOSE: if (st) st->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

void showAboutDialog(HWND owner) {
    static bool reg = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelAboutDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    AboutState st; st.pad = S(20); st.iconSz = S(72);
    st.icon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(100), IMAGE_ICON, st.iconSz, st.iconSz, LR_DEFAULTCOLOR);

    HFONT big = CreateFontW(-S(24), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT ui  = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    st.badgeFont = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");

    const int M = S(20), tx = M + st.iconSz + S(20), clientW = S(520);
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelAboutDlg", L"About Sentinel-IDE",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(120), orc.top + S(100), clientW, S(280), owner, nullptr, hi, &st);
    if (!hwnd) { if (st.icon) DestroyIcon(st.icon); DeleteObject(big); DeleteObject(ui); return; }

    auto mk = [&](const wchar_t* txt, DWORD style, int y, int ch, int id, HFONT f) {
        HWND c = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | style, tx, y, clientW - tx - M, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    };

    int yy = M - S(2);
    mk(L"Sentinel-IDE", SS_LEFT, yy, S(32), IDC_TITLE, big); yy += S(34);
    mk(L"Version " SENTINEL_VERSION_DISPLAY_W, SS_LEFT, yy, S(18), IDC_VER, ui); yy += S(28);
    mk(L"Native, Windows-first IDE for the Sentinel language — built in Sentinel.", SS_LEFT, yy, S(36), IDC_TAG, ui); yy += S(40);
    mk(L"Interpretation of untrusted bytes: Sentinel.", SS_LEFT, yy, S(18), IDC_LINE1, ui); yy += S(22);
    mk(L"Chrome: native host (a tracked, shrinking debt).", SS_LEFT, yy, S(18), IDC_LINE2, ui); yy += S(26);
    mk(L"© 2026 · GPL-3.0-or-later", SS_LEFT, yy, S(18), IDC_COPY, ui); yy += S(22);

    const int contentBottom = std::max(M + st.iconSz, yy);   // fit both the icon column and the text
    st.capY   = contentBottom + S(8);                        // LOC caption + badge row (drawn in WM_PAINT)
    st.badgeH = S(20);
    st.badgeY = st.capY + S(20);
    const int by = st.badgeY + st.badgeH + S(16), bw = S(96);
    HWND ok = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                              clientW - M - bw, by, bw, S(28), hwnd, (HMENU)(INT_PTR)IDC_CLOSE, hi, nullptr);
    SendMessageW(ok, WM_SETFONT, (WPARAM)ui, TRUE);
    const int clientH = by + S(28) + M;

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); SetFocus(ok);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    EnableWindow(owner, TRUE); if (owner) SetForegroundWindow(owner);
    DestroyWindow(hwnd);
    if (st.icon) DestroyIcon(st.icon); DeleteObject(big); DeleteObject(ui); if (st.badgeFont) DeleteObject(st.badgeFont);
    logMsg(LogLevel::Debug, L"About dialog closed");
}

}  // namespace sentinelide
