// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE project model — a Sentinel project is a folder with a `sentinel.toml`
// manifest (sources, libraries, metadata, signing). Header-only. The manifest is
// real TOML; the IDE reads the flat parts via the Win32 profile API (a TOML subset)
// and strips quotes — snc itself parses the full TOML.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cwctype>

namespace sentinelide {

enum class ProjectType { Executable, Library, Shared };
inline const wchar_t* typeName(ProjectType t) {
    return t == ProjectType::Library ? L"library" : t == ProjectType::Shared ? L"shared" : L"executable";
}

// Build tiers (TIERED_RELEASES.md): Development / Experimental / Stable / Hardened.
inline const wchar_t* tierName(int t) { static const wchar_t* n[] = { L"Development", L"Experimental", L"Stable", L"Hardened" }; return n[(t < 0 || t > 3) ? 1 : t]; }
inline const wchar_t* tierDir(int t)  { static const wchar_t* n[] = { L"dev", L"experimental", L"stable", L"hardened" }; return n[(t < 0 || t > 3) ? 1 : t]; }
inline int tierFromName(const std::wstring& s) {
    if (s.find(L"dev") != std::wstring::npos) return 0;
    if (s.find(L"stable") != std::wstring::npos) return 2;
    if (s.find(L"hard") != std::wstring::npos) return 3;
    return 1;  // experimental
}

// A build target — one artifact (name/type/entry). A project has one or more,
// declared as `[[target]]` array-of-tables; a project with none gets a single
// synthesized target from its [project]/[build] fields (backward compatible).
struct Target {
    std::wstring name, entry;
    ProjectType type = ProjectType::Executable;
    std::vector<std::wstring> links;   // per-target native links (else the [build] links apply)
};

struct SentinelProject {
    bool loaded = false;
    std::wstring manifest = L"sentinel.toml";   // manifest filename in `dir` (*.sntproject preferred)
    std::wstring dir, name, version = L"0.1.0", entry, srcDir = L"src", icon;
    ProjectType type = ProjectType::Executable;
    int defaultTier = 1;  // 0=dev 1=experimental 2=stable 3=hardened
    std::vector<std::wstring> libPaths, links;
    std::wstring signRequire = L"off";          // off | warn | strict
    std::wstring trust = L"sentinel-trust.toml"; // ADR 0061 consumer trust manifest
    bool signOutput = false;                     // snc sign the artifact
    std::vector<Target> targets;                 // [[target]] blocks (>=1 after load)
    bool explicitTargets = false;                // true if the manifest declared real [[target]] blocks
};

inline std::wstring projBase(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? p : p.substr(s + 1); }

inline std::wstring projUnq(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t"); if (a == std::wstring::npos) return L""; s = s.substr(a);
    size_t b = s.find_last_not_of(L" \t\r"); s = s.substr(0, b + 1);
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = s.substr(1, s.size() - 2);
    return s;
}
inline std::wstring projIni(const std::wstring& path, const wchar_t* sect, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[2048]; GetPrivateProfileStringW(sect, key, def, buf, 2048, path.c_str()); return projUnq(buf);
}
inline std::vector<std::wstring> projArr(const std::wstring& path, const wchar_t* sect, const wchar_t* key) {
    wchar_t buf[2048]; GetPrivateProfileStringW(sect, key, L"", buf, 2048, path.c_str());
    std::vector<std::wstring> out; std::wstring cur; bool inq = false;
    for (wchar_t c : std::wstring(buf)) { if (c == L'"') { inq = !inq; if (!inq) { out.push_back(cur); cur.clear(); } } else if (inq) cur += c; }
    return out;
}

inline std::wstring projTrim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
inline std::vector<std::wstring> projInlineArr(const std::wstring& s) {
    std::vector<std::wstring> v; std::wstring cur; bool inq = false;
    for (wchar_t c : s) { if (c == L'"') { inq = !inq; if (!inq) { v.push_back(cur); cur.clear(); } } else if (inq) cur += c; }
    return v;
}
inline std::wstring readUtf8(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    DWORD size = GetFileSize(f, nullptr), read = 0;
    std::string bytes(size, '\0');
    ReadFile(f, bytes.data(), size, &read, nullptr); CloseHandle(f);
    int wl = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, nullptr, 0);
    std::wstring w(wl, 0); MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)read, w.data(), wl);
    return w;
}

inline ProjectType typeFromName(const std::wstring& ty) {
    if (ty.find(L"lib") != std::wstring::npos) return ProjectType::Library;
    if (ty.find(L"shared") != std::wstring::npos || ty.find(L"dll") != std::wstring::npos) return ProjectType::Shared;
    return ProjectType::Executable;
}

// Parse `[[target]]` array-of-tables (the Win32 profile API can't — section names
// must be unique). Returns one Target per block; empty if none are declared.
inline std::vector<Target> parseTargets(const std::wstring& path) {
    std::vector<Target> out;
    std::wstring text = readUtf8(path);
    Target* cur = nullptr;
    auto handle = [&](std::wstring l) {
        std::wstring t = projTrim(l);
        if (t.empty() || t[0] == L'#') return;
        if (t.size() >= 2 && t[0] == L'[' && t[1] == L'[') {     // [[target]]
            if (t.find(L"target") != std::wstring::npos) { out.push_back(Target{}); cur = &out.back(); } else cur = nullptr;
            return;
        }
        if (t[0] == L'[') { cur = nullptr; return; }              // a normal section closes the block
        if (!cur) return;
        size_t eq = t.find(L'='); if (eq == std::wstring::npos) return;
        std::wstring k = projTrim(t.substr(0, eq)), v = t.substr(eq + 1);
        if (k == L"name")  cur->name = projUnq(v);
        else if (k == L"entry") cur->entry = projUnq(v);
        else if (k == L"links") cur->links = projInlineArr(v);
        else if (k == L"type")  cur->type = typeFromName(projUnq(v));
    };
    std::wstring line;
    for (wchar_t c : text) { if (c == L'\n') { if (!line.empty() && line.back() == L'\r') line.pop_back(); handle(line); line.clear(); } else line += c; }
    if (!line.empty()) handle(line);
    return out;
}

// The IDE recognizes a folder as a project by a `*.sntproject` file (preferred,
// the native IDE project file) or a legacy `sentinel.toml`. Returns the manifest
// filename within `folder`, or empty if none.
inline std::wstring findManifest(const std::wstring& folder) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((folder + L"\\*.sntproject").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { std::wstring n = fd.cFileName; FindClose(h); return n; }
    if (GetFileAttributesW((folder + L"\\sentinel.toml").c_str()) != INVALID_FILE_ATTRIBUTES) return L"sentinel.toml";
    return L"";
}
inline bool hasProject(const std::wstring& folder) { return !findManifest(folder).empty(); }

inline SentinelProject loadProject(const std::wstring& folder) {
    SentinelProject p; p.dir = folder;
    p.manifest = findManifest(folder);
    if (p.manifest.empty()) return p;
    const std::wstring path = folder + L"\\" + p.manifest;
    p.name    = projIni(path, L"project", L"name", projBase(folder).c_str());
    p.version = projIni(path, L"project", L"version", L"0.1.0");
    p.type    = typeFromName(projIni(path, L"project", L"type", L"executable"));
    p.entry  = projIni(path, L"project", L"entry", L"");
    p.icon   = projIni(path, L"project", L"icon", L"");
    p.srcDir = projIni(path, L"build", L"src", L"src");
    p.defaultTier = tierFromName(projIni(path, L"build", L"default_tier", L"experimental"));
    p.libPaths = projArr(path, L"build", L"lib_paths");
    p.links    = projArr(path, L"build", L"links");
    p.signRequire = projIni(path, L"signing", L"require", L"off");
    p.trust       = projIni(path, L"signing", L"trust", L"sentinel-trust.toml");
    std::wstring so = projIni(path, L"signing", L"sign", L"false");
    p.signOutput = (so == L"true" || so == L"1");
    p.targets = parseTargets(path);
    p.explicitTargets = !p.targets.empty();
    if (p.targets.empty())   // single-target fallback: the [project] itself is the one target
        p.targets.push_back(Target{ p.name, p.entry, p.type, p.links });
    p.loaded = true;
    return p;
}

// ---- writing sentinel.toml -------------------------------------------------
// A faithful, surgical writer: it rewrites only the values of the keys the IDE
// manages and leaves every other line — comments, blank lines, and unmodeled
// keys like `icon`/`authors` — exactly as it found them. (TOML forbids duplicate
// section headers, so missing managed keys are inserted inside their existing
// section rather than appended as a new block.) projTrim/readUtf8 are defined above.
inline std::wstring projLower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
inline std::wstring tomlStr(const std::wstring& s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) { if (c == L'\\' || c == L'"') o += L'\\'; o += c; }  // valid basic string
    return o + L"\"";
}
inline std::wstring tomlArr(const std::vector<std::wstring>& v) {
    std::wstring o = L"[";
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += L", "; o += tomlStr(v[i]); }
    return o + L"]";
}

inline bool writeUtf8(const std::wstring& path, const std::wstring& text) {
    int bl = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes(bl, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), bytes.data(), bl, nullptr, nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0; BOOL ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr); CloseHandle(f);
    return ok && wrote == bytes.size();
}

inline bool saveProject(const SentinelProject& p) {
    const std::wstring manifestPath = p.dir + L"\\" + (p.manifest.empty() ? L"sentinel.toml" : p.manifest);
    struct KV { std::wstring section, key, value; bool written = false; };
    std::vector<KV> kv = {
        { L"project", L"name",         tomlStr(p.name) },
        { L"project", L"version",      tomlStr(p.version) },
        { L"project", L"type",         tomlStr(typeName(p.type)) },
        { L"project", L"entry",        tomlStr(p.entry) },
        { L"build",   L"src",          tomlStr(p.srcDir) },
        { L"build",   L"lib_paths",    tomlArr(p.libPaths) },
        { L"build",   L"links",        tomlArr(p.links) },
        { L"build",   L"default_tier", tomlStr(tierDir(p.defaultTier)) },
        { L"signing", L"require",      tomlStr(p.signRequire) },
        { L"signing", L"trust",        tomlStr(p.trust) },
        { L"signing", L"sign",         p.signOutput ? L"true" : L"false" },
    };

    // Parse into ordered sections (the first, name-empty section is the preamble).
    struct Section { std::wstring name; std::vector<std::wstring> lines; };
    std::vector<Section> secs; secs.push_back({ L"", {} });
    { std::wstring text = readUtf8(manifestPath), cur;
      auto pushLine = [&](std::wstring l) {
          std::wstring t = projTrim(l);
          if (!t.empty() && t.front() == L'[') {
              size_t e = t.find(L']');
              secs.push_back({ e == std::wstring::npos ? L"" : projLower(projTrim(t.substr(1, e - 1))), {} });
          }
          secs.back().lines.push_back(l);
      };
      for (wchar_t c : text) { if (c == L'\n') { if (!cur.empty() && cur.back() == L'\r') cur.pop_back(); pushLine(cur); cur.clear(); } else cur += c; }
      if (!cur.empty()) pushLine(cur);
    }

    // Replace existing managed values in place (skip each section's header line).
    for (auto& sec : secs)
        for (size_t i = sec.name.empty() ? 0 : 1; i < sec.lines.size(); ++i) {
            std::wstring& line = sec.lines[i];
            std::wstring t = projTrim(line);
            if (t.empty() || t.front() == L'#' || t.front() == L'[') continue;
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = projTrim(line.substr(0, eq));
            for (auto& e : kv)
                if (!e.written && e.section == sec.name && e.key == key) {
                    line = line.substr(0, eq + 1) + L" " + e.value;  // keep the key's original alignment
                    e.written = true; break;
                }
        }

    // Insert any still-missing managed keys into their (existing or new) section.
    for (const wchar_t* sname : { L"project", L"build", L"signing" }) {
        std::vector<std::wstring> missing;
        for (auto& e : kv) if (!e.written && e.section == sname) { missing.push_back(e.key + L" = " + e.value); e.written = true; }
        if (missing.empty()) continue;
        Section* target = nullptr;
        for (auto& sec : secs) if (sec.name == sname) { target = &sec; break; }
        if (!target) { secs.push_back({ sname, { L"", std::wstring(L"[") + sname + L"]" } }); target = &secs.back(); }
        for (auto& m : missing) target->lines.push_back(m);
    }

    // Rewrite values inside [[target]] blocks in place, matched by order. The
    // double-bracket header parses to the section name "[target"; we only update
    // existing name/entry/type/links lines (never insert) so the writer stays
    // non-destructive — comments and unmodeled per-target keys survive untouched.
    {
        int ti = 0;
        for (auto& sec : secs) {
            if (sec.name != L"[target") continue;
            if (ti < (int)p.targets.size()) {
                const Target& tg = p.targets[ti];
                for (size_t i = 1; i < sec.lines.size(); ++i) {
                    std::wstring& line = sec.lines[i];
                    std::wstring t = projTrim(line);
                    if (t.empty() || t.front() == L'#' || t.front() == L'[') continue;
                    size_t eq = line.find(L'='); if (eq == std::wstring::npos) continue;
                    std::wstring key = projTrim(line.substr(0, eq)), val;
                    if (key == L"name")       val = tomlStr(tg.name);
                    else if (key == L"entry") val = tomlStr(tg.entry);
                    else if (key == L"type")  val = tomlStr(typeName(tg.type));
                    else if (key == L"links") val = tomlArr(tg.links);
                    else continue;
                    line = line.substr(0, eq + 1) + L" " + val;
                }
            }
            ti++;
        }
    }

    // Re-emit (CRLF, single trailing newline).
    std::wstring out;
    for (auto& sec : secs) for (auto& line : sec.lines) out += line + L"\r\n";
    while (out.size() >= 4 && out.substr(out.size() - 4) == L"\r\n\r\n") out.erase(out.size() - 2);
    return writeUtf8(manifestPath, out);
}

}  // namespace sentinelide
