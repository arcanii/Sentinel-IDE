// SPDX-License-Identifier: GPL-3.0-or-later
//
// manifest_xcheck — proves the Sentinel manifest reader (parse_manifest in
// parsers.sentinel) is byte-identical to the C++ loadProject oracle. IMPORTANT: this
// TU must NOT define SENTINELIDE_SENTINEL, so the Project.h it includes provides the
// C++ loadProject (the oracle); parse_manifest is linked from parsers.lib and unpacked
// with loadProject's own defaults + post-processing. 14 cases incl. the flat-vs-target
// case-sensitivity asymmetry.
//     cmake --build build --target manifest_xcheck && build\manifest_xcheck.exe
// Cross-check the Sentinel parse_manifest against the C++ loadProject oracle.
// loadProject reads a folder's manifest via the Win32 profile API + parseTargets;
// we write each manifest to a temp folder, run both, apply the SAME post-processing
// to the Sentinel raw output, and compare the full SentinelProject.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "Project.h"    // the C++ oracle: loadProject + structs + typeFromName/tierFromName/projBase

extern "C" {
    void parse_manifest(const uint8_t*, int64_t, uint8_t**, int64_t*);
    void sentinel_free_bytes(uint8_t*);
}
using namespace sentinelide;

// ---- unpack parse_manifest's record + apply loadProject's post-processing ----
static uint64_t rd8(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
struct Reader {
    const uint8_t* d; size_t pos = 0, len = 0;
    std::wstring lp() {
        uint64_t n = rd8(d + pos); pos += 8;
        std::wstring w;
        if (n > 0) { int fw = MultiByteToWideChar(CP_UTF8, 0, (const char*)d + pos, (int)n, nullptr, 0); w.resize((size_t)fw); MultiByteToWideChar(CP_UTF8, 0, (const char*)d + pos, (int)n, w.data(), fw); }
        pos += (size_t)n; return w;
    }
    bool scalar(std::wstring& out) { bool found = d[pos] == 1; pos++; std::wstring v = lp(); if (found) out = v; return found; }
    std::vector<std::wstring> arr() { std::vector<std::wstring> v; while (pos < len && d[pos] == 1) { pos++; v.push_back(lp()); } if (pos < len) pos++; return v; }
};

static SentinelProject sentinelLoad(const std::wstring& folder, const std::string& u8) {
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_manifest((const uint8_t*)u8.data(), (int64_t)u8.size(), &out, &olen);
    Reader r{ out, 0, (size_t)olen };
    SentinelProject p; p.dir = folder;
    std::wstring v;
    // scalars, in the order parse_manifest emits, with loadProject's defaults
    p.name = projBase(folder); r.scalar(p.name);
    p.version = L"0.1.0"; r.scalar(p.version);
    std::wstring typ = L"executable"; r.scalar(typ); p.type = typeFromName(typ);
    p.entry = L""; r.scalar(p.entry);
    p.icon = L""; r.scalar(p.icon);
    p.srcDir = L"src"; r.scalar(p.srcDir);
    std::wstring tier = L"experimental"; r.scalar(tier); p.defaultTier = tierFromName(tier);
    p.signRequire = L"off"; r.scalar(p.signRequire);
    p.trust = L"sentinel-trust.toml"; r.scalar(p.trust);
    std::wstring so = L"false"; r.scalar(so); p.signOutput = (so == L"true" || so == L"1");
    p.libPaths = r.arr();
    p.links = r.arr();
    // targets
    while (r.pos < r.len && out[r.pos] == 1) {
        r.pos++;
        Target t;
        t.name = r.lp(); t.entry = r.lp();
        std::wstring tt = r.lp(); t.type = typeFromName(tt);
        // links stream
        while (r.pos < r.len && out[r.pos] == 1) { r.pos++; t.links.push_back(r.lp()); }
        if (r.pos < r.len) r.pos++;   // links terminator
        p.targets.push_back(t);
    }
    if (out) sentinel_free_bytes(out);
    p.explicitTargets = !p.targets.empty();
    if (p.targets.empty()) p.targets.push_back(Target{ p.name, p.entry, p.type, p.links });
    p.loaded = true;
    return p;
}

// ---- comparison ----
static int pass = 0, fail = 0;
static std::wstring join(const std::vector<std::wstring>& v) { std::wstring s; for (size_t i = 0; i < v.size(); i++) { if (i) s += L"|"; s += v[i]; } return s; }
static bool eqT(const Target& a, const Target& b) { return a.name == b.name && a.entry == b.entry && a.type == b.type && a.links == b.links; }

static std::wstring tmpDir;
void check(const char* label, const std::string& manifest) {
    // write manifest to tmpDir\sentinel.toml, load via the oracle
    std::wstring path = tmpDir + L"\\sentinel.toml";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0; WriteFile(f, manifest.data(), (DWORD)manifest.size(), &w, nullptr); CloseHandle(f);
    SentinelProject c = loadProject(tmpDir);
    SentinelProject s = sentinelLoad(tmpDir, manifest);

    bool ok = c.name == s.name && c.version == s.version && c.type == s.type && c.entry == s.entry
        && c.icon == s.icon && c.srcDir == s.srcDir && c.defaultTier == s.defaultTier
        && c.libPaths == s.libPaths && c.links == s.links && c.signRequire == s.signRequire
        && c.trust == s.trust && c.signOutput == s.signOutput && c.explicitTargets == s.explicitTargets
        && c.targets.size() == s.targets.size();
    for (size_t i = 0; ok && i < c.targets.size(); i++) ok = eqT(c.targets[i], s.targets[i]);

    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) {
        printf("   name c=\"%ls\" s=\"%ls\"\n", c.name.c_str(), s.name.c_str());
        printf("   ver  c=\"%ls\" s=\"%ls\"  type c=%d s=%d  entry c=\"%ls\" s=\"%ls\"\n", c.version.c_str(), s.version.c_str(), (int)c.type, (int)s.type, c.entry.c_str(), s.entry.c_str());
        printf("   icon c=\"%ls\" s=\"%ls\"  src c=\"%ls\" s=\"%ls\"  tier c=%d s=%d\n", c.icon.c_str(), s.icon.c_str(), c.srcDir.c_str(), s.srcDir.c_str(), c.defaultTier, s.defaultTier);
        printf("   libp c=[%ls] s=[%ls]  links c=[%ls] s=[%ls]\n", join(c.libPaths).c_str(), join(s.libPaths).c_str(), join(c.links).c_str(), join(s.links).c_str());
        printf("   sign req c=\"%ls\" s=\"%ls\"  trust c=\"%ls\" s=\"%ls\"  out c=%d s=%d\n", c.signRequire.c_str(), s.signRequire.c_str(), c.trust.c_str(), s.trust.c_str(), c.signOutput, s.signOutput);
        printf("   explicitTargets c=%d s=%d  targets c=%zu s=%zu\n", c.explicitTargets, s.explicitTargets, c.targets.size(), s.targets.size());
        for (size_t i = 0; i < c.targets.size() || i < s.targets.size(); i++) {
            std::wstring cn = i < c.targets.size() ? c.targets[i].name : L"<none>", sn = i < s.targets.size() ? s.targets[i].name : L"<none>";
            std::wstring ce = i < c.targets.size() ? c.targets[i].entry : L"", se = i < s.targets.size() ? s.targets[i].entry : L"";
            std::wstring cl = i < c.targets.size() ? join(c.targets[i].links) : L"", sl = i < s.targets.size() ? join(s.targets[i].links) : L"";
            printf("     tgt%zu c={%ls,%ls,[%ls]} s={%ls,%ls,[%ls]}\n", i, cn.c_str(), ce.c_str(), cl.c_str(), sn.c_str(), se.c_str(), sl.c_str());
        }
    }
    if (ok) pass++; else fail++;
}

int main() {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    tmpDir = std::wstring(t) + L"manixcheck";
    CreateDirectoryW(tmpDir.c_str(), nullptr);

    check("real examples/sentinel.toml",
        "# Sentinel project manifest (read by Sentinel-IDE; snc parses the full TOML).\r\n"
        "[project]\r\nname    = \"crypto-lib\"\r\nversion = \"0.1.0\"\r\ntype    = \"executable\"\r\n"
        "entry   = \"crypto.sentinel\"\r\nicon    = \"../art/S2_icon.png\"\r\nauthors = [\"Bryan\"]\r\n\r\n"
        "[build]\r\nsrc          = \".\"\r\nlib_paths    = []\r\nlinks        = []\r\ndefault_tier = \"experimental\"\r\n\r\n"
        "[signing]\r\nrequire = \"warn\"\r\ntrust   = \"sentinel-trust.toml\"\r\nsign    = false\r\n\r\n"
        "[[target]]\r\nname  = \"crypto\"\r\ntype  = \"executable\"\r\nentry = \"crypto.sentinel\"\r\n\r\n"
        "[[target]]\r\nname  = \"hello\"\r\ntype  = \"executable\"\r\nentry = \"hello.sentinel\"\r\n");
    check("minimal (defaults everywhere)", "[project]\r\nname = \"m\"\r\n");
    check("no sections at all (all defaults, single-target fallback)", "# just a comment\r\n");
    check("flat fields case-insensitive", "[PROJECT]\r\nNAME = \"cap\"\r\nVeRsIoN = \"9.9\"\r\n[BUILD]\r\nSRC = \"s2\"\r\n");
    check("target field keys are case-SENSITIVE (Name != name)",
        "[project]\r\nname = \"p\"\r\n[[target]]\r\nName = \"WrongCase\"\r\nentry = \"e.sentinel\"\r\n");
    check("lib_paths + links arrays", "[build]\r\nlib_paths = [\"a\", \"b/c\"]\r\nlinks = [ \"user32\" ,\"gdi32\"]\r\n");
    check("sign true/1/false", "[signing]\r\nsign = true\r\n");
    check("sign = 1", "[signing]\r\nsign = 1\r\n");
    check("library type", "[project]\r\ntype = \"library\"\r\nname = \"lib\"\r\n");
    check("shared/dll type", "[project]\r\ntype = \"dll\"\r\n");
    check("tier stable/hard/dev", "[build]\r\ndefault_tier = \"hardened\"\r\n");
    check("per-target links + type", "[[target]]\r\nname = \"t\"\r\nentry = \"t.sentinel\"\r\ntype = \"library\"\r\nlinks = [\"ws2_32\"]\r\n");
    check("target closed by a normal section", "[[target]]\r\nname = \"t1\"\r\n[meta]\r\nname = \"ignored\"\r\n");
    check("comments and blanks between keys", "[project]\r\n\r\n# c\r\nname = \"x\"\r\n\r\nversion = \"2.0\"\r\n");
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
