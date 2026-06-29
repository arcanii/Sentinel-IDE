// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE diagnostic logger — a small, thread-safe, append-only file logger
// with a configurable level and location (set from Settings). Header-only.
#pragma once
#include <windows.h>
#include <string>
#include <mutex>
#include <cstdio>

namespace sentinelide {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3, Trace = 4 };

inline const wchar_t* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Error: return L"ERROR";
        case LogLevel::Warn:  return L"WARN ";
        case LogLevel::Info:  return L"INFO ";
        case LogLevel::Debug: return L"DEBUG";
        default:              return L"TRACE";
    }
}

class Logger {
public:
    void configure(const std::wstring& file, LogLevel level) {
        std::lock_guard<std::mutex> lk(m_); file_ = file; level_ = level;
    }
    void setLevel(LogLevel l) { std::lock_guard<std::mutex> lk(m_); level_ = l; }
    void setFile(const std::wstring& f) { std::lock_guard<std::mutex> lk(m_); file_ = f; }
    std::wstring file() { std::lock_guard<std::mutex> lk(m_); return file_; }
    LogLevel level() { std::lock_guard<std::mutex> lk(m_); return level_; }

    void log(LogLevel l, const std::wstring& msg) {
        std::lock_guard<std::mutex> lk(m_);
        if (static_cast<int>(l) > static_cast<int>(level_) || file_.empty()) return;
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t ts[40];
        swprintf(ts, 40, L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        std::wstring line = std::wstring(ts) + L" [" + levelName(l) + L"] " + msg + L"\r\n";
        int n = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1) return;
        std::string u(n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, u.data(), n, nullptr, nullptr);
        HANDLE h = CreateFileW(file_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD w = 0; WriteFile(h, u.data(), (DWORD)u.size(), &w, nullptr); CloseHandle(h); }
    }

private:
    std::mutex m_;
    std::wstring file_;
    LogLevel level_ = LogLevel::Info;
};

inline Logger& logger() { static Logger g; return g; }
inline void logMsg(LogLevel l, const std::wstring& m) { logger().log(l, m); }

}  // namespace sentinelide
