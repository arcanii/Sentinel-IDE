// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE Project Settings — a dark-themed modal form over `sentinel.toml`
// (mirrors the SettingsDialog pattern). Edits name/version/type/entry, the build
// source + lib_paths/links + default tier, and the ADR-0061 signing block
// (require/trust/sign). On Save it mutates the passed SentinelProject; the caller
// persists it with saveProject(), which preserves comments + unmodeled keys.
#ifndef UNICODE
#define UNICODE
#endif
#include "ui/ProjectSettingsDialog.h"
#include "ui/Theme.h"
#include "core/Logger.h"

#include <windows.h>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace sentinelide {
namespace {

enum {
    IDC_NAME = 201, IDC_VER, IDC_ENTRY, IDC_SRC, IDC_LIBP, IDC_LINKS, IDC_TIER, IDC_TRUST,
    IDC_TYPE_EXE, IDC_TYPE_LIB, IDC_TYPE_DLL,
    IDC_REQ_OFF, IDC_REQ_WARN, IDC_REQ_STRICT, IDC_SIGN,
    IDC_TGT_SEL, IDC_TGT_NAME, IDC_TGT_ENTRY, IDC_TGT_TYPE_EXE, IDC_TGT_TYPE_LIB, IDC_TGT_TYPE_DLL,
    IDC_HDR_PROJECT = 301, IDC_HDR_BUILD, IDC_HDR_SIGNING, IDC_HDR_TARGETS, IDC_HINT, IDC_NOTE,
};

struct DlgState {
    SentinelProject* p = nullptr;
    bool done = false;
    int  result = IDCANCEL;
    HWND eName = nullptr, eVer = nullptr, eEntry = nullptr, eSrc = nullptr,
         eLibP = nullptr, eLinks = nullptr, cTier = nullptr, eTrust = nullptr;
    HWND cTgtSel = nullptr, eTgtName = nullptr, eTgtEntry = nullptr;   // per-target editing
    int  curTarget = 0;
    bool hasTargets = false;
};

std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n, 0); GetWindowTextW(h, s.data(), n + 1); s.resize(n);
    return s;
}
std::vector<std::wstring> splitList(const std::wstring& s) {
    std::vector<std::wstring> v; std::wstring cur;
    for (wchar_t c : s) { if (c == L',') { auto t = projTrim(cur); if (!t.empty()) v.push_back(t); cur.clear(); } else cur += c; }
    auto t = projTrim(cur); if (!t.empty()) v.push_back(t);
    return v;
}
std::wstring joinList(const std::vector<std::wstring>& v) {
    std::wstring o; for (size_t i = 0; i < v.size(); ++i) { if (i) o += L", "; o += v[i]; } return o;
}

// Commit the visible per-target fields back into the model; load a target into them.
void commitTarget(HWND hwnd, DlgState* st) {
    if (!st->hasTargets || st->curTarget < 0 || st->curTarget >= (int)st->p->targets.size()) return;
    Target& tg = st->p->targets[st->curTarget];
    tg.name  = projTrim(getText(st->eTgtName));
    tg.entry = projTrim(getText(st->eTgtEntry));
    tg.type  = IsDlgButtonChecked(hwnd, IDC_TGT_TYPE_LIB) ? ProjectType::Library
             : IsDlgButtonChecked(hwnd, IDC_TGT_TYPE_DLL) ? ProjectType::Shared
             : ProjectType::Executable;
}
void loadTarget(HWND hwnd, DlgState* st, int idx) {
    if (idx < 0 || idx >= (int)st->p->targets.size()) return;
    const Target& tg = st->p->targets[idx];
    SetWindowTextW(st->eTgtName, tg.name.c_str());
    SetWindowTextW(st->eTgtEntry, tg.entry.c_str());
    CheckRadioButton(hwnd, IDC_TGT_TYPE_EXE, IDC_TGT_TYPE_DLL,
        tg.type == ProjectType::Library ? IDC_TGT_TYPE_LIB : tg.type == ProjectType::Shared ? IDC_TGT_TYPE_DLL : IDC_TGT_TYPE_EXE);
    st->curTarget = idx;
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
            const Theme& th = currentTheme(); HDC hdc = (HDC)w; int id = GetDlgCtrlID((HWND)l);
            if (id == IDC_HDR_PROJECT || id == IDC_HDR_BUILD || id == IDC_HDR_SIGNING) {
                SetTextColor(hdc, th.accent); SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg);
            }
            if (id == IDC_HINT || id == IDC_NOTE) {
                SetTextColor(hdc, th.textMuted); SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg);
            }
            return dialogCtlColor(msg, w);
        }
        case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
            return dialogCtlColor(msg, w);
        case WM_COMMAND: {
            const int id = LOWORD(w);
            if (HIWORD(w) == CBN_SELCHANGE && id == IDC_TGT_SEL && st && st->hasTargets) {
                commitTarget(hwnd, st);                       // save the target we're leaving
                int sel = (int)SendMessageW(st->cTgtSel, CB_GETCURSEL, 0, 0);
                if (sel >= 0) loadTarget(hwnd, st, sel);      // load the one we're switching to
                return 0;
            }
            if (id == IDOK && st) {
                commitTarget(hwnd, st);                       // capture the currently-shown target
                SentinelProject& p = *st->p;
                p.name    = projTrim(getText(st->eName));
                p.version = projTrim(getText(st->eVer));
                p.type    = IsDlgButtonChecked(hwnd, IDC_TYPE_LIB) ? ProjectType::Library
                          : IsDlgButtonChecked(hwnd, IDC_TYPE_DLL) ? ProjectType::Shared
                          : ProjectType::Executable;
                p.entry   = projTrim(getText(st->eEntry));
                p.srcDir  = projTrim(getText(st->eSrc));
                p.libPaths = splitList(getText(st->eLibP));
                p.links    = splitList(getText(st->eLinks));
                int t = (int)SendMessageW(st->cTier, CB_GETCURSEL, 0, 0); if (t < 0) t = 1;
                p.defaultTier = t;
                p.signRequire = IsDlgButtonChecked(hwnd, IDC_REQ_WARN)   ? L"warn"
                              : IsDlgButtonChecked(hwnd, IDC_REQ_STRICT) ? L"strict" : L"off";
                p.trust = projTrim(getText(st->eTrust));
                p.signOutput = IsDlgButtonChecked(hwnd, IDC_SIGN) != 0;
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

bool showProjectSettingsDialog(HWND owner, SentinelProject& p) {
    static bool reg = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelProjectDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    DlgState st; st.p = &p;
    HFONT ui  = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT hdr = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");

    const int M = S(16), lblW = S(88), fldW = S(300), rowH = S(30), x0 = M, fx = M + lblW + S(8);
    const int clientW = fx + fldW + M;

    // Create hidden; lay out with a running y; size the window to fit afterwards.
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelProjectDlg", L"Project Settings",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(80), orc.top + S(60),
                               clientW, S(600), owner, nullptr, hi, &st);
    logMsg(hwnd ? LogLevel::Debug : LogLevel::Error, hwnd ? L"Project Settings dialog created" : L"Project Settings: CreateWindow FAILED");
    if (!hwnd) { DeleteObject(ui); DeleteObject(hdr); return false; }

    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int cx, int cy, int cw, int ch, int id) -> HWND {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, cx, cy, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)ui, TRUE); return c;
    };
    auto lbl = [&](const wchar_t* txt, int yy) { mk(L"STATIC", txt, SS_LEFT, x0, yy + S(5), lblW, S(20), 0); };

    int yy = M;
    // ---- Project --------------------------------------------------------
    { HWND h = mk(L"STATIC", L"PROJECT", SS_LEFT, x0, yy, fldW, S(18), IDC_HDR_PROJECT); SendMessageW(h, WM_SETFONT, (WPARAM)hdr, TRUE); }
    yy += S(24);
    lbl(L"Name", yy);    st.eName = mk(L"EDIT", p.name.c_str(),    ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW, S(22), IDC_NAME); yy += rowH;
    lbl(L"Version", yy); st.eVer  = mk(L"EDIT", p.version.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, S(140), S(22), IDC_VER); yy += rowH;
    lbl(L"Type", yy);
    mk(L"BUTTON", L"Executable", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, fx,          yy + S(2), S(98), S(22), IDC_TYPE_EXE);
    mk(L"BUTTON", L"Library",    BS_AUTORADIOBUTTON,                         fx + S(102), yy + S(2), S(80), S(22), IDC_TYPE_LIB);
    mk(L"BUTTON", L"Shared",     BS_AUTORADIOBUTTON,                         fx + S(186), yy + S(2), S(80), S(22), IDC_TYPE_DLL); yy += rowH;
    lbl(L"Entry", yy);
    st.eEntry = mk(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_GROUP | WS_TABSTOP, fx, yy, fldW, S(220), IDC_ENTRY); yy += rowH;

    // ---- Build ----------------------------------------------------------
    yy += S(8);
    { HWND h = mk(L"STATIC", L"BUILD", SS_LEFT, x0, yy, fldW, S(18), IDC_HDR_BUILD); SendMessageW(h, WM_SETFONT, (WPARAM)hdr, TRUE); }
    yy += S(24);
    lbl(L"Source", yy);    st.eSrc   = mk(L"EDIT", p.srcDir.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, S(140), S(22), IDC_SRC); yy += rowH;
    lbl(L"Lib paths", yy); st.eLibP  = mk(L"EDIT", L"",              ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW, S(22), IDC_LIBP); yy += rowH;
    lbl(L"Links", yy);     st.eLinks = mk(L"EDIT", L"",              ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW, S(22), IDC_LINKS); yy += rowH - S(6);
    mk(L"STATIC", L"comma-separated, relative to the project folder", SS_LEFT, x0, yy, clientW - 2 * M, S(18), IDC_HINT); yy += S(24);
    lbl(L"Default tier", yy);
    st.cTier = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, fx, yy, S(190), S(200), IDC_TIER); yy += rowH;

    // ---- Targets ([[target]] blocks) -----------------------------------
    if (p.explicitTargets) {
        yy += S(8);
        { HWND h = mk(L"STATIC", L"TARGETS", SS_LEFT, x0, yy, fldW, S(18), IDC_HDR_TARGETS); SendMessageW(h, WM_SETFONT, (WPARAM)hdr, TRUE); }
        yy += S(24);
        lbl(L"Target", yy);
        st.cTgtSel = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, fx, yy, S(190), S(200), IDC_TGT_SEL); yy += rowH;
        lbl(L"Name", yy);    st.eTgtName  = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, S(200), S(22), IDC_TGT_NAME); yy += rowH;
        lbl(L"Entry", yy);   st.eTgtEntry = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, fx, yy, fldW, S(22), IDC_TGT_ENTRY); yy += rowH;
        lbl(L"Type", yy);
        mk(L"BUTTON", L"Executable", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, fx,          yy + S(2), S(98), S(22), IDC_TGT_TYPE_EXE);
        mk(L"BUTTON", L"Library",    BS_AUTORADIOBUTTON,                         fx + S(102), yy + S(2), S(80), S(22), IDC_TGT_TYPE_LIB);
        mk(L"BUTTON", L"Shared",     BS_AUTORADIOBUTTON,                         fx + S(186), yy + S(2), S(80), S(22), IDC_TGT_TYPE_DLL); yy += rowH;
        st.hasTargets = true;
    }

    // ---- Signing (ADR 0061) --------------------------------------------
    yy += S(8);
    { HWND h = mk(L"STATIC", L"SIGNING", SS_LEFT, x0, yy, fldW, S(18), IDC_HDR_SIGNING); SendMessageW(h, WM_SETFONT, (WPARAM)hdr, TRUE); }
    yy += S(24);
    lbl(L"Require", yy);
    mk(L"BUTTON", L"off",    BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, fx,          yy + S(2), S(64), S(22), IDC_REQ_OFF);
    mk(L"BUTTON", L"warn",   BS_AUTORADIOBUTTON,                         fx + S(68),  yy + S(2), S(64), S(22), IDC_REQ_WARN);
    mk(L"BUTTON", L"strict", BS_AUTORADIOBUTTON,                         fx + S(136), yy + S(2), S(72), S(22), IDC_REQ_STRICT); yy += rowH;
    lbl(L"Trust", yy);     st.eTrust = mk(L"EDIT", p.trust.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_GROUP | WS_TABSTOP, fx, yy, fldW, S(22), IDC_TRUST); yy += rowH;
    mk(L"BUTTON", L"Sign the built artifact  (snc sign → .sig on a successful build)", BS_AUTOCHECKBOX | WS_TABSTOP, fx, yy, fldW + S(40), S(22), IDC_SIGN); yy += rowH - S(4);
    mk(L"STATIC", L"warn/strict pass --require-signatures --trust; signing uses sentinel.key in the project.", SS_LEFT, x0, yy, clientW - 2 * M, S(18), IDC_NOTE); yy += S(24);

    // ---- buttons --------------------------------------------------------
    const int by = yy + S(8), rx = clientW - M;
    mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, rx - 2 * S(90) - S(8), by, S(90), S(28), IDCANCEL);
    mk(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, rx - S(90), by, S(90), S(28), IDOK);
    const int clientH = by + S(28) + M;

    // Populate combos / radios from the model.
    for (auto t : { L"Development", L"Experimental", L"Stable", L"Hardened" }) SendMessageW(st.cTier, CB_ADDSTRING, 0, (LPARAM)t);
    SendMessageW(st.cTier, CB_SETCURSEL, (WPARAM)(p.defaultTier < 0 || p.defaultTier > 3 ? 1 : p.defaultTier), 0);
    { WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((p.dir + L"\\*.sentinel").c_str(), &fd);
      if (h != INVALID_HANDLE_VALUE) { do SendMessageW(st.eEntry, CB_ADDSTRING, 0, (LPARAM)fd.cFileName); while (FindNextFileW(h, &fd)); FindClose(h); } }
    SetWindowTextW(st.eEntry, p.entry.c_str());
    SetWindowTextW(st.eLibP, joinList(p.libPaths).c_str());
    SetWindowTextW(st.eLinks, joinList(p.links).c_str());
    CheckRadioButton(hwnd, IDC_TYPE_EXE, IDC_TYPE_DLL,
                     p.type == ProjectType::Library ? IDC_TYPE_LIB : p.type == ProjectType::Shared ? IDC_TYPE_DLL : IDC_TYPE_EXE);
    CheckRadioButton(hwnd, IDC_REQ_OFF, IDC_REQ_STRICT,
                     p.signRequire == L"warn" ? IDC_REQ_WARN : p.signRequire == L"strict" ? IDC_REQ_STRICT : IDC_REQ_OFF);
    CheckDlgButton(hwnd, IDC_SIGN, p.signOutput ? BST_CHECKED : BST_UNCHECKED);
    if (p.explicitTargets) {
        for (auto& t : p.targets) SendMessageW(st.cTgtSel, CB_ADDSTRING, 0, (LPARAM)(t.name.empty() ? L"(unnamed)" : t.name.c_str()));
        SendMessageW(st.cTgtSel, CB_SETCURSEL, 0, 0);
        loadTarget(hwnd, &st, 0);
    }

    // Size to fit and centre over the owner.
    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    const int x = orc.left + ((orc.right - orc.left) - fullW) / 2;
    const int y = orc.top + ((orc.bottom - orc.top) - fullH) / 2;
    SetWindowPos(hwnd, nullptr, x, y, fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); SetFocus(st.eName);
    SendMessageW(st.eEntry, CB_SETEDITSEL, 0, MAKELPARAM(0, 0));   // collapse the combo's auto-selection highlight

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    EnableWindow(owner, TRUE); if (owner) SetForegroundWindow(owner);
    DestroyWindow(hwnd); DeleteObject(ui); DeleteObject(hdr);
    logMsg(LogLevel::Debug, L"Project Settings dialog closed (result=" + std::to_wstring(st.result) + L")");
    return st.result == IDOK;
}

}  // namespace sentinelide
