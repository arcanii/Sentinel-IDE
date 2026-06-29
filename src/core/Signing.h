// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE signing/trust model (ADR 0061). Parses the consumer trust manifest
// (`sentinel-trust.toml` — [dependencies.<name>] with key/policy/grants/forbids)
// and detached `.sig` carriers, and wraps `snc keygen/sign/verify`. The shipped
// snc C1.0b release lacks these subcommands; the build dated 2026-06-28 (and the
// debug build) has them — sncSupportsSigning() probes for it. Header-only.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include "core/Project.h"   // readUtf8, projTrim, projUnq
#include "core/Proc.h"      // runCapture

namespace sentinelide {

enum class SignState { Unknown, Unsigned, Checking, Signed, Invalid };

struct TrustDep {
    std::wstring name, key, policy = L"exact-key";
    std::vector<std::wstring> grants, forbids;
};
struct TrustManifest {
    bool loaded = false;
    std::vector<TrustDep> deps;
};

// Detached-signature fields (the human-readable `.sig` carrier).
struct SigInfo {
    bool present = false;
    std::wstring algorithm, key, grants;
};

inline std::vector<std::wstring> parseInlineArr(const std::wstring& s) {
    std::vector<std::wstring> v; std::wstring cur; bool inq = false;
    for (wchar_t c : s) { if (c == L'"') { inq = !inq; if (!inq) { v.push_back(cur); cur.clear(); } } else if (inq) cur += c; }
    return v;
}

inline TrustManifest loadTrust(const std::wstring& path) {
    TrustManifest m;
    std::wstring text = readUtf8(path);
    if (text.empty()) return m;
    TrustDep* cur = nullptr;
    auto handle = [&](std::wstring l) {
        std::wstring t = projTrim(l);
        if (t.empty() || t[0] == L'#') return;
        if (t[0] == L'[') {
            size_t e = t.find(L']');
            std::wstring sec = projTrim(t.substr(1, (e == std::wstring::npos ? t.size() : e) - 1));
            const std::wstring pre = L"dependencies.";
            if (sec.size() > pre.size() && sec.compare(0, pre.size(), pre) == 0) {
                m.deps.push_back(TrustDep{}); cur = &m.deps.back(); cur->name = sec.substr(pre.size());
            } else cur = nullptr;
            return;
        }
        if (!cur) return;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) return;
        std::wstring k = projTrim(t.substr(0, eq)), v = t.substr(eq + 1);
        if (k == L"sig" || k == L"key")   cur->key = projUnq(v);
        else if (k == L"policy")          cur->policy = projUnq(v);
        else if (k == L"grants")          cur->grants = parseInlineArr(v);
        else if (k == L"forbids")         cur->forbids = parseInlineArr(v);
    };
    std::wstring line;
    for (wchar_t c : text) { if (c == L'\n') { if (!line.empty() && line.back() == L'\r') line.pop_back(); handle(line); line.clear(); } else line += c; }
    if (!line.empty()) handle(line);
    m.loaded = !m.deps.empty();
    return m;
}

inline SigInfo readSig(const std::wstring& sigPath) {
    SigInfo s;
    std::wstring text = readUtf8(sigPath);
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

// Short fingerprint for display (snc prints the first 16 hex chars + ellipsis).
inline std::wstring shortKey(const std::wstring& key) {
    std::wstring k = key;
    size_t colon = k.find(L':'); if (colon != std::wstring::npos) k = k.substr(colon + 1);  // drop "ed25519:"
    return k.size() > 16 ? k.substr(0, 16) + L"…" : k;
}

// Does this snc build expose the ADR-0061 subcommands? (Probes `snc help`.)
inline bool sncSupportsSigning(const std::wstring& sncPath) {
    std::wstring out; runCapture(L"\"" + sncPath + L"\" help", L"", out);
    return out.find(L"snc keygen") != std::wstring::npos && out.find(L"snc sign") != std::wstring::npos;
}

struct VerifyResult { SignState state = SignState::Unsigned; std::wstring key, grants, message; };

// Verify <file> against <file>.sig via snc. Unsigned if no .sig present.
inline VerifyResult verifyFile(const std::wstring& sncPath, const std::wstring& file) {
    VerifyResult r;
    std::wstring sig = file + L".sig";
    if (GetFileAttributesW(sig.c_str()) == INVALID_FILE_ATTRIBUTES) { r.message = L"Unsigned — no .sig carrier."; return r; }
    SigInfo si = readSig(sig); r.key = si.key; r.grants = si.grants;
    std::wstring out;
    DWORD code = runCapture(L"\"" + sncPath + L"\" verify \"" + file + L"\" --sig \"" + sig + L"\"", L"", out);
    r.message = projTrim(out);
    r.state = (code == 0) ? SignState::Signed : SignState::Invalid;
    return r;
}

}  // namespace sentinelide
