// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE Signing & Trust panel (ADR 0061) — a dark-themed modal that views the
// consumer trust manifest and drives the *real* snc signing subcommands over the open
// file: keygen, sign (with capability grants), verify. The signed object is the file's
// raw bytes (comments included); trust is the consumer's own sentinel-trust.toml.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/SigningDialog.h"
#include "host/win32/Theme.h"
#include "core/Logger.h"
#include "core/Signing.h"

#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

namespace sentinelide {
namespace {

enum {
    IDC_GENKEY = 401, IDC_BROWSEKEY, IDC_SIGN, IDC_VERIFY, IDC_IMPORT,
    IDC_KEYFILE, IDC_GRANTS, IDC_LIST, IDC_CLOSE,
    IDC_STATE = 420, IDC_DETAIL, IDC_HDR_FILE, IDC_HDR_TRUST, IDC_FILE, IDC_NOTE,
};

struct SignDlg {
    std::wstring snc, file, dir, trustPath;
    SncSigningCaps caps;                 // verify and keygen/sign are separate — see Signing.h
    bool done = false;
    SignState state = SignState::Unsigned;
    std::wstring detail, signKey, signGrants;
    HWND eKey = nullptr, eGrants = nullptr, list = nullptr, stState = nullptr, stDetail = nullptr;
};

std::wstring getText(HWND h) { int n = GetWindowTextLengthW(h); std::wstring s(n, 0); GetWindowTextW(h, s.data(), n + 1); s.resize(n); return s; }
std::wstring base(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? p : p.substr(s + 1); }
std::wstring stem(const std::wstring& p) { std::wstring b = base(p); size_t d = b.find_last_of(L'.'); return d == std::wstring::npos ? b : b.substr(0, d); }

std::wstring stateLabel(SignState s) {
    switch (s) {
        case SignState::Signed:   return L"✓   Signed";
        case SignState::Invalid:  return L"⚠   Signature invalid";
        case SignState::Checking: return L"…   verifying";
        case SignState::Unknown:  return L"—   no file open";
        default:                  return L"⊘   Unsigned";
    }
}
COLORREF stateColor(SignState s) {
    const Theme& th = currentTheme();
    return s == SignState::Signed ? th.trustVerified : s == SignState::Invalid ? th.diagError : th.textSecondary;
}

std::wstring joinCsv(const std::vector<std::wstring>& v) { std::wstring o; for (size_t i = 0; i < v.size(); ++i) { if (i) o += L", "; o += v[i]; } return o; }
std::vector<std::wstring> splitCsv(const std::wstring& s) {
    std::vector<std::wstring> v; std::wstring cur;
    for (wchar_t c : s) { if (c == L',') { auto t = projTrim(cur); if (!t.empty()) v.push_back(t); cur.clear(); } else cur += c; }
    auto t = projTrim(cur); if (!t.empty()) v.push_back(t); return v;
}

void fillTrustList(HWND list, const std::wstring& trustPath) {
    ListView_DeleteAllItems(list);
    TrustManifest m = loadTrust(trustPath);
    int row = 0;
    for (auto& k : m.keys) {
        std::wstring label = k.name.empty() ? L"(unnamed)" : k.name;
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = row; it.pszText = const_cast<LPWSTR>(label.c_str());
        ListView_InsertItem(list, &it);
        std::wstring key = shortKey(k.pubkey), grants = joinCsv(k.grants);
        ListView_SetItemText(list, row, 1, const_cast<LPWSTR>(key.c_str()));
        ListView_SetItemText(list, row, 2, const_cast<LPWSTR>(grants.c_str()));
        row++;
    }
}

void recompute(SignDlg& st) {
    st.signKey.clear(); st.signGrants.clear();
    if (st.file.empty()) { st.state = SignState::Unknown; st.detail = L"Open a .sentinel file to sign or verify it."; }
    else {
        std::wstring sig = st.file + L".sig";
        if (GetFileAttributesW(sig.c_str()) == INVALID_FILE_ATTRIBUTES) { st.state = SignState::Unsigned; st.detail = L"No detached signature (" + base(sig) + L") next to this file."; }
        else {
            SigInfo si = readSig(sig); st.signKey = si.key; st.signGrants = si.grants;
            if (st.caps.verify) {
                VerifyResult r = verifyFile(st.snc, st.file); st.state = r.state;
                st.detail = (r.state == SignState::Signed) ? L"Verified — the file bytes match the signature." : projTrim(r.message);
            } else { st.state = SignState::Signed; st.detail = L"Signature present — this snc build can't verify it."; }
        }
    }
    SetWindowTextW(st.stState, stateLabel(st.state).c_str());
    std::wstring d = st.detail;
    if (!st.signKey.empty()) d += L"\nKey: " + shortKey(st.signKey) + (st.signGrants.empty() ? L"" : L"     Grants: " + st.signGrants);
    SetWindowTextW(st.stDetail, d.c_str());
    InvalidateRect(GetParent(st.stState), nullptr, FALSE);
}

std::wstring pickPath(HWND owner, bool save, const std::wstring& defName, const std::wstring& defDir) {
    IFileDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) return L"";
    if (!defName.empty()) pfd->SetFileName(defName.c_str());
    if (!defDir.empty()) { IShellItem* si = nullptr; if (SUCCEEDED(SHCreateItemFromParsingName(defDir.c_str(), nullptr, IID_PPV_ARGS(&si)))) { pfd->SetFolder(si); si->Release(); } }
    std::wstring out;
    if (SUCCEEDED(pfd->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) { PWSTR p = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = p; CoTaskMemFree(p); } item->Release(); }
    }
    pfd->Release();
    return out;
}

// blocking snc call with a wait cursor
DWORD runWait(const std::wstring& cmd, const std::wstring& dir, std::wstring& out) {
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    DWORD code = runCapture(cmd, dir, out);
    SetCursor(old);
    return code;
}

void doGenKey(HWND hwnd, SignDlg& st) {
    std::wstring path = pickPath(hwnd, true, L"sentinel.key", st.dir);
    if (path.empty()) return;
    std::wstring out; DWORD code = runWait(L"\"" + st.snc + L"\" keygen -o \"" + path + L"\"", st.dir, out);
    logMsg(code == 0 ? LogLevel::Info : LogLevel::Warn, L"snc keygen → " + path + L" (exit " + std::to_wstring((int)code) + L")");
    if (code == 0) SetWindowTextW(st.eKey, path.c_str());
    MessageBoxW(hwnd, projTrim(out).c_str(), code == 0 ? L"Key generated" : L"keygen failed",
                MB_OK | (code == 0 ? MB_ICONINFORMATION : MB_ICONERROR));
}

void doBrowseKey(HWND hwnd, SignDlg& st) {
    std::wstring path = pickPath(hwnd, false, L"", st.dir);
    if (!path.empty()) SetWindowTextW(st.eKey, path.c_str());
}

void doSign(HWND hwnd, SignDlg& st) {
    if (st.file.empty()) { MessageBoxW(hwnd, L"Open a .sentinel file first.", L"Sign", MB_OK | MB_ICONINFORMATION); return; }
    std::wstring key = projTrim(getText(st.eKey));
    if (key.empty()) { MessageBoxW(hwnd, L"Choose a key file (Browse) or Generate one first.", L"Sign", MB_OK | MB_ICONINFORMATION); return; }
    std::wstring cmd = L"\"" + st.snc + L"\" sign \"" + st.file + L"\" --key \"" + key + L"\"";
    for (auto& g : splitCsv(getText(st.eGrants))) cmd += L" --grant " + g;
    std::wstring out; DWORD code = runWait(cmd, st.dir, out);
    logMsg(code == 0 ? LogLevel::Info : LogLevel::Warn, L"snc sign " + base(st.file) + L" (exit " + std::to_wstring((int)code) + L")");
    if (code != 0) MessageBoxW(hwnd, projTrim(out).c_str(), L"Sign failed", MB_OK | MB_ICONERROR);
    recompute(st);
}

void doImport(HWND hwnd, SignDlg& st) {
    if (st.trustPath.empty()) { MessageBoxW(hwnd, L"Open a project/folder first.", L"Import", MB_OK | MB_ICONINFORMATION); return; }
    if (st.file.empty()) { MessageBoxW(hwnd, L"Open a signed .sentinel file first.", L"Import", MB_OK | MB_ICONINFORMATION); return; }
    SigInfo si = readSig(st.file + L".sig");
    if (!si.present || si.key.empty()) { MessageBoxW(hwnd, L"This file has no signature to trust — sign it first.", L"Import", MB_OK | MB_ICONINFORMATION); return; }
    std::wstring name = stem(st.file);
    TrustManifest m = loadTrust(st.trustPath);
    for (auto& k : m.keys)
        if (_wcsicmp(k.pubkey.c_str(), si.key.c_str()) == 0) {
            MessageBoxW(hwnd, L"That key is already in the trust manifest.", L"Import", MB_OK | MB_ICONINFORMATION); return;
        }

    // snc's schema (deny_unknown_fields): [[keys]] with a BARE 64-hex pubkey. An
    // "ed25519:" prefix parses but never matches — it would silently read as UNTRUSTED.
    std::wstring block = L"\n[[keys]]\n";
    block += L"name   = \"" + name + L"\"\n";
    block += L"pubkey = \"" + si.key + L"\"\n";
    auto grants = splitCsv(si.grants);
    if (!grants.empty()) { std::wstring g; for (size_t i = 0; i < grants.size(); ++i) { if (i) g += L", "; g += L"\"" + grants[i] + L"\""; } block += L"grants = [" + g + L"]\n"; }

    std::wstring text = readUtf8(st.trustPath);
    if (text.empty())
        text = L"# sentinel-trust.toml — consumer trust manifest (ADR 0061).\n"
               L"# Schema: [[keys]] with a bare 64-hex `pubkey` (no \"ed25519:\" prefix).\n"
               L"# Consumed by: snc build --require-signatures warn|strict --trust <this file>\n";
    text += block;
    std::wstring crlf; for (wchar_t c : text) { if (c == L'\r') continue; if (c == L'\n') crlf += L"\r\n"; else crlf += c; }
    if (!writeUtf8(st.trustPath, crlf)) { MessageBoxW(hwnd, L"Could not write the trust manifest.", L"Import", MB_OK | MB_ICONERROR); return; }
    logMsg(LogLevel::Info, L"Trust: added key " + shortKey(si.key) + L" ('" + name + L"') to " + st.trustPath);
    fillTrustList(st.list, st.trustPath);
    MessageBoxW(hwnd, (L"Added the key for '" + name + L"' to the trust manifest.").c_str(), L"Trust updated", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<SignDlg*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, w, l);
        }
        case WM_ERASEBKGND: { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)w, &rc, themeBrush(currentTheme().panelBg)); return 1; }
        case WM_CTLCOLORSTATIC: {
            const Theme& th = currentTheme(); HDC hdc = (HDC)w; int id = GetDlgCtrlID((HWND)l);
            COLORREF fg = th.textPrimary;
            if (id == IDC_HDR_FILE || id == IDC_HDR_TRUST) fg = th.accent;
            else if (id == IDC_NOTE) fg = th.textMuted;
            else if (id == IDC_STATE) fg = stateColor(st ? st->state : SignState::Unsigned);
            SetTextColor(hdc, fg); SetBkColor(hdc, th.panelBg); return (LRESULT)themeBrush(th.panelBg);
        }
        case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
            return dialogCtlColor(msg, w);
        case WM_COMMAND: {
            const int id = LOWORD(w);
            if (!st) return 0;
            switch (id) {
                case IDC_GENKEY:   doGenKey(hwnd, *st); return 0;
                case IDC_BROWSEKEY:doBrowseKey(hwnd, *st); return 0;
                case IDC_SIGN:     doSign(hwnd, *st); return 0;
                case IDC_VERIFY:   recompute(*st); return 0;
                case IDC_IMPORT:   doImport(hwnd, *st); return 0;
                case IDC_CLOSE: case IDCANCEL: st->done = true; return 0;
            }
            return 0;
        }
        case WM_CLOSE: if (st) st->done = true; return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

}  // namespace

void showSigningDialog(HWND owner, const std::wstring& sncPath, SncSigningCaps caps,
                       const std::wstring& filePath, const std::wstring& dir,
                       const std::wstring& trustPath) {
    static bool reg = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc; wc.hInstance = hi;
        wc.lpszClassName = L"SentinelSigningDlg"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; RegisterClassExW(&wc); reg = true;
    }
    const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    auto S = [dpi](int v) { return dpiScale(v, dpi); };

    SignDlg st; st.snc = sncPath; st.caps = caps; st.file = filePath; st.dir = dir; st.trustPath = trustPath;

    HFONT ui  = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT hdr = CreateFontW(-S(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    HFONT big = CreateFontW(-S(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");

    const int M = S(16), clientW = S(620), x0 = M;
    RECT orc{}; if (owner) GetWindowRect(owner, &orc); else SystemParametersInfoW(SPI_GETWORKAREA, 0, &orc, 0);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SentinelSigningDlg", L"Signing & Trust",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, orc.left + S(70), orc.top + S(50), clientW, S(640), owner, nullptr, hi, &st);
    if (!hwnd) { DeleteObject(ui); DeleteObject(hdr); DeleteObject(big); return; }

    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int cx, int cy, int cw, int ch, int id, HFONT f) -> HWND {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, cx, cy, cw, ch, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c;
    };
    const int btnH = S(26), bw = S(116);

    int yy = M;
    mk(L"STATIC", L"FILE SIGNATURE", SS_LEFT, x0, yy, clientW - 2 * M, S(18), IDC_HDR_FILE, hdr); yy += S(24);
    std::wstring fileLine = st.file.empty() ? L"(no file open)" : base(st.file);
    mk(L"STATIC", (L"File:  " + fileLine).c_str(), SS_LEFT, x0, yy, clientW - 2 * M, S(20), IDC_FILE, ui); yy += S(26);
    st.stState  = mk(L"STATIC", L"", SS_LEFT, x0, yy, clientW - 2 * M, S(28), IDC_STATE, big); yy += S(32);
    st.stDetail = mk(L"STATIC", L"", SS_LEFT, x0, yy, clientW - 2 * M, S(40), IDC_DETAIL, ui); yy += S(48);

    mk(L"STATIC", L"Key file", SS_LEFT, x0, yy + S(4), S(64), S(20), 0, ui);
    st.eKey = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, x0 + S(70), yy, clientW - 2 * M - S(70) - 2 * (bw + S(8)), S(22), IDC_KEYFILE, ui);
    mk(L"BUTTON", L"Browse…",      BS_PUSHBUTTON | WS_TABSTOP, clientW - M - 2 * (bw + S(0)) - S(8), yy - S(1), bw, btnH, IDC_BROWSEKEY, ui);
    mk(L"BUTTON", L"Generate Key…",BS_PUSHBUTTON | WS_TABSTOP, clientW - M - bw,                    yy - S(1), bw, btnH, IDC_GENKEY, ui); yy += S(32);

    mk(L"STATIC", L"Grants", SS_LEFT, x0, yy + S(4), S(64), S(20), 0, ui);
    st.eGrants = mk(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, x0 + S(70), yy, S(260), S(22), IDC_GRANTS, ui);
    mk(L"BUTTON", L"Sign File",  BS_DEFPUSHBUTTON | WS_TABSTOP, clientW - M - 2 * (bw + S(8)) + S(8), yy - S(1), bw, btnH, IDC_SIGN, ui);
    mk(L"BUTTON", L"Verify",     BS_PUSHBUTTON | WS_TABSTOP,    clientW - M - bw,                     yy - S(1), bw, btnH, IDC_VERIFY, ui); yy += S(30);
    mk(L"STATIC", L"capabilities the author vouches for (comma-separated) — e.g. secret, alloc", SS_LEFT, x0 + S(70), yy, clientW - 2 * M - S(70), S(18), IDC_NOTE, ui); yy += S(28);

    mk(L"STATIC", L"TRUSTED KEYS  ·  sentinel-trust.toml", SS_LEFT, x0, yy, clientW - 2 * M, S(18), IDC_HDR_TRUST, hdr); yy += S(24);
    const int listH = S(150);
    st.list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | WS_TABSTOP,
                              x0, yy, clientW - 2 * M, listH, hwnd, (HMENU)(INT_PTR)IDC_LIST, hi, nullptr);
    ListView_SetExtendedListViewStyle(st.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    SendMessageW(st.list, WM_SETFONT, (WPARAM)ui, TRUE);
    { const Theme& th = currentTheme();
      SetWindowTheme(st.list, th.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
      ListView_SetBkColor(st.list, th.panelBg); ListView_SetTextColor(st.list, th.textPrimary); ListView_SetTextBkColor(st.list, th.panelBg); }
    struct { const wchar_t* t; int w; } cols[] = { { L"Name", S(160) }, { L"Trusted key", S(210) }, { L"Grants (ceiling)", S(200) } };
    for (int i = 0; i < 3; i++) { LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i].t; c.cx = cols[i].w; ListView_InsertColumn(st.list, i, &c); }
    fillTrustList(st.list, st.trustPath);
    yy += listH + S(8);

    mk(L"BUTTON", L"Import current key as trusted…", BS_PUSHBUTTON | WS_TABSTOP, x0, yy, S(230), btnH, IDC_IMPORT, ui); yy += S(34);

    const int by = yy + S(2);
    mk(L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, clientW - M - S(96), by, S(96), S(28), IDC_CLOSE, ui);
    const int clientH = by + S(28) + M;

    // Gate each action on the capability it actually needs — they fail independently.
    // A release snc verifies fine but has no keygen_core/sign_core beside it, so
    // enabling Generate Key / Sign there would offer buttons that error at runtime.
    if (!st.caps.sign) {
        for (int id : { IDC_GENKEY, IDC_SIGN }) EnableWindow(GetDlgItem(hwnd, id), FALSE);
        st.detail = st.caps.verify
            ? L"This snc can verify but not sign — keygen_core/sign_core are missing beside it."
            : L"This snc build has no keygen/sign/verify — the trust viewer is read-only.";
    }
    if (!st.caps.verify) EnableWindow(GetDlgItem(hwnd, IDC_VERIFY), FALSE);
    recompute(st);

    RECT wr{ 0, 0, clientW, clientH };
    AdjustWindowRectExForDpi(&wr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int fullW = wr.right - wr.left, fullH = wr.bottom - wr.top;
    SetWindowPos(hwnd, nullptr, orc.left + ((orc.right - orc.left) - fullW) / 2, orc.top + ((orc.bottom - orc.top) - fullH) / 2,
                 fullW, fullH, SWP_NOZORDER | SWP_NOACTIVATE);

    applyDialogDarkMode(hwnd);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); SetFocus(GetDlgItem(hwnd, IDC_CLOSE));

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    EnableWindow(owner, TRUE); if (owner) SetForegroundWindow(owner);
    DestroyWindow(hwnd); DeleteObject(ui); DeleteObject(hdr); DeleteObject(big);
}

}  // namespace sentinelide
