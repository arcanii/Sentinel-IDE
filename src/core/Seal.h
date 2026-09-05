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
// .sealed layout v2 (all integers little-endian):
//   "SNTSEAL2"(8) | version:u32(=2) | aead_alg:u32(1=AES-256-GCM) |   <-- AAD prefix (24 B)
//   archive_size:u64 |                                                <-- ...ends here
//   slot_count:u32 |
//   slots[slot_count]:  slot_type:u32(1=password) | slot_len:u32 | slot_body(slot_len)
//                       password body (type 1, len 84): salt(16) | iters:u64 |
//                                            wrap_nonce(12) | wrapped_dek(32) | wrap_tag(16)
//   payload_nonce(12) | payload_len:u64 | payload(payload_len) | payload_tag(16)
//
// Two properties the v1 layout got wrong, both fixed here:
//
//   * `slot_len` makes slots SKIPPABLE. v1 had no length, so a reader hitting an
//     unknown slot_type could not step over it and had to abort — which flatly
//     contradicted the extensibility promise above. A v2 reader skips slot types
//     it does not understand and keeps looking for one it does.
//
//   * The 24-byte prefix is bound into the payload AEAD as ADDITIONAL AUTHENTICATED
//     DATA. In v1 `archive_size` was unauthenticated yet fed straight to
//     `sealDecompress` as the output-buffer size, so flipping those 8 bytes in a
//     file you could not decrypt still steered a multi-gigabyte allocation in the
//     victim's process. Binding it makes tampering fail as an auth error instead.
//
// AAD deliberately covers ONLY that fixed prefix — NOT slot_count and NOT the slot
// bodies. Authenticating the slot table would tie the payload tag to the current set
// of slots, so adding an unlock method would force re-encrypting the whole payload
// and destroy the LUKS-style property this format exists to have. Slots defend
// themselves instead: each wrapped DEK carries its own GCM tag, so a tampered slot
// simply fails to unlock. The one field that is neither authenticated nor
// self-checking is the per-slot `iters`, which is why it is range-checked below
// before being handed to PBKDF2.
//
// v1 files ("SNTSEAL1") are still READ, so anything sealed before this change keeps
// opening; only the writer moved to v2.
//
// THE FRAMING IS SENTINEL; THE CRYPTO IS NOT. Reading this container — the header, the
// unlock-slot table, the archive index — is src/sentinel/parsers.sentinel
// (parse_seal_header / parse_seal_archive), the seventh and eighth exports of the one
// C-ABI lib. It hands back BYTE OFFSETS to the salt, the wrapped DEK, the nonces and
// the tags; every CNG call below is unchanged and no secret ever crosses the boundary.
// The crypto core stays here because docs/Sentinel-lang_request.md R1 (no secure-zero
// for `[secret u8]`) would make moving it a net security regression. See the framing
// section below, and the long comment above parse_seal_header.
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
// AES-256-GCM one-shot. enc: tag is OUT; dec: tag is IN (mismatch → false = wrong
// key/tamper). `aad`/`aadLen` are optional additional authenticated data: covered by
// the tag but not encrypted. Pass nullptr/0 for none (the DEK key-wrap does).
inline bool sealAesGcm(bool enc, const uint8_t key[32], const uint8_t nonce[12], const Bytes& in, Bytes& out, uint8_t tag[16],
                       const uint8_t* aad = nullptr, ULONG aadLen = 0) {
    BCRYPT_ALG_HANDLE hAlg = nullptr; BCRYPT_KEY_HANDLE hKey = nullptr; bool ok = false;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) return false;
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) == 0 &&
        BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, (PUCHAR)key, 32, 0) == 0) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info; BCRYPT_INIT_AUTH_MODE_INFO(info);
        info.pbNonce = (PUCHAR)nonce; info.cbNonce = 12;
        info.pbTag = tag; info.cbTag = 16;
        info.pbAuthData = (PUCHAR)aad; info.cbAuthData = aadLen;
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
// ---- container FRAMING: the parse half, in Sentinel -----------------------
//
// Everything from here to sealProject is the part of the container that INTERPRETS
// attacker-chosen bytes: the header, the unlock-slot table and the archive index. It
// moved to src/sentinel/parsers.sentinel (parse_seal_header / parse_seal_archive), the
// seventh and eighth exports of the one C-ABI lib (ADR 0059). The long comment above
// those two functions is the record of what changed and why.
//
// THE CRYPTO DID NOT MOVE, and deliberately: no password, salt, KEK or DEK crosses the
// FFI boundary, and the CNG calls below are untouched. The crypto core is blocked on
// docs/Sentinel-lang_request.md R1 — there is no way to secure-zero a `[secret u8]`, so
// key handling outside SecureZeroMemory's reach would be a net security regression.
// Sentinel gets the bytes with nothing secret in them and hands back OFFSETS; the host
// reads the salt, the wrapped DEK, the nonces and the tags from those offsets exactly
// as it did.
//
// A `.sealed` file is fully attacker-chosen — the feature exists so someone can SEND
// you a sealed project — and GCM proves only that the bytes did not change in transit.
// This was the last place in the IDE where such bytes met hand-rolled pointer
// arithmetic BEFORE anything was authenticated.
//
// Cross-checked against the C++ below, case for case, by tests/seal_xcheck.cpp.

// The on-disk FORMAT constants. They live with the framing because that is the only
// thing that reads them; the PBKDF2 iteration policy stays down with the crypto.
constexpr uint32_t kSealVersion1 = 1;           // legacy: no slot_len, no AAD (still read)
constexpr uint32_t kSealVersion2 = 2;           // current writer
constexpr uint32_t kAeadAesGcm   = 1;           // aead_alg id (2 = ChaCha20-Poly1305, reserved)
constexpr uint32_t kSlotPassword = 1;           // slot_type id
constexpr uint32_t kPasswordSlotLen = 16 + 8 + 12 + 32 + 16;   // salt|iters|nonce|wrapped|tag = 84
constexpr size_t   kSealAadLen   = 24;          // magic(8) + version(4) + aead_alg(4) + archive_size(8)

// Why a reason CODE and not a message: the parser is a library that knows the format,
// not the UI. Every code below maps to a string unsealProject already had, so this port
// added no user-facing text — see sealHeaderMessage.
enum SealHeaderReason {
    kSealHdrOk = 0,
    kSealHdrBadMagic = 1,          // not 8 bytes, or not SNTSEAL1/SNTSEAL2
    kSealHdrTruncHeader = 2,       // version/alg/archive_size/slot_count do not fit
    kSealHdrBadVersion = 3,        // version disagrees with the magic, or unknown aead_alg
    kSealHdrTruncSlots = 4,        // a slot header does not fit
    kSealHdrSlotType = 5,          // v1 only: a slot type with no slot_len to step over
    kSealHdrTruncSlotBody = 6,     // a slot body does not fit
    kSealHdrTruncPayload = 7,      // payload nonce + length do not fit
    kSealHdrTruncPayloadBody = 8,  // payload + tag do not fit, or payload_len >= 2^63
    kSealHdrArchiveSize = 9,       // archive_size not plausible for the container — see readSealHeader
    kSealHdrSlotCount = 10         // slot_count above the ceiling — a PBKDF2 hang, see readSealHeader
};
// Unlock slots a container may declare. See readSealHeader for why this is bounded.
constexpr uint32_t kMaxUnlockSlots = 64;
// The archive side has one outcome in the UI ("bad archive"); the codes are kept apart
// only so the cross-check can name which bound fired.
enum SealArchiveReason {
    kSealArcOk = 0,
    kSealArcShort = 1,       // fewer than the 4 bytes of file_count
    kSealArcTrunc = 2,       // a path or a data run runs off the end
    kSealArcUnsafePath = 3,  // rooted, drive-qualified, empty, or a ".." component
    kSealArcDataLen = 4      // data_len >= 2^63
};

struct SealSlot { uint32_t type = 0; size_t bodyOff = 0; size_t bodyLen = 0; };
// Byte offsets into the sealed file. Nothing here is secret: it is where to look, not
// what is there.
struct SealHeader {
    int reason = kSealHdrBadMagic;
    uint32_t version = 0;
    uint64_t archiveSize = 0;
    size_t payloadNonceOff = 0, payloadOff = 0, payloadLen = 0, payloadTagOff = 0;
    std::vector<SealSlot> slots;
};
struct SealEntry { std::wstring rel; size_t dataOff = 0, dataLen = 0; };
struct SealArchiveIndex { int reason = kSealArcShort; std::vector<SealEntry> files; };

// Is this archive-relative path unsafe to write under destRoot?
// The ".." test is PER COMPONENT: a substring search also rejects perfectly ordinary
// names like "notes..txt" or "v1..2.md", which would abort the whole unseal over a
// file that was never a traversal attempt.
//
// The colon is refused ANYWHERE, not just at position 1 as the pre-port code had it.
// Two reasons, and neither costs a real file (':' is not legal in a Windows filename,
// and every path in an archive came from FindFirstFileW walking a Windows directory):
// `ab:c` used to be allowed and writes an NTFS ALTERNATE DATA STREAM rather than the
// file a tree walk would show; and "second byte" and "second character" are the same
// question only while the first character is ASCII, so this is also the one rule where
// the UTF-8 and UTF-16 forms of the guard could otherwise part company.
//
// Defined unconditionally: the C++ readSealArchive below uses it, and tests/seal_test.cpp
// pins its truth table directly. The Sentinel port applies the SAME rules to the UTF-8
// bytes instead of this UTF-16 form — argued in full above seal_unsafe_rel in
// parsers.sentinel, and held to it by tests/seal_xcheck.cpp.
inline bool sealUnsafeRelPath(const std::wstring& p) {
    if (p.empty()) return true;
    if (p[0] == L'\\' || p[0] == L'/') return true;                   // rooted
    if (p.find(L':') != std::wstring::npos) return true;              // drive-qualified / ADS
    for (size_t start = 0; start <= p.size(); ) {
        const size_t sep = p.find(L'\\', start);
        const size_t end = (sep == std::wstring::npos) ? p.size() : sep;
        if (p.compare(start, end - start, L"..") == 0) return true;   // exactly ".."
        if (sep == std::wstring::npos) break;
        start = sep + 1;
    }
    return false;
}

// Reason code -> the message unsealProject reported before this port. One table, so the
// strings live in one place and neither implementation carries them.
inline const wchar_t* sealHeaderMessage(int reason) {
    switch (reason) {
        case kSealHdrTruncHeader:      return L"Sealed file is truncated.";
        case kSealHdrBadVersion:       return L"Unsupported sealed-file version/algorithm.";
        case kSealHdrTruncSlots:       return L"Sealed file is truncated (slots).";
        case kSealHdrSlotType:         return L"Unsupported unlock-slot type (newer IDE?).";
        case kSealHdrTruncSlotBody:    return L"Sealed file is truncated (slot body).";
        case kSealHdrTruncPayload:     return L"Sealed file is truncated (payload).";
        case kSealHdrTruncPayloadBody: return L"Sealed file is truncated (payload body).";
        case kSealHdrBadMagic:         return L"Not a sealed project (bad header).";
        // THESE TWO GET THEIR OWN STRINGS, and the reason is not tidiness. Both refuse a
        // file that IS a sealed project — correct magic, correct version — because a
        // header field states something implausible. Telling that user "not a sealed
        // project" would send them looking for a corrupt download or the wrong file,
        // when what they have is their own data and a bound they exceeded. A lockout
        // wearing the wrong label is the worst failure this feature has, so it says
        // what actually happened and roughly what to do about it.
        case kSealHdrArchiveSize:
            return L"This sealed file states an archive size far larger than the file "
                   L"itself could hold. It is refused rather than acted on, because that "
                   L"size is what the unsealer would allocate. If this is genuinely your "
                   L"project and it is very large, unseal it with an older build and "
                   L"re-seal it.";
        case kSealHdrSlotCount:
            return L"This sealed file declares more unlock slots than any real container "
                   L"uses. Each one costs a full key derivation on a wrong password, so "
                   L"it is refused rather than attempted.";
        default:                       return L"";
    }
}

#ifdef SENTINELIDE_SENTINEL
#include "sentinel_parsers.h"   // generated by snc: parse_seal_header/_archive + sentinel_free_bytes()

// A zero-length Bytes has a null data(); the FFI takes a slice, so hand it something.
inline const uint8_t* sealPtr(const Bytes& b) {
    static const uint8_t kNone = 0;
    return b.empty() ? &kNone : b.data();
}
inline uint64_t sealRd8(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }

inline SealHeader readSealHeader(const Bytes& f) {
    SealHeader h;
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_seal_header(sealPtr(f), (int64_t)f.size(), &out, &olen);
    // Record: [reason][version][archive_size][slot_count][pnonce][poff][plen][ptag]
    // then slot_count * [type][body_off][body_len] — 57 bytes minimum.
    if (out && olen >= 57) {
        h.reason        = (int)out[0];
        h.version       = (uint32_t)sealRd8(out + 1);
        h.archiveSize   = sealRd8(out + 9);
        const uint64_t nslots = sealRd8(out + 17);
        h.payloadNonceOff = (size_t)sealRd8(out + 25);
        h.payloadOff      = (size_t)sealRd8(out + 33);
        h.payloadLen      = (size_t)sealRd8(out + 41);
        h.payloadTagOff   = (size_t)sealRd8(out + 49);
        for (uint64_t i = 0; i < nslots && (uint64_t)olen >= 57 + (i + 1) * 24; i++) {
            const uint8_t* rec = out + 57 + (size_t)i * 24;
            SealSlot s;
            s.type    = (uint32_t)sealRd8(rec);
            s.bodyOff = (size_t)sealRd8(rec + 8);
            s.bodyLen = (size_t)sealRd8(rec + 16);
            h.slots.push_back(s);
        }
    }
    if (out) sentinel_free_bytes(out);
    return h;
}

inline SealArchiveIndex readSealArchive(const Bytes& a) {
    SealArchiveIndex idx;
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_seal_archive(sealPtr(a), (int64_t)a.size(), &out, &olen);
    // Record: [reason][file_count] then file_count * [data_off][data_len][path_len][path].
    if (out && olen >= 9) {
        idx.reason = (int)out[0];
        const uint64_t n = sealRd8(out + 1);
        size_t at = 9;
        for (uint64_t i = 0; i < n; i++) {
            if ((uint64_t)olen < (uint64_t)at + 24) break;
            SealEntry e;
            e.dataOff = (size_t)sealRd8(out + at);
            e.dataLen = (size_t)sealRd8(out + at + 8);
            const uint64_t pl = sealRd8(out + at + 16);
            at += 24;
            if ((uint64_t)olen < (uint64_t)at + pl) break;
            // Already '\'-separated and already guarded, so this is a transcode and
            // nothing else — no separator fixing, no second traversal check.
            e.rel = sealWide((const char*)out + at, (size_t)pl);
            at += (size_t)pl;
            idx.files.push_back(std::move(e));
        }
    }
    if (out) sentinel_free_bytes(out);
    return idx;
}
#else
// C++ fallback for a snc-less build (parsers.lib absent), as every earlier port keeps
// one. It is the shipped parsing, restructured to hand back offsets instead of doing the
// crypto inline, plus the two refusals the port added: a length off disk that does not
// fit an i64 is refused rather than used. tests/seal_xcheck.cpp holds it and the Sentinel
// side to the same corpus, and carries the pre-port code as a third opinion.
//
// Bounds are written as `remaining < want` rather than `pos + want > size`: the lengths
// are u64 straight off disk, so the additive form can wrap and pass.
inline SealHeader readSealHeader(const Bytes& f) {
    SealHeader h;
    size_t pos = 0;
    auto remaining = [&]() -> size_t { return f.size() - pos; };
    auto need = [&](size_t n) { return remaining() >= n; };

    if (f.size() < 8) { h.reason = kSealHdrBadMagic; return h; }
    const bool v2 = memcmp(f.data(), "SNTSEAL2", 8) == 0;
    const bool v1 = memcmp(f.data(), "SNTSEAL1", 8) == 0;
    if (!v1 && !v2) { h.reason = kSealHdrBadMagic; return h; }
    pos = 8;
    if (!need(20)) { h.reason = kSealHdrTruncHeader; return h; }
    h.version = getU32(&f[pos]); pos += 4;
    const uint32_t alg = getU32(&f[pos]); pos += 4;
    const uint64_t archiveSize = getU64(&f[pos]); pos += 8;
    const uint32_t slots = getU32(&f[pos]); pos += 4;
    if (h.version != (v2 ? kSealVersion2 : kSealVersion1) || alg != kAeadAesGcm) {
        h.reason = kSealHdrBadVersion; return h;
    }
    // archive_size is the output-buffer size sealDecompress is handed. v2 binds the
    // 24-byte prefix in as GCM AAD so a tampered value fails authentication first; a v1
    // file has no AAD at all, which is the exposure v2 exists to close and which v1
    // files still carry because they are still read. A value that does not fit an i64
    // is not a size any real file states, so it is refused before anything sizes a
    // buffer with it. (Sentinel has no u64 — see rd_u64 in parsers.sentinel — so this
    // is also the shape both implementations must share to stay comparable.)
    // REFUSING ONLY "does not fit an i64" WAS NOT ENOUGH. A 206-byte container stating
    // 64 GiB was measured committing 65,667 MB of private bytes and 14 seconds before
    // failing, and near the top of the accepted range the std::bad_alloc escaped
    // unsealProject. Authentication cannot help: the attacker is the SEALER, not a
    // tamperer, which is exactly the threat model of "someone sent me a sealed project".
    // So the size must be plausible for the container it arrived in — the ratio does the
    // work, the floor keeps small real files working, the cap sits above any source tree.
    // Kept identical to parse_seal_header so the two stay comparable.
    const uint64_t capAbs = 2147483648ull;                       // 2 GiB
    const uint64_t capFloor = 67108864ull;                       // 64 MiB whatever the ratio
    uint64_t allow = (uint64_t)f.size() * 1024ull;
    if (allow < capFloor) allow = capFloor;
    if (allow > capAbs)   allow = capAbs;
    if (archiveSize > (uint64_t)INT64_MAX || archiveSize > allow) {
        h.reason = kSealHdrArchiveSize; return h;
    }
    h.archiveSize = archiveSize;

    // THE SLOT-COUNT CEILING. unsealProject runs a full PBKDF2 per password-type slot
    // until one unlocks, so a WRONG password runs them all — and the attacker guarantees
    // that path by simply not giving you the right password. Measured: 200 slots in an
    // 18 KB file took 18.7 s on the UI thread; a 1 MB file holds ~11,400, about 17.7
    // minutes, with no progress and no cancel. Phase 31 bounded `iters` because "above
    // the ceiling is a hang dressed up as a password prompt"; this is that hang by
    // another route. 64 is far above anything real (LUKS, whose slot table this borrows
    // from, has 8) and far below anything that costs a user their afternoon.
    if (slots > kMaxUnlockSlots) { h.reason = kSealHdrSlotCount; return h; }

    for (uint32_t i = 0; i < slots; i++) {
        if (!need(v2 ? 8u : 4u)) { h.reason = kSealHdrTruncSlots; return h; }
        SealSlot s;
        s.type = getU32(&f[pos]); pos += 4;
        // v1 has no slot_len, so its only navigable slot type is the password one.
        uint32_t slen = kPasswordSlotLen;
        if (v2) { slen = getU32(&f[pos]); pos += 4; }
        else if (s.type != kSlotPassword) { h.reason = kSealHdrSlotType; return h; }
        if (!need(slen)) { h.reason = kSealHdrTruncSlotBody; return h; }
        s.bodyOff = pos; s.bodyLen = slen;
        h.slots.push_back(s);
        pos += slen;   // ALWAYS advance, understood or not — v1 broke exactly here
    }

    if (!need(12 + 8)) { h.reason = kSealHdrTruncPayload; return h; }
    h.payloadNonceOff = pos; pos += 12;
    const uint64_t plen = getU64(&f[pos]); pos += 8;
    if (plen > (uint64_t)INT64_MAX || plen > (uint64_t)remaining() || remaining() - (size_t)plen < 16) {
        h.reason = kSealHdrTruncPayloadBody; return h;
    }
    h.payloadOff = pos; h.payloadLen = (size_t)plen; h.payloadTagOff = pos + (size_t)plen;
    h.reason = kSealHdrOk;
    return h;
}

inline SealArchiveIndex readSealArchive(const Bytes& a) {
    SealArchiveIndex idx;
    if (a.size() < 4) { idx.reason = kSealArcShort; return idx; }
    size_t pos = 0;
    const uint32_t n = getU32(&a[pos]); pos += 4;
    for (uint32_t i = 0; i < n; i++) {
        if (a.size() - pos < 4) { idx.reason = kSealArcTrunc; return idx; }
        const uint32_t pl = getU32(&a[pos]); pos += 4;
        if (a.size() - pos < pl) { idx.reason = kSealArcTrunc; return idx; }
        std::wstring rel = sealWide((const char*)a.data() + pos, pl); pos += pl;
        if (a.size() - pos < 8) { idx.reason = kSealArcTrunc; return idx; }
        const uint64_t dl = getU64(&a[pos]); pos += 8;
        if (dl > (uint64_t)INT64_MAX) { idx.reason = kSealArcDataLen; return idx; }
        if (dl > (uint64_t)(a.size() - pos)) { idx.reason = kSealArcTrunc; return idx; }
        for (auto& ch : rel) if (ch == L'/') ch = L'\\';                // → native separators
        if (sealUnsafeRelPath(rel)) { idx.reason = kSealArcUnsafePath; return idx; }
        idx.files.push_back(SealEntry{ rel, pos, (size_t)dl });
        pos += (size_t)dl;
    }
    idx.reason = kSealArcOk;
    return idx;
}
#endif

// Write out an already-parsed index. NOTHING IS WRITTEN UNTIL EVERYTHING IS CHECKED:
// the parse validates every bound and every path first, so an archive whose 500th entry
// is `..\evil` no longer drops 499 files into the destination before it aborts. The old
// shape interleaved the two, and MainWindow's failure cleanup is a RemoveDirectoryW,
// which only removes an EMPTY directory — so those files stayed.
inline bool sealExtractArchive(const Bytes& a, const std::wstring& destRoot) {
    const SealArchiveIndex idx = readSealArchive(a);
    if (idx.reason != kSealArcOk) return false;
    for (const SealEntry& e : idx.files) {
        const std::wstring full = destRoot + L"\\" + e.rel;
        const size_t s = full.find_last_of(L'\\');
        if (s != std::wstring::npos) SHCreateDirectoryExW(nullptr, full.substr(0, s).c_str(), nullptr);
        if (!sealWriteBytes(full, e.dataLen ? a.data() + e.dataOff : (const uint8_t*)"", e.dataLen)) return false;
    }
    return true;
}

// ---- seal / unseal --------------------------------------------------------
constexpr uint64_t kSealPbkdf2Iters = 600000;   // OWASP-class for PBKDF2-HMAC-SHA256
// The on-disk format constants (kSealVersion1/2, kAeadAesGcm, kSlotPassword,
// kPasswordSlotLen, kSealAadLen) moved up to the framing section — that is what reads
// them. What is left here is crypto POLICY, which is not the parser's business.

// `iters` lives in the slot body, which is neither covered by the AAD nor checked by
// the slot's own GCM tag (that tag only authenticates the wrapped DEK). So a crafted
// file can name any iteration count and we would obediently run it. Bound it: below
// the floor is a weakened KDF, above the ceiling is a hang dressed up as a password
// prompt. Both are rejected rather than clamped — a real file is always in range, so
// out-of-range means the file is lying, and silently "fixing" it would just fail the
// unwrap later with a misleading "wrong password".
constexpr uint64_t kMinPbkdf2Iters = 1000;
constexpr uint64_t kMaxPbkdf2Iters = 10000000;

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

    // Build the AAD prefix FIRST — the payload tag has to commit to it, so it must
    // exist before the payload is encrypted, and the bytes written to disk must be
    // the very same ones fed to the AEAD.
    Bytes out;
    const char magic[8] = { 'S','N','T','S','E','A','L','2' };
    out.insert(out.end(), magic, magic + 8);
    putU32(out, kSealVersion2);
    putU32(out, kAeadAesGcm);
    putU64(out, archiveSize);
    // out.size() == kSealAadLen here; everything after this point is outside the AAD.

    Bytes payload;
    if (!sealAesGcm(true, dek, payloadNonce, comp, payload, payloadTag, out.data(), (ULONG)kSealAadLen)) {
        SecureZeroMemory(dek, 32); r.message = L"Encrypt failed."; return r;
    }
    if (!sealPbkdf2(password, salt, 16, kSealPbkdf2Iters, kek, 32)) { SecureZeroMemory(dek, 32); r.message = L"Key derivation failed."; return r; }
    Bytes dekIn(dek, dek + 32), wrapped;
    bool wrapOk = sealAesGcm(true, kek, wrapNonce, dekIn, wrapped, wrapTag);
    SecureZeroMemory(dek, 32); SecureZeroMemory(kek, 32); SecureZeroMemory(dekIn.data(), dekIn.size());
    if (!wrapOk || wrapped.size() != 32) { r.message = L"Key wrap failed."; return r; }

    putU32(out, 1);                     // slot count
    putU32(out, kSlotPassword);         // slot 0 type
    putU32(out, kPasswordSlotLen);      // slot 0 length — lets a reader skip what it can't parse
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

// The parsing is gone from here: readSealHeader (Sentinel, or the C++ fallback)
// validates the container and hands back byte offsets, and what is left below is the
// crypto and only the crypto. Every CNG call, every SecureZeroMemory and the AAD rule
// are unchanged — the offsets just say where to read.
inline SealResult unsealProject(const std::wstring& sealedPath, const std::wstring& destDir, const Bytes& password) {
    SealResult r;
    Bytes f; if (!sealReadBytes(sealedPath, f)) { r.message = L"Cannot read the sealed file."; return r; }

    const SealHeader h = readSealHeader(f);
    // A payload-framing failure is held back until AFTER the unlock attempt, because
    // that is the order the messages came out in before: a file that is both truncated
    // and given the wrong password still says "wrong password". Everything earlier than
    // the payload is fatal here, exactly as it was.
    if (h.reason != kSealHdrOk && h.reason != kSealHdrTruncPayload && h.reason != kSealHdrTruncPayloadBody) {
        r.message = sealHeaderMessage(h.reason); return r;
    }
    const bool v2 = h.version == kSealVersion2;

    uint8_t dek[32]; bool unlocked = false;
    bool sawUnknownSlot = false;
    for (const SealSlot& s : h.slots) {
        // Try this slot only if we still need a DEK and we understand the shape.
        // Everything else was stepped over by slot_len during the parse — that is the
        // whole point of v2, and here it costs nothing: an unknown slot is simply a
        // record we do not act on.
        if (!unlocked && s.type == kSlotPassword && s.bodyLen == kPasswordSlotLen) {
            const uint8_t* body = f.data() + s.bodyOff;
            const uint8_t* salt = body;
            uint64_t iters = getU64(body + 16);
            const uint8_t* nonce = body + 24;
            Bytes wrapped(body + 36, body + 68);
            uint8_t tag[16]; memcpy(tag, body + 68, 16);
            if (iters >= kMinPbkdf2Iters && iters <= kMaxPbkdf2Iters) {
                uint8_t kek[32];
                if (sealPbkdf2(password, salt, 16, iters, kek, 32)) {
                    Bytes dekOut;
                    if (sealAesGcm(false, kek, nonce, wrapped, dekOut, tag) && dekOut.size() == 32) {
                        memcpy(dek, dekOut.data(), 32); unlocked = true;
                    }
                    SecureZeroMemory(kek, 32);
                    SecureZeroMemory(dekOut.data(), dekOut.size());
                }
            }
        } else if (!unlocked && s.type != kSlotPassword) {
            sawUnknownSlot = true;
        }
    }
    if (!unlocked) {
        r.message = sawUnknownSlot
            ? L"Wrong password, and this file also carries unlock methods this build doesn't support (newer IDE?)."
            : L"Wrong password — could not unlock the project.";
        return r;
    }
    if (h.reason != kSealHdrOk) { SecureZeroMemory(dek, 32); r.message = sealHeaderMessage(h.reason); return r; }

    const uint8_t* pnonce = f.data() + h.payloadNonceOff;
    Bytes payload(f.data() + h.payloadOff, f.data() + h.payloadOff + h.payloadLen);
    uint8_t ptag[16]; memcpy(ptag, f.data() + h.payloadTagOff, 16);

    // v2 binds the 24-byte header prefix into the payload tag, so a tampered
    // archive_size fails here rather than steering the allocation below. v1 files
    // have no AAD to check — read as-is; that exposure is why v2 exists, and it is
    // also why an archive_size that cannot fit an i64 is refused during the parse
    // rather than reaching sealDecompress.
    Bytes comp;
    bool decOk = sealAesGcm(false, dek, pnonce, payload, comp, ptag,
                            v2 ? f.data() : nullptr, v2 ? (ULONG)kSealAadLen : 0);
    SecureZeroMemory(dek, 32);
    if (!decOk) { r.message = L"Payload failed authentication — the sealed file is corrupt or tampered."; return r; }
    Bytes archive; if (!sealDecompress(comp, (size_t)h.archiveSize, archive)) { r.message = L"Decompression failed."; return r; }
    if (!sealExtractArchive(archive, destDir)) { r.message = L"Could not extract the project (bad archive)."; return r; }
    r.ok = true; r.outPath = destDir; r.message = L"Unsealed " + std::to_wstring(archive.size()) + L" B to " + destDir;
    return r;
}

}  // namespace sentinelide
