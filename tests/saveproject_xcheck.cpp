// SPDX-License-Identifier: GPL-3.0-or-later
// saveproject_xcheck — proves the Sentinel manifest WRITER stays byte-identical to the
// C++ oracle it replaces, the same way diag/trust/sig/manifest_xcheck do for the readers.
//
// Build+run:  cmake --build build --target saveproject_xcheck && build\saveproject_xcheck.exe
//
// This TU deliberately does NOT define SENTINELIDE_SENTINEL, so its Project.h is the C++
// oracle (saveProject), and it links parsers.lib to call save_manifest directly. Both
// sides build the model through the SAME encodeSaveModel(), because two encoders that
// have to agree is exactly the kind of silent divergence this test exists to catch.
//
// KNOWN, DELIBERATE DIVERGENCE — not tested here, documented instead. save_manifest is
// byte-transparent; the C++ oracle round-trips the file through UTF-16 (readUtf8 calls
// MultiByteToWideChar without MB_ERR_INVALID_CHARS), so the C++ rewrites invalid UTF-8 to
// U+FFFD and Sentinel does not. It only shows on bytes inside a line the writer does not
// rewrite. Sentinel's behaviour is the better one and is kept, so every case below feeds
// valid UTF-8, where the two agree exactly. See the header of parsers.sentinel.
#include "Project.h"

#include <cstdio>
#include <string>
#include <vector>

extern "C" void save_manifest(const uint8_t*, int64_t, const uint8_t*, int64_t, uint8_t**, int64_t*);
extern "C" void sentinel_free_bytes(uint8_t*);

using namespace sentinelide;

static int gPass = 0, gFail = 0;
static std::wstring gDir;

static void writeManifest(const std::string& utf8) {
    HANDLE f = CreateFileW((gDir + L"\\sentinel.toml").c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(f, utf8.data(), (DWORD)utf8.size(), &w, nullptr);
    CloseHandle(f);
}
static std::string readManifest() {
    HANDLE f = CreateFileW((gDir + L"\\sentinel.toml").c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return std::string();
    DWORD n = GetFileSize(f, nullptr), got = 0;
    std::string s(n, '\0');
    ReadFile(f, s.data(), n, &got, nullptr);
    CloseHandle(f);
    s.resize(got);
    return s;
}

// Show the first differing byte — a diff of two near-identical manifests is unreadable.
static void reportDiff(const char* what, const std::string& a, const std::string& b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    printf("  [FAIL] %s\n", what);
    printf("         oracle   %zu bytes, sentinel %zu bytes, first diff at %zu\n", a.size(), b.size(), i);
    auto win = [](const std::string& s, size_t at) {
        std::string o;
        for (size_t k = (at > 12 ? at - 12 : 0); k < s.size() && k < at + 12; ++k)
            o += (s[k] == '\r') ? "\\r" : (s[k] == '\n') ? "\\n" : std::string(1, s[k]);
        return o;
    };
    printf("         oracle   ...%s...\n", win(a, i).c_str());
    printf("         sentinel ...%s...\n", win(b, i).c_str());
}

// Run both writers over the same input + model and compare the produced bytes.
static void xcheck(const char* what, const std::string& input,
                   void (*mutate)(SentinelProject&) = nullptr) {
    writeManifest(input);
    SentinelProject p = loadProject(gDir);
    if (!p.loaded) { p.dir = gDir; p.manifest = L"sentinel.toml"; }
    if (mutate) mutate(p);

    // --- the C++ oracle, via the real saveProject (it writes the file) ---
    saveProject(p);
    const std::string oracle = readManifest();

    // --- the Sentinel writer, over the ORIGINAL input bytes ---
    const std::string model = encodeSaveModel(p);
    uint8_t* out = nullptr; int64_t olen = 0;
    save_manifest((const uint8_t*)input.data(), (int64_t)input.size(),
                  (const uint8_t*)model.data(), (int64_t)model.size(), &out, &olen);
    const std::string sentinel(out ? (const char*)out : "", out ? (size_t)olen : 0);
    if (out) sentinel_free_bytes(out);

    if (oracle == sentinel) { printf("  [PASS] %s\n", what); gPass++; }
    else { reportDiff(what, oracle, sentinel); gFail++; }
}

int main() {
    wchar_t tmp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, tmp);
    gDir = std::wstring(tmp) + L"sntxsave_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(gDir.c_str(), nullptr);
    printf("saveProject: Sentinel vs C++ oracle\n\n");

    const std::string full =
        "# leading comment\r\n\r\n"
        "[project]\r\n"
        "name    = \"demo\"\r\n"
        "version = \"0.1.0\"\r\n"
        "type    = \"executable\"\r\n"
        "entry   = \"main.sentinel\"\r\n"
        "icon    = \"../art/S2_icon.png\"   # unmodeled\r\n"
        "authors = [\"Bryan\"]\r\n\r\n"
        "# comment inside build\r\n"
        "[build]\r\n"
        "src          = \".\"\r\n"
        "lib_paths    = []\r\n"
        "links        = []\r\n"
        "default_tier = \"experimental\"\r\n\r\n"
        "[signing]\r\n"
        "require = \"warn\"\r\n"
        "trust   = \"sentinel-trust.toml\"\r\n"
        "sign    = false\r\n";

    xcheck("1  full manifest, no model change", full);
    xcheck("2  renamed project", full, [](SentinelProject& p) { p.name = L"renamed"; });
    xcheck("3  every managed scalar changed", full, [](SentinelProject& p) {
        p.name = L"n2"; p.version = L"9.9.9"; p.entry = L"other.sentinel";
        p.srcDir = L"src"; p.signRequire = L"strict"; p.trust = L"t.toml"; p.signOutput = true;
    });
    xcheck("4  arrays populated", full, [](SentinelProject& p) {
        p.libPaths = { L"a", L"b" }; p.links = { L"ws2_32" };
    });

    xcheck("5  empty file", "");
    xcheck("6  preamble only", "# just a comment\r\n");
    xcheck("7  missing keys inserted into existing sections",
           "[project]\r\nname = \"demo\"\r\n\r\n[signing]\r\nrequire = \"off\"\r\n");
    xcheck("8  absent sections created", "[project]\r\nname = \"demo\"\r\n");
    xcheck("9  no trailing newline", "[project]\r\nname = \"demo\"");
    xcheck("10 LF-only line endings", "[project]\nname = \"demo\"\n");
    xcheck("11 trailing blank lines collapse", "[project]\r\nname = \"demo\"\r\n\r\n\r\n\r\n");

    xcheck("12 section case-insensitive", "[PROJECT]\r\nname = \"demo\"\r\n");
    xcheck("13 key case-SENSITIVE ('Name' is not 'name')", "[project]\r\nName = \"demo\"\r\n");
    xcheck("14 duplicate key: only the first is rewritten",
           "[project]\r\nname = \"a\"\r\nname = \"b\"\r\n");
    xcheck("15 duplicate section header",
           "[project]\r\nname = \"a\"\r\n\r\n[project]\r\nversion = \"1\"\r\n");
    xcheck("16 alignment preserved (wide gutter)",
           "[project]\r\nname          = \"demo\"\r\n");
    xcheck("17 header with no closing bracket", "[project\r\nname = \"demo\"\r\n");
    xcheck("18 line that is only '='", "[project]\r\n=\r\nname = \"demo\"\r\n");
    xcheck("19 managed key in the preamble is left alone",
           "name = \"stray\"\r\n[project]\r\nname = \"demo\"\r\n");

    const std::string targets =
        "[project]\r\nname  = \"demo\"\r\nentry = \"a.sentinel\"\r\n\r\n"
        "[[target]]\r\n# first\r\nname  = \"alpha\"\r\nentry = \"a.sentinel\"\r\ncustom = 42\r\n\r\n"
        "[[target]]\r\nname  = \"beta\"\r\nentry = \"b.sentinel\"\r\n";
    xcheck("20 two target blocks, unchanged", targets);
    xcheck("21 first target renamed", targets, [](SentinelProject& p) {
        if (!p.targets.empty()) p.targets[0].name = L"alphaZ";
    });
    xcheck("22 both targets rewritten", targets, [](SentinelProject& p) {
        for (size_t i = 0; i < p.targets.size(); ++i) {
            p.targets[i].entry = L"z.sentinel";
            p.targets[i].type = ProjectType::Library;
        }
    });
    xcheck("23 more blocks than model targets", targets, [](SentinelProject& p) {
        if (p.targets.size() > 1) p.targets.resize(1);
    });
    xcheck("24 spaced '[[ target ]]' is not a target block",
           "[project]\r\nname = \"demo\"\r\n\r\n[[ target ]]\r\nname = \"spaced\"\r\n");
    xcheck("25 target block before [project]",
           "[[target]]\r\nname = \"t\"\r\n\r\n[project]\r\nname = \"demo\"\r\n");

    xcheck("26 UTF-8 content in an unmodeled value",
           "[project]\r\nname = \"demo\"\r\nauthors = [\"Bj\xC3\xB6rn\"]\r\n");
    xcheck("27 quotes and backslashes in a value", full, [](SentinelProject& p) {
        p.entry = L"dir\\sub\\a.sentinel";
    });

    DeleteFileW((gDir + L"\\sentinel.toml").c_str());
    RemoveDirectoryW(gDir.c_str());

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
