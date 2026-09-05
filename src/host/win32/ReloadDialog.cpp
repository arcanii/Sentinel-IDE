// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE changed-on-disk prompt — a small dark-themed modal, same shape as
// SaveChangesDialog and UpdateDialog. Two answers, because there really are only
// two: the buffer wins or the file wins.
//
// It shares UpdateDialog's Enter handling rather than SaveChangesDialog's, and the
// reason is which answer is irreversible. In SaveChangesDialog the user asked for
// something (a close, an open) and Save is both the affirmative and the safe answer,
// so it is the default. Here nobody asked for anything -- the prompt arrives because
// another program touched a file -- and the affirmative-looking button is the one
// that destroys typing. So the default is Keep, exactly as v0.1.12 made it Later.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/ReloadDialog.h"
#include "host/win32/Theme.h"

#include <windows.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace sentinelide {
namespace {

enum { IDC_HEAD = 241, IDC_BODY };

struct ReloadState {
    bool done = false;
    ReloadChoice result = ReloadChoice::Keep;   // Esc / close box / anything unexpected
};

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<ReloadState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
        // ENTER MUST MEAN "Keep my edits". See UpdateDialog.cpp for the full account: this
        // is a registered class, not a dialog resource, so DefWindowProc answers DM_GETDEFID
        // with "no default" and IsDialogMessageW's VK_RETURN fallback sends IDOK -- which
        // here is Reload, i.e. an unattended keystroke silently discarding someone's typing.
        case DM_GETDEFID: return MAKELRESULT(IDCANCEL, DC_HASDEFID);
        case WM_COMMAND:
            if (!st) return 0;
            if (LOWORD(w) == IDOK)     { st->result = ReloadChoice::Reload; st->done = true; }
            if (LOWORD(w) == IDCANCEL) { st->result = ReloadChoice::Keep;   st->done = true; }
            return 0;
        case WM_CLOSE: if (st) { st->result = ReloadChoice::Keep; st->done = true; } return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

ReloadChoice showReloadDialog(HWND owner, const std::wstring& fileName) {
    static bool reg = false; HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelReloadDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = nullptr;
        RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    ReloadState st;
    HFONT ui = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT head = CreateFontW(-S(17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    const int M = S(18), clientW = S(460), fw = clientW - 2 * M;
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelReloadDlg", L"Sentinel-IDE",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(120), orc.top + S(120), clientW, S(210), owner, nullptr, hi, &st);
    if (!hwnd) { DeleteObject(ui); DeleteObject(head); return ReloadChoice::Keep; }

    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int cw, int ch, int id, HFONT f) -> HWND {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c;
    };
    // Measure the body rather than reserving lines — same reason as SaveChangesDialog: a
    // static one line short clips silently, and this text is longer than that one's.
    const std::wstring body =
        L"You have unsaved changes to this file. Reloading replaces them with the version on "
        L"disk and cannot be undone; keeping them means your next save overwrites that version.";
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
    mk(L"STATIC", (L"“" + fileName + L"” changed on disk.").c_str(),
       SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, M, yy, fw, S(24), IDC_HEAD, head); yy += S(32);
    mk(L"STATIC", body.c_str(), SS_LEFT | SS_NOPREFIX, M, yy, fw, bodyH, IDC_BODY, ui); yy += bodyH + S(20);

    const int by = yy, bw = S(124), gap = S(8), rx = clientW - M;
    mk(L"BUTTON", L"Reload",        BS_PUSHBUTTON    | WS_TABSTOP, rx - 2 * bw - gap, by, bw, S(28), IDOK, ui);
    mk(L"BUTTON", L"Keep my edits", BS_DEFPUSHBUTTON | WS_TABSTOP, rx - bw,           by, bw, S(28), IDCANCEL, ui);
    const int clientH = by + S(28) + M;

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    // Restore the owner's PRIOR state rather than an unconditional TRUE — same reasoning as
    // UpdateDialog: this is raised from a POSTED message, and every other modal's loop
    // dispatches posted messages, so it can open on top of one that already disabled the owner.
    const BOOL ownerWasEnabled = owner ? IsWindowEnabled(owner) : FALSE;
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd);
    SetFocus(GetDlgItem(hwnd, IDCANCEL));

    // Re-post WM_QUIT — GetMessageW consumes it, and a nested loop that swallows it leaves
    // runApp's outer loop blocked forever with the process still alive. See PasswordDialog.cpp.
    MSG msg;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; }
        // ENTER MUST OBEY FOCUS — the v0.1.12 defect, transplanted. Forcing DM_GETDEFID to
        // IDCANCEL stops a stray Enter reaching Reload, but it also stops IsDialogMessageW
        // routing Enter to whatever the user actually Tabbed to, so a keyboard user pressing
        // Enter on "Reload" would get "Keep". Click the focused button ourselves; focus is
        // explicit intent, an unattended dialog is not.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND f = GetFocus();
            if (f && IsChild(hwnd, f) && (SendMessageW(f, WM_GETDLGCODE, 0, 0) & DLGC_BUTTON)) {
                SendMessageW(f, BM_CLICK, 0, 0);
                continue;
            }
        }
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    if (owner && ownerWasEnabled) EnableWindow(owner, TRUE);
    if (owner && ownerWasEnabled) SetForegroundWindow(owner);
    DestroyWindow(hwnd); DeleteObject(ui); DeleteObject(head);
    return st.result;
}

}  // namespace sentinelide
