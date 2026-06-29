// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE project sealing — encrypt a project so only the developer can open it.
//
// Pipeline:  archive(folder) → compress(LZMS) → AEAD-encrypt under a random master
// key (DEK).  The DEK is wrapped per unlock "slot" (LUKS-style): v1 ships a single
// PASSWORD slot — PBKDF2-HMAC-SHA256(password, salt) → KEK → AES-256-GCM wrap of the
// DEK.  Adding more unlock mechanisms (key file, Ed25519/smartcard, TPM, …) means
// adding slot types that wrap the SAME DEK, so no re-encryption is needed and a
// project can carry several unlock methods at once.
//
// Crypto is native CNG (BCrypt) + the Windows Compression API — no third-party deps.
// The AEAD + KDF core is a planned Sentinel rewrite target (std/security has a
// machine-verified constant-time ChaCha20-Poly1305 + SHA-256); the on-disk format
// records algorithm ids so a future ChaCha slot/payload coexists with AES files.
//
// .sealed layout (all integers little-endian):
//   "SNTSEAL1"(8) | version:u32(=1) | aead_alg:u32(1=AES-256-GCM) |
//   archive_size:u64 | slot_count:u32 |
//   slots[slot_count]:  slot_type:u32(1=password)
//                       password slot body: salt(16) | iters:u64 | wrap_nonce(12) |
//                                            wrapped_dek(32) | wrap_tag(16)
//   payload_nonce(12) | payload_len:u64 | payload(payload_len) | payload_tag(16)
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "cabinet.lib")

namespace sentinelide {

using Bytes = std::vector<uint8_t>;

struct SealResult { bool ok = false; std::wstring message, outPath; };

// ---- little-endian (de)serialization -------------------------------------
inline void putU32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
inline void putU64(Bytes& b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back((uint8_t)(v >> (8 * i))); }
inline uint32_t getU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint64_t getU64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

// ---- raw byte file I/O + utf-8 -------------------------------------------
inline bool sealReadBytes(const std::wstring& path, Bytes& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    size_t off = 0; bool ok = true;
    while (off < out.size()) {
        DWORD want = (out.size() - off) > 0x100000 ? 0x100000 : (DWORD)(out.size() - off), rd = 0;
        if (!ReadFile(h, out.data() + off, want, &rd, nullptr) || rd == 0) { ok = false; break; }
        off += rd;
    }
    CloseHandle(h);
    return ok && off == out.size();
}
inline bool sealWriteBytes(const std::wstring& path, const uint8_t* data, size_t len) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t off = 0; bool ok = true;
    while (off < len) {
        DWORD want = (len - off) > 0x100000 ? 0x100000 : (DWORD)(len - off), wr = 0;
        if (!WriteFile(h, data + off, want, &wr, nullptr)) { ok = false; break; }
        off += wr;
    }
    CloseHandle(h);
    return ok && off == len;
}
inline std::string sealUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}
inline std::wstring sealWide(const char* p, size_t n) {
    if (!n) return {};
    int w = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, nullptr, 0);
    std::wstring s(w, 0); MultiByteToWideChar(CP_UTF8, 0, p, (int)n, s.data(), w); return s;
}

// ---- crypto primitives (CNG) ---------------------------------------------
inline bool sealRng(uint8_t* p, ULONG n) {
    return BCryptGenRandom(nullptr, p, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
inline bool sealPbkdf2(const Bytes& pw, const uint8_t* salt, ULONG saltLen, ULONGLONG iters, uint8_t* out, ULONG outLen) {
    BCRYPT_ALG_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return false;
    NTSTATUS s = BCryptDeriveKeyPBKDF2(h, (PUCHAR)pw.data(), (ULONG)pw.size(), (PUCHAR)salt, saltLen, iters, out, outLen, 0);
    BCryptCloseAlgorithmProvider(h, 0);
    return s == 0;
}
// AES-256-GCM one-shot. enc: tag is OUT; dec: tag is IN (mismatch → false = wrong key/tamper).
inline bool sealAesGcm(bool enc, const uint8_t key[32], const uint8_t nonce[12], const Bytes& in, Bytes& out, uint8_t tag[16]) {
    BCRYPT_ALG_HANDLE hAlg = nullptr; BCRYPT_KEY_HANDLE hKey = nullptr; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) == 0 &&
        BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)key, 32, 0) == 0) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = (PUCHAR)nonce; info.cbNonce = 12;
        info.pbTag = tag; info.cbTag = 16;
        uint8_t iv[16] = {};                 // GCM working IV buffer (per CNG sample)
        out.resize(in.size()); ULONG outLen = 0;
        NTSTATUS s = enc
            ? BCryptEncrypt(hKey, (PUCHAR)in.data(), (ULONG)in.size(), &info, iv, sizeof(iv), out.data(), (ULONG)out.size(), &outLen, 0)
            : BCryptDecrypt(hKey, (PUCHAR)in.data(), (ULONG)in.size(), &info, iv, sizeof(iv), out.data(), (ULONG)out.size(), &outLen, 0);
        if (s == 0) { out.resize(outLen); ok = true; }
    }
    if (hKey) BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

// ---- LZMS compression (Windows Compression API) --------------------------
inline bool sealCompress(const Bytes& in, Bytes& out) {
    COMPRESSOR_HANDLE c = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &c)) return false;
    SIZE_T need = 0;
    Compress(c, (PVOID)in.data(), in.size(), nullptr, 0, &need);   // query bound
    out.resize(need); SIZE_T got = 0;
    bool ok = Compress(c, (PVOID)in.data(), in.size(), out.data(), out.size(), &got) != FALSE;
    if (ok) out.resize(got);
    CloseCompressor(c);
    return ok;
}
inline bool sealDecompress(const Bytes& in, size_t origSize, Bytes& out) {
    DECOMPRESSOR_HANDLE d = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &d)) return false;
    out.resize(origSize); SIZE_T got = 0;
    bool ok = Decompress(d, (PVOID)in.data(), in.size(), out.data(), out.size(), &got) != FALSE;
    if (ok) out.resize(got);
    CloseDecompressor(d);
    return ok;
}

// ---- archive: bundle a project folder into one blob ----------------------
//   file_count:u32 | { path_len:u32 | path(utf8, '/'-separated) | data_len:u64 | data } *
inline void sealCollect(const std::wstring& root, const std::wstring& rel, std::vector<std::pair<std::wstring, Bytes>>& out) {
    std::wstring dir = rel.empty() ? root : root + L"\\" + rel;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring childRel = rel.empty() ? name : rel + L"/" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (name == L"target" || name == L"build" || name == L".git" || name == L"node_modules") continue;  // build/VCS output
            sealCollect(root, childRel, out);
        } else {
            size_t dot = name.find_last_of(L'.');
            if (dot != std::wstring::npos && _wcsicmp(name.c_str() + dot, L".sealed") == 0) continue;  // don't seal a seal
            Bytes data; if (sealReadBytes(dir + L"\\" + name, data)) out.push_back({ childRel, std::move(data) });
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
inline Bytes sealPackArchive(const std::vector<std::pair<std::wstring, Bytes>>& files) {
    Bytes a; putU32(a, (uint32_t)files.size());
    for (auto& f : files) {
        std::string p = sealUtf8(f.first);
        putU32(a, (uint32_t)p.size()); a.insert(a.end(), p.begin(), p.end());
        putU64(a, (uint64_t)f.second.size()); a.insert(a.end(), f.second.begin(), f.second.end());
    }
    return a;
}
inline bool sealExtractArchive(const Bytes& a, const std::wstring& destRoot) {
    if (a.size() < 4) return false;
    size_t pos = 0; uint32_t n = getU32(&a[pos]); pos += 4;
    for (uint32_t i = 0; i < n; i++) {
        if (pos + 4 > a.size()) return false;
        uint32_t pl = getU32(&a[pos]); pos += 4;
        if (pos + pl > a.size()) return false;
        std::wstring rel = sealWide((const char*)&a[pos], pl); pos += pl;
        if (pos + 8 > a.size()) return false;
        uint64_t dl = getU64(&a[pos]); pos += 8;
        if (pos + dl > a.size()) return false;
        for (auto& ch : rel) if (ch == L'/') ch = L'\\';                 // → native separators
        if (rel.find(L"..") != std::wstring::npos || rel.empty() ||
            rel[0] == L'\\' || (rel.size() > 1 && rel[1] == L':')) return false;   // reject traversal / absolute
        std::wstring full = destRoot + L"\\" + rel;
        size_t s = full.find_last_of(L'\\');
        if (s != std::wstring::npos) SHCreateDirectoryExW(nullptr, full.substr(0, s).c_str(), nullptr);
        if (!sealWriteBytes(full, dl ? &a[pos] : (const uint8_t*)"", (size_t)dl)) return false;
        pos += (size_t)dl;
    }
    return true;
}

// ---- seal / unseal --------------------------------------------------------
constexpr uint64_t kSealPbkdf2Iters = 600000;   // OWASP-class for PBKDF2-HMAC-SHA256

inline SealResult sealProject(const std::wstring& projectDir, const std::wstring& sealedPath, const Bytes& password) {
    SealResult r;
    std::vector<std::pair<std::wstring, Bytes>> files;
    sealCollect(projectDir, L"", files);
    if (files.empty()) { r.message = L"No files to seal."; return r; }
    Bytes archive = sealPackArchive(files);
    uint64_t archiveSize = archive.size();
    Bytes comp; if (!sealCompress(archive, comp)) { r.message = L"Compression failed."; return r; }

    uint8_t dek[32], payloadNonce[12], payloadTag[16], salt[16], kek[32], wrapNonce[12], wrapTag[16];
    if (!sealRng(dek, 32) || !sealRng(payloadNonce, 12) || !sealRng(salt, 16) || !sealRng(wrapNonce, 12)) { r.message = L"RNG failed."; return r; }
    Bytes payload; if (!sealAesGcm(true, dek, payloadNonce, comp, payload, payloadTag)) { r.message = L"Encrypt failed."; return r; }
    if (!sealPbkdf2(password, salt, 16, kSealPbkdf2Iters, kek, 32)) { SecureZeroMemory(dek, 32); r.message = L"Key derivation failed."; return r; }
    Bytes dekIn(dek, dek + 32), wrapped;
    bool wrapOk = sealAesGcm(true, kek, wrapNonce, dekIn, wrapped, wrapTag);
    SecureZeroMemory(dek, 32); SecureZeroMemory(kek, 32); SecureZeroMemory(dekIn.data(), dekIn.size());
    if (!wrapOk || wrapped.size() != 32) { r.message = L"Key wrap failed."; return r; }

    Bytes out;
    const char magic[8] = { 'S','N','T','S','E','A','L','1' };
    out.insert(out.end(), magic, magic + 8);
    putU32(out, 1);            // format version
    putU32(out, 1);            // AEAD alg: 1 = AES-256-GCM
    putU64(out, archiveSize);
    putU32(out, 1);            // slot count
    putU32(out, 1);            // slot 0 type: 1 = password (PBKDF2-HMAC-SHA256)
    out.insert(out.end(), salt, salt + 16);
    putU64(out, kSealPbkdf2Iters);
    out.insert(out.end(), wrapNonce, wrapNonce + 12);
    out.insert(out.end(), wrapped.begin(), wrapped.end());
    out.insert(out.end(), wrapTag, wrapTag + 16);
    out.insert(out.end(), payloadNonce, payloadNonce + 12);
    putU64(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    out.insert(out.end(), payloadTag, payloadTag + 16);

    if (!sealWriteBytes(sealedPath, out.data(), out.size())) { r.message = L"Could not write the sealed file."; return r; }
    r.ok = true; r.outPath = sealedPath;
    r.message = std::to_wstring(files.size()) + L" files · " + std::to_wstring(archiveSize) + L" B → " +
                std::to_wstring(comp.size()) + L" B compressed → " + std::to_wstring(out.size()) + L" B sealed";
    return r;
}

inline SealResult unsealProject(const std::wstring& sealedPath, const std::wstring& destDir, const Bytes& password) {
    SealResult r;
    Bytes f; if (!sealReadBytes(sealedPath, f)) { r.message = L"Cannot read the sealed file."; return r; }
    size_t pos = 0;
    auto need = [&](size_t n) { return pos + n <= f.size(); };
    if (f.size() < 8 || memcmp(f.data(), "SNTSEAL1", 8) != 0) { r.message = L"Not a sealed project (bad header)."; return r; }
    pos = 8;
    if (!need(20)) { r.message = L"Sealed file is truncated."; return r; }
    uint32_t ver = getU32(&f[pos]); pos += 4;
    uint32_t alg = getU32(&f[pos]); pos += 4;
    uint64_t archiveSize = getU64(&f[pos]); pos += 8;
    uint32_t slots = getU32(&f[pos]); pos += 4;
    if (ver != 1 || alg != 1) { r.message = L"Unsupported sealed-file version/algorithm."; return r; }

    uint8_t dek[32]; bool unlocked = false;
    for (uint32_t i = 0; i < slots && !unlocked; i++) {
        if (!need(4)) { r.message = L"Sealed file is truncated (slots)."; return r; }
        uint32_t type = getU32(&f[pos]); pos += 4;
        if (type != 1) { r.message = L"Unsupported unlock-slot type (newer IDE?)."; return r; }   // can't skip unknown-length slots
        if (!need(16 + 8 + 12 + 32 + 16)) { r.message = L"Sealed file is truncated (password slot)."; return r; }
        const uint8_t* salt = &f[pos]; pos += 16;
        uint64_t iters = getU64(&f[pos]); pos += 8;
        const uint8_t* nonce = &f[pos]; pos += 12;
        Bytes wrapped(&f[pos], &f[pos] + 32); pos += 32;
        uint8_t tag[16]; memcpy(tag, &f[pos], 16); pos += 16;
        uint8_t kek[32];
        if (sealPbkdf2(password, salt, 16, iters, kek, 32)) {
            Bytes dekOut;
            if (sealAesGcm(false, kek, nonce, wrapped, dekOut, tag) && dekOut.size() == 32) { memcpy(dek, dekOut.data(), 32); unlocked = true; }
            SecureZeroMemory(kek, 32);
        }
    }
    if (!unlocked) { r.message = L"Wrong password — could not unlock the project."; return r; }

    if (!need(12 + 8)) { SecureZeroMemory(dek, 32); r.message = L"Sealed file is truncated (payload)."; return r; }
    const uint8_t* pnonce = &f[pos]; pos += 12;
    uint64_t plen = getU64(&f[pos]); pos += 8;
    if (!need((size_t)plen + 16)) { SecureZeroMemory(dek, 32); r.message = L"Sealed file is truncated (payload body)."; return r; }
    Bytes payload(&f[pos], &f[pos] + plen); pos += (size_t)plen;
    uint8_t ptag[16]; memcpy(ptag, &f[pos], 16); pos += 16;

    Bytes comp;
    bool decOk = sealAesGcm(false, dek, pnonce, payload, comp, ptag);
    SecureZeroMemory(dek, 32);
    if (!decOk) { r.message = L"Payload failed authentication — the sealed file is corrupt or tampered."; return r; }
    Bytes archive; if (!sealDecompress(comp, (size_t)archiveSize, archive)) { r.message = L"Decompression failed."; return r; }
    if (!sealExtractArchive(archive, destDir)) { r.message = L"Could not extract the project (bad archive)."; return r; }
    r.ok = true; r.outPath = destDir; r.message = L"Unsealed " + std::to_wstring(archive.size()) + L" B to " + destDir;
    return r;
}

}  // namespace sentinelide
