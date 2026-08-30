// SPDX-License-Identifier: GPL-3.0-or-later
//
// diag_xcheck — proves the Sentinel diagnostic parser (src/sentinel/diag.sentinel,
// compiled to build/generated/diag.lib) produces byte-identical results to the C++
// parseDiag oracle it replaced. This is what keeps the two implementations in
// lockstep: the Sentinel export ships by default, the C++ fallback compiles when snc
// is absent, and this test fails if they ever diverge.
//
// Build+run (needs build\generated\diag.lib, i.e. a build.bat run first):
//     cmake --build build --target diag_xcheck && build\diag_xcheck.exe
// or via ctest.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" {
    void parse_diag(const uint8_t*, int64_t, uint8_t**, int64_t*);
    void sentinel_free_bytes(uint8_t*);
}

// ---- verbatim copy of src/host/win32/MainWindow.cpp parseDiag (the oracle) ----
struct Diag { std::wstring file; int line = 1, col = 1; std::wstring msg; };
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
    size_t ts = start; while (ts < s.size() && s[ts] == L' ') ts++;
    std::wstring file = s.substr(ts, (e + 9) - ts);
    d.file = file; d.line = ln; d.col = col; d.msg = s;
    if (tokStart) *tokStart = ts;
    if (tokEnd) *tokEnd = q;
    return true;
}

// ---- Sentinel-side wrapper: line (wstring) -> parsed fields ----
static uint64_t rdU64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

struct SResult { bool matched = false; long long line = 0, col = 0, ts = 0, te = 0; std::wstring file; };
SResult sentinelParse(const std::wstring& w) {
    int un = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u8(un, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), u8.data(), un, nullptr, nullptr);
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_diag((const uint8_t*)u8.data(), (int64_t)u8.size(), &out, &olen);
    SResult r;
    if (olen >= 1 && out[0] == 1) {
        r.matched = true;
        r.line = (long long)rdU64(out + 1);
        r.col  = (long long)rdU64(out + 9);
        int64_t ts_b = (int64_t)rdU64(out + 17), te_b = (int64_t)rdU64(out + 25);
        // token bytes: out[33 .. olen)
        int flen = (int)(olen - 33);
        std::string ftok((const char*)out + 33, flen);
        int fw = MultiByteToWideChar(CP_UTF8, 0, ftok.data(), (int)ftok.size(), nullptr, 0);
        std::wstring fwide(fw, 0); MultiByteToWideChar(CP_UTF8, 0, ftok.data(), (int)ftok.size(), fwide.data(), fw);
        r.file = fwide;
        // byte offset -> wchar offset (prefix length in wchars)
        r.ts = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)ts_b, nullptr, 0);
        r.te = MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)te_b, nullptr, 0);
    }
    if (out) sentinel_free_bytes(out);
    return r;
}

static int pass = 0, fail = 0;
void check(const std::wstring& line) {
    Diag d; size_t cts = 0, cte = 0; bool cm = parseDiag(line, d, &cts, &cte);
    SResult s = sentinelParse(line);
    bool ok = (cm == s.matched);
    if (cm) ok = ok && d.file == s.file && d.line == s.line && d.col == s.col
                   && (long long)cts == s.ts && (long long)cte == s.te;
    printf("[%s] \"%ls\"\n", ok ? "PASS" : "FAIL", line.c_str());
    if (!ok) {
        printf("      C++      : match=%d file=\"%ls\" ln=%d col=%d ts=%zu te=%zu\n", cm, d.file.c_str(), d.line, d.col, cts, cte);
        printf("      Sentinel : match=%d file=\"%ls\" ln=%lld col=%lld ts=%lld te=%lld\n", s.matched, s.file.c_str(), s.line, s.col, s.ts, s.te);
    }
    if (ok) pass++; else fail++;
}

int main() {
    check(L"snc: error: crypto.sentinel:3:5: mismatched types");
    check(L"src/main.sentinel:12: warning: unused");
    check(L"no diagnostic on this line at all");
    check(L"  G:\\proj\\src\\a.sentinel:7:2  something");
    check(L"crypto.sentinel.sig:3:5 not a real diag (.sentinel not followed by colon)");
    check(L"trailing.sentinel:99");                 // no col
    check(L"[dependency] pulls in lib.sentinel:4:1"); // '[' stops the walk-back
    check(L"multi a.sentinel:1:1 then b.sentinel:2:2"); // first wins
    check(L".sentinel:5:5 degenerate empty-name");
    check(L"exactly.sentinel:0:0 zeros");
    check(L"café/app.sentinel:8:3 non-ascii dir");   // UTF-8 multibyte in path
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
