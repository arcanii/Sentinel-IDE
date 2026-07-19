// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE main window — phase 4. The themed shell (phases 1-3: DWM dark
// titlebar, ≡ popup menu, dark TreeView + RichEdit with Sentinel syntax
// highlighting, draggable splitter, Open Folder) now drives the build loop:
// Build/Run spawn snc.exe on a worker thread, stream stdout/stderr live into a
// dark Output pane, and parse miette diagnostics (file:line:col) into a clickable
// Problems list. Logging + Settings + signing arrive in phase 5.
#ifndef UNICODE
#define UNICODE
#endif
#include "host/win32/MainWindow.h"
#include "Version.h"     // generated: SENTINEL_VERSION_DISPLAY_W (marketing + auto build number)
#include "host/win32/Theme.h"
#include "core/Logger.h"
#include "core/Settings.h"
#include "core/Project.h"
#include "core/Signing.h"
#include "core/Toolchain.h"
#include "core/Seal.h"
#include "core/FileAssoc.h"
#include "host/win32/SettingsDialog.h"
#include "host/win32/ProjectSettingsDialog.h"
#include "host/win32/SigningDialog.h"
#include "host/win32/AboutDialog.h"
#include "host/win32/PasswordDialog.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>          // Text Object Model — ITextDocument, for undo suspend/resume
#include <shobjidl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <cwctype>
#include <thread>
#include <shellapi.h>
#include <cstdlib>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace sentinelide {
namespace {

constexpr wchar_t kClassName[] = L"SentinelIDEMainWindow";
constexpr wchar_t kAppName[]   = L"Sentinel-IDE";
constexpr wchar_t kVersion[]   = SENTINEL_VERSION_DISPLAY_W;
constexpr UINT WM_APP_LINE = WM_APP + 1;   // lParam = heap wchar_t* (UI frees)
constexpr UINT WM_APP_DONE = WM_APP + 2;   // wParam = exit code
constexpr UINT WM_APP_SIGN = WM_APP + 3;   // wParam = SignState, lParam = heap "file\tkey\tgrants" (UI frees)

enum CtrlId : int { IDC_TREE = 2001, IDC_EDIT = 2002, IDC_OUT = 2003, IDC_PROBLEMS = 2004 };
enum TreeImg : int { IMG_PROJECT = 0, IMG_FOLDER, IMG_FILE, IMG_TOML };
enum MenuId : UINT { ID_OPEN_PROJECT = 1001, ID_NEW_PROJECT, ID_NEW_FILE, ID_SAVE, ID_UNDO, ID_REDO, ID_PROJECT_SETTINGS, ID_BUILD, ID_RUN, ID_SIGNING, ID_LINE_NUMBERS, ID_SETTINGS, ID_ABOUT, ID_EXIT,
                     ID_CLOSE_PROJECT, ID_RECENT_CLEAR, ID_SEAL_PROJECT, ID_OPEN_SEALED, ID_FILE_ASSOC,
                     ID_TIER_DEV = 1100, ID_TIER_EXP, ID_TIER_STABLE, ID_TIER_HARD,
                     ID_TARGET_BASE = 1200,    // ID_TARGET_BASE + <target index>
                     ID_RECENT_BASE = 1300 };  // ID_RECENT_BASE + <recent index>
// Tree lParam sentinels (all negative so the TVN_SELCHANGED file-open guard `data >= 0` skips them).
constexpr LPARAM kProjectSettingsNode = -2;   // project root → opens Project Settings
constexpr LPARAM kTargetNodeBase      = -100; // a target node → kTargetNodeBase - <target index>

struct Diag { std::wstring file; int line = 1, col = 1; std::wstring msg; };

struct AppState {
    HINSTANCE hInst = nullptr;
    Settings settings;
    int   dpi = 96;
    HFONT ui = nullptr, uiSm = nullptr, title = nullptr, mono = nullptr;
    HWND  hTree = nullptr, hEdit = nullptr, hOut = nullptr, hProblems = nullptr;
    ITextDocument* textDoc = nullptr;   // editor's TOM doc — undo suspend/resume (lazily fetched)
    HIMAGELIST himl = nullptr;
    HDC   memDC = nullptr; HBITMAP memBmp = nullptr; int memW = 0, memH = 0;  // cached paint back-buffer
    int   sidebarView = 0;  // 0 = Project, 1 = Files
    int   sidebarW = 240, dockH = 200;
    bool  folderOpen = false, fileOpen = false, dragV = false, highlighting = false, building = false;
    bool  dirty = false, loadingFile = false, lineNumbers = false, errorMarks = false;
    bool  tbCanUndo = false, tbCanRedo = false;   // last-painted undo/redo button state (repaint on change)
    int   gutterW = 0;          // line-number gutter width (0 = off / no file)
    int   dockTab = 0;  // 0 = Problems, 1 = Output
    SentinelProject project;
    int   tier = 1;  // 0=dev 1=experimental 2=stable 3=hardened (TIERED_RELEASES.md)
    int   target = 0;  // active build target (index into project.targets)
    std::wstring rootPath, curFilePath, curFileName, sncPath;
    std::wstring statusLeft = L"Ln 1, Col 1", statusMsg = L"Open a folder to start", pendingMsg;
    std::vector<std::wstring> nodePaths;
    std::vector<Diag> problems;
    SncSigningCaps sncCaps;                       // what the active snc can do: verify / keygen+sign (they differ)
    std::wstring vcvarsPath;                       // resolved MSVC vcvars64.bat (link.exe env for builds)
    SignState signState = SignState::Unknown;     // signature state of the open file
    std::wstring signKey, signGrants;             // from the open file's .sig / verify
    RECT rToolbar{}, rMenuBtn{}, rBuild{}, rRun{}, rSave{}, rUndo{}, rRedo{}, rScheme{}, rSchemeTarget{}, rSchemeTier{}, rTreeTabs{}, rProjectTab{}, rFilesTab{},
         rTree{}, rVSplit{}, rTabs{}, rEditor{}, rGutter{},
         rDock{}, rDockBody{}, rProblemsTab{}, rOutputTab{}, rStatus{}, rStatusSign{};
};
AppState g;

int sc(int v) { return MulDiv(v, g.dpi, 96); }

// The active build target (clamped). Projects always have >=1 after load.
const Target& activeTarget() {
    static Target fallback;
    if (!g.project.loaded || g.project.targets.empty()) return fallback;
    if (g.target < 0 || g.target >= (int)g.project.targets.size()) g.target = 0;
    return g.project.targets[g.target];
}

// ---- fonts ----------------------------------------------------------------
void destroyFonts() { for (HFONT* f : { &g.ui, &g.uiSm, &g.title, &g.mono }) if (*f) { DeleteObject(*f); *f = nullptr; } }
HFONT makeFont(int px, const wchar_t* face, int weight) {
    return CreateFontW(-MulDiv(px, g.dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}
void createFonts() {
    destroyFonts();
    g.ui = makeFont(15, L"Segoe UI", FW_NORMAL);
    g.uiSm = makeFont(12, L"Segoe UI", FW_NORMAL);
    g.title = makeFont(30, L"Segoe UI", FW_SEMIBOLD);
    g.mono = makeFont(14, g.settings.editorFont.c_str(), FW_NORMAL);
}

// ---- undo recording (Text Object Model) -----------------------------------
// The highlighter and error-tints call EM_SETCHARFORMAT on every edit; each such
// programmatic format would otherwise land on RichEdit's native undo stack, so Ctrl+Z
// would undo a *color change* rather than the user's edit. We suspend undo recording
// around all programmatic formatting via ITextDocument::Undo(tomSuspend/tomResume).
ITextDocument* editorDoc() {
    if (g.textDoc || !g.hEdit) return g.textDoc;
    IUnknown* unk = nullptr;
    SendMessageW(g.hEdit, EM_GETOLEINTERFACE, 0, (LPARAM)&unk);
    if (unk) { unk->QueryInterface(__uuidof(ITextDocument), (void**)&g.textDoc); unk->Release(); }
    return g.textDoc;
}
void suspendUndo() { if (ITextDocument* d = editorDoc()) d->Undo(tomSuspend, nullptr); }
void resumeUndo()  { if (ITextDocument* d = editorDoc()) d->Undo(tomResume, nullptr); }

void styleEditor(HWND hEdit, COLORREF bg) {
    const Theme& th = currentTheme();
    SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)bg);
    bool guard = (hEdit == g.hEdit);   // keep this base reformat off the user-visible undo stack
    if (guard) suspendUndo();
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = th.textPrimary; cf.yHeight = 11 * 20;
    wcscpy_s(cf.szFaceName, g.settings.editorFont.c_str());
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    if (guard) resumeUndo();
}

void createControls(HWND hwnd) {
    const Theme& th = currentTheme();
    g.hTree = CreateWindowExW(0, WC_TREEVIEW, L"",
        WS_CHILD | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_TREE, g.hInst, nullptr);
    TreeView_SetBkColor(g.hTree, th.panelBg);
    TreeView_SetTextColor(g.hTree, th.textPrimary);
    if (th.dark) SetWindowTheme(g.hTree, L"DarkMode_Explorer", nullptr);
    SendMessageW(g.hTree, TVM_SETEXTENDEDSTYLE, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    SendMessageW(g.hTree, WM_SETFONT, (WPARAM)g.ui, TRUE);
    // Tree icons: project (S-shield), folder, .sentinel file, .toml.
    int icx = GetSystemMetrics(SM_CXSMICON), icy = GetSystemMetrics(SM_CYSMICON);
    g.himl = ImageList_Create(icx, icy, ILC_COLOR32 | ILC_MASK, 4, 1);
    if (HICON pr = (HICON)LoadImageW(g.hInst, MAKEINTRESOURCEW(100), IMAGE_ICON, icx, icy, LR_DEFAULTCOLOR)) { ImageList_AddIcon(g.himl, pr); DestroyIcon(pr); }
    SHFILEINFOW sfi{};
    SHGetFileInfoW(L"f", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES); if (sfi.hIcon) { ImageList_AddIcon(g.himl, sfi.hIcon); DestroyIcon(sfi.hIcon); }   // IMG_FOLDER
    // IMG_FILE — custom .sentinel source icon (resource 101); fall back to the shell's generic file icon.
    if (HICON fi = (HICON)LoadImageW(g.hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, icx, icy, LR_DEFAULTCOLOR)) { ImageList_AddIcon(g.himl, fi); DestroyIcon(fi); }
    else { SHGetFileInfoW(L"f.sentinel", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES); if (sfi.hIcon) { ImageList_AddIcon(g.himl, sfi.hIcon); DestroyIcon(sfi.hIcon); } }
    SHGetFileInfoW(L"f.toml", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES); if (sfi.hIcon) { ImageList_AddIcon(g.himl, sfi.hIcon); DestroyIcon(sfi.hIcon); }   // IMG_TOML
    TreeView_SetImageList(g.hTree, g.himl, TVSIL_NORMAL);

    LoadLibraryW(L"Msftedit.dll");
    g.hEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | ES_MULTILINE | ES_NOHIDESEL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_EDIT, g.hInst, nullptr);
    if (th.dark) SetWindowTheme(g.hEdit, L"DarkMode_Explorer", nullptr);
    SendMessageW(g.hEdit, EM_SETEVENTMASK, 0, ENM_SELCHANGE | ENM_CHANGE | ENM_SCROLL);
    SendMessageW(g.hEdit, EM_SETTARGETDEVICE, 0, 1);   // no word-wrap → width changes don't reflow (code-editor default; smooth splitter)
    styleEditor(g.hEdit, th.windowBg);

    g.hOut = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
        WS_CHILD | ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_OUT, g.hInst, nullptr);
    if (th.dark) SetWindowTheme(g.hOut, L"DarkMode_Explorer", nullptr);
    SendMessageW(g.hOut, EM_SETEVENTMASK, 0, ENM_LINK);   // EN_LINK for clickable file:line:col
    styleEditor(g.hOut, th.windowBg);

    g.hProblems = CreateWindowExW(0, WC_LISTVIEW, L"",
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_PROBLEMS, g.hInst, nullptr);
    ListView_SetExtendedListViewStyle(g.hProblems, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    if (th.dark) SetWindowTheme(g.hProblems, L"DarkMode_Explorer", nullptr);
    ListView_SetBkColor(g.hProblems, th.panelBg);
    ListView_SetTextColor(g.hProblems, th.textPrimary);
    ListView_SetTextBkColor(g.hProblems, th.panelBg);
    SendMessageW(g.hProblems, WM_SETFONT, (WPARAM)g.ui, TRUE);
    LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = (LPWSTR)L"Message"; c.cx = sc(440); ListView_InsertColumn(g.hProblems, 0, &c);
    c.pszText = (LPWSTR)L"File";    c.cx = sc(150); ListView_InsertColumn(g.hProblems, 1, &c);
    c.pszText = (LPWSTR)L"Line";    c.cx = sc(60);  ListView_InsertColumn(g.hProblems, 2, &c);
}

void showDock() {
    ShowWindow(g.hProblems, g.dockTab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.hOut, g.dockTab == 1 ? SW_SHOW : SW_HIDE);
}

// ---- layout ---------------------------------------------------------------
void layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int toolbarH = sc(40), statusH = sc(24), tabsH = sc(34), gutter = sc(6), hdrH = sc(31);
    g.rToolbar = { 0, 0, W, toolbarH };
    g.rMenuBtn = { sc(8), sc(7), sc(8) + sc(30), sc(7) + sc(26) };
    g.rBuild   = { g.rMenuBtn.right + sc(8), sc(7), g.rMenuBtn.right + sc(8) + sc(82), sc(7) + sc(26) };
    g.rRun     = { g.rBuild.right + sc(8), sc(7), g.rBuild.right + sc(8) + sc(58), sc(7) + sc(26) };
    g.rSave    = { g.rRun.right + sc(8), sc(7), g.rRun.right + sc(8) + sc(62), sc(7) + sc(26) };
    g.rUndo    = { g.rSave.right + sc(10), sc(7), g.rSave.right + sc(10) + sc(30), sc(7) + sc(26) };
    g.rRedo    = { g.rUndo.right + sc(4), sc(7), g.rUndo.right + sc(4) + sc(30), sc(7) + sc(26) };
    g.rScheme  = { g.rRedo.right + sc(10), sc(7), g.rRedo.right + sc(10) + sc(280), sc(7) + sc(26) };
    { int div = g.rScheme.left + sc(176);   // target zone | tier zone
      g.rSchemeTarget = { g.rScheme.left, g.rScheme.top, div, g.rScheme.bottom };
      g.rSchemeTier   = { div, g.rScheme.top, g.rScheme.right, g.rScheme.bottom }; }
    g.rStatus  = { 0, H - statusH, W, H };
    const int bodyTop = toolbarH, bodyBot = H - statusH;

    g.sidebarW = std::max(sc(140), std::min(g.sidebarW, W - sc(320)));
    g.dockH    = std::max(sc(80), std::min(g.dockH, H - sc(260)));
    const int treeTabsH = g.folderOpen ? sc(28) : 0;
    g.rTreeTabs   = { 0, bodyTop, g.sidebarW, bodyTop + treeTabsH };
    g.rProjectTab = { sc(10), bodyTop + sc(3), sc(10) + sc(62), bodyTop + treeTabsH };
    g.rFilesTab   = { g.rProjectTab.right + sc(6), bodyTop + sc(3), g.rProjectTab.right + sc(6) + sc(46), bodyTop + treeTabsH };
    g.rTree   = { 0, bodyTop + treeTabsH, g.sidebarW, bodyBot };
    g.rVSplit = { g.sidebarW, bodyTop, g.sidebarW + gutter, bodyBot };
    const int mainL = g.sidebarW + gutter;
    g.rTabs   = { mainL, bodyTop, W, bodyTop + tabsH };
    const int editorBot = bodyBot - g.dockH;
    g.rEditor = { mainL, bodyTop + tabsH, W, editorBot };
    g.gutterW = (g.lineNumbers && g.fileOpen) ? sc(46) : 0;     // line-number gutter
    g.rGutter = { mainL, bodyTop + tabsH, mainL + g.gutterW, editorBot };
    const RECT rEditChild = { mainL + g.gutterW, bodyTop + tabsH, W, editorBot };
    g.rDock   = { mainL, editorBot, W, bodyBot };
    g.rProblemsTab = { mainL + sc(14), editorBot, mainL + sc(14) + sc(82), editorBot + hdrH };
    g.rOutputTab   = { g.rProblemsTab.right + sc(8), editorBot, g.rProblemsTab.right + sc(8) + sc(64), editorBot + hdrH };
    g.rDockBody = { mainL, editorBot + hdrH, W, bodyBot };

    // Batch all pane moves into one atomic pass so a resize/splitter-drag repaints
    // the panes together rather than child-by-child (which causes drag lag).
    HDWP dwp = BeginDeferWindowPos(4);
    auto place = [&](HWND h, const RECT& r) {
        if (h && dwp) dwp = DeferWindowPos(dwp, h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(g.hTree, g.rTree);
    place(g.hEdit, rEditChild);
    place(g.hOut, g.rDockBody);
    place(g.hProblems, g.rDockBody);
    if (dwp) EndDeferWindowPos(dwp);
}

// ---- paint ----------------------------------------------------------------
void fillRect(HDC dc, const RECT& r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
void hline(HDC dc, int x1, int x2, int y, COLORREF c) { RECT r{ x1, y, x2, y + 1 }; fillRect(dc, r, c); }
void vline(HDC dc, int x, int y1, int y2, COLORREF c) { RECT r{ x, y1, x + 1, y2 }; fillRect(dc, r, c); }
void drawText(HDC dc, RECT r, const std::wstring& s, COLORREF col, HFONT f, UINT fmt) {
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, col);
    HGDIOBJ old = SelectObject(dc, f); DrawTextW(dc, s.c_str(), -1, &r, fmt); SelectObject(dc, old);
}

void onPaint(HWND hwnd) {
    const Theme& th = currentTheme();
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) { EndPaint(hwnd, &ps); return; }
    // Reuse a cached back-buffer (recreate only when the client size changes) — avoids
    // a full-window bitmap allocation on every paint, which is hot during splitter drags.
    if (!g.memDC) g.memDC = CreateCompatibleDC(hdc);
    if (!g.memBmp || g.memW != rc.right || g.memH != rc.bottom) {
        if (g.memBmp) DeleteObject(g.memBmp);
        g.memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        g.memW = rc.right; g.memH = rc.bottom;
    }
    HDC mem = g.memDC;
    HGDIOBJ oldBmp = SelectObject(mem, g.memBmp);
    fillRect(mem, rc, th.windowBg);

    if (!g.folderOpen) {
        fillRect(mem, g.rTree, th.panelBg);
        RECT r = g.rTree; r.left += sc(14); r.top += sc(8); r.bottom = r.top + sc(18);
        drawText(mem, r, L"PROJECT", th.textSecondary, g.uiSm, DT_LEFT | DT_TOP | DT_SINGLELINE);
        RECT r2 = g.rTree; r2.left += sc(14); r2.top += sc(32); r2.bottom = r2.top + sc(18);
        drawText(mem, r2, L"(no folder open)", th.textMuted, g.ui, DT_LEFT | DT_TOP | DT_SINGLELINE);
    } else {
        // explorer view switcher: Project | Files
        fillRect(mem, g.rTreeTabs, th.panelBg);
        drawText(mem, g.rProjectTab, L"Project", g.sidebarView == 0 ? th.textPrimary : th.textSecondary, g.uiSm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(mem, g.rFilesTab, L"Files", g.sidebarView == 1 ? th.textPrimary : th.textSecondary, g.uiSm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        const RECT& at = g.sidebarView == 0 ? g.rProjectTab : g.rFilesTab;
        hline(mem, at.left, at.right, g.rTreeTabs.bottom - sc(2), th.accent);
        hline(mem, 0, g.sidebarW, g.rTreeTabs.bottom - 1, th.border);
    }
    fillRect(mem, g.rVSplit, th.windowBg);

    fillRect(mem, g.rTabs, th.panelElevBg);
    { RECT r = g.rTabs; r.left += sc(14);
      std::wstring tab = g.fileOpen ? ((g.dirty ? L"●  " : L"") + g.curFileName) : L"untitled";
      drawText(mem, r, tab, g.fileOpen ? th.textPrimary : th.textSecondary, g.ui, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      if (g.fileOpen) hline(mem, g.rTabs.left, g.rTabs.left + sc(2) + (int)tab.size() * sc(8), g.rTabs.top + 1, th.accent); }
    hline(mem, g.rTabs.left, g.rTabs.right, g.rTabs.bottom - 1, th.border);

    if (!g.fileOpen) {
        RECT r = g.rEditor; r.bottom = r.top + (g.rEditor.bottom - g.rEditor.top) / 2;
        drawText(mem, r, kAppName, th.accent, g.title, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        RECT r2 = g.rEditor; r2.top += (g.rEditor.bottom - g.rEditor.top) / 2 + sc(10);
        drawText(mem, r2, g.folderOpen ? L"Select a file from the tree" : L"≡  ▸  New Project…   or   Open Project…",
                 th.textSecondary, g.ui, DT_CENTER | DT_TOP | DT_SINGLELINE);
    } else if (g.lineNumbers && g.gutterW > 0) {
        // line-number gutter: query each visible line's y from the editor (no drift)
        fillRect(mem, g.rGutter, th.panelBg);
        vline(mem, g.rGutter.right - 1, g.rGutter.top, g.rGutter.bottom, th.border);
        LONG first = (LONG)SendMessageW(g.hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
        LONG total = (LONG)SendMessageW(g.hEdit, EM_GETLINECOUNT, 0, 0);
        const int edH = g.rGutter.bottom - g.rGutter.top;
        HGDIOBJ oldF = SelectObject(mem, g.mono); SetBkMode(mem, TRANSPARENT);
        for (LONG i = first; i < total; i++) {
            POINTL pt{}; LONG ci = (LONG)SendMessageW(g.hEdit, EM_LINEINDEX, i, 0);
            SendMessageW(g.hEdit, EM_POSFROMCHAR, (WPARAM)&pt, (LPARAM)ci);
            if (pt.y > edH) break;
            RECT lr{ g.rGutter.left, g.rGutter.top + pt.y, g.rGutter.right - sc(8), g.rGutter.top + pt.y + sc(20) };
            SetTextColor(mem, th.textMuted);
            std::wstring num = std::to_wstring(i + 1);
            DrawTextW(mem, num.c_str(), -1, &lr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
        SelectObject(mem, oldF);
    }

    // dock header (the body is covered by the active child control)
    fillRect(mem, { g.rDock.left, g.rDock.top, g.rDock.right, g.rDock.top + sc(31) }, th.panelBg);
    hline(mem, g.rDock.left, g.rDock.right, g.rDock.top, th.border);
    { std::wstring pl = L"Problems" + std::wstring(g.problems.empty() ? L"" : L" (" + std::to_wstring(g.problems.size()) + L")");
      drawText(mem, g.rProblemsTab, pl, g.dockTab == 0 ? th.textPrimary : th.textSecondary, g.uiSm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      drawText(mem, g.rOutputTab, L"Output", g.dockTab == 1 ? th.textPrimary : th.textSecondary, g.uiSm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      const RECT& at = g.dockTab == 0 ? g.rProblemsTab : g.rOutputTab;
      hline(mem, at.left, at.right, g.rDock.top + sc(29), th.accent); }
    hline(mem, g.rDock.left, g.rDock.right, g.rDock.top + sc(31), th.border);

    // toolbar
    fillRect(mem, g.rToolbar, th.panelBg);
    hline(mem, 0, rc.right, g.rToolbar.bottom - 1, th.border);
    drawText(mem, g.rMenuBtn, L"≡", th.textPrimary, g.ui, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    fillRect(mem, g.rBuild, g.building ? th.hoverBg : th.accent);
    drawText(mem, g.rBuild, g.building ? L"Building…" : L"▶  Build", g.building ? th.textSecondary : th.accentText, g.ui, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    { HBRUSH fb = CreateSolidBrush(th.border); FrameRect(mem, &g.rRun, fb); DeleteObject(fb); }
    drawText(mem, g.rRun, L"Run", th.textPrimary, g.ui, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // Save button — lights up coral with a dot when there are unsaved edits.
    { HBRUSH fb = CreateSolidBrush(th.border); FrameRect(mem, &g.rSave, fb); DeleteObject(fb);
      bool canSave = g.fileOpen && g.dirty;
      drawText(mem, g.rSave, canSave ? L"●  Save" : L"Save", canSave ? th.accent : th.textMuted, g.ui, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
    // Undo / Redo — glyph buttons; grayed when the action isn't available (EM_CANUNDO/EM_CANREDO).
    { HBRUSH fb = CreateSolidBrush(th.border); FrameRect(mem, &g.rUndo, fb); FrameRect(mem, &g.rRedo, fb); DeleteObject(fb);
      bool canU = g.fileOpen && SendMessageW(g.hEdit, EM_CANUNDO, 0, 0);
      bool canR = g.fileOpen && SendMessageW(g.hEdit, EM_CANREDO, 0, 0);
      drawText(mem, g.rUndo, L"↶", canU ? th.textPrimary : th.textMuted, g.ui, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      drawText(mem, g.rRedo, L"↷", canR ? th.textPrimary : th.textMuted, g.ui, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
    if (g.project.loaded) {
        // Xcode-style scheme selector: [● target ▾] [tier ▾]
        const Target& t = activeTarget();
        HBRUSH fb = CreateSolidBrush(th.border); FrameRect(mem, &g.rScheme, fb); DeleteObject(fb);
        vline(mem, g.rSchemeTier.left, g.rScheme.top + sc(4), g.rScheme.bottom - sc(4), th.border);
        // target zone: a type dot + the active target name
        COLORREF dot = t.type == ProjectType::Library ? th.diagInfo : t.type == ProjectType::Shared ? th.diagWarning : th.accent;
        int mid = (g.rScheme.top + g.rScheme.bottom) / 2;
        fillRect(mem, { g.rSchemeTarget.left + sc(10), mid - sc(3), g.rSchemeTarget.left + sc(16), mid + sc(3) }, dot);
        RECT tt = g.rSchemeTarget; tt.left += sc(22); tt.right -= sc(6);
        std::wstring tlabel = (t.name.empty() ? g.project.name : t.name) + (g.project.targets.size() > 1 ? L"  ▾" : L"");
        drawText(mem, tt, tlabel, th.textPrimary, g.uiSm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // tier zone
        COLORREF td = g.tier == 0 ? th.textSecondary : g.tier == 1 ? th.trustVerified : g.tier == 2 ? th.accent : th.diagWarning;
        fillRect(mem, { g.rSchemeTier.left + sc(10), mid - sc(3), g.rSchemeTier.left + sc(16), mid + sc(3) }, td);
        RECT zt = g.rSchemeTier; zt.left += sc(22);
        drawText(mem, zt, tierName(g.tier) + std::wstring(L"  ▾"), th.textPrimary, g.uiSm, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t* ext = t.type == ProjectType::Library ? L".a" : t.type == ProjectType::Shared ? L".dll" : L".exe";
        RECT cmd{ g.rScheme.right + sc(12), g.rToolbar.top, rc.right - sc(12), g.rToolbar.bottom };
        drawText(mem, cmd, L"→ target\\" + std::wstring(tierDir(g.tier)) + L"\\" + (t.name.empty() ? g.project.name : t.name) + ext, th.textMuted, g.mono, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        RECT cmd{ g.rRedo.right + sc(12), g.rToolbar.top, rc.right - sc(12), g.rToolbar.bottom };
        drawText(mem, cmd, g.fileOpen ? (L"snc build " + g.curFileName) : L"snc build", th.textMuted, g.mono, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // status bar
    fillRect(mem, g.rStatus, th.panelElevBg);
    hline(mem, 0, rc.right, g.rStatus.top, th.border);
    { RECT r = g.rStatus; r.left += sc(12);
      drawText(mem, r, g.statusLeft, th.textSecondary, g.uiSm, DT_LEFT | DT_VCENTER | DT_SINGLELINE); }
    { RECT r = g.rStatus; r.left += sc(120);
      drawText(mem, r, g.statusMsg, th.textMuted, g.uiSm, DT_LEFT | DT_VCENTER | DT_SINGLELINE); }
    { // right side: version, then the clickable signing chip (ADR-0061 verify state) to its left
      auto measure = [&](const std::wstring& s, HFONT f) { HGDIOBJ o = SelectObject(mem, f); SIZE sz{}; GetTextExtentPoint32W(mem, s.c_str(), (int)s.size(), &sz); SelectObject(mem, o); return (int)sz.cx; };
      int x = rc.right - sc(12);
      std::wstring ver = L"Sentinel    " + std::wstring(kVersion);
      int vw = measure(ver, g.uiSm);
      drawText(mem, { x - vw, g.rStatus.top, x, g.rStatus.bottom }, ver, th.textMuted, g.uiSm, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
      x -= vw + sc(16);
      std::wstring chip; COLORREF cc;
      switch (g.signState) {
        case SignState::Signed:   chip = L"✓ Signed";             cc = th.trustVerified; break;
        case SignState::Invalid:  chip = L"⚠ Signature invalid";  cc = th.diagError;     break;
        case SignState::Checking: chip = L"…  verifying";         cc = th.textMuted;     break;
        default:                  chip = L"⊘ Unsigned";           cc = th.textSecondary; break;
      }
      int cw = measure(chip, g.uiSm);
      g.rStatusSign = { x - cw - sc(8), g.rStatus.top, x + sc(2), g.rStatus.bottom };
      drawText(mem, { x - cw, g.rStatus.top, x, g.rStatus.bottom }, chip, cc, g.uiSm, DT_RIGHT | DT_VCENTER | DT_SINGLELINE); }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);   // back-buffer (g.memDC/g.memBmp) is cached, freed in WM_DESTROY
    EndPaint(hwnd, &ps);
}

// ---- syntax highlighting --------------------------------------------------
const std::unordered_set<std::wstring>& keywords() {
    static const std::unordered_set<std::wstring> k = {
        L"fn",L"let",L"mut",L"const",L"static",L"return",L"if",L"else",L"for",L"while",
        L"loop",L"match",L"struct",L"enum",L"impl",L"trait",L"pub",L"use",L"mod",L"type",
        L"as",L"in",L"break",L"continue",L"true",L"false",L"self",L"Self",L"where",L"move",
        L"ref",L"async",L"await",L"unsafe",L"extern",L"crate",L"super",L"dyn",
        L"secret",L"effect",L"effects",L"borrow",L"capability",L"cap",L"declassify",
        L"u8",L"u16",L"u32",L"u64",L"usize",L"i8",L"i16",L"i32",L"i64",L"isize",
        L"f32",L"f64",L"bool",L"char",L"str",L"void",
    };
    return k;
}
void applyColor(HWND h, LONG a, LONG b, COLORREF color, bool withFont) {
    CHARRANGE cr{ a, b }; SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR; cf.crTextColor = color;
    if (withFont) { cf.dwMask |= CFM_FACE | CFM_SIZE; cf.yHeight = 11 * 20; wcscpy_s(cf.szFaceName, g.settings.editorFont.c_str()); }
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
COLORREF blendColor(COLORREF a, COLORREF b, int pct) {  // pct% of b mixed over a
    int ia = 100 - pct;
    return RGB((GetRValue(a) * ia + GetRValue(b) * pct) / 100,
               (GetGValue(a) * ia + GetGValue(b) * pct) / 100,
               (GetBValue(a) * ia + GetBValue(b) * pct) / 100);
}
// Set the character background of [a,b) (b=-1 → to end). Used for error-line tints.
void applyBackColor(LONG a, LONG b, COLORREF color) {
    CHARRANGE save; SendMessageW(g.hEdit, EM_EXGETSEL, 0, (LPARAM)&save);
    CHARRANGE cr{ a, b }; SendMessageW(g.hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf); cf.dwMask = CFM_BACKCOLOR; cf.crBackColor = color;
    SendMessageW(g.hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(g.hEdit, EM_EXSETSEL, 0, (LPARAM)&save);
}
void highlight() {
    if (g.highlighting || !g.hEdit) return;
    g.highlighting = true;
    suspendUndo();   // keep the re-colorize off the native undo stack
    const Theme& th = currentTheme();
    GETTEXTLENGTHEX gtl{ GTL_NUMCHARS, 1200 };
    LONG n = (LONG)SendMessageW(g.hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    std::wstring s; s.resize(n + 1);
    GETTEXTEX gt{}; gt.cb = (DWORD)((n + 1) * sizeof(wchar_t)); gt.flags = GT_DEFAULT; gt.codepage = 1200;
    LONG got = (LONG)SendMessageW(g.hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data());
    s.resize(got < 0 ? 0 : got);
    CHARRANGE save; SendMessageW(g.hEdit, EM_EXGETSEL, 0, (LPARAM)&save);
    SendMessageW(g.hEdit, WM_SETREDRAW, FALSE, 0);
    applyColor(g.hEdit, 0, -1, th.textPrimary, true);
    LONG i = 0, len = (LONG)s.size();
    while (i < len) {
        wchar_t c = s[i];
        if (c == L'/' && i + 1 < len && s[i + 1] == L'/') { LONG j = i; while (j < len && s[j] != L'\r' && s[j] != L'\n') j++; applyColor(g.hEdit, i, j, th.synComment, false); i = j; continue; }
        if (c == L'/' && i + 1 < len && s[i + 1] == L'*') { LONG j = i + 2; while (j + 1 < len && !(s[j] == L'*' && s[j + 1] == L'/')) j++; j = (j + 2 <= len) ? j + 2 : len; applyColor(g.hEdit, i, j, th.synComment, false); i = j; continue; }
        if (c == L'"' || c == L'\'') { wchar_t q = c; LONG j = i + 1; while (j < len && s[j] != q) { if (s[j] == L'\\' && j + 1 < len) j += 2; else j++; } if (j < len) j++; applyColor(g.hEdit, i, j, th.synString, false); i = j; continue; }
        if (iswdigit(c)) { LONG j = i; while (j < len && (iswalnum(s[j]) || s[j] == L'.' || s[j] == L'_')) j++; applyColor(g.hEdit, i, j, th.synNumber, false); i = j; continue; }
        if (iswalpha(c) || c == L'_') { LONG j = i; while (j < len && (iswalnum(s[j]) || s[j] == L'_')) j++; if (keywords().count(s.substr(i, j - i))) applyColor(g.hEdit, i, j, th.synKeyword, false); i = j; continue; }
        i++;
    }
    SendMessageW(g.hEdit, EM_EXSETSEL, 0, (LPARAM)&save);
    SendMessageW(g.hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.hEdit, nullptr, TRUE);
    resumeUndo();
    g.highlighting = false;
}

// ---- files / tree ---------------------------------------------------------
std::wstring baseName(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? p : p.substr(s + 1); }
std::wstring dirName(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"." : p.substr(0, s); }

// ---- error-line highlight + edit tracking ---------------------------------
void clearErrorMarks() {
    if (!g.errorMarks || !g.hEdit) return;
    g.highlighting = true;   // suppress the EN_CHANGE that EM_SETCHARFORMAT raises
    suspendUndo();           // ...and keep the tint clear off the native undo stack
    SendMessageW(g.hEdit, WM_SETREDRAW, FALSE, 0);
    applyBackColor(0, -1, currentTheme().windowBg);
    SendMessageW(g.hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.hEdit, nullptr, TRUE);
    resumeUndo();
    g.errorMarks = false;
    g.highlighting = false;
}
// Tint the lines that have diagnostics in the open file (after a build).
void markErrorLines(HWND hwnd) {
    if (!g.hEdit || !g.fileOpen) return;
    const Theme& th = currentTheme();
    COLORREF tint = blendColor(th.windowBg, th.diagError, 24);
    g.highlighting = true;   // suppress the EN_CHANGE that EM_SETCHARFORMAT raises
    suspendUndo();           // ...and keep the tinting off the native undo stack
    SendMessageW(g.hEdit, WM_SETREDRAW, FALSE, 0);
    applyBackColor(0, -1, th.windowBg);   // clear any previous tints first
    LONG total = (LONG)SendMessageW(g.hEdit, EM_GETLINECOUNT, 0, 0);
    bool any = false;
    for (auto& d : g.problems) {
        if (_wcsicmp(baseName(d.file).c_str(), g.curFileName.c_str()) != 0) continue;
        LONG ln = d.line - 1; if (ln < 0 || ln >= total) continue;
        LONG start = (LONG)SendMessageW(g.hEdit, EM_LINEINDEX, ln, 0);
        if (start < 0) continue;
        LONG next = (LONG)SendMessageW(g.hEdit, EM_LINEINDEX, ln + 1, 0);
        applyBackColor(start, next < 0 ? -1 : next, tint);
        any = true;
    }
    SendMessageW(g.hEdit, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.hEdit, nullptr, TRUE);
    resumeUndo();
    g.errorMarks = any;
    g.highlighting = false;
    if (g.lineNumbers) InvalidateRect(hwnd, &g.rGutter, FALSE);
}
// Repaint the toolbar Undo/Redo buttons only when their availability actually changes
// (any invalidate forces a full-window repaint, so we skip the no-op steady-state case).
void refreshUndoButtons(HWND hwnd) {
    bool cu = g.fileOpen && SendMessageW(g.hEdit, EM_CANUNDO, 0, 0) != 0;
    bool cr = g.fileOpen && SendMessageW(g.hEdit, EM_CANREDO, 0, 0) != 0;
    if (cu == g.tbCanUndo && cr == g.tbCanRedo) return;
    g.tbCanUndo = cu; g.tbCanRedo = cr;
    RECT r{ g.rUndo.left, g.rUndo.top, g.rRedo.right, g.rRedo.bottom };
    InvalidateRect(hwnd, &r, FALSE);
}
// Editor content changed: re-highlight; on a real user edit mark dirty, drop stale
// error tints (lines shifted), refresh the line-number gutter, and the undo/redo buttons.
// EM_SETCHARFORMAT (highlight/error tints) also raises EN_CHANGE — g.highlighting guards those out.
void onEditChanged(HWND hwnd) {
    if (g.highlighting || g.loadingFile) return;   // programmatic format/load, not a user edit
    highlight();
    if (!g.dirty) { g.dirty = true; InvalidateRect(hwnd, &g.rTabs, FALSE); }
    if (g.errorMarks) clearErrorMarks();
    if (g.lineNumbers) InvalidateRect(hwnd, &g.rGutter, FALSE);
    refreshUndoButtons(hwnd);   // edits/undo/redo all arrive here via EN_CHANGE
}

HTREEITEM insertNode(HTREEITEM parent, const std::wstring& label, LPARAM data, int image) {
    TVINSERTSTRUCTW ins{}; ins.hParent = parent; ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    ins.item.pszText = const_cast<LPWSTR>(label.c_str()); ins.item.lParam = data;
    ins.item.iImage = image; ins.item.iSelectedImage = image;
    return TreeView_InsertItem(g.hTree, &ins);
}
void addDir(HTREEITEM parent, const std::wstring& dir, int depth) {
    if (depth > 8) return;
    std::vector<std::wstring> dirs, files;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (name[0] == L'.' || name == L"build" || name == L"target" || name == L"node_modules") continue;
            dirs.push_back(name);
        } else {
            size_t dot = name.find_last_of(L'.');
            if (dot != std::wstring::npos && _wcsicmp(name.c_str() + dot, L".sentinel") == 0) files.push_back(name);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(dirs.begin(), dirs.end()); std::sort(files.begin(), files.end());
    for (auto& d : dirs) { HTREEITEM n = insertNode(parent, d, -1, IMG_FOLDER); addDir(n, dir + L"\\" + d, depth + 1); }
    for (auto& f : files) { g.nodePaths.push_back(dir + L"\\" + f); insertNode(parent, f, (LPARAM)(g.nodePaths.size() - 1), IMG_FILE); }
}
bool isSentinelFile(const std::wstring& p) { size_t d = p.find_last_of(L'.'); return d != std::wstring::npos && _wcsicmp(p.c_str() + d, L".sentinel") == 0; }

// Recompute the signing chip for the open file: Unsigned if no `.sig`; otherwise
// snc-verify on a worker thread (→ WM_APP_SIGN) so the UI never blocks.
void refreshSignState(HWND hwnd) {
    g.signKey.clear(); g.signGrants.clear();
    if (!g.fileOpen || !isSentinelFile(g.curFilePath)) { g.signState = SignState::Unknown; return; }
    const std::wstring sig = g.curFilePath + L".sig";
    if (GetFileAttributesW(sig.c_str()) == INVALID_FILE_ATTRIBUTES) { g.signState = SignState::Unsigned; return; }
    SigInfo si = readSig(sig); g.signKey = si.key; g.signGrants = si.grants;
    if (!g.sncCaps.verify) { g.signState = SignState::Signed; return; }   // .sig present but this snc can't verify
    g.signState = SignState::Checking;
    const std::wstring file = g.curFilePath, snc = g.sncPath;
    std::thread([hwnd, file, snc]() {
        VerifyResult r = verifyFile(snc, file);
        std::wstring info = file + L"\t" + r.key + L"\t" + r.grants;
        PostMessageW(hwnd, WM_APP_SIGN, (WPARAM)r.state, (LPARAM)_wcsdup(info.c_str()));
    }).detach();
}

void openFile(HWND hwnd, const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { g.statusMsg = L"Could not open " + baseName(path); InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    DWORD size = GetFileSize(f, nullptr), read = 0;
    std::string bytes(size, '\0'); ReadFile(f, bytes.data(), size, &read, nullptr); CloseHandle(f);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, nullptr, 0);
    std::wstring text(wlen, L'\0'); MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, text.data(), wlen);
    g.loadingFile = true;                 // suppress the dirty flag for this programmatic change
    SetWindowTextW(g.hEdit, text.c_str());
    styleEditor(g.hEdit, currentTheme().windowBg);
    highlight();
    g.loadingFile = false;
    g.fileOpen = true; g.curFilePath = path; g.curFileName = baseName(path);
    g.dirty = false; g.errorMarks = false;
    g.tbCanUndo = false; g.tbCanRedo = false;   // SetWindowText cleared the undo buffer
    g.statusMsg = path;
    logMsg(LogLevel::Info, L"Opened file: " + path);
    refreshSignState(hwnd);
    ShowWindow(g.hEdit, SW_SHOW);
    layout(hwnd); InvalidateRect(hwnd, nullptr, FALSE);
}

// Write the editor's text back to the open file (UTF-8, CRLF). Clears the dirty flag.
bool saveFile(HWND hwnd) {
    if (!g.fileOpen) return false;
    GETTEXTLENGTHEX gtl{ GTL_NUMCHARS, 1200 };
    LONG n = (LONG)SendMessageW(g.hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    // GT_USECRLF expands each internal '\r' line break to '\r\n', so the output can be
    // up to ~2× the NUMCHARS count — size generously or the text is truncated at the end.
    std::wstring s; s.resize((size_t)n * 2 + 16);
    GETTEXTEX gt{}; gt.cb = (DWORD)(s.size() * sizeof(wchar_t)); gt.flags = GT_USECRLF; gt.codepage = 1200;
    LONG got = (LONG)SendMessageW(g.hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.data());
    s.resize(got < 0 ? 0 : got);
    if (!writeUtf8(g.curFilePath, s)) { g.statusMsg = L"Could not save " + g.curFileName; InvalidateRect(hwnd, &g.rStatus, FALSE); return false; }
    g.dirty = false;
    logMsg(LogLevel::Info, L"Saved: " + g.curFilePath);
    g.statusMsg = L"Saved " + g.curFileName;
    refreshSignState(hwnd);   // edits invalidate any existing .sig
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}
void populateFiles() {
    g.nodePaths.clear(); TreeView_DeleteAllItems(g.hTree);
    HTREEITEM root = insertNode(TVI_ROOT, projBase(g.rootPath), -1, IMG_FOLDER);
    addDir(root, g.rootPath, 0);
    TreeView_Expand(g.hTree, root, TVE_EXPAND);
}
void populateProject() {
    if (!g.project.loaded) { populateFiles(); return; }
    g.nodePaths.clear(); TreeView_DeleteAllItems(g.hTree);
    g.nodePaths.push_back(g.project.dir + L"\\" + g.project.manifest);  // index 0 — the raw manifest
    HTREEITEM root = insertNode(TVI_ROOT, g.project.name, kProjectSettingsNode, IMG_PROJECT);  // S-shield; click → settings form
    insertNode(root, g.project.manifest, (LPARAM)0, IMG_TOML);          // child → raw manifest in the editor
    std::wstring trustPath = g.project.dir + L"\\" + g.project.trust;
    if (GetFileAttributesW(trustPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        g.nodePaths.push_back(trustPath);
        insertNode(root, g.project.trust, (LPARAM)(g.nodePaths.size() - 1), IMG_TOML);
    }
    if (g.project.targets.size() > 1) {   // an explicit multi-target project
        HTREEITEM tg = insertNode(root, L"Targets", -1, IMG_FOLDER);
        for (size_t i = 0; i < g.project.targets.size(); i++) {
            const Target& t = g.project.targets[i];
            std::wstring label = (t.name.empty() ? g.project.name : t.name) + L"  (" + typeName(t.type) + L")";
            insertNode(tg, label, kTargetNodeBase - (LPARAM)i, IMG_FILE);   // click → setActiveTarget
        }
        TreeView_Expand(g.hTree, tg, TVE_EXPAND);
    }
    HTREEITEM src = insertNode(root, L"Sources", -1, IMG_FOLDER);
    addDir(src, g.project.dir, 0);
    TreeView_Expand(g.hTree, src, TVE_EXPAND);
    TreeView_Expand(g.hTree, root, TVE_EXPAND);
}
void populateTree() {
    if (g.sidebarView == 0 && g.project.loaded) populateProject();
    else populateFiles();
}
void setProjectTitle(HWND hwnd) {
    std::wstring t = kAppName;
    if (g.project.loaded) {
        const Target& tg = activeTarget();
        t += L"  —  " + g.project.name;
        if (g.project.targets.size() > 1) t += L"  ›  " + (tg.name.empty() ? g.project.name : tg.name);
        t += L" (" + std::wstring(typeName(tg.type)) + L")  ·  " + tierName(g.tier);
    } else if (g.folderOpen)
        t += L"  —  " + projBase(g.rootPath);
    SetWindowTextW(hwnd, t.c_str());
}

void openFolderPath(HWND hwnd, const std::wstring& folder) {
    g.rootPath = folder; g.folderOpen = true; ShowWindow(g.hTree, SW_SHOW);
    logMsg(LogLevel::Info, L"Opened folder: " + folder);
    g.project = SentinelProject{};
    if (hasProject(folder)) {
        g.project = loadProject(folder);
        g.tier = g.project.defaultTier; g.target = 0;
        g.sidebarView = 0;   // default to the Project view
        addRecent(g.settings, folder); saveSettings(g.settings);   // track in Recent Projects
        logMsg(LogLevel::Info, L"Loaded project: " + g.project.name + L" (" + typeName(g.project.type) +
               L"), " + std::to_wstring(g.project.targets.size()) + L" target(s), tier=" + tierName(g.tier) + L", signing require=" + g.project.signRequire);
    } else {
        g.sidebarView = 1;   // plain folder → Files view
    }
    populateTree();
    if (g.project.loaded) {   // land in the active target's entry source; the project node opens the settings form
        const Target& t = activeTarget();
        std::wstring e = g.project.dir + L"\\" + t.entry;
        if (!t.entry.empty() && GetFileAttributesW(e.c_str()) != INVALID_FILE_ATTRIBUTES) openFile(hwnd, e);
    }
    setProjectTitle(hwnd);
    g.statusMsg = folder; layout(hwnd); InvalidateRect(hwnd, nullptr, FALSE);
}
// Open Project: pick the project manifest (*.sntproject / sentinel.toml) and load its folder.
void openProject(HWND hwnd) {
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) return;
    COMDLG_FILTERSPEC filt[] = { { L"Sentinel project", L"*.sntproject;sentinel.toml" }, { L"All files", L"*.*" } };
    pfd->SetFileTypes(2, filt); pfd->SetTitle(L"Open Sentinel Project");
    DWORD opts = 0; pfd->GetOptions(&opts); pfd->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    if (SUCCEEDED(pfd->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { openFolderPath(hwnd, dirName(path)); CoTaskMemFree(path); }
            item->Release();
        }
    }
    pfd->Release();
}

// New File: create a new .sentinel source in the project's src/ (or project/root), then open it.
void newFile(HWND hwnd) {
    if (!g.folderOpen) { g.statusMsg = L"Open a project first"; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    std::wstring def = g.project.loaded ? g.project.dir : g.rootPath;
    if (g.project.loaded && !g.project.srcDir.empty()) {
        std::wstring sd = g.project.dir + L"\\" + g.project.srcDir;
        if (GetFileAttributesW(sd.c_str()) != INVALID_FILE_ATTRIBUTES) def = sd;
    }
    IFileSaveDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) return;
    COMDLG_FILTERSPEC filt[] = { { L"Sentinel source", L"*.sentinel" } };
    pfd->SetFileTypes(1, filt); pfd->SetDefaultExtension(L"sentinel");
    pfd->SetFileName(L"untitled.sentinel"); pfd->SetTitle(L"New Source File");
    { IShellItem* si = nullptr; if (SUCCEEDED(SHCreateItemFromParsingName(def.c_str(), nullptr, IID_PPV_ARGS(&si)))) { pfd->SetFolder(si); si->Release(); } }
    std::wstring path;
    if (SUCCEEDED(pfd->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) { PWSTR p = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); } item->Release(); }
    }
    pfd->Release();
    if (path.empty()) return;
    if (path.size() < 9 || _wcsicmp(path.c_str() + path.size() - 9, L".sentinel") != 0) path += L".sentinel";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) writeUtf8(path, L"// " + baseName(path) + L"\r\n\r\n");
    logMsg(LogLevel::Info, L"New file: " + path);
    populateTree();
    openFile(hwnd, path);
    g.statusMsg = L"Created " + baseName(path);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---- new project ----------------------------------------------------------
// Scaffold a project at `dir`: create dir (+ any missing parents) and a src/ folder,
// a starter src/main.sentinel, and the <name>.sntproject manifest (UTF-8 + CRLF).
bool createNewProject(const std::wstring& dir, const std::wstring& name) {
    int r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS) return false;
    SHCreateDirectoryExW(nullptr, (dir + L"\\src").c_str(), nullptr);

    writeUtf8(dir + L"\\src\\main.sentinel",
        L"// main.sentinel — entry point for " + name + L".\r\n"
        L"fn main() -> i64 {\r\n"
        L"    42\r\n"
        L"}\r\n");

    std::wstring man =
        L"# " + name + L" — a Sentinel project (created by SentinelIDE).\r\n\r\n"
        L"[project]\r\n"
        L"name    = " + tomlStr(name) + L"\r\n"
        L"version = \"0.1.0\"\r\n"
        L"type    = \"executable\"\r\n"
        L"entry   = \"src/main.sentinel\"\r\n\r\n"
        L"[build]\r\n"
        L"src          = \"src\"\r\n"
        L"lib_paths    = []\r\n"
        L"links        = []\r\n"
        L"default_tier = \"experimental\"\r\n\r\n"
        L"[signing]\r\n"
        L"require = \"off\"                 # off | warn | strict (ADR 0061)\r\n"
        L"trust   = \"sentinel-trust.toml\"\r\n"
        L"sign    = false\r\n";
    return writeUtf8(dir + L"\\" + name + L".sntproject", man);
}

void newProject(HWND hwnd) {
    IFileSaveDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) return;
    COMDLG_FILTERSPEC filt[] = { { L"Sentinel project", L"*.sntproject" } };
    pfd->SetFileTypes(1, filt); pfd->SetDefaultExtension(L"sntproject");
    pfd->SetFileName(L"MyProject.sntproject"); pfd->SetTitle(L"New Sentinel Project");
    std::wstring path;
    if (SUCCEEDED(pfd->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) { PWSTR p = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); } item->Release(); }
    }
    pfd->Release();
    if (path.empty()) return;

    std::wstring dir = dirName(path), name = baseName(path);
    size_t dot = name.find_last_of(L'.'); if (dot != std::wstring::npos) name = name.substr(0, dot);
    if (name.empty()) name = projBase(dir);
    if (!createNewProject(dir, name)) { g.statusMsg = L"Could not create project at " + dir; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    logMsg(LogLevel::Info, L"New project: " + name + L" → " + dir + L"\\" + name + L".sntproject");
    openFolderPath(hwnd, dir);
    g.statusMsg = L"Created project " + name;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---- output pane + diagnostics --------------------------------------------
// stripAnsi lives in core/Proc.h (shared with the synchronous run-and-capture path).
void outClear() {
    SendMessageW(g.hOut, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(g.hOut, L"");
    SendMessageW(g.hOut, EM_SETREADONLY, TRUE, 0);
    g.problems.clear(); g.pendingMsg.clear(); ListView_DeleteAllItems(g.hProblems);
}
// Append a line to the Output pane; returns the char position where it started
// (so the caller can apply CFE_LINK to a sub-range — e.g. a clickable file:line).
LONG outAppend(const std::wstring& line, COLORREF color) {
    SendMessageW(g.hOut, EM_SETREADONLY, FALSE, 0);
    CHARRANGE toEnd{ 0x7FFFFFFF, 0x7FFFFFFF }; SendMessageW(g.hOut, EM_EXSETSEL, 0, (LPARAM)&toEnd);
    CHARRANGE cur{}; SendMessageW(g.hOut, EM_EXGETSEL, 0, (LPARAM)&cur);   // true end char pos (CRLF-safe)
    LONG start = cur.cpMin;
    CHARFORMAT2W cf{}; cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = color; cf.yHeight = 10 * 20; wcscpy_s(cf.szFaceName, g.settings.editorFont.c_str());
    SendMessageW(g.hOut, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    std::wstring t = line + L"\r\n";
    SendMessageW(g.hOut, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
    SendMessageW(g.hOut, EM_SETREADONLY, TRUE, 0);
    SendMessageW(g.hOut, WM_VSCROLL, SB_BOTTOM, 0);
    return start;
}
// Mark [base+ts, base+te) in the Output pane as a clickable link (CFE_LINK → EN_LINK).
void outLinkify(LONG base, size_t ts, size_t te) {
    SendMessageW(g.hOut, EM_SETREADONLY, FALSE, 0);
    CHARRANGE keep; SendMessageW(g.hOut, EM_EXGETSEL, 0, (LPARAM)&keep);
    CHARRANGE lr{ base + (LONG)ts, base + (LONG)te };
    SendMessageW(g.hOut, EM_EXSETSEL, 0, (LPARAM)&lr);
    CHARFORMAT2W lf{}; lf.cbSize = sizeof(lf); lf.dwMask = CFM_LINK; lf.dwEffects = CFE_LINK;
    SendMessageW(g.hOut, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&lf);
    SendMessageW(g.hOut, EM_EXSETSEL, 0, (LPARAM)&keep);
    SendMessageW(g.hOut, EM_SETREADONLY, TRUE, 0);
}
// Parse a miette-style `path.sentinel:line:col` out of a line. If tokStart/tokEnd
// are given, they receive the [start,end) char span of the file:line:col token
// within `s` (used to mark it as a clickable link in the Output pane).
bool parseDiag(const std::wstring& s, Diag& d, size_t* tokStart = nullptr, size_t* tokEnd = nullptr) {
    size_t e = s.find(L".sentinel");
    if (e == std::wstring::npos) return false;
    size_t p = e + 9;
    if (p >= s.size() || s[p] != L':') return false;
    size_t q = p + 1; if (q >= s.size() || !iswdigit(s[q])) return false;
    int ln = 0; while (q < s.size() && iswdigit(s[q])) { ln = ln * 10 + (s[q] - L'0'); q++; }
    int col = 1; if (q < s.size() && s[q] == L':') { q++; int c = 0; bool any = false; while (q < s.size() && iswdigit(s[q])) { c = c * 10 + (s[q] - L'0'); q++; any = true; } if (any) col = c; }
    size_t start = e;
    auto isPath = [](wchar_t ch) { return iswalnum(ch) || ch == L'_' || ch == L'.' || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'-' || ch == L' '; };
    while (start > 0 && isPath(s[start - 1]) && s[start - 1] != L'[') start--;
    size_t ts = start; while (ts < s.size() && s[ts] == L' ') ts++;   // trim leading spaces → real token start
    std::wstring file = s.substr(ts, (e + 9) - ts);
    d.file = file; d.line = ln; d.col = col; d.msg = s;
    if (tokStart) *tokStart = ts;
    if (tokEnd) *tokEnd = q;
    return true;
}
void addProblem(const Diag& d) {
    g.problems.push_back(d);
    // NB: ListView_SetItemText is a macro that assigns pszText and SendMessages in
    // *separate* statements, so the text pointer must outlive the call — never pass a
    // temporary's .c_str() (it dangles before the message is sent → garbage glyphs).
    std::wstring msg = d.msg, fileBase = baseName(d.file), ln = std::to_wstring(d.line);
    LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = (int)g.problems.size() - 1;
    it.pszText = const_cast<LPWSTR>(msg.c_str()); it.lParam = (LPARAM)(g.problems.size() - 1);
    int row = ListView_InsertItem(g.hProblems, &it);
    ListView_SetItemText(g.hProblems, row, 1, const_cast<LPWSTR>(fileBase.c_str()));
    ListView_SetItemText(g.hProblems, row, 2, const_cast<LPWSTR>(ln.c_str()));
}

// ---- build / run ----------------------------------------------------------
// Prefer the most ADR-0061-capable snc. Release is listed first (faster builds),
// but capability wins over position: both builds carry the subcommands, yet only
// the one with keygen_core/sign_core beside it can actually sign — so a fully
// signing-capable build beats a verify-only one, which beats first-existing.
// (See sncSigningCaps() — advertising `snc keygen` in help proves nothing.)
std::wstring findSnc() {
    const wchar_t* cands[] = { L"G:\\Sentinel-lang\\target\\release\\snc.exe", L"G:\\Sentinel-lang\\target\\debug\\snc.exe" };
    std::wstring firstFound, verifyOnly;
    for (auto c : cands) {
        if (GetFileAttributesW(c) == INVALID_FILE_ATTRIBUTES) continue;
        if (firstFound.empty()) firstFound = c;
        SncSigningCaps caps = sncSigningCaps(c);
        if (caps.sign) return c;
        if (caps.verify && verifyOnly.empty()) verifyOnly = c;
    }
    if (!verifyOnly.empty()) return verifyOnly;
    return firstFound.empty() ? L"snc.exe" : firstFound;
}
// Resolve the build toolchain from Settings (explicit override, else auto-detect):
// the snc compiler + whether it signs, and the MSVC vcvars64.bat (link.exe env).
void resolveToolchain() {
    g.sncPath = g.settings.sncPath.empty() ? findSnc() : g.settings.sncPath;
    g.sncCaps = sncSigningCaps(g.sncPath);
    g.vcvarsPath = g.settings.vcvarsPath.empty() ? findVcvars() : g.settings.vcvarsPath;
    logMsg(LogLevel::Info, L"Toolchain — snc=" + g.sncPath + L" (verify=" + (g.sncCaps.verify ? L"yes" : L"no") +
           L", sign=" + (g.sncCaps.sign ? L"yes" : L"no") +
           L"), vcvars=" + (g.vcvarsPath.empty() ? L"<none>" : g.vcvarsPath));
}
// The captured MSVC environment block for g.vcvarsPath (empty if unavailable),
// built lazily and cached. Called from the build worker thread; builds are
// serialized by g.building, so the static cache needs no extra locking.
std::vector<wchar_t>& msvcEnvBlock() {
    static std::vector<wchar_t> cache;
    static std::wstring cachedFor = L"\x01";   // sentinel ≠ any real path (incl. empty)
    if (cachedFor != g.vcvarsPath) { captureMsvcEnv(g.vcvarsPath, cache); cachedFor = g.vcvarsPath; }
    return cache;
}
// `useMsvcEnv` injects the MSVC Developer environment so snc's link step finds link.exe.
void worker(HWND hwnd, std::wstring cmd, std::wstring dir, bool useMsvcEnv) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { PostMessageW(hwnd, WM_APP_DONE, (WPARAM)(DWORD)-1, 0); return; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring c = cmd;
    DWORD flags = CREATE_NO_WINDOW;
    void* env = nullptr;
    std::vector<wchar_t> envBlock;
    if (useMsvcEnv) { envBlock = msvcEnvBlock(); if (!envBlock.empty()) { env = envBlock.data(); flags |= CREATE_UNICODE_ENVIRONMENT; } }
    BOOL ok = CreateProcessW(nullptr, &c[0], nullptr, nullptr, TRUE, flags, env,
                             dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        PostMessageW(hwnd, WM_APP_LINE, 0, (LPARAM)_wcsdup(L"Failed to start snc.exe (not found on PATH or known locations)."));
        PostMessageW(hwnd, WM_APP_DONE, (WPARAM)(DWORD)-1, 0); CloseHandle(rd); return;
    }
    std::string buf; char tmp[4096]; DWORD nread = 0;
    auto flush = [&](const std::string& line) {
        int wl = MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(), nullptr, 0);
        std::wstring w(wl, 0); MultiByteToWideChar(CP_UTF8, 0, line.data(), (int)line.size(), w.data(), wl);
        PostMessageW(hwnd, WM_APP_LINE, 0, (LPARAM)_wcsdup(w.c_str()));
    };
    while (ReadFile(rd, tmp, sizeof(tmp), &nread, nullptr) && nread > 0) {
        buf.append(tmp, nread);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buf.erase(0, pos + 1); flush(line);
        }
    }
    if (!buf.empty()) flush(buf);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    PostMessageW(hwnd, WM_APP_DONE, (WPARAM)code, 0);
}
std::wstring outExe() { return dirName(g.curFilePath) + L"\\" + [] { std::wstring b = baseName(g.curFilePath); size_t d = b.find_last_of(L'.'); return d == std::wstring::npos ? b : b.substr(0, d); }() + L".exe"; }
std::wstring projectOutDir() {
    std::wstring out = g.project.dir + L"\\target\\" + tierDir(g.tier);
    CreateDirectoryW((g.project.dir + L"\\target").c_str(), nullptr);
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}
// The built artifact path for the active target (mirrors composeBuild's -o target).
std::wstring artifactPath() {
    const Target& t = activeTarget();
    std::wstring nm = t.name.empty() ? g.project.name : t.name;
    const wchar_t* ext = t.type == ProjectType::Library ? L".a" : t.type == ProjectType::Shared ? L".dll" : L".exe";
    return g.project.dir + L"\\target\\" + tierDir(g.tier) + L"\\" + nm + ext;
}
std::wstring composeBuild() {
    const Target& t = activeTarget();
    std::wstring out = projectOutDir(), entry = g.project.dir + L"\\" + t.entry, nm = t.name.empty() ? g.project.name : t.name;
    std::wstring c = L"\"" + g.sncPath + L"\" build ";
    if (t.type == ProjectType::Library)     c += L"--lib \"" + entry + L"\" -o \"" + out + L"\\" + nm + L".a\" --emit-header \"" + out + L"\\" + nm + L".h\"";
    else if (t.type == ProjectType::Shared) c += L"--shared \"" + entry + L"\" -o \"" + out + L"\\" + nm + L".dll\" --emit-header \"" + out + L"\\" + nm + L".h\"";
    else                                    c += L"\"" + entry + L"\" -o \"" + out + L"\\" + nm + L".exe\"";
    for (auto& lp : g.project.libPaths) c += L" --lib-path \"" + g.project.dir + L"\\" + lp + L"\"";
    for (auto& ln : (t.links.empty() ? g.project.links : t.links)) c += L" --link \"" + ln + L"\"";
    if (g.project.signRequire != L"off") c += L" --require-signatures " + g.project.signRequire + L" --trust \"" + g.project.dir + L"\\" + g.project.trust + L"\"";
    return c;
}
void runBuild(HWND hwnd) {
    if (g.building) return;
    if (g.fileOpen && g.dirty) { saveFile(hwnd); logMsg(LogLevel::Info, L"Auto-saved " + g.curFileName + L" before build"); }
    std::wstring cmd, workdir;
    if (g.project.loaded) {
        const Target& t = activeTarget();
        std::wstring e = g.project.dir + L"\\" + t.entry;
        if (t.entry.empty() || GetFileAttributesW(e.c_str()) == INVALID_FILE_ATTRIBUTES) {
            g.statusMsg = L"Target entry not found: " + t.entry; InvalidateRect(hwnd, &g.rStatus, FALSE); return;
        }
        cmd = composeBuild(); workdir = g.project.dir;
    } else {
        if (!g.fileOpen) { g.statusMsg = L"Open a .sentinel file or project to build"; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
        cmd = L"\"" + g.sncPath + L"\" build \"" + g.curFilePath + L"\" -o \"" + outExe() + L"\""; workdir = dirName(g.curFilePath);
    }
    g.building = true; g.dockTab = 1; showDock(); outClear();
    g.statusMsg = L"Building…"; outAppend(L"> " + cmd, currentTheme().textSecondary);
    if (g.project.loaded) outAppend(std::wstring(L"  (tier ") + tierName(g.tier) + L": snc has no tier flag yet — TIERED_RELEASES is post-1.0; built at -O0 → target\\" + tierDir(g.tier) + L")", currentTheme().textMuted);
    if (g.vcvarsPath.empty())
        outAppend(L"  (no MSVC environment found — link.exe won't be on PATH; set it in Settings → MSVC environment)", currentTheme().diagWarning);
    else
        outAppend(L"  (MSVC environment: " + g.vcvarsPath + L" — provides link.exe + libs)", currentTheme().textMuted);
    logMsg(LogLevel::Info, L"Build: " + cmd + L"  [vcvars=" + (g.vcvarsPath.empty() ? L"<none>" : g.vcvarsPath) + L"]");
    InvalidateRect(hwnd, nullptr, FALSE);
    std::thread(worker, hwnd, cmd, workdir, /*useMsvcEnv=*/true).detach();
}
void runRun(HWND hwnd) {
    if (g.building) return;
    if (!g.fileOpen && !g.project.loaded) { g.statusMsg = L"Open a .sentinel file or project first"; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    if (g.project.loaded && activeTarget().type != ProjectType::Executable) { g.statusMsg = L"Run applies to executable targets"; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    std::wstring exe = g.project.loaded ? (projectOutDir() + L"\\" + (activeTarget().name.empty() ? g.project.name : activeTarget().name) + L".exe") : outExe();
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g.dockTab = 1; showDock(); outAppend(L"No executable yet — Build first. (Windows build→link may be incomplete; see Output.)", currentTheme().diagWarning);
        InvalidateRect(hwnd, nullptr, FALSE); return;
    }
    g.building = true; g.dockTab = 1; showDock(); outClear();
    outAppend(L"> " + exe, currentTheme().textSecondary);
    std::thread(worker, hwnd, L"\"" + exe + L"\"", dirName(g.curFilePath), /*useMsvcEnv=*/false).detach();
}
void gotoLineCol(HWND hwnd, const std::wstring& file, int line, int col) {
    if (!file.empty() && file != g.curFilePath && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) openFile(hwnd, file);
    LONG ci = (LONG)SendMessageW(g.hEdit, EM_LINEINDEX, line - 1, 0);
    if (ci < 0) ci = 0; ci += (col > 0 ? col - 1 : 0);
    CHARRANGE cr{ ci, ci }; SendMessageW(g.hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(g.hEdit, EM_SCROLLCARET, 0, 0); SetFocus(g.hEdit);
}

// Open a path given on the command line (or via a file-association double-click).
// A folder opens directly; a file opens the nearest enclosing project (walk up to
// a folder with a manifest, else its own folder). A manifest itself just opens the
// project (landing in the entry source); any other file is also opened in the editor.
void openPathArg(HWND hwnd, const std::wstring& arg) {
    DWORD attr = GetFileAttributesW(arg.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) { openFolderPath(hwnd, arg); return; }
    std::wstring root = dirName(arg), d = root;
    for (int i = 0; i < 16 && !d.empty(); i++) {        // walk up to the nearest project root
        if (hasProject(d)) { root = d; break; }
        size_t s = d.find_last_of(L"\\/"); if (s == std::wstring::npos) break;
        d = d.substr(0, s);
    }
    std::wstring name = baseName(arg); size_t dot = name.find_last_of(L'.');
    bool isManifest = _wcsicmp(name.c_str(), L"sentinel.toml") == 0 ||
                      (dot != std::wstring::npos && _wcsicmp(name.c_str() + dot, L".sntproject") == 0);
    openFolderPath(hwnd, root);
    if (!isManifest) openFile(hwnd, arg);
}

void showTierMenu(HWND hwnd) {
    HMENU m = CreatePopupMenu();
    const wchar_t* labels[] = { L"Development  ·  Tier D", L"Experimental  ·  Tier E", L"Stable  ·  Tier S", L"Hardened  ·  Tier H" };
    for (int i = 0; i < 4; i++) AppendMenuW(m, MF_STRING | (g.tier == i ? MF_CHECKED : 0), ID_TIER_DEV + i, labels[i]);
    POINT pt{ g.rSchemeTier.left, g.rScheme.bottom }; ClientToScreen(hwnd, &pt);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr); DestroyMenu(m);
}

void showTargetMenu(HWND hwnd) {
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < g.project.targets.size() && i < 64; i++) {
        const Target& t = g.project.targets[i];
        std::wstring label = (t.name.empty() ? g.project.name : t.name) + L"   (" + typeName(t.type) + L")";
        AppendMenuW(m, MF_STRING | ((int)i == g.target ? MF_CHECKED : 0), ID_TARGET_BASE + (UINT)i, label.c_str());
    }
    POINT pt{ g.rSchemeTarget.left, g.rScheme.bottom }; ClientToScreen(hwnd, &pt);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr); DestroyMenu(m);
}

// Switch the active build target: open its entry source, refresh the title + toolbar.
void setActiveTarget(HWND hwnd, int idx) {
    if (idx == g.target || idx < 0 || idx >= (int)g.project.targets.size()) return;
    g.target = idx;
    const Target& t = g.project.targets[g.target];
    logMsg(LogLevel::Info, L"Target → " + (t.name.empty() ? g.project.name : t.name) + L" (" + typeName(t.type) + L")");
    std::wstring e = g.project.dir + L"\\" + t.entry;
    if (!t.entry.empty() && GetFileAttributesW(e.c_str()) != INVALID_FILE_ATTRIBUTES) openFile(hwnd, e);
    setProjectTitle(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Open the structured Project Settings form over sentinel.toml; on Save, persist
// (preserving comments + unmodeled keys), reload, and refresh the UI.
void openProjectSettings(HWND hwnd) {
    if (!g.project.loaded) { g.statusMsg = L"Open a Sentinel project first"; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    SentinelProject p = g.project;
    if (!showProjectSettingsDialog(hwnd, p)) return;
    if (!saveProject(p)) {
        logMsg(LogLevel::Error, L"saveProject failed for " + p.dir);
        g.statusMsg = L"Could not write sentinel.toml"; InvalidateRect(hwnd, &g.rStatus, FALSE); return;
    }
    g.project = loadProject(g.project.dir);
    g.tier = g.project.defaultTier;
    populateTree();
    setProjectTitle(hwnd);
    if (g.fileOpen && _wcsicmp(g.curFilePath.c_str(), (g.project.dir + L"\\" + g.project.manifest).c_str()) == 0)
        openFile(hwnd, g.curFilePath);   // refresh the raw manifest if it's the open file
    logMsg(LogLevel::Info, L"Project settings saved — " + g.project.name + L" (" + typeName(g.project.type) +
           L"), tier=" + tierName(g.tier) + L", signing require=" + g.project.signRequire);
    g.statusMsg = L"Project settings saved";
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Open the Signing & Trust panel (ADR-0061): trust-manifest viewer + real
// snc keygen/sign/verify over the open file. Re-check the chip on close.
void openSigning(HWND hwnd) {
    const std::wstring dir = g.project.loaded ? g.project.dir : g.rootPath;
    const std::wstring trustPath = g.project.loaded ? (g.project.dir + L"\\" + g.project.trust)
                                 : (g.folderOpen ? g.rootPath + L"\\sentinel-trust.toml" : L"");
    showSigningDialog(hwnd, g.sncPath, g.sncCaps, g.fileOpen ? g.curFilePath : L"", dir, trustPath);
    refreshSignState(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Close the open project/folder: return to the empty welcome state. Auto-saves a
// dirty file first (mirrors Build's behavior) so edits aren't silently lost.
void closeProject(HWND hwnd) {
    if (!g.folderOpen) return;
    if (g.fileOpen && g.dirty) { saveFile(hwnd); logMsg(LogLevel::Info, L"Auto-saved " + g.curFileName + L" before closing project"); }
    logMsg(LogLevel::Info, L"Closed: " + g.rootPath);
    g.folderOpen = false; g.fileOpen = false;
    g.project = SentinelProject{};
    g.rootPath.clear(); g.curFilePath.clear(); g.curFileName.clear();
    g.nodePaths.clear();
    g.dirty = false; g.errorMarks = false; g.tbCanUndo = false; g.tbCanRedo = false; g.target = 0;
    g.signState = SignState::Unknown; g.signKey.clear(); g.signGrants.clear();
    TreeView_DeleteAllItems(g.hTree);
    ShowWindow(g.hTree, SW_HIDE);
    g.loadingFile = true; SetWindowTextW(g.hEdit, L""); g.loadingFile = false;
    ShowWindow(g.hEdit, SW_HIDE);
    outClear();   // clear Output + Problems
    g.statusLeft = L"Ln 1, Col 1"; g.statusMsg = L"No project open";
    setProjectTitle(hwnd);
    layout(hwnd); InvalidateRect(hwnd, nullptr, FALSE);
}

// Open a project from the Recent Projects list; drop it if the folder is gone.
void openRecent(HWND hwnd, int idx) {
    if (idx < 0 || idx >= (int)g.settings.recents.size()) return;
    std::wstring folder = g.settings.recents[idx];
    if (!hasProject(folder)) {
        g.settings.recents.erase(g.settings.recents.begin() + idx); saveSettings(g.settings);
        g.statusMsg = L"Recent project not found: " + folder; InvalidateRect(hwnd, &g.rStatus, FALSE);
        logMsg(LogLevel::Warn, L"Recent project missing, dropped: " + folder);
        return;
    }
    openFolderPath(hwnd, folder);   // re-promotes it to the front + saves
}

// Seal the open project: archive → compress → encrypt to <parent>\<name>.sealed,
// unlocked by a password (the slot format allows more unlock methods later). The
// plaintext project is left in place — sealing is non-destructive.
void sealCurrentProject(HWND hwnd) {
    if (!g.project.loaded) { g.statusMsg = L"Open a project to seal it"; InvalidateRect(hwnd, &g.rStatus, FALSE); return; }
    if (g.fileOpen && g.dirty) saveFile(hwnd);   // capture unsaved edits into the seal
    std::wstring pw;
    std::wstring prompt = L"Set a password to seal “" + g.project.name + L"”.\nEnter it twice — you'll need it to open the project again.";
    if (!showPasswordDialog(hwnd, L"Seal Project", prompt, true, pw)) return;
    std::wstring sealed = dirName(g.project.dir) + L"\\" + baseName(g.project.dir) + L".sealed";
    g.dockTab = 1; showDock(); outClear();
    outAppend(L"> Sealing " + g.project.name + L" → " + sealed, currentTheme().textSecondary);
    g.statusMsg = L"Sealing…"; InvalidateRect(hwnd, &g.rStatus, FALSE); UpdateWindow(hwnd);
    std::string pu = sealUtf8(pw); Bytes pwb(pu.begin(), pu.end());
    SealResult sr = sealProject(g.project.dir, sealed, pwb);
    SecureZeroMemory(pwb.data(), pwb.size());
    if (!pw.empty()) SecureZeroMemory(pw.data(), pw.size() * sizeof(wchar_t));
    if (sr.ok) {
        outAppend(L"[sealed · " + baseName(sealed) + L"]  " + sr.message, currentTheme().trustVerified);
        outAppend(L"  The plaintext project is unchanged — delete it manually if you want only the sealed copy.", currentTheme().textMuted);
        g.statusMsg = L"Sealed → " + sealed;
        logMsg(LogLevel::Info, L"Sealed project " + g.project.name + L" → " + sealed);
    } else {
        outAppend(L"[seal failed]  " + sr.message, currentTheme().diagError);
        g.statusMsg = L"Seal failed: " + sr.message;
        logMsg(LogLevel::Warn, L"Seal failed: " + sr.message);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Open a sealed project: pick a .sealed, unlock with its password, decrypt to a
// sibling <name>-unsealed folder, and open it.
void openSealedProject(HWND hwnd) {
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) return;
    COMDLG_FILTERSPEC filt[] = { { L"Sealed project", L"*.sealed" }, { L"All files", L"*.*" } };
    pfd->SetFileTypes(2, filt); pfd->SetTitle(L"Open Sealed Project");
    DWORD opts = 0; pfd->GetOptions(&opts); pfd->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    std::wstring sealed;
    if (SUCCEEDED(pfd->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) { PWSTR p = nullptr; if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { sealed = p; CoTaskMemFree(p); } item->Release(); }
    }
    pfd->Release();
    if (sealed.empty()) return;

    std::wstring pw;
    if (!showPasswordDialog(hwnd, L"Open Sealed Project", L"Enter the password for “" + baseName(sealed) + L"”.", false, pw)) return;

    std::wstring stem = baseName(sealed); size_t dot = stem.find_last_of(L'.'); if (dot != std::wstring::npos) stem = stem.substr(0, dot);
    std::wstring base = dirName(sealed) + L"\\" + stem + L"-unsealed", dest = base;
    for (int i = 2; GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) dest = base + L" (" + std::to_wstring(i) + L")";
    SHCreateDirectoryExW(nullptr, dest.c_str(), nullptr);

    g.statusMsg = L"Unsealing…"; InvalidateRect(hwnd, &g.rStatus, FALSE); UpdateWindow(hwnd);
    std::string pu = sealUtf8(pw); Bytes pwb(pu.begin(), pu.end());
    SealResult ur = unsealProject(sealed, dest, pwb);
    SecureZeroMemory(pwb.data(), pwb.size());
    if (!pw.empty()) SecureZeroMemory(pw.data(), pw.size() * sizeof(wchar_t));
    if (ur.ok) {
        logMsg(LogLevel::Info, L"Unsealed " + sealed + L" → " + dest);
        openFolderPath(hwnd, dest);
        g.dockTab = 1; showDock(); outAppend(L"[unsealed]  " + ur.message, currentTheme().trustVerified);
        g.statusMsg = L"Unsealed → " + dest;
    } else {
        RemoveDirectoryW(dest.c_str());   // remove the (empty) dest on failure
        g.statusMsg = L"Unseal failed: " + ur.message;
        logMsg(LogLevel::Warn, L"Unseal failed: " + ur.message);
        MessageBoxW(hwnd, ur.message.c_str(), L"Open Sealed Project", MB_OK | MB_ICONWARNING);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

std::wstring menuEscape(const std::wstring& s) {   // && so a literal & isn't a mnemonic
    std::wstring o; for (wchar_t c : s) { if (c == L'&') o += L"&&"; else o += c; } return o;
}
// Build the Recent Projects submenu: "name  ⟶  parent dir" per entry, then Clear.
HMENU buildRecentsMenu() {
    HMENU m = CreatePopupMenu();
    const auto& r = g.settings.recents;
    if (r.empty()) { AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"(no recent projects)"); return m; }
    for (size_t i = 0; i < r.size(); ++i)
        AppendMenuW(m, MF_STRING, ID_RECENT_BASE + (UINT)i, menuEscape(projBase(r[i]) + L"\t" + dirName(r[i])).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_RECENT_CLEAR, L"Clear Recent Projects");
    return m;
}

void showAppMenu(HWND hwnd) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_NEW_PROJECT, L"New Project…\tCtrl+N");
    AppendMenuW(m, MF_STRING, ID_OPEN_PROJECT, L"Open Project…\tCtrl+O");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)buildRecentsMenu(), L"Recent Projects");
    AppendMenuW(m, MF_STRING | (g.folderOpen ? 0 : MF_GRAYED), ID_CLOSE_PROJECT, L"Close Project");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g.project.loaded ? 0 : MF_GRAYED), ID_SEAL_PROJECT, L"Seal Project…");
    AppendMenuW(m, MF_STRING, ID_OPEN_SEALED, L"Open Sealed Project…");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g.folderOpen ? 0 : MF_GRAYED), ID_NEW_FILE, L"New File…\tCtrl+Shift+N");
    AppendMenuW(m, MF_STRING | (g.fileOpen && g.dirty ? 0 : MF_GRAYED), ID_SAVE, L"Save\tCtrl+S");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    bool canUndo = g.fileOpen && SendMessageW(g.hEdit, EM_CANUNDO, 0, 0);
    bool canRedo = g.fileOpen && SendMessageW(g.hEdit, EM_CANREDO, 0, 0);
    AppendMenuW(m, MF_STRING | (canUndo ? 0 : MF_GRAYED), ID_UNDO, L"Undo\tCtrl+Z");
    AppendMenuW(m, MF_STRING | (canRedo ? 0 : MF_GRAYED), ID_REDO, L"Redo\tCtrl+Y");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g.project.loaded ? 0 : MF_GRAYED), ID_PROJECT_SETTINGS, L"Project Settings…\tCtrl+;");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_BUILD, L"Build\tCtrl+Shift+B");
    AppendMenuW(m, MF_STRING, ID_RUN, L"Run\tF5");
    AppendMenuW(m, MF_STRING | (g.lineNumbers ? MF_CHECKED : 0), ID_LINE_NUMBERS, L"Line Numbers\tCtrl+L");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SIGNING, L"Signing && Trust…");
    AppendMenuW(m, MF_STRING, ID_FILE_ASSOC, L"Register File Associations…");
    AppendMenuW(m, MF_STRING, ID_SETTINGS, L"Settings…\tCtrl+,");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_ABOUT, L"About SentinelIDE");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");
    POINT pt{ g.rMenuBtn.left, g.rMenuBtn.bottom }; ClientToScreen(hwnd, &pt);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, nullptr); DestroyMenu(m);
}

// Right-click context menu for the explorer tree — New File (the headline action),
// plus New/Open Project. Items route to the existing WM_COMMAND handlers.
void showTreeMenu(HWND hwnd, int x, int y) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g.folderOpen ? 0 : MF_GRAYED), ID_NEW_FILE, L"New File…\tCtrl+Shift+N");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_NEW_PROJECT, L"New Project…\tCtrl+N");
    AppendMenuW(m, MF_STRING, ID_OPEN_PROJECT, L"Open Project…\tCtrl+O");
    AppendMenuW(m, MF_STRING | (g.folderOpen ? 0 : MF_GRAYED), ID_CLOSE_PROJECT, L"Close Project");
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, x, y, 0, hwnd, nullptr); DestroyMenu(m);
}

void applyTheme(HWND hwnd) {
    const Theme& th = currentTheme();
    applyWindowDarkMode(hwnd);
    applyMenuDarkMode();   // keep popup/context menus matched to the theme
    const wchar_t* sub = th.dark ? L"DarkMode_Explorer" : L"Explorer";
    TreeView_SetBkColor(g.hTree, th.panelBg); TreeView_SetTextColor(g.hTree, th.textPrimary); SetWindowTheme(g.hTree, sub, nullptr);
    SetWindowTheme(g.hEdit, sub, nullptr); styleEditor(g.hEdit, th.windowBg); highlight();
    SetWindowTheme(g.hOut, sub, nullptr); styleEditor(g.hOut, th.windowBg);
    SetWindowTheme(g.hProblems, sub, nullptr);
    ListView_SetBkColor(g.hProblems, th.panelBg); ListView_SetTextColor(g.hProblems, th.textPrimary); ListView_SetTextBkColor(g.hProblems, th.panelBg);
    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g.dpi = (int)GetDpiForWindow(hwnd); g.sidebarW = sc(240); g.dockH = sc(200);
        loadSettings(g.settings);
        themeOverride() = g.settings.themeMode;
        g.lineNumbers = g.settings.lineNumbers;
        logger().configure(g.settings.logFile, g.settings.logLevel);
        resolveToolchain();
        createFonts(); createControls(hwnd); layout(hwnd); showDock();
        return 0;
    case WM_SIZE: layout(hwnd); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: onPaint(hwnd); return 0;
    case WM_SETCURSOR:
        if ((HWND)wParam == hwnd && LOWORD(lParam) == HTCLIENT) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            if (PtInRect(&g.rVSplit, pt)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_CONTEXTMENU:
        if ((HWND)wParam == g.hTree) {
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            if (x == -1 && y == -1) {   // keyboard (Shift+F10 / menu key): anchor near the tree's top-left
                RECT rc; GetWindowRect(g.hTree, &rc); x = rc.left + sc(24); y = rc.top + sc(24);
            }
            showTreeMenu(hwnd, x, y);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (PtInRect(&g.rVSplit, p)) { g.dragV = true; SetCapture(hwnd); return 0; }
        if (PtInRect(&g.rMenuBtn, p)) { showAppMenu(hwnd); return 0; }
        if (PtInRect(&g.rBuild, p)) { runBuild(hwnd); return 0; }
        if (PtInRect(&g.rRun, p)) { runRun(hwnd); return 0; }
        if (PtInRect(&g.rSave, p)) { if (g.fileOpen && g.dirty) saveFile(hwnd); return 0; }
        if (PtInRect(&g.rUndo, p)) { if (g.fileOpen && SendMessageW(g.hEdit, EM_CANUNDO, 0, 0)) { SendMessageW(g.hEdit, EM_UNDO, 0, 0); SetFocus(g.hEdit); } return 0; }
        if (PtInRect(&g.rRedo, p)) { if (g.fileOpen && SendMessageW(g.hEdit, EM_CANREDO, 0, 0)) { SendMessageW(g.hEdit, EM_REDO, 0, 0); SetFocus(g.hEdit); } return 0; }
        if (g.folderOpen && PtInRect(&g.rProjectTab, p)) { if (g.sidebarView != 0) { g.sidebarView = 0; populateTree(); InvalidateRect(hwnd, &g.rTreeTabs, FALSE); } return 0; }
        if (g.folderOpen && PtInRect(&g.rFilesTab, p)) { if (g.sidebarView != 1) { g.sidebarView = 1; populateTree(); InvalidateRect(hwnd, &g.rTreeTabs, FALSE); } return 0; }
        if (g.project.loaded && g.project.targets.size() > 1 && PtInRect(&g.rSchemeTarget, p)) { showTargetMenu(hwnd); return 0; }
        if (g.project.loaded && PtInRect(&g.rSchemeTier, p)) { showTierMenu(hwnd); return 0; }
        if (PtInRect(&g.rProblemsTab, p)) { g.dockTab = 0; showDock(); InvalidateRect(hwnd, &g.rDock, FALSE); return 0; }
        if (PtInRect(&g.rOutputTab, p)) { g.dockTab = 1; showDock(); InvalidateRect(hwnd, &g.rDock, FALSE); return 0; }
        if (PtInRect(&g.rStatusSign, p)) { openSigning(hwnd); return 0; }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g.dragV && (wParam & MK_LBUTTON)) {
            RECT cr; GetClientRect(hwnd, &cr);
            int w = std::max(sc(140), std::min((int)GET_X_LPARAM(lParam), (int)cr.right - sc(320)));
            if (w != g.sidebarW) {   // only relayout/repaint on an actual change
                g.sidebarW = w; layout(hwnd);
                // RDW_INVALIDATE marks the whole window dirty (incl. the parent-painted bands —
                // tab strip, dock header, gutter); UPDATENOW repaints synchronously. Without
                // INVALIDATE, only the moved child regions repaint → trails on those bands.
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
            }
        }
        return 0;
    case WM_LBUTTONUP: if (g.dragV) { g.dragV = false; ReleaseCapture(); } return 0;
    case WM_APP_LINE: {
        wchar_t* raw = (wchar_t*)lParam;
        std::wstring line = stripAnsi(raw ? raw : L""); if (raw) free(raw);
        const Theme& th = currentTheme();
        COLORREF col = th.textPrimary;
        if (line.find(L'×') != std::wstring::npos || line.find(L"error") != std::wstring::npos) col = th.diagError;
        else if (line.find(L"warning") != std::wstring::npos) col = th.diagWarning;
        else if (!line.empty() && line[0] == L'>') col = th.textSecondary;
        LONG base = outAppend(line, col);
        if (line.find(L'×') != std::wstring::npos) { size_t x = line.find(L'×'); g.pendingMsg = line.substr(x + 1); while (!g.pendingMsg.empty() && g.pendingMsg.front() == L' ') g.pendingMsg.erase(g.pendingMsg.begin()); }
        Diag d; size_t ts = 0, te = 0;
        if (parseDiag(line, d, &ts, &te)) { if (!g.pendingMsg.empty()) d.msg = g.pendingMsg; addProblem(d); g.pendingMsg.clear(); outLinkify(base, ts, te); InvalidateRect(hwnd, &g.rDock, FALSE); }
        return 0;
    }
    case WM_APP_DONE: {
        DWORD code = (DWORD)wParam; const Theme& th = currentTheme();
        outAppend(L"", th.textMuted);
        outAppend(code == 0 ? L"[done · exit 0]" : (L"[done · exit " + std::to_wstring((int)code) + L"]"),
                  code == 0 ? th.trustVerified : th.diagError);
        g.building = false;
        // Sign the built artifact (ADR 0061) when the project opts in (signing.sign=true)
        // and a sentinel.key is present — makes the project's "Sign the artifact" real.
        if (code == 0 && g.project.loaded && g.project.signOutput && g.sncCaps.sign) {
            std::wstring key = g.project.dir + L"\\sentinel.key", art = artifactPath();
            if (GetFileAttributesW(key.c_str()) == INVALID_FILE_ATTRIBUTES)
                outAppend(L"  (signing: sign=true but no sentinel.key — generate one via Signing & Trust)", th.diagWarning);
            else if (GetFileAttributesW(art.c_str()) == INVALID_FILE_ATTRIBUTES)
                outAppend(L"  (signing: artifact not found: " + art + L")", th.diagWarning);
            else {
                std::wstring so; DWORD sc = runCapture(L"\"" + g.sncPath + L"\" sign \"" + art + L"\" --key \"" + key + L"\"", g.project.dir, so);
                outAppend(sc == 0 ? (L"[signed · " + baseName(art) + L".sig]") : (L"[sign failed · exit " + std::to_wstring((int)sc) + L"]"),
                          sc == 0 ? th.trustVerified : th.diagError);
                std::wstring t2 = projTrim(so); if (!t2.empty()) outAppend(L"  " + t2, th.textMuted);
                logMsg(sc == 0 ? LogLevel::Info : LogLevel::Warn, L"snc sign artifact " + baseName(art) + L" (exit " + std::to_wstring((int)sc) + L")");
            }
        }
        logMsg(code == 0 ? LogLevel::Info : LogLevel::Warn, L"Build finished — exit " + std::to_wstring((int)code));
        g.statusMsg = code == 0 ? L"Build finished — exit 0" : (L"Build finished — exit " + std::to_wstring((int)code) + (g.problems.empty() ? L"" : L" · " + std::to_wstring(g.problems.size()) + L" problem(s)"));
        if (!g.problems.empty()) { g.dockTab = 0; showDock(); }
        markErrorLines(hwnd);   // tint error lines in the open file (clears prior tints)
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_APP_SIGN: {   // async verify result: lParam = heap "file\tkey\tgrants"
        wchar_t* raw = (wchar_t*)lParam; std::wstring s = raw ? raw : L""; if (raw) free(raw);
        size_t t1 = s.find(L'\t'); std::wstring file = (t1 == std::wstring::npos) ? s : s.substr(0, t1);
        if (file != g.curFilePath) return 0;   // a stale result for a since-closed file
        std::wstring rest = (t1 == std::wstring::npos) ? L"" : s.substr(t1 + 1);
        size_t t2 = rest.find(L'\t');
        g.signKey = (t2 == std::wstring::npos) ? rest : rest.substr(0, t2);
        g.signGrants = (t2 == std::wstring::npos) ? L"" : rest.substr(t2 + 1);
        g.signState = (SignState)wParam;
        InvalidateRect(hwnd, &g.rStatus, FALSE);
        return 0;
    }
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->idFrom == IDC_TREE && nm->code == NM_CLICK) {
            TVHITTESTINFO ht{}; GetCursorPos(&ht.pt); ScreenToClient(g.hTree, &ht.pt);
            if (HTREEITEM it = TreeView_HitTest(g.hTree, &ht)) {
                TVITEMW tvi{}; tvi.mask = TVIF_PARAM; tvi.hItem = it;
                if (TreeView_GetItem(g.hTree, &tvi) && tvi.lParam == kProjectSettingsNode)
                    PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_PROJECT_SETTINGS, 0), 0);  // open after the click settles
            }
            return 0;
        }
        if (nm->idFrom == IDC_TREE && nm->code == TVN_SELCHANGEDW) {
            auto* tv = reinterpret_cast<NMTREEVIEWW*>(lParam); LPARAM data = tv->itemNew.lParam;
            if (data <= kTargetNodeBase) { setActiveTarget(hwnd, (int)(kTargetNodeBase - data)); return 0; }   // a target node
            if (data >= 0 && (size_t)data < g.nodePaths.size()) openFile(hwnd, g.nodePaths[data]);
            return 0;
        }
        if (nm->idFrom == IDC_PROBLEMS && nm->code == NM_DBLCLK) {
            auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
            if (ia->iItem >= 0 && (size_t)ia->iItem < g.problems.size()) { const Diag& d = g.problems[ia->iItem]; gotoLineCol(hwnd, d.file, d.line, d.col); }
            return 0;
        }
        if (nm->idFrom == IDC_OUT && nm->code == EN_LINK) {   // click a file:line:col link in the Output pane
            auto* el = reinterpret_cast<ENLINK*>(lParam);
            if (el->msg == WM_LBUTTONUP) {
                LONG n = el->chrg.cpMax - el->chrg.cpMin;
                if (n > 0 && n < 1024) {
                    std::wstring t(n + 1, L'\0');
                    TEXTRANGEW tr{}; tr.chrg = el->chrg; tr.lpstrText = t.data();
                    SendMessageW(g.hOut, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
                    t.resize(wcslen(t.c_str()));
                    Diag d; if (parseDiag(t, d)) gotoLineCol(hwnd, d.file, d.line, d.col);
                }
            }
            return 0;
        }
        if (nm->idFrom == IDC_EDIT && nm->code == EN_CHANGE) { onEditChanged(hwnd); return 0; }
        if (nm->idFrom == IDC_EDIT && (nm->code == EN_VSCROLL || nm->code == EN_HSCROLL)) { if (g.lineNumbers) InvalidateRect(hwnd, &g.rGutter, FALSE); return 0; }
        if (nm->idFrom == IDC_EDIT && nm->code == EN_SELCHANGE) {
            auto* sc2 = reinterpret_cast<SELCHANGE*>(lParam);
            LONG line = (LONG)SendMessageW(g.hEdit, EM_EXLINEFROMCHAR, 0, sc2->chrg.cpMin);
            LONG lineStart = (LONG)SendMessageW(g.hEdit, EM_LINEINDEX, line, 0);
            g.statusLeft = L"Ln " + std::to_wstring(line + 1) + L", Col " + std::to_wstring(sc2->chrg.cpMin - lineStart + 1);
            InvalidateRect(hwnd, &g.rStatus, FALSE); return 0;
        }
        break;
    }
    case WM_COMMAND:
        if (lParam != 0 && LOWORD(wParam) == IDC_EDIT && HIWORD(wParam) == EN_CHANGE) { onEditChanged(hwnd); return 0; }
        if (lParam != 0 && LOWORD(wParam) == IDC_EDIT && (HIWORD(wParam) == EN_VSCROLL || HIWORD(wParam) == EN_HSCROLL)) { if (g.lineNumbers) InvalidateRect(hwnd, &g.rGutter, FALSE); return 0; }
        if (LOWORD(wParam) >= ID_TARGET_BASE && LOWORD(wParam) < ID_TARGET_BASE + 64) { setActiveTarget(hwnd, LOWORD(wParam) - ID_TARGET_BASE); return 0; }
        if (LOWORD(wParam) >= ID_RECENT_BASE && LOWORD(wParam) < ID_RECENT_BASE + kMaxRecents) { openRecent(hwnd, LOWORD(wParam) - ID_RECENT_BASE); return 0; }
        switch (LOWORD(wParam)) {
        case ID_NEW_PROJECT: newProject(hwnd); break;
        case ID_OPEN_PROJECT: openProject(hwnd); break;
        case ID_CLOSE_PROJECT: closeProject(hwnd); break;
        case ID_RECENT_CLEAR: g.settings.recents.clear(); saveSettings(g.settings); g.statusMsg = L"Recent projects cleared"; InvalidateRect(hwnd, &g.rStatus, FALSE); break;
        case ID_SEAL_PROJECT: sealCurrentProject(hwnd); break;
        case ID_OPEN_SEALED: openSealedProject(hwnd); break;
        case ID_FILE_ASSOC: {
            std::wstring exe; bool ok = registerFileAssociations(&exe);
            logMsg(ok ? LogLevel::Info : LogLevel::Warn, L"Register file associations (.sntproject/.sentinel) → " + exe + (ok ? L" [ok]" : L" [failed]"));
            if (ok) MessageBoxW(hwnd, (L".sntproject and .sentinel files now open in SentinelIDE — double-click them in Explorer.\n\nHandler: " + exe +
                                       L"\n\nIf one already has a different default, set it via Explorer ▸ Open with ▸ Choose another app ▸ Always.").c_str(),
                                L"File Associations", MB_OK | MB_ICONINFORMATION);
            else MessageBoxW(hwnd, L"Could not register file associations (registry write failed).", L"File Associations", MB_OK | MB_ICONWARNING);
            g.statusMsg = ok ? L"Registered .sntproject / .sentinel file associations" : L"File association registration failed";
            InvalidateRect(hwnd, &g.rStatus, FALSE);
            break;
        }
        case ID_NEW_FILE: newFile(hwnd); break;
        case ID_SAVE: if (g.fileOpen && g.dirty) saveFile(hwnd); break;
        // Undo/Redo drive RichEdit's native multi-level undo. The highlighter no longer
        // pollutes the stack (formatting runs with undo suspended), so these revert real
        // edits. EM_UNDO/EM_REDO raise EN_CHANGE → onEditChanged re-highlights + marks dirty.
        case ID_UNDO: if (g.fileOpen && SendMessageW(g.hEdit, EM_CANUNDO, 0, 0)) { SendMessageW(g.hEdit, EM_UNDO, 0, 0); SetFocus(g.hEdit); } break;
        case ID_REDO: if (g.fileOpen && SendMessageW(g.hEdit, EM_CANREDO, 0, 0)) { SendMessageW(g.hEdit, EM_REDO, 0, 0); SetFocus(g.hEdit); } break;
        case ID_LINE_NUMBERS:
            g.lineNumbers = !g.lineNumbers; g.settings.lineNumbers = g.lineNumbers; saveSettings(g.settings);
            layout(hwnd); InvalidateRect(hwnd, nullptr, FALSE);
            if (g.fileOpen) SetFocus(g.hEdit);
            break;
        case ID_PROJECT_SETTINGS: openProjectSettings(hwnd); break;
        case ID_BUILD: runBuild(hwnd); break;
        case ID_RUN: runRun(hwnd); break;
        case ID_SIGNING: openSigning(hwnd); break;
        case ID_SETTINGS:
            logMsg(LogLevel::Info, L"Settings: dialog requested");
            if (showSettingsDialog(hwnd, g.settings, g.sncPath, g.vcvarsPath)) {
                themeOverride() = g.settings.themeMode;
                createFonts();
                SendMessageW(g.hTree, WM_SETFONT, (WPARAM)g.ui, TRUE);
                SendMessageW(g.hProblems, WM_SETFONT, (WPARAM)g.ui, TRUE);
                applyTheme(hwnd);
                logger().configure(g.settings.logFile, g.settings.logLevel);
                saveSettings(g.settings);
                resolveToolchain();   // re-resolve snc + MSVC env from the new settings
                if (g.fileOpen) refreshSignState(hwnd);   // snc may have changed → re-verify chip
                logMsg(LogLevel::Info, L"Settings updated — font=" + g.settings.editorFont + L", theme=" + std::to_wstring(g.settings.themeMode) + L", logLevel=" + std::to_wstring((int)g.settings.logLevel));
                g.statusMsg = L"Settings saved";
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case ID_ABOUT: showAboutDialog(hwnd); break;
        case ID_TIER_DEV: case ID_TIER_EXP: case ID_TIER_STABLE: case ID_TIER_HARD:
            g.tier = LOWORD(wParam) - ID_TIER_DEV; setProjectTitle(hwnd);
            logMsg(LogLevel::Info, std::wstring(L"Tier → ") + tierName(g.tier));
            InvalidateRect(hwnd, nullptr, FALSE); break;
        case ID_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_GETMINMAXINFO: { auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam); mmi->ptMinTrackSize.x = sc(820); mmi->ptMinTrackSize.y = sc(560); return 0; }
    case WM_DPICHANGED: {
        int old = g.dpi; g.dpi = HIWORD(wParam);
        if (old > 0) { g.sidebarW = MulDiv(g.sidebarW, g.dpi, old); g.dockH = MulDiv(g.dockH, g.dpi, old); }
        createFonts(); SendMessageW(g.hTree, WM_SETFONT, (WPARAM)g.ui, TRUE); SendMessageW(g.hProblems, WM_SETFONT, (WPARAM)g.ui, TRUE);
        RECT* nr = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, nr->left, nr->top, nr->right - nr->left, nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout(hwnd); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_DESTROY:
        destroyFonts(); if (g.himl) ImageList_Destroy(g.himl);
        if (g.textDoc) { g.textDoc->Release(); g.textDoc = nullptr; }
        if (g.memBmp) DeleteObject(g.memBmp); if (g.memDC) DeleteDC(g.memDC);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int runApp(HINSTANCE hInstance, int nCmdShow, PWSTR /*cmdLine*/) {
    g.hInst = hInstance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hInstance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(100));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, kClassName, kAppName, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1200, 820, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;
    { UINT dpi = GetDpiForWindow(hwnd); SetWindowPos(hwnd, nullptr, 0, 0, MulDiv(1200, (int)dpi, 96), MulDiv(820, (int)dpi, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); }
    applyWindowDarkMode(hwnd);
    applyMenuDarkMode();   // dark popup/context menus from first open
    ShowWindow(hwnd, nCmdShow); UpdateWindow(hwnd);

    // CLI: SentinelIDE.exe <file|folder> [--build]
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring openArg, tierArg; bool autoBuild = false, autoSettings = false, autoProjSettings = false, autoSigning = false, autoAbout = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--build") autoBuild = true;
        else if (a == L"--settings") autoSettings = true;
        else if (a == L"--project-settings") autoProjSettings = true;
        else if (a == L"--signing") autoSigning = true;
        else if (a == L"--about") autoAbout = true;
        else if (a == L"--tier" && i + 1 < argc) tierArg = argv[++i];
        else openArg = a;
    }
    if (argv) LocalFree(argv);
    if (!openArg.empty()) openPathArg(hwnd, openArg);
    if (!tierArg.empty() && g.project.loaded) { g.tier = tierFromName(tierArg); setProjectTitle(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
    if (autoBuild && (g.fileOpen || g.project.loaded)) runBuild(hwnd);
    if (autoSettings) PostMessageW(hwnd, WM_COMMAND, ID_SETTINGS, 0);
    if (autoProjSettings && g.project.loaded) PostMessageW(hwnd, WM_COMMAND, ID_PROJECT_SETTINGS, 0);
    if (autoSigning) PostMessageW(hwnd, WM_COMMAND, ID_SIGNING, 0);
    if (autoAbout) PostMessageW(hwnd, WM_COMMAND, ID_ABOUT, 0);

    // Keyboard accelerators — make the menu shortcuts real (work even when the editor has focus).
    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL,          (WORD)'S',    ID_SAVE },
        { FVIRTKEY | FCONTROL,          (WORD)'Z',    ID_UNDO },
        { FVIRTKEY | FCONTROL,          (WORD)'Y',    ID_REDO },
        { FVIRTKEY | FCONTROL,          (WORD)'N',    ID_NEW_PROJECT },
        { FVIRTKEY | FCONTROL,          (WORD)'O',    ID_OPEN_PROJECT },
        { FVIRTKEY | FCONTROL | FSHIFT, (WORD)'N',    ID_NEW_FILE },
        { FVIRTKEY | FCONTROL | FSHIFT, (WORD)'B',    ID_BUILD },
        { FVIRTKEY,                     VK_F5,        ID_RUN },
        { FVIRTKEY | FCONTROL,          (WORD)'L',    ID_LINE_NUMBERS },
        { FVIRTKEY | FCONTROL,          VK_OEM_COMMA, ID_SETTINGS },
    };
    HACCEL hAccel = CreateAcceleratorTableW(accels, (int)(sizeof(accels) / sizeof(accels[0])));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (hAccel && TranslateAcceleratorW(hwnd, hAccel, &msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (hAccel) DestroyAcceleratorTable(hAccel);
    CoUninitialize();
    return (int)msg.wParam;
}

}  // namespace sentinelide
