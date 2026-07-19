// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE signing/trust model (ADR 0061). Parses the consumer trust manifest
// and detached `.sig` carriers, and wraps `snc keygen/sign/verify`. Header-only.
//
// TRUST MANIFEST SCHEMA — this must match snc exactly, or builds break. snc's
// parser (crates/sentinel-trust/src/trust_model.rs) is `deny_unknown_fields` over:
//     [[keys]]
//     name   = "label"                      # optional, diagnostics only
//     pubkey = "<64 hex chars>"             # REQUIRED. Bare hex — an "ed25519:"
//                                           # prefix parses but never matches,
//                                           # silently yielding UNTRUSTED.
//     grants = ["secret", "..."]            # optional capability ceiling
// There is no [dependencies.<name>], no `sig`/`key`, no `policy`, no `forbids` —
// those are hard parse errors that abort `snc build --require-signatures` in BOTH
// warn and strict. Keep this parser and SigningDialog's writer in lockstep with it.
//
// sncSupportsSigning() probes `snc help` for the ADR-0061 subcommands (both the
// release and debug snc builds carry them as of 2026-07).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include "core/Project.h"   // readUtf8, projTrim, projUnq
#include "core/Proc.h"      // runCapture

namespace sentinelide {

enum class SignState { Unknown, Unsigned, Checking, Signed, Invalid };

// One [[keys]] entry: a trusted Ed25519 public key (bare 64-hex), an optional
// label, and an optional capability ceiling intersected with the signature's grants.
struct TrustedKey {
    std::wstring name, pubkey;
    std::vector<std::wstring> grants;
};
struct TrustManifest {
    bool loaded = false;
    std::vector<TrustedKey> keys;
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
    TrustedKey* cur = nullptr;
    auto handle = [&](std::wstring l) {
        std::wstring t = projTrim(l);
        if (t.empty() || t[0] == L'#') return;
        if (t.size() >= 2 && t[0] == L'[' && t[1] == L'[') {          // [[keys]] array-of-tables
            if (t.find(L"keys") != std::wstring::npos) { m.keys.push_back(TrustedKey{}); cur = &m.keys.back(); }
            else cur = nullptr;
            return;
        }
        if (t[0] == L'[') { cur = nullptr; return; }                  // any other table closes the block
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
    m.loaded = !m.keys.empty();
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
