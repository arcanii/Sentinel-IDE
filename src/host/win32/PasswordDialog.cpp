// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE password prompt — a small dark-themed modal (mirrors the other
// dialogs). One masked field, or two with a match check when sealing.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/PasswordDialog.h"
#include "host/win32/Theme.h"

#include <windows.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace sentinelide {
namespace {

enum { IDC_PW = 201, IDC_PW2, IDC_PROMPT, IDC_ERR };

struct PwState {
    bool done = false; int result = IDCANCEL; bool confirm = false;
    std::wstring* out = nullptr;
    HWND ePw = nullptr, ePw2 = nullptr, err = nullptr;
};

std::wstring gtext(HWND h) {
    int n = GetWindowTextLengthW(h); std::wstring s(n, 0); GetWindowTextW(h, s.data(), n + 1); s.resize(n); return s;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<PwState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, w, l);
        }
        case WM_ERASEBKGND: { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)w, &rc, themeBrush(currentTheme().panelBg)); return 1; }
        case WM_CTLCOLORSTATIC: {
            const Theme& th = currentTheme(); HDC hdc = (HDC)w;
            SetTextColor(hdc, GetDlgCtrlID((HWND)l) == IDC_ERR ? th.diagError : th.textSecondary);
            SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg);
        }
        case WM_CTLCOLOREDIT: case WM_CTLCOLORBTN: return dialogCtlColor(msg, w);
        case WM_COMMAND: {
            int id = LOWORD(w);
            if (id == IDOK && st) {
                std::wstring p = gtext(st->ePw);
                if (p.empty()) { SetWindowTextW(st->err, L"Enter a password."); return 0; }
                if (st->confirm && p != gtext(st->ePw2)) { SetWindowTextW(st->err, L"Passwords don't match."); return 0; }
                *st->out = p; st->result = IDOK; st->done = true; return 0;
            }
            if (id == IDCANCEL && st) { st->done = true; return 0; }
            return 0;
        }
        case WM_CLOSE: if (st) st->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

bool showPasswordDialog(HWND owner, const std::wstring& title, const std::wstring& prompt, bool confirm, std::wstring& out) {
    static bool reg = false; HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelPwDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
        RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    PwState st; st.confirm = confirm; st.out = &out;
    HFONT ui = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    const int M = S(18), clientW = S(420), fw = clientW - 2 * M;
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelPwDlg", title.c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(120), orc.top + S(120), clientW, S(240), owner, nullptr, hi, &st);
    if (!hwnd) { DeleteObject(ui); return false; }

    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int cw, int ch, int id) -> HWND {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)ui, TRUE); return c;
    };
    int yy = M;
    mk(L"STATIC", prompt.c_str(), SS_LEFT, M, yy, fw, S(38), IDC_PROMPT); yy += S(44);
    st.ePw = mk(L"EDIT", L"", ES_PASSWORD | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, M, yy, fw, S(24), IDC_PW); yy += S(32);
    if (confirm) { st.ePw2 = mk(L"EDIT", L"", ES_PASSWORD | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, M, yy, fw, S(24), IDC_PW2); yy += S(32); }
    st.err = mk(L"STATIC", L"", SS_LEFT, M, yy, fw, S(18), IDC_ERR); yy += S(24);

    const int by = yy + S(4), bw = S(96), rx = clientW - M;
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, rx - 2 * bw - S(8), by, bw, S(28), IDCANCEL);
    mk(L"BUTTON", confirm ? L"Seal" : L"Unlock", BS_DEFPUSHBUTTON | WS_TABSTOP, rx - bw, by, bw, S(28), IDOK);
    const int clientH = by + S(28) + M;

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); SetFocus(st.ePw);

    // Re-post WM_QUIT — GetMessageW consumes it, and a nested loop that swallows it
    // leaves runApp's outer loop blocked forever with the process still alive. Matters
    // because a background update check can request shutdown while this is open.
    // See host/win32/Updater.cpp.
    MSG msg;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; }
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    EnableWindow(owner, TRUE); if (owner) SetForegroundWindow(owner);
    DestroyWindow(hwnd); DeleteObject(ui);
    return st.result == IDOK;
}

}  // namespace sentinelide
