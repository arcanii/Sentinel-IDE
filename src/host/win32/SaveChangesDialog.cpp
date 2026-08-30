// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE unsaved-changes prompt — a small dark-themed modal, same shape as
// PasswordDialog. Three answers (Save / Don't Save / Cancel), because a two-answer
// prompt forces the user to guess which one loses their work.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/SaveChangesDialog.h"
#include "host/win32/Theme.h"

#include <windows.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace sentinelide {
namespace {

enum { IDC_HEAD = 221, IDC_BODY, IDC_DISCARD };

struct SaveState {
    bool done = false;
    SaveChoice result = SaveChoice::Cancel;   // Esc / close box / anything unexpected
};

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<SaveState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
        case WM_COMMAND: {
            if (!st) return 0;
            switch (LOWORD(w)) {
                case IDOK:       st->result = SaveChoice::Save;    st->done = true; break;
                case IDC_DISCARD:st->result = SaveChoice::Discard; st->done = true; break;
                case IDCANCEL:   st->result = SaveChoice::Cancel;  st->done = true; break;
            }
            return 0;
        }
        case WM_CLOSE: if (st) { st->result = SaveChoice::Cancel; st->done = true; } return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

SaveChoice showSaveChangesDialog(HWND owner, const std::wstring& fileName, const std::wstring& action) {
    static bool reg = false; HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelSaveDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
        RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    SaveState st;
    HFONT ui = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT head = CreateFontW(-S(17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    const int M = S(18), clientW = S(440), fw = clientW - 2 * M;
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelSaveDlg", L"Sentinel-IDE",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(120), orc.top + S(120), clientW, S(200), owner, nullptr, hi, &st);
    if (!hwnd) { DeleteObject(ui); DeleteObject(head); return SaveChoice::Cancel; }

    auto mk = [&](const wchar_t* txt, DWORD style, int x, int y, int cw, int ch, int id, HFONT f) -> HWND {
        HWND c = CreateWindowExW(0, id == IDC_HEAD || id == IDC_BODY ? L"STATIC" : L"BUTTON", txt,
                                 WS_CHILD | WS_VISIBLE | style, x, y, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c;
    };
    // Measure the body rather than reserving a guessed number of lines — `action`
    // varies in length, and a static that is one line short silently clips the text.
    const std::wstring body = L"Your changes will be lost if you continue " + action + L" without saving.";
    int bodyH = S(20);
    if (HDC dc = GetDC(hwnd)) {
        HFONT old = (HFONT)SelectObject(dc, ui);
        RECT mr{ 0, 0, fw, 0 };
        DrawTextW(dc, body.c_str(), -1, &mr, DT_CALCRECT | DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
        bodyH = mr.bottom - mr.top;
        SelectObject(dc, old); ReleaseDC(hwnd, dc);
    }
    int yy = M;
    // SS_NOPREFIX or a STATIC swallows '&' as a mnemonic — file names may contain one.
    mk((L"Save changes to “" + fileName + L"”?").c_str(), SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, M, yy, fw, S(24), IDC_HEAD, head); yy += S(32);
    mk(body.c_str(), SS_LEFT | SS_NOPREFIX, M, yy, fw, bodyH, IDC_BODY, ui); yy += bodyH + S(20);

    // Save is the default (Enter); Cancel is leftmost and also reachable with Esc.
    const int by = yy, bw = S(104), gap = S(8), rx = clientW - M;
    mk(L"Cancel",     BS_PUSHBUTTON | WS_TABSTOP,    rx - 3 * bw - 2 * gap, by, bw, S(28), IDCANCEL, ui);
    mk(L"Don't Save", BS_PUSHBUTTON | WS_TABSTOP,    rx - 2 * bw - gap,     by, bw, S(28), IDC_DISCARD, ui);
    mk(L"Save",       BS_DEFPUSHBUTTON | WS_TABSTOP, rx - bw,               by, bw, S(28), IDOK, ui);
    const int clientH = by + S(28) + M;

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    SetFocus(GetDlgItem(hwnd, IDOK));

    // Re-post WM_QUIT — GetMessageW consumes it, and a nested loop that swallows it
    // leaves runApp's outer loop blocked forever with the process still alive.
    // See host/win32/PasswordDialog.cpp and host/win32/Updater.cpp.
    MSG msg;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; }
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    EnableWindow(owner, TRUE); if (owner) SetForegroundWindow(owner);
    DestroyWindow(hwnd); DeleteObject(ui); DeleteObject(head);
    return st.result;
}

}  // namespace sentinelide
