// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE process helpers — synchronous run-and-capture for short tools
// (snc keygen/sign/verify, capability probes). The streaming build/run loop in
// MainWindow stays separate (it pumps output live into the dock). Header-only.
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// Strip ANSI/VT escape sequences (snc colorizes its diagnostics).
inline std::wstring stripAnsi(const std::wstring& s) {
    std::wstring o; o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == 0x1B && i + 1 < s.size() && s[i + 1] == L'[') {
            i += 2; while (i < s.size() && !(s[i] >= L'@' && s[i] <= L'~')) i++; if (i < s.size()) i++;
        } else o += s[i++];
    }
    return o;
}

// Run `cmd` (working dir `dir`), capture merged stdout+stderr (UTF-8 → wide,
// ANSI-stripped) into `out`, and return the process exit code. (DWORD)-1 if the
// process couldn't be started. Blocks until the child exits — for short tools only.
inline DWORD runCapture(const std::wstring& cmd, const std::wstring& dir, std::wstring& out) {
    out.clear();
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return (DWORD)-1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring c = cmd;  // CreateProcessW may mutate the command-line buffer
    BOOL ok = CreateProcessW(nullptr, &c[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return (DWORD)-1; }
    std::string buf; char tmp[4096]; DWORD n = 0;
    while (ReadFile(rd, tmp, sizeof(tmp), &n, nullptr) && n > 0) buf.append(tmp, n);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    int wl = MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), nullptr, 0);
    std::wstring w(wl, 0); MultiByteToWideChar(CP_UTF8, 0, buf.data(), (int)buf.size(), w.data(), wl);
    out = stripAnsi(w);
    return code;
}

}  // namespace sentinelide
