// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE update offer — a small dark-themed modal, same shape as
// SaveChangesDialog. Raised by our own appcast poll, not by WinSparkle: its
// prompt leads to the install path that silently does nothing (phase 40).
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/UpdateDialog.h"
#include "host/win32/Theme.h"

#include <windows.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace sentinelide {
namespace {

enum { IDC_HEAD = 241, IDC_BODY, IDC_SKIP };

struct UpdState {
    bool done = false;
    UpdateChoice choice = UpdateChoice::Later;
    // This dialog appears UNBIDDEN, mid-work. A stray Enter or Space aimed at the editor
    // must not be able to accept it, so IDOK is ignored for a moment after it opens.
    DWORD shownAt = 0;
};
constexpr DWORD kAcceptGuardMs = 700;

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<UpdState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, w, l);
        }
        case WM_ERASEBKGND: { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)w, &rc, themeBrush(currentTheme().panelBg)); return 1; }
        case WM_CTLCOLORSTATIC: {
            const Theme& th = currentTheme(); HDC hdc = (HDC)w;
            SetTextColor(hdc, GetDlgCtrlID((HWND)l) == IDC_HEAD ? th.textPrimary : th.textSecondary);
            SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg);
        }
        case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: return dialogCtlColor(msg, w);
        case WM_COMMAND:
            if (!st) return 0;
            if (LOWORD(w) == IDOK) {
                if (GetTickCount() - st->shownAt < kAcceptGuardMs) return 0;   // too soon: a stray keypress
                st->choice = UpdateChoice::InstallNow; st->done = true;
            }
            if (LOWORD(w) == IDC_SKIP)  { st->choice = UpdateChoice::SkipVersion; st->done = true; }
            if (LOWORD(w) == IDCANCEL)  { st->choice = UpdateChoice::Later;       st->done = true; }
            return 0;
        case WM_CLOSE: if (st) { st->choice = UpdateChoice::Later; st->done = true; } return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

UpdateChoice showUpdateAvailableDialog(HWND owner, const std::wstring& newVersion,
                                       const std::wstring& currentVersion) {
    static bool reg = false; HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelUpdateDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
        RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    UpdState st;
    HFONT ui = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT head = CreateFontW(-S(17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    const int M = S(18), clientW = S(440), fw = clientW - 2 * M;
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelUpdateDlg", L"Sentinel-IDE",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(120), orc.top + S(120), clientW, S(200), owner, nullptr, hi, &st);
    if (!hwnd) { DeleteObject(ui); DeleteObject(head); return UpdateChoice::Later; }

    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int cw, int ch, int id, HFONT f) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c;
    };

    const std::wstring body = L"Sentinel-IDE " + newVersion + L" is available — you have " +
                              currentVersion + L". The update is verified before it is installed.";
    int bodyH = S(20);
    if (HDC dc = GetDC(hwnd)) {
        HFONT old = (HFONT)SelectObject(dc, ui);
        RECT mr{ 0, 0, fw, 0 };
        DrawTextW(dc, body.c_str(), -1, &mr, DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
        bodyH = mr.bottom - mr.top;
        SelectObject(dc, old); ReleaseDC(hwnd, dc);
    }
    int yy = M;
    mk(L"STATIC", L"An update is available", SS_LEFT | SS_NOPREFIX, M, yy, fw, S(24), IDC_HEAD, head); yy += S(32);
    mk(L"STATIC", body.c_str(), SS_LEFT | SS_NOPREFIX, M, yy, fw, bodyH, IDC_BODY, ui); yy += bodyH + S(20);

    // "Later" is the DEFAULT and gets focus — the opposite of SaveChangesDialog, deliberately.
    // There the user asked for the dialog, so defaulting to the affirmative is right; here it
    // arrives uninvited while they are typing, so the default must be the harmless answer.
    const int by = yy, bw = S(104), gap = S(8), rx = clientW - M;
    mk(L"BUTTON", L"Skip this version", BS_PUSHBUTTON | WS_TABSTOP, M, by, S(140), S(28), IDC_SKIP, ui);
    mk(L"BUTTON", L"Install now", BS_PUSHBUTTON    | WS_TABSTOP, rx - 2 * bw - gap, by, bw, S(28), IDOK, ui);
    mk(L"BUTTON", L"Later",       BS_DEFPUSHBUTTON | WS_TABSTOP, rx - bw,           by, bw, S(28), IDCANCEL, ui);
    const int clientH = by + S(28) + M;

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    // Restore the owner's PRIOR state, not an unconditional TRUE. This dialog can be raised
    // while another modal already has the main window disabled (the poll posts asynchronously
    // and every modal's loop dispatches it), and re-enabling underneath that one would let the
    // user drive the main window with a prompt still pending behind it.
    const BOOL ownerWasEnabled = owner ? IsWindowEnabled(owner) : FALSE;
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    st.shownAt = GetTickCount();
    SetFocus(GetDlgItem(hwnd, IDCANCEL));

    // Re-post WM_QUIT — see host/win32/PasswordDialog.cpp. Doubly important here: this
    // dialog is open precisely when an update install may be requested.
    MSG msg;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; }
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    if (ownerWasEnabled) { EnableWindow(owner, TRUE); if (owner) SetForegroundWindow(owner); }
    DestroyWindow(hwnd); DeleteObject(ui); DeleteObject(head);
    return st.choice;
}

}  // namespace sentinelide
