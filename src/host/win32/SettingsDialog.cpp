// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE Settings — a dark-themed modal dialog (mirrors the SQLTerminal
// themed-dialog pattern): editor font, the Direct2D editor opt-in, theme, the log
// level + location, and the build toolchain (the snc compiler + the MSVC
// environment that provides link.exe).
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/SettingsDialog.h"
#include "host/win32/Theme.h"
#include "core/Logger.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comdlg32.lib")

namespace sentinelide {
namespace {

enum { IDC_FONT = 101, IDC_THEME, IDC_LEVEL, IDC_LOG, IDC_REVEAL,
       IDC_SNC, IDC_SNC_BROWSE, IDC_VCVARS, IDC_VCVARS_BROWSE, IDC_D2D,
       IDC_HDR_BUILD = 130, IDC_SNC_HINT, IDC_VCVARS_HINT, IDC_D2D_HINT };

struct DlgState {
    Settings* s = nullptr;
    bool done = false;
    int  result = IDCANCEL;
    HWND eFont = nullptr, cTheme = nullptr, cLevel = nullptr, eLog = nullptr, eSnc = nullptr, eVcvars = nullptr;
};

std::wstring browseFile(HWND owner, const wchar_t* filter, const std::wstring& initial) {
    wchar_t buf[MAX_PATH] = L"";
    if (!initial.empty() && initial.size() < MAX_PATH) wcscpy_s(buf, initial.c_str());
    OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter; ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::wstring(buf) : L"";
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, w, l);
        }
        case WM_ERASEBKGND: { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)w, &rc, themeBrush(currentTheme().panelBg)); return 1; }
        case WM_CTLCOLORSTATIC: {
            const Theme& th = currentTheme(); int id = GetDlgCtrlID((HWND)l); HDC hdc = (HDC)w;
            if (id == IDC_HDR_BUILD) { SetTextColor(hdc, th.accent); SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg); }
            if (id == IDC_SNC_HINT || id == IDC_VCVARS_HINT || id == IDC_D2D_HINT) { SetTextColor(hdc, th.textMuted); SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg); }
            // The checkbox is a BUTTON but sends WM_CTLCOLORSTATIC, and it falls through to
            // dialogCtlColor with every other label: panelBg ground, textPrimary text. Its
            // BOX and its text are drawn by the theme engine, not by this colour — which is
            // why it needs the DarkMode_Explorer hint applyDialogDarkMode pushes onto every
            // child (a stock checkbox would draw a white box and near-black text on the dark
            // panel). Same route as ProjectSettingsDialog's IDC_SIGN, which is the only
            // other checkbox in the app; the light theme takes no hint and needs none.
            return dialogCtlColor(msg, w);
        }
        case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
            return dialogCtlColor(msg, w);
        case WM_COMMAND: {
            const int id = LOWORD(w);
            if (id == IDC_REVEAL && st) {
                wchar_t buf[1024]; GetWindowTextW(st->eLog, buf, 1024);
                std::wstring a = L"/select,\"" + std::wstring(buf) + L"\"";
                ShellExecuteW(hwnd, L"open", L"explorer.exe", a.c_str(), nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (id == IDC_SNC_BROWSE && st) {
                wchar_t cur[MAX_PATH]; GetWindowTextW(st->eSnc, cur, MAX_PATH);
                std::wstring p = browseFile(hwnd, L"snc.exe\0snc.exe\0Executables (*.exe)\0*.exe\0All files\0*.*\0", cur);
                if (!p.empty()) SetWindowTextW(st->eSnc, p.c_str());
                return 0;
            }
            if (id == IDC_VCVARS_BROWSE && st) {
                wchar_t cur[MAX_PATH]; GetWindowTextW(st->eVcvars, cur, MAX_PATH);
                std::wstring p = browseFile(hwnd, L"vcvars64.bat\0vcvars*.bat\0Batch files (*.bat)\0*.bat\0All files\0*.*\0", cur);
                if (!p.empty()) SetWindowTextW(st->eVcvars, p.c_str());
                return 0;
            }
            if (id == IDOK && st) {
                wchar_t buf[1024];
                GetWindowTextW(st->eFont, buf, 1024); st->s->editorFont = buf;
                st->s->themeMode = (int)SendMessageW(st->cTheme, CB_GETCURSEL, 0, 0) - 1;
                st->s->logLevel  = (LogLevel)SendMessageW(st->cLevel, CB_GETCURSEL, 0, 0);
                GetWindowTextW(st->eLog, buf, 1024); st->s->logFile = buf;
                GetWindowTextW(st->eSnc, buf, 1024); st->s->sncPath = buf;
                GetWindowTextW(st->eVcvars, buf, 1024); st->s->vcvarsPath = buf;
                // Written to [editor] d2d by saveSettings; READ once, at WM_CREATE, by
                // createControls' wantD2DEditor(). Nothing here tries to swap the live
                // control — the editor class is chosen at the single CreateWindowExW that
                // built it, and re-creating it mid-session would mean re-homing the text,
                // the undo stack, the dirty state and every host handle that points at it.
                // The hint under the checkbox says "restart" for exactly that reason.
                st->s->d2dEditor = IsDlgButtonChecked(hwnd, IDC_D2D) == BST_CHECKED;
                st->result = IDOK; st->done = true; return 0;
            }
            if (id == IDCANCEL && st) { st->done = true; return 0; }
            return 0;
        }
        case WM_CLOSE: if (st) st->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

bool showSettingsDialog(HWND owner, Settings& s, const std::wstring& resolvedSnc, const std::wstring& resolvedVcvars) {
    static bool reg = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelSettingsDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    DlgState st; st.s = &s;
    HFONT ui = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT hdr = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");

    const int M = S(16), lblW = S(96), fldW = S(320), rowH = S(32), x0 = M, fx = M + lblW + S(8);
    const int clientW = fx + fldW + M;
    const int browseW = S(78);

    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelSettingsDlg", L"Settings",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(80), orc.top + S(60), clientW, S(560), owner, nullptr, hi, &st);
    logMsg(hwnd ? LogLevel::Debug : LogLevel::Error, hwnd ? L"Settings dialog created" : L"Settings dialog: CreateWindow FAILED");
    if (!hwnd) { DeleteObject(ui); DeleteObject(hdr); return false; }

    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int cx, int cy, int cw, int ch, int id, HFONT f) -> HWND {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, cx, cy, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c;
    };

    int yy = M;
    mk(L"STATIC", L"Editor font", SS_LEFT, x0, yy + S(4), lblW, S(20), 0, ui);
    st.eFont = mk(L"EDIT", s.editorFont.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW, S(22), IDC_FONT, ui); yy += rowH;

    // The Direct2D editor (phase 46 slice 5) — the first thing in this dialog the user is
    // being asked to OPT IN to rather than configure, so the label says "preview" and the
    // hint says what it costs. In the field column with no label of its own, which is the
    // shape ProjectSettingsDialog's IDC_SIGN already set for a checkbox in this app.
    // BS_AUTOCHECKBOX, not owner-draw: the state and the keyboard behaviour come free, and
    // it reports itself to UIA/Narrator as a real checkbox with a real checked state, which
    // an owner-drawn BS_OWNERDRAW button does not.
    mk(L"BUTTON", L"Use the Direct2D editor  (preview)", BS_AUTOCHECKBOX | WS_TABSTOP, fx, yy + S(2), fldW, S(22), IDC_D2D, ui);
    CheckDlgButton(hwnd, IDC_D2D, s.d2dEditor ? BST_CHECKED : BST_UNCHECKED);
    yy += rowH - S(8);
    // A wrapping muted line, the same voice as the snc/vcvars hints below. It has to be
    // here: the editor class is chosen once, at WM_CREATE, so a toggle cannot take effect
    // in this session and a checkbox that appears to do nothing is worse than no checkbox.
    // TWO lines at every DPI this ships at, so it is sized for two. S(34) was one line
    // plus a sliver, which clipped the second one mid-word into the Theme row below --
    // caught by screenshotting the dialog, not by anything that compiles.
    mk(L"STATIC", L"Takes effect after a restart. Dragging text within the editor is not supported yet.",
       SS_LEFT, fx, yy, fldW, S(46), IDC_D2D_HINT, ui); yy += S(52);

    mk(L"STATIC", L"Theme", SS_LEFT, x0, yy + S(4), lblW, S(20), 0, ui);
    st.cTheme = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, fx, yy, S(190), S(200), IDC_THEME, ui); yy += rowH;
    for (auto t : { L"Follow system", L"Light", L"Dark" }) SendMessageW(st.cTheme, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(st.cTheme, CB_SETCURSEL, (WPARAM)(s.themeMode + 1), 0);

    mk(L"STATIC", L"Log level", SS_LEFT, x0, yy + S(4), lblW, S(20), 0, ui);
    st.cLevel = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, fx, yy, S(190), S(220), IDC_LEVEL, ui); yy += rowH;
    for (auto t : { L"Error", L"Warn", L"Info", L"Debug", L"Trace" }) SendMessageW(st.cLevel, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(st.cLevel, CB_SETCURSEL, (WPARAM)(int)s.logLevel, 0);

    mk(L"STATIC", L"Log file", SS_LEFT, x0, yy + S(4), lblW, S(20), 0, ui);
    st.eLog = mk(L"EDIT", s.logFile.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW - browseW, S(22), IDC_LOG, ui);
    mk(L"BUTTON", L"Reveal", BS_PUSHBUTTON | WS_TABSTOP, fx + fldW - browseW + S(6), yy - S(1), browseW - S(6), S(24), IDC_REVEAL, ui); yy += rowH + S(10);

    // ---- build toolchain ------------------------------------------------
    { HWND h = mk(L"STATIC", L"BUILD TOOLCHAIN", SS_LEFT, x0, yy, clientW - 2 * M, S(18), IDC_HDR_BUILD, ui); SendMessageW(h, WM_SETFONT, (WPARAM)hdr, TRUE); } yy += S(24);

    mk(L"STATIC", L"Sentinel (snc)", SS_LEFT, x0, yy + S(4), lblW, S(20), 0, ui);
    st.eSnc = mk(L"EDIT", s.sncPath.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW - browseW, S(22), IDC_SNC, ui);
    mk(L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, fx + fldW - browseW + S(6), yy - S(1), browseW - S(6), S(24), IDC_SNC_BROWSE, ui); yy += rowH - S(6);
    mk(L"STATIC", (L"blank = auto-detect →  " + (resolvedSnc.empty() ? std::wstring(L"not found") : resolvedSnc)).c_str(),
       SS_LEFT | SS_PATHELLIPSIS, fx, yy, fldW, S(16), IDC_SNC_HINT, ui); yy += S(24);

    mk(L"STATIC", L"MSVC env", SS_LEFT, x0, yy + S(4), lblW, S(20), 0, ui);
    st.eVcvars = mk(L"EDIT", s.vcvarsPath.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW - browseW, S(22), IDC_VCVARS, ui);
    mk(L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, fx + fldW - browseW + S(6), yy - S(1), browseW - S(6), S(24), IDC_VCVARS_BROWSE, ui); yy += rowH - S(6);
    mk(L"STATIC", (L"vcvars64.bat → link.exe for builds.  auto-detect →  " + (resolvedVcvars.empty() ? std::wstring(L"not found") : resolvedVcvars)).c_str(),
       SS_LEFT | SS_PATHELLIPSIS, fx, yy, fldW, S(16), IDC_VCVARS_HINT, ui); yy += S(24);

    const int by = yy + S(10), rx = clientW - M;
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, rx - 2 * S(90) - S(8), by, S(90), S(28), IDCANCEL, ui);
    mk(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, rx - S(90), by, S(90), S(28), IDOK, ui);
    const int clientH = by + S(28) + M;

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); SetFocus(st.eFont);

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
    DestroyWindow(hwnd); DeleteObject(ui); DeleteObject(hdr);
    logMsg(LogLevel::Debug, L"Settings dialog closed (result=" + std::to_wstring(st.result) + L")");
    return st.result == IDOK;
}

}  // namespace sentinelide
