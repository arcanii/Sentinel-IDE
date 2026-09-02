// SPDX-License-Identifier: GPL-3.0-or-later
// Characterization tests for Project.h::saveProject — the comment-preserving manifest
// WRITER, and the last file-touching path in the IDE still implemented in C++.
//
// Build+run:  cmake --build build --target saveproject_test && build\saveproject_test.exe
// Exit code is 0 only if every case passes, so it works as a CI gate.
//
// WHY THIS EXISTS. saveProject had ZERO coverage while being simultaneously the most
// dangerous code in the repo: it rewrites the user's project manifest in place, and its
// entire promise is that everything it does NOT manage — comments, blank lines, key
// alignment, unmodeled keys like icon/authors, and [[target]] blocks — survives byte for
// byte. Nothing checked that. These cases pin the behaviour that exists today so the
// planned port to Sentinel has an oracle to be held against, exactly as the four reader
// ports were held against theirs by tests/*_xcheck.cpp.
//
// Two of the cases below pin behaviour that is arguably WRONG (case 8's case-sensitivity
// asymmetry, case 9's "[[ target ]]" spacing hole). They are recorded as-is on purpose:
// a characterization test's job is to describe what the code does, so that a port can be
// proven equivalent before anyone argues about what it should do. Both are called out in
// their case names and in HANDOVER.
#include "Project.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace sentinelide;

static int gPass = 0, gFail = 0;
static void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

// ---- scratch dir -----------------------------------------------------------
static std::wstring gDir;

static std::wstring scratchDir() {
    wchar_t tmp[MAX_PATH] = L"";
    GetTempPathW(MAX_PATH, tmp);
    std::wstring d = std::wstring(tmp) + L"sntsave_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

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
    if (f == INVALID_HANDLE_VALUE) return "<missing>";
    DWORD n = GetFileSize(f, nullptr), got = 0;
    std::string s(n, '\0');
    ReadFile(f, s.data(), n, &got, nullptr);
    CloseHandle(f);
    s.resize(got);
    return s;
}

// Load the manifest we just wrote, mutate via the model, save, and return the new text.
static std::string roundTrip(void (*mutate)(SentinelProject&)) {
    SentinelProject p = loadProject(gDir);
    if (mutate) mutate(p);
    saveProject(p);
    return readManifest();
}

static bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}
static int countOf(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t i = hay.find(needle); i != std::string::npos; i = hay.find(needle, i + needle.size())) ++n;
    return n;
}

int main() {
    gDir = scratchDir();
    printf("saveProject characterization  (%ls)\n", gDir.c_str());

    // -- 1. comments, blank lines and unmodeled keys survive a no-op save ----
    printf("\n1. a save that changes nothing preserves everything it does not manage\n");
    writeManifest(
        "# leading comment\r\n"
        "\r\n"
        "[project]\r\n"
        "name    = \"demo\"\r\n"
        "version = \"0.1.0\"\r\n"
        "type    = \"executable\"\r\n"
        "entry   = \"main.sentinel\"\r\n"
        "icon    = \"../art/S2_icon.png\"   # unmodeled\r\n"
        "authors = [\"Bryan\"]\r\n"
        "\r\n"
        "# a comment inside build\r\n"
        "[build]\r\n"
        "src          = \".\"\r\n"
        "lib_paths    = []\r\n"
        "links        = []\r\n"
        "default_tier = \"experimental\"\r\n"
        "\r\n"
        "[signing]\r\n"
        "require = \"warn\"\r\n"
        "trust   = \"sentinel-trust.toml\"\r\n"
        "sign    = false\r\n");
    {
        const std::string out = roundTrip(nullptr);
        check(contains(out, "# leading comment"),        "leading comment survives");
        check(contains(out, "# a comment inside build"), "in-section comment survives");
        check(contains(out, "icon    = \"../art/S2_icon.png\"   # unmodeled"),
                                                          "unmodeled key + its trailing comment survive verbatim");
        check(contains(out, "authors = [\"Bryan\"]"),      "unmodeled array key survives");
        check(contains(out, "name    = \"demo\""),         "key alignment is preserved on rewrite");
    }

    // -- 2. a managed value changes, and only that line changes -------------
    printf("\n2. changing one managed value rewrites only that value\n");
    {
        const std::string before = readManifest();
        const std::string out = roundTrip([](SentinelProject& p) { p.name = L"renamed"; });
        check(contains(out, "name    = \"renamed\""), "new value written with the original alignment");
        check(!contains(out, "\"demo\""),             "old value gone");
        check(contains(out, "version = \"0.1.0\""),   "sibling managed key untouched");
        check(contains(out, "icon    = \"../art/S2_icon.png\"   # unmodeled"),
                                                       "unmodeled key still untouched after a real edit");
        check(before != out,                           "the file did change");
    }

    // -- 3. a missing managed key is inserted into its existing section -----
    printf("\n3. a managed key absent from the file is inserted into its section\n");
    writeManifest(
        "[project]\r\n"
        "name = \"demo\"\r\n"
        "\r\n"
        "[signing]\r\n"
        "require = \"off\"\r\n");
    {
        const std::string out = roundTrip(nullptr);
        check(countOf(out, "[project]") == 1, "no duplicate [project] header (TOML forbids it)");
        check(countOf(out, "[signing]") == 1, "no duplicate [signing] header");
        check(contains(out, "version ="),     "missing project key inserted");
        check(contains(out, "trust ="),       "missing signing key inserted");
        check(contains(out, "[build]"),       "entirely absent section is created");
        check(contains(out, "src ="),         "keys for the created section are written");
    }

    // -- 4. [[target]] blocks: values rewritten in order, nothing inserted ---
    printf("\n4. [[target]] blocks are rewritten in order and never gain new keys\n");
    writeManifest(
        "[project]\r\n"
        "name  = \"demo\"\r\n"
        "entry = \"a.sentinel\"\r\n"
        "\r\n"
        "[[target]]\r\n"
        "# first target\r\n"
        "name  = \"alpha\"\r\n"
        "entry = \"a.sentinel\"\r\n"
        "custom = 42   # unmodeled per-target key\r\n"
        "\r\n"
        "[[target]]\r\n"
        "name  = \"beta\"\r\n"
        "entry = \"b.sentinel\"\r\n");
    {
        const std::string out = roundTrip([](SentinelProject& p) {
            if (p.targets.size() >= 1) p.targets[0].name = L"alphaZ";
        });
        check(contains(out, "name  = \"alphaZ\""),   "first target renamed in place");
        check(contains(out, "name  = \"beta\""),     "second target untouched");
        check(contains(out, "# first target"),       "per-target comment survives");
        check(contains(out, "custom = 42   # unmodeled per-target key"),
                                                      "unmodeled per-target key survives verbatim");
        check(countOf(out, "[[target]]") == 2,        "target block count unchanged");
        // Scope the "nothing inserted" check to the first target BLOCK: `type` is a managed
        // [project] key, so it legitimately appears elsewhere in the file.
        {
            const size_t a = out.find("[[target]]");
            const size_t b = out.find("[[target]]", a + 1);
            const std::string block = (a == std::string::npos) ? "" : out.substr(a, b - a);
            check(!contains(block, "type ="), "no key is INSERTED into a target block");
            check(!contains(block, "links ="), "...including links, which targets can carry but this one lacks");
        }
    }

    // -- 5. output is CRLF with exactly one trailing newline ----------------
    printf("\n5. output normalises to CRLF with a single trailing newline\n");
    {
        const std::string out = readManifest();
        check(!out.empty() && out.size() >= 2 && out.substr(out.size() - 2) == "\r\n",
              "ends with exactly one CRLF");
        check(out.size() < 4 || out.substr(out.size() - 4) != "\r\n\r\n",
              "no blank line at end of file");
        // every LF is part of a CRLF
        bool allCrlf = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i] == '\n' && (i == 0 || out[i - 1] != '\r')) allCrlf = false;
        check(allCrlf, "no bare LF anywhere in the output");
    }

    // -- 6. an LF-only input is normalised to CRLF --------------------------
    printf("\n6. an LF-only manifest is read and re-emitted as CRLF\n");
    writeManifest("[project]\nname = \"lf\"\n");
    {
        const std::string out = roundTrip(nullptr);
        bool allCrlf = true;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i] == '\n' && (i == 0 || out[i - 1] != '\r')) allCrlf = false;
        check(allCrlf,                      "LF input re-emitted as CRLF");
        check(contains(out, "name = \"lf\""), "value preserved across the normalisation");
    }

    // -- 7. section names match case-INSENSITIVELY --------------------------
    printf("\n7. section names are matched case-insensitively\n");
    writeManifest(
        "[PROJECT]\r\n"
        "name = \"demo\"\r\n");
    {
        const std::string out = roundTrip([](SentinelProject& p) { p.name = L"cased"; });
        check(contains(out, "name = \"cased\""), "value under [PROJECT] was rewritten");
        check(countOf(out, "[PROJECT]") == 1,    "the original header casing is preserved");
        check(!contains(out, "[project]"),       "no second lower-cased [project] section added");
    }

    // -- 8. ...but KEY names match case-SENSITIVELY (asymmetry, pinned) -----
    printf("\n8. key names are matched case-SENSITIVELY -- an asymmetry with case 7\n");
    writeManifest(
        "[project]\r\n"
        "Name = \"demo\"\r\n");
    {
        const std::string out = roundTrip([](SentinelProject& p) { p.name = L"newname"; });
        check(contains(out, "Name = \"demo\""),
              "'Name' is NOT treated as the managed key 'name' -- left untouched");
        check(contains(out, "name = \"newname\""),
              "...and 'name' is inserted separately, so the file now has both");
    }

    // -- 9. "[[ target ]]" with inner spaces is not recognised (pinned) -----
    printf("\n9. a spaced '[[ target ]]' header is not recognised as a target block\n");
    writeManifest(
        "[project]\r\n"
        "name = \"demo\"\r\n"
        "\r\n"
        "[[ target ]]\r\n"
        "name = \"spaced\"\r\n");
    {
        const std::string out = roundTrip([](SentinelProject& p) {
            for (auto& t : p.targets) t.name = L"SHOULD_NOT_APPEAR";
        });
        check(contains(out, "name = \"spaced\""),
              "the spaced block is left entirely alone (header parses to '[ target')");
        check(!contains(out, "SHOULD_NOT_APPEAR"),
              "no target rewrite reached it");
    }

    // -- 10. a missing manifest file does not crash -------------------------
    printf("\n10. saving over a manifest that does not exist yet\n");
    DeleteFileW((gDir + L"\\sentinel.toml").c_str());
    {
        SentinelProject p;
        p.dir = gDir;
        p.manifest = L"sentinel.toml";
        p.name = L"fresh";
        const bool ok = saveProject(p);
        const std::string out = readManifest();
        check(ok,                          "saveProject reports success");
        check(contains(out, "[project]"),  "a usable manifest is produced from nothing");
        check(contains(out, "\"fresh\""),  "the model's value is in it");
    }

    DeleteFileW((gDir + L"\\sentinel.toml").c_str());
    RemoveDirectoryW(gDir.c_str());

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
