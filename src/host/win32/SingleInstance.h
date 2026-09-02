// SPDX-License-Identifier: GPL-3.0-or-later
// Single-instance hand-off. A double-click on an associated .sentinel/.sntproject used to
// spawn a whole second IDE; now the second process gives its path to the running one and exits.
//
// Named mutex + WM_COPYDATA, not a named pipe. The payload is one path, the receiver is a
// single-UI-thread WndProc app, SendMessageTimeoutW yields a definite delivered/not-delivered
// answer for free (a pipe needs an explicit ack), and the window has to be found anyway for
// AllowSetForegroundWindow. A pipe would add a CreateNamedPipe/ConnectNamedPipe/worker-thread/
// ACL lifecycle and still have to post onto the UI thread to do anything. It would only win for
// a future long-lived channel (a `code -w` style "open and wait"), which is why the sender sits
// behind this seam.
#pragma once
#include <windows.h>
#include <string>

#include "core/FileAssoc.h"   // moduleExePath()

namespace sentinelide {

// Identifies a WM_COPYDATA as ours rather than any other app's.
constexpr ULONG_PTR kCopyDataOpenPath = 0x5E27107E;

namespace detail {

// Key the mutex on the exe path, so a dev build in build\ and an installed copy are separate
// instances. Without this, testing a local build would hand its argv to whatever release the
// user has installed — the window class name alone cannot tell them apart. Both processes
// derive this the same way (GetModuleFileNameW), so it is consistent even on a mapped drive.
inline std::wstring instanceKey() {
    std::wstring p = moduleExePath();
    for (auto& c : p) c = (wchar_t)towlower(c);
    unsigned long long h = 1469598103934665603ull;          // FNV-1a
    for (wchar_t c : p) { h ^= (unsigned long long)c; h *= 1099511628211ull; }
    wchar_t buf[64];
    swprintf_s(buf, L"Local\\SentinelIDE.SingleInstance.%016llx", h);
    return buf;
}

// Identity, not spelling. Comparing path STRINGS is wrong here: on a mapped network drive
// GetModuleFileNameW reports "G:\...\Sentinel-IDE.exe" while QueryFullProcessImageNameW reports
// the UNC "\\server\share\...\Sentinel-IDE.exe" for the very same file, so a string compare
// rejects a legitimate sibling instance (measured on this machine). Volume serial + file index
// is the same for both spellings.
inline bool sameFile(const std::wstring& a, const std::wstring& b) {
    auto info = [](const std::wstring& path, BY_HANDLE_FILE_INFORMATION& out) {
        HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const bool ok = GetFileInformationByHandle(h, &out) != 0;
        CloseHandle(h);
        return ok;
    };
    BY_HANDLE_FILE_INFORMATION ia{}, ib{};
    if (!info(a, ia) || !info(b, ib)) return false;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
           ia.nFileIndexHigh == ib.nFileIndexHigh &&
           ia.nFileIndexLow == ib.nFileIndexLow;
}

inline bool processImageIs(DWORD pid, const std::wstring& want) {
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;
    wchar_t theirs[MAX_PATH * 2] = L"";
    DWORD n = MAX_PATH * 2;
    const bool ok = QueryFullProcessImageNameW(proc, 0, theirs, &n) != 0;
    CloseHandle(proc);
    if (!ok) return false;
    return _wcsicmp(theirs, want.c_str()) == 0 || sameFile(theirs, want);
}

struct FindCtx { const wchar_t* cls; const std::wstring* exe; HWND found; };

inline BOOL CALLBACK findProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    wchar_t cls[128] = L"";
    if (!GetClassNameW(hwnd, cls, 128) || _wcsicmp(cls, ctx->cls) != 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    if (!processImageIs(pid, *ctx->exe)) return TRUE;   // a different build of the app
    ctx->found = hwnd;
    return FALSE;                                        // stop at the first real match
}

}  // namespace detail

// True if THIS process is the first instance. The handle is deliberately leaked: it must live
// as long as the process, and the OS reclaims it at exit.
inline bool acquireSingleInstance() {
    const std::wstring key = detail::instanceKey();
    HANDLE m = CreateMutexW(nullptr, TRUE, key.c_str());
    if (!m) return true;                     // cannot tell — behave as first, never block a launch
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

// Hand `fullPath` (may be empty = "just come to the front") to the running instance.
// Returns true only if it was definitely received; on ANY failure the caller must fall through
// to a normal launch, because an extra window is a nuisance but a dropped double-click is a bug.
//
// EnumWindows rather than FindWindowW: FindWindowW was measured returning NULL here while
// EnumWindows found the very same window by the very same class name, and enumerating is needed
// anyway to pick the right window when several copies exist (a stray per-machine install, say)
// instead of whichever one FindWindow happens to return first.
inline bool handOffToRunningInstance(const std::wstring& fullPath) {
    const std::wstring me = moduleExePath();
    HWND target = nullptr;
    // The winner creates its mutex before its window, so a race here is normal: retry briefly.
    for (int i = 0; i < 40 && !target; ++i) {
        detail::FindCtx ctx{ L"SentinelIDEMainWindow", &me, nullptr };
        EnumWindows(detail::findProc, reinterpret_cast<LPARAM>(&ctx));
        target = ctx.found;
        if (!target) Sleep(100);
    }
    if (!target) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    if (pid) AllowSetForegroundWindow(pid);   // let the running instance raise itself

    COPYDATASTRUCT cds{};
    cds.dwData = kCopyDataOpenPath;
    cds.cbData = (DWORD)((fullPath.size() + 1) * sizeof(wchar_t));
    cds.lpData = (PVOID)fullPath.c_str();
    DWORD_PTR result = 0;
    // Time-boxed: the target can be busy in a synchronous snc call at startup, and a blocked
    // second process looks like a hang to whoever double-clicked.
    const LRESULT sent = SendMessageTimeoutW(target, WM_COPYDATA, 0, (LPARAM)&cds,
                                             SMTO_ABORTIFHUNG | SMTO_NORMAL, 5000, &result);
    return sent != 0 && result != 0;
}

}  // namespace sentinelide
