// SPDX-License-Identifier: GPL-3.0-or-later
// d2d_editor_demo — a standalone host for the Direct2D editor control (slice 2, phase 46).
//
// WHY THIS EXISTS. Slice 2 deliberately does not touch MainWindow.cpp and is not linked
// into Sentinel-IDE.exe; the message dialect that lets the host talk to this control is
// slice 3. So the only way to verify the renderer without risking the shipping exe is a
// throwaway window that does nothing but create one SentinelD2DEditor filling its client
// area. Build + run:
//
//     cmake --build build --target d2d_editor_demo
//     build\d2d_editor_demo.exe [<utf-8 file>]        (default: examples\crypto.sentinel)
//
// AND THE MODE THAT ACTUALLY PROVES ANYTHING:
//
//     build\d2d_editor_demo.exe --render out.png [<utf-8 file>]   (exit 0 = wrote it)
//
// --render goes through d2dEditorRenderToPng: the same drawContent, into a WIC bitmap,
// encoded as a PNG. It prints nothing on success and never shows the window, so it works
// headless -- which is what makes ctest's d2d_render possible. For simply LOOKING at the
// control, scripts\capture.ps1 -Class SentinelD2DEditorDemo works on the live window.
//
// READ-ONLY, ON PURPOSE. examples/crypto.sentinel is a COMMITTED SIGNED file that the IDE
// opens by default; rewriting it with LF line endings would invalidate its .sig without
// anyone pressing Ctrl+S (phase 46's second way to lose work). This demo opens files with
// GENERIC_READ and there is no save path: no Ctrl+S, no write back to the source file.
// (--render does write, but only the PNG you name, never the document.)
// You can type into the buffer freely; the edits die with the process.
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <objbase.h>   // CoInitializeEx — d2dEditorRenderToPng needs an apartment
#include <shellapi.h>  // CommandLineToArgvW (WIN32_LEAN_AND_MEAN drops it from windows.h)

#include <cwchar>
#include <string>

#include "host/win32/D2DEditor.h"
#include "host/win32/Theme.h"

namespace {

constexpr const wchar_t* kHostClass = L"SentinelD2DEditorDemo";
HWND g_edit = nullptr;

std::string readFileBytes(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(f, &sz) || sz.QuadPart > (64 << 20)) {  // 64 MB sanity cap
        CloseHandle(f);
        return std::string();
    }
    std::string s(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD got = 0;
    if (!s.empty()) ReadFile(f, s.data(), static_cast<DWORD>(s.size()), &got, nullptr);
    CloseHandle(f);
    s.resize(got);
    return s;
}

std::wstring utf8ToW(const std::string& u8) {
    if (u8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.data(), static_cast<int>(u8.size()), w.data(), n);
    return w;
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    const size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash);
}

bool fileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// argv[1] if given; otherwise examples\crypto.sentinel, looked for beside the working
// directory first and then one level above the exe (build\d2d_editor_demo.exe -> repo root).
std::wstring resolveInput(const std::wstring& arg) {
    if (!arg.empty()) return arg;
    const wchar_t* rel = L"examples\\crypto.sentinel";
    if (fileExists(rel)) return rel;
    const std::wstring up = exeDir() + L"\\..\\" + rel;
    if (fileExists(up)) return up;
    return rel;  // report the miss in the title bar rather than guessing further
}

void layoutChild(HWND hwnd) {
    if (!g_edit) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    SetWindowPos(g_edit, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            // WS_HSCROLL | WS_VSCROLL: the control maintains both bars itself via
            // SetScrollInfo, and relies on them being present (SIF_DISABLENOSCROLL).
            g_edit = CreateWindowExW(0, sentinelide::kD2DEditorClass, L"",
                                     WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL, 0, 0, 10, 10,
                                     hwnd, nullptr,
                                     reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                                     nullptr);
            if (!g_edit) return -1;
            sentinelide::d2dEditorApplyTheme(g_edit);
            return 0;
        case WM_SIZE: layoutChild(hwnd); return 0;
        case WM_SETFOCUS:
            if (g_edit) SetFocus(g_edit);
            return 0;
        case WM_DPICHANGED: {
            // Per-monitor-v2: the control rebuilds its font metrics, then the suggested
            // rect is honoured. Same shape as MainWindow's WM_DPICHANGED.
            if (g_edit) sentinelide::d2dEditorUpdateDpi(g_edit, HIWORD(wParam));
            RECT* nr = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, nr->left, nr->top, nr->right - nr->left,
                         nr->bottom - nr->top, SWP_NOZORDER | SWP_NOACTIVATE);
            layoutChild(hwnd);
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // The demo target links no .rc, so there is no app.manifest asserting per-monitor-v2
    // awareness the way Sentinel-IDE.exe does. Assert it here instead, or the control's
    // GetDpiForWindow would report a lie on a scaled display and the text would be blurry
    // for the wrong reason.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Sentinel-IDE does this at startup too (MainWindow.cpp). Without the PROCESS-wide
    // preferred app mode, the control's own SetWindowTheme(L"DarkMode_Explorer") is
    // ignored and its scrollbars come up stock-light against the dark editor.
    sentinelide::applyMenuDarkMode();

    // Two modes off one binary: interactive (no args, or a file) and --render, which
    // produces a PNG and exits without ever showing a window.
    std::wstring arg, outPng;
    bool renderMode = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1 && std::wcscmp(argv[1], L"--render") == 0) {
            renderMode = true;
            if (argc > 2) outPng = argv[2];
            if (argc > 3) arg = argv[3];
        } else if (argc > 1) {
            arg = argv[1];
        }
        LocalFree(argv);
    }
    if (renderMode && outPng.empty()) return 1;  // --render with no destination
    const std::wstring path = resolveInput(arg);
    const std::string bytes = readFileBytes(path);
    const std::wstring text = utf8ToW(bytes);

    if (!sentinelide::registerD2DEditorClass(hInst)) return 1;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // the child covers the whole client
    wc.lpszClassName = kHostClass;
    if (!RegisterClassExW(&wc)) return 1;

    std::wstring title = L"D2D editor demo — ";
    title += bytes.empty() ? (L"COULD NOT READ " + path) : path;
    title += L"  (" + std::to_wstring(bytes.size()) + L" bytes)";

    HWND hwnd = CreateWindowExW(0, kHostClass, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1100, 760, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    sentinelide::applyWindowDarkMode(hwnd);
    if (g_edit) sentinelide::d2dEditorSetText(g_edit, text);

    if (renderMode) {
        // The child is sized from WM_SIZE, which a window created but never shown may not
        // have received with a meaningful rect yet — size it explicitly rather than render
        // a 10x10 image. The window stays HIDDEN: D2D draws into a WIC bitmap, so nothing
        // here needs a visible window -- which is why this mode works headless.
        layoutChild(hwnd);
        if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
        const bool ok = g_edit && sentinelide::d2dEditorRenderToPng(g_edit, outPng.c_str());
        CoUninitialize();
        DestroyWindow(hwnd);
        return ok ? 0 : 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetFocus(g_edit);

    // NULL message filter, exactly like every modal loop in this app — the control's
    // WM_TIMER and WM_PAINT have to survive being pumped by a loop that is not its own.
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
