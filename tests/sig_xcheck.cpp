// SPDX-License-Identifier: GPL-3.0-or-later
//
// sig_xcheck — proves the Sentinel .sig-carrier parser (parse_sig in
// src/sentinel/parsers.sentinel) is byte-identical to the C++ readSig oracle. Fails if
// the two ever diverge.
//     cmake --build build --target sig_xcheck && build\sig_xcheck.exe
// Cross-check the Sentinel parse_sig against the C++ readSig oracle.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" {
    void parse_sig(const uint8_t*, int64_t, uint8_t**, int64_t*);
    void sentinel_free_bytes(uint8_t*);
}

// ---- oracle: readSig (text form) + projTrim, verbatim ----
struct SigInfo { bool present = false; std::wstring algorithm, key, grants; };
static std::wstring projTrim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static SigInfo readSigText(const std::wstring& text) {
    SigInfo s;
    if (text.empty()) return s;
    s.present = true;
    auto handle = [&](std::wstring l) {
        std::wstring t = projTrim(l);
        size_t c = t.find(L':');
        if (c == std::wstring::npos) return;
        std::wstring k = projTrim(t.substr(0, c)), v = projTrim(t.substr(c + 1));
        if (k == L"algorithm") s.algorithm = v;
        else if (k == L"key")  s.key = v;
        else if (k == L"grants") s.grants = v;
    };
    std::wstring line;
    for (wchar_t c : text) { if (c == L'\n') { if (!line.empty() && line.back() == L'\r') line.pop_back(); handle(line); line.clear(); } else line += c; }
    if (!line.empty()) handle(line);
    return s;
}

// ---- Sentinel side ----
static uint64_t rd8(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static SigInfo sentinelParse(const std::wstring& text) {
    int un = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string u8((size_t)un, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), u8.data(), un, nullptr, nullptr);
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_sig((const uint8_t*)u8.data(), (int64_t)u8.size(), &out, &olen);
    SigInfo s;
    size_t pos = 0;
    s.present = (olen >= 1 && out[0] == 1); pos = 1;
    auto readLP = [&]() -> std::wstring {
        uint64_t len = rd8(out + pos); pos += 8;
        std::wstring w;
        if (len > 0) {
            int fw = MultiByteToWideChar(CP_UTF8, 0, (const char*)out + pos, (int)len, nullptr, 0);
            w.resize((size_t)fw);
            MultiByteToWideChar(CP_UTF8, 0, (const char*)out + pos, (int)len, w.data(), fw);
        }
        pos += (size_t)len; return w;
    };
    s.algorithm = readLP(); s.key = readLP(); s.grants = readLP();
    if (out) sentinel_free_bytes(out);
    return s;
}

static int pass = 0, fail = 0;
void check(const char* label, const std::wstring& text) {
    SigInfo c = readSigText(text), s = sentinelParse(text);
    bool ok = c.present == s.present && c.algorithm == s.algorithm && c.key == s.key && c.grants == s.grants;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) {
        printf("      C++      : present=%d alg=\"%ls\" key=\"%ls\" grants=\"%ls\"\n", c.present, c.algorithm.c_str(), c.key.c_str(), c.grants.c_str());
        printf("      Sentinel : present=%d alg=\"%ls\" key=\"%ls\" grants=\"%ls\"\n", s.present, s.algorithm.c_str(), s.key.c_str(), s.grants.c_str());
    }
    if (ok) pass++; else fail++;
}

int main() {
    check("real crypto.sentinel.sig",
        L"sentinel-signature v1\r\nalgorithm: ed25519\r\n"
        L"key: 58ad2d8cf5294de180a25c2cb8046f422d114dbb5c8a3a91f6483b0b9c476ca5\r\n"
        L"grants: secret,constant_time,alloc\r\n"
        L"signature: af13dd7c1d4306c8\r\n");
    check("empty (present=false)", L"");
    check("whitespace only (present=true, no fields)", L"  \r\n\t\n");
    check("first line no colon is skipped", L"header line\nkey: abc\n");
    check("value contains a colon (split on first)", L"key: ed25519:deadbeef\n");
    check("unknown keys ignored", L"foo: bar\nsignature: xyz\nversion: 9\n");
    check("last-wins", L"key: first\nkey: second\nalgorithm: a\n");
    check("extra whitespace around key and value", L"   algorithm   :    ed448   \n");
    check("no trailing newline", L"key: nolinefeed");
    check("non-ascii grant value", L"grants: café,secret\nkey: aa\n");
    check("empty value", L"key:\nalgorithm: x\n");
    check("colon with no key", L": orphan\nkey: k\n");
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
