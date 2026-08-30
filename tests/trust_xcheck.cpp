// SPDX-License-Identifier: GPL-3.0-or-later
//
// trust_xcheck — proves the Sentinel trust-manifest parser (parse_trust in
// src/sentinel/parsers.sentinel) is byte-identical to the C++ loadTrust oracle it
// replaced. This is a SECURITY-boundary parser, so lockstep matters doubly. Fails if
// the two ever diverge.
//     cmake --build build --target trust_xcheck && build	rust_xcheck.exe
// Cross-check the Sentinel parse_trust against the C++ loadTrust oracle.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {
    void parse_trust(const uint8_t*, int64_t, uint8_t**, int64_t*);
    void sentinel_free_bytes(uint8_t*);
}

// ---- oracle: verbatim projTrim/projUnq/parseInlineArr/loadTrust (text form) ----
struct TrustedKey { std::wstring name, pubkey; std::vector<std::wstring> grants; };
static std::wstring projUnq(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t"); if (a == std::wstring::npos) return L""; s = s.substr(a);
    size_t b = s.find_last_not_of(L" \t\r"); s = s.substr(0, b + 1);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = s.substr(1, s.size() - 2);
    return s;
}
static std::wstring projTrim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::vector<std::wstring> parseInlineArr(const std::wstring& s) {
    std::vector<std::wstring> v; std::wstring cur; bool inq = false;
    for (wchar_t c : s) { if (c == L'"') { inq = !inq; if (!inq) { v.push_back(cur); cur.clear(); } } else if (inq) cur += c; }
    return v;
}
static std::vector<TrustedKey> loadTrustText(const std::wstring& text) {
    std::vector<TrustedKey> keys;
    if (text.empty()) return keys;
    TrustedKey* cur = nullptr;
    auto handle = [&](std::wstring l) {
        std::wstring t = projTrim(l);
        if (t.empty() || t[0] == L'#') return;
        if (t.size() >= 2 && t[0] == L'[' && t[1] == L'[') {
            if (t.find(L"keys") != std::wstring::npos) { keys.push_back(TrustedKey{}); cur = &keys.back(); }
            else cur = nullptr;
            return;
        }
        if (t[0] == L'[') { cur = nullptr; return; }
        if (!cur) return;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) return;
        std::wstring k = projTrim(t.substr(0, eq)), v = t.substr(eq + 1);
        if (k == L"pubkey")      cur->pubkey = projUnq(v);
        else if (k == L"name")   cur->name = projUnq(v);
        else if (k == L"grants") cur->grants = parseInlineArr(v);
    };
    std::wstring line;
    for (wchar_t c : text) { if (c == L'\n') { if (!line.empty() && line.back() == L'\r') line.pop_back(); handle(line); line.clear(); } else line += c; }
    if (!line.empty()) handle(line);
    return keys;
}

// ---- Sentinel side: wstring text -> vector<TrustedKey> via the stream ----
static uint64_t rdU64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static std::wstring readLP(const uint8_t* out, size_t& pos) {
    uint64_t len = rdU64(out + pos); pos += 8;
    std::wstring w;
    if (len > 0) {
        int fw = MultiByteToWideChar(CP_UTF8, 0, (const char*)out + pos, (int)len, nullptr, 0);
        w.resize((size_t)fw);
        MultiByteToWideChar(CP_UTF8, 0, (const char*)out + pos, (int)len, w.data(), fw);
    }
    pos += (size_t)len;
    return w;
}
static std::vector<TrustedKey> sentinelParse(const std::wstring& text) {
    int un = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string u8((size_t)un, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), u8.data(), un, nullptr, nullptr);
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_trust((const uint8_t*)u8.data(), (int64_t)u8.size(), &out, &olen);
    std::vector<TrustedKey> keys;
    size_t pos = 0;
    while (pos < (size_t)olen && out[pos] == 1) {
        pos++;
        TrustedKey k;
        k.name = readLP(out, pos);
        k.pubkey = readLP(out, pos);
        while (pos < (size_t)olen && out[pos] == 1) { pos++; k.grants.push_back(readLP(out, pos)); }
        if (pos < (size_t)olen) pos++;   // grants terminator (0)
        keys.push_back(k);
    }
    if (out) sentinel_free_bytes(out);
    return keys;
}

static int pass = 0, fail = 0;
static std::wstring keyStr(const TrustedKey& k) {
    std::wstring s = L"name=" + k.name + L" pub=" + k.pubkey + L" grants=[";
    for (size_t i = 0; i < k.grants.size(); i++) { if (i) s += L","; s += k.grants[i]; }
    return s + L"]";
}
void check(const char* label, const std::wstring& text) {
    auto c = loadTrustText(text);
    auto s = sentinelParse(text);
    bool ok = c.size() == s.size();
    for (size_t i = 0; ok && i < c.size(); i++)
        ok = c[i].name == s[i].name && c[i].pubkey == s[i].pubkey && c[i].grants == s[i].grants;
    printf("[%s] %s  (%zu keys)\n", ok ? "PASS" : "FAIL", label, c.size());
    if (!ok) {
        printf("      C++ (%zu):\n", c.size());      for (auto& k : c) printf("        %ls\n", keyStr(k).c_str());
        printf("      Sentinel (%zu):\n", s.size()); for (auto& k : s) printf("        %ls\n", keyStr(k).c_str());
    }
    if (ok) pass++; else fail++;
}

int main() {
    check("empty", L"");
    check("comments only", L"# just a comment\r\n  # another\r\n");
    check("one full key",
        L"[[keys]]\r\nname = \"Sentinel-IDE demo key\"\r\n"
        L"pubkey = \"58ad2d8cf5294de180a25c2cb8046f422d114dbb5c8a3a91f6483b0b9c476ca5\"\r\n"
        L"grants = [\"secret\", \"constant_time\", \"alloc\"]\r\n");
    check("empty key (header, no fields)", L"[[keys]]\r\n");
    check("two keys",
        L"[[keys]]\npubkey = \"aa\"\nname = \"one\"\n"
        L"[[keys]]\nname = \"two\"\ngrants = [\"x\"]\npubkey = \"bb\"\n");
    check("loose match [[keystore]] treated as keys", L"[[keystore]]\npubkey = \"cc\"\n");
    check("other table closes block", L"[[keys]]\npubkey = \"dd\"\n[meta]\npubkey = \"ignored\"\n");
    check("grants spacing + empty array", L"[[keys]]\ngrants=[  \"a\"  ,\"b\"]\npubkey=\"ee\"\ngrants2=[\"nope\"]\n");
    check("no-quote value kept raw", L"[[keys]]\npubkey = ff_no_quotes\n");
    check("ed25519 prefix kept verbatim", L"[[keys]]\npubkey = \"ed25519:deadbeef\"\n");
    check("non-ascii name", L"[[keys]]\nname = \"clé café\"\npubkey = \"aa\"\n");
    check("crlf and trailing spaces", L"[[keys]]   \r\n  pubkey  =  \"gg\"  \r\n");
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
