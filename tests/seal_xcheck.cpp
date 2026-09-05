// SPDX-License-Identifier: GPL-3.0-or-later
//
// seal_xcheck — the cross-check for the SEALED-CONTAINER FRAMING
// (src/sentinel/parsers.sentinel::parse_seal_header / parse_seal_archive, compiled into
// build/generated/parsers.lib). It is the seventh and eighth export, and the first pair
// that reads a BINARY container rather than text.
//
// WHY THIS ONE MATTERS MOST. A `.sealed` file is fully attacker-chosen — the feature
// exists so someone can SEND you a sealed project — and GCM proves only that the bytes
// did not change in transit, not that whoever sealed them meant well. The header and
// the slot table are parsed BEFORE anything is authenticated at all. Phase 31 found
// five real defects in this framing, three of them integer/bounds, and one case below
// exists for each.
//
// NOTHING SECRET IS UNDER TEST HERE, and that is the point of the split: no password,
// salt, KEK or DEK ever crosses the FFI boundary. Sentinel reads the framing and hands
// back byte offsets; the host does every CNG call from those offsets exactly as before.
// The crypto core stays in C++ (docs/Sentinel-lang_request.md R1: no secure-zero for
// `[secret u8]`).
//
// THREE implementations are run against every case and all three are asserted:
//
//   oracle::    the C++ that shipped at 72a6b82 — unsealProject's parsing loop and
//               sealExtractArchive. sealExtractArchive is VERBATIM (it already deals in
//               offsets and writes). The header parse is mechanically restructured to
//               record each slot's offsets where the shipped loop ran PBKDF2, and to
//               set a reason code where it set r.message; every bound, every branch and
//               their order are the shipped ones. Its gaps are the measurement — do not
//               "fix" anything in there.
//   fallback::  Seal.h's own C++ readSealHeader/readSealArchive, i.e. what a snc-less
//               build compiles. It must agree with Sentinel EVERYWHERE: they are one
//               behaviour with two spellings, and this is what pins them.
//   Sentinel    parse_seal_header / parse_seal_archive themselves.
//
// WHERE THE ORACLE IS WRONG THIS TEST ASSERTS THE NEW BEHAVIOUR AND SAYS SO. Every case
// carries an explicit Parity/Diverges expectation plus the reason, and a Diverges case
// FAILS if the two ever agree again — so nobody can quietly restore the old behaviour
// and still be green. The divergences are exactly the defects the port closes:
//
//   1. NO u64 IN SENTINEL. Every length in this container is a u64 on disk and Sentinel
//      has i64/i32/u8/u128 and no u64 (checked against crates/sentinel-types), so a
//      hostile length of 2^63 or more reads back NEGATIVE. rd_u64 refuses such a value
//      instead of returning a wrapped one. For `archive_size` that is a behaviour
//      change from the oracle, which accepted it and handed it to sealDecompress as an
//      output-buffer size — cases H11/H12 print the number.
//   2. THE COLON IS REFUSED ANYWHERE, not only at position 1 as the shipped guard had it:
//      `ab:c` used to pass and writes an NTFS alternate data stream. Case A17.
//   3. NOTHING IS WRITTEN UNTIL EVERYTHING IS CHECKED. sealExtractArchive parsed and
//      wrote in one loop, so an archive whose last entry is `..\evil` had already
//      dropped every earlier file into the destination before it refused. Cases A20 and
//      A26 count the files each side leaves behind.
//
// A refused parse still REPORTS the entries it walked before it stopped (A26 shows three
// slots' worth of index for an archive it rejects). That is diagnostic only — every
// caller gates on the reason code — and it is what lets these cases say how far the walk
// got rather than only that it failed.
//
// Build+run (needs build\generated\parsers.lib, i.e. a build.bat run first):
//     cmake --build build --target seal_xcheck
//     build\seal_xcheck.exe
//
// This TU deliberately does NOT define SENTINELIDE_SENTINEL, so its Seal.h is the C++
// fallback, and it links parsers.lib to call the two exports directly. The Sentinel-side
// record decoders below are the same ones Seal.h's #ifdef branch uses; keeping a copy
// here is the appcast_xcheck precedent — the test must be able to disagree with the
// header it is checking.
//
// Writes only under %TEMP%\sealxcheck. Never touches examples/.

#include "Seal.h"

#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

extern "C" {
    void parse_seal_header(const uint8_t*, int64_t, uint8_t**, int64_t*);
    void parse_seal_archive(const uint8_t*, int64_t, uint8_t**, int64_t*);
    void sentinel_free_bytes(uint8_t*);
}

using namespace sentinelide;

// ---------------------------------------------------------------------------
// The oracle: src/core/Seal.h at 72a6b82.
// ---------------------------------------------------------------------------
namespace oracle {

// unsealProject's parsing loop (Seal.h:316-360 at 72a6b82). Restructured only where it
// had to be: the PBKDF2/GCM block becomes a record of the slot's offsets, and each
// `r.message = L"…"; return r;` becomes the reason code that message now maps to.
// `archive_size` is read and NOT range-checked, which is the shipped behaviour and the
// thing cases H11/H12 measure.
SealHeader readSealHeader(const Bytes& f) {
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
    const uint64_t archiveSize = getU64(&f[pos]); pos += 8;   // <-- NOT range-checked. The gap.
    const uint32_t slots = getU32(&f[pos]); pos += 4;
    if (h.version != (v2 ? kSealVersion2 : kSealVersion1) || alg != kAeadAesGcm) {
        h.reason = kSealHdrBadVersion; return h;
    }
    // The shipped code held archive_size in a local until sealDecompress, so on the
    // version/alg path above it never escaped. Publishing it only here keeps the oracle
    // faithful to what a CALLER could see, not merely to the order of the statements.
    h.archiveSize = archiveSize;

    for (uint32_t i = 0; i < slots; i++) {
        if (!need(v2 ? 8u : 4u)) { h.reason = kSealHdrTruncSlots; return h; }
        SealSlot s;
        s.type = getU32(&f[pos]); pos += 4;
        uint32_t slen = kPasswordSlotLen;
        if (v2) { slen = getU32(&f[pos]); pos += 4; }
        else if (s.type != kSlotPassword) { h.reason = kSealHdrSlotType; return h; }
        if (!need(slen)) { h.reason = kSealHdrTruncSlotBody; return h; }
        s.bodyOff = pos; s.bodyLen = slen;
        h.slots.push_back(s);
        pos += slen;
    }

    if (!need(12 + 8)) { h.reason = kSealHdrTruncPayload; return h; }
    h.payloadNonceOff = pos; pos += 12;
    const uint64_t plen = getU64(&f[pos]); pos += 8;
    if (plen > (uint64_t)remaining() || remaining() - (size_t)plen < 16) {
        h.reason = kSealHdrTruncPayloadBody; return h;
    }
    h.payloadOff = pos; h.payloadLen = (size_t)plen; h.payloadTagOff = pos + (size_t)plen;
    h.reason = kSealHdrOk;
    return h;
}

// sealUnsafeRelPath + sealExtractArchive (Seal.h:205-241 at 72a6b82), VERBATIM. It parses
// and WRITES in the same loop (cases A20/A26), and its colon test only looks at position 1
// (case A17).
bool sealUnsafeRelPath(const std::wstring& p) {
    if (p.empty()) return true;
    if (p[0] == L'\\' || p[0] == L'/') return true;                   // rooted
    if (p.size() > 1 && p[1] == L':') return true;                    // drive-qualified
    for (size_t start = 0; start <= p.size(); ) {
        const size_t sep = p.find(L'\\', start);
        const size_t end = (sep == std::wstring::npos) ? p.size() : sep;
        if (p.compare(start, end - start, L"..") == 0) return true;   // exactly ".."
        if (sep == std::wstring::npos) break;
        start = sep + 1;
    }
    return false;
}
bool sealExtractArchive(const Bytes& a, const std::wstring& destRoot) {
    if (a.size() < 4) return false;
    size_t pos = 0; uint32_t n = getU32(&a[pos]); pos += 4;
    for (uint32_t i = 0; i < n; i++) {
        if (a.size() - pos < 4) return false;
        uint32_t pl = getU32(&a[pos]); pos += 4;
        if (a.size() - pos < pl) return false;
        std::wstring rel = sealWide((const char*)a.data() + pos, pl); pos += pl;
        if (a.size() - pos < 8) return false;
        uint64_t dl = getU64(&a[pos]); pos += 8;
        if (dl > (uint64_t)(a.size() - pos)) return false;
        for (auto& ch : rel) if (ch == L'/') ch = L'\\';                 // → native separators
        if (sealUnsafeRelPath(rel)) return false;
        std::wstring full = destRoot + L"\\" + rel;
        size_t s = full.find_last_of(L'\\');
        if (s != std::wstring::npos) SHCreateDirectoryExW(nullptr, full.substr(0, s).c_str(), nullptr);
        if (!sealWriteBytes(full, dl ? &a[pos] : (const uint8_t*)"", (size_t)dl)) return false;
        pos += (size_t)dl;
    }
    return true;
}

}  // namespace oracle

// ---- Sentinel side: the same decode Seal.h's #ifdef branch does -------------
static uint64_t rd8(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static const uint8_t* ptrOf(const Bytes& b) { static const uint8_t kNone = 0; return b.empty() ? &kNone : b.data(); }

static SealHeader sentinelReadHeader(const Bytes& f) {
    SealHeader h;
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_seal_header(ptrOf(f), (int64_t)f.size(), &out, &olen);
    if (out && olen >= 57) {
        h.reason          = (int)out[0];
        h.version         = (uint32_t)rd8(out + 1);
        h.archiveSize     = rd8(out + 9);
        const uint64_t ns = rd8(out + 17);
        h.payloadNonceOff = (size_t)rd8(out + 25);
        h.payloadOff      = (size_t)rd8(out + 33);
        h.payloadLen      = (size_t)rd8(out + 41);
        h.payloadTagOff   = (size_t)rd8(out + 49);
        for (uint64_t i = 0; i < ns && (uint64_t)olen >= 57 + (i + 1) * 24; i++) {
            const uint8_t* rec = out + 57 + (size_t)i * 24;
            SealSlot s;
            s.type    = (uint32_t)rd8(rec);
            s.bodyOff = (size_t)rd8(rec + 8);
            s.bodyLen = (size_t)rd8(rec + 16);
            h.slots.push_back(s);
        }
    }
    if (out) sentinel_free_bytes(out);
    return h;
}

static SealArchiveIndex sentinelReadArchive(const Bytes& a) {
    SealArchiveIndex idx;
    uint8_t* out = nullptr; int64_t olen = 0;
    parse_seal_archive(ptrOf(a), (int64_t)a.size(), &out, &olen);
    if (out && olen >= 9) {
        idx.reason = (int)out[0];
        const uint64_t n = rd8(out + 1);
        size_t at = 9;
        for (uint64_t i = 0; i < n; i++) {
            if ((uint64_t)olen < (uint64_t)at + 24) break;
            SealEntry e;
            e.dataOff = (size_t)rd8(out + at);
            e.dataLen = (size_t)rd8(out + at + 8);
            const uint64_t pl = rd8(out + at + 16);
            at += 24;
            if ((uint64_t)olen < (uint64_t)at + pl) break;
            e.rel = sealWide((const char*)out + at, (size_t)pl);
            at += (size_t)pl;
            idx.files.push_back(std::move(e));
        }
    }
    if (out) sentinel_free_bytes(out);
    return idx;
}

// ---- harness ---------------------------------------------------------------
enum class Rel { Parity, Diverges };

static int pass = 0, fail = 0;
static std::wstring gBase;

static void putU32At(Bytes& b, size_t off, uint32_t v) { for (int i = 0; i < 4; i++) b[off + i] = (uint8_t)(v >> (8 * i)); }
static void putU64At(Bytes& b, size_t off, uint64_t v) { for (int i = 0; i < 8; i++) b[off + i] = (uint8_t)(v >> (8 * i)); }

// Offsets in a v2 file — the same named offsets tests/seal_test.cpp patches.
static const size_t OFF_ARCHIVE_SIZE = 16;   // magic(8)+ver(4)+alg(4)
static const size_t OFF_VERSION      = 8;
static const size_t OFF_ALG          = 12;
static const size_t OFF_SLOT_COUNT   = 24;
static const size_t OFF_SLOT0_TYPE   = 28;
static const size_t OFF_SLOT0_LEN    = 32;
static const size_t OFF_SLOT0_BODY   = 36;

static std::string describeHdr(const SealHeader& h) {
    char buf[256];
    sprintf_s(buf, "reason=%d ver=%u arc=%llu slots=%zu pnonce=%zu poff=%zu plen=%zu ptag=%zu",
              h.reason, h.version, (unsigned long long)h.archiveSize, h.slots.size(),
              h.payloadNonceOff, h.payloadOff, h.payloadLen, h.payloadTagOff);
    return buf;
}
static bool sameHdr(const SealHeader& a, const SealHeader& b) {
    if (a.reason != b.reason || a.version != b.version || a.archiveSize != b.archiveSize) return false;
    if (a.payloadNonceOff != b.payloadNonceOff || a.payloadOff != b.payloadOff ||
        a.payloadLen != b.payloadLen || a.payloadTagOff != b.payloadTagOff) return false;
    if (a.slots.size() != b.slots.size()) return false;
    for (size_t i = 0; i < a.slots.size(); i++)
        if (a.slots[i].type != b.slots[i].type || a.slots[i].bodyOff != b.slots[i].bodyOff ||
            a.slots[i].bodyLen != b.slots[i].bodyLen) return false;
    return true;
}

// A header case: assert the Sentinel result, that the C++ fallback agrees with it, and
// that the relationship to the shipped C++ is the one we declared.
static void hcheck(const char* name, const Bytes& f, int eReason, size_t eSlots,
                   Rel rel, const char* why) {
    const SealHeader s = sentinelReadHeader(f);
    const SealHeader b = sentinelide::readSealHeader(f);
    const SealHeader o = oracle::readSealHeader(f);

    const bool asserted = s.reason == eReason && s.slots.size() == eSlots;
    const bool agree = sameHdr(s, b);
    const bool same = sameHdr(s, o);
    const bool relOk = (rel == Rel::Parity) ? same : !same;
    const bool ok = asserted && agree && relOk;

    printf("[%s] %-52s %s\n", ok ? "PASS" : "FAIL", name,
           rel == Rel::Parity ? "(parity)" : "(DIVERGES)");
    if (!ok || rel == Rel::Diverges) {
        printf("        shipped C++ : %s\n", describeHdr(o).c_str());
        printf("        Sentinel    : %s\n", describeHdr(s).c_str());
        if (rel == Rel::Diverges) printf("        why         : %s\n", why);
    }
    if (!ok) {
        if (!asserted) printf("        !! expected reason=%d slots=%zu\n", eReason, eSlots);
        if (!agree)    printf("        !! C++ fallback disagrees with Sentinel: %s\n", describeHdr(b).c_str());
        if (!relOk)    printf("        !! expected %s against the shipped C++, got the opposite\n",
                              rel == Rel::Parity ? "parity" : "a divergence");
    }
    if (ok) pass++; else fail++;
}

// ---- directory helpers (all under %TEMP%\sealxcheck) -----------------------
static void rmTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring full = dir + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rmTree(full);
        else DeleteFileW(full.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir.c_str());
}
static void listTree(const std::wstring& root, const std::wstring& rel,
                     std::vector<std::pair<std::wstring, std::string>>& out) {
    const std::wstring dir = rel.empty() ? root : root + L"\\" + rel;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring childRel = rel.empty() ? name : rel + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) listTree(root, childRel, out);
        else {
            Bytes b; sealReadBytes(dir + L"\\" + name, b);
            out.push_back({ childRel, std::string((const char*)b.data(), b.size()) });
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
}
// Write out an already-parsed index — the loop sealExtractArchive keeps after the parse
// moved out of it.
static bool materialise(const SealArchiveIndex& idx, const Bytes& a, const std::wstring& dest) {
    if (idx.reason != kSealArcOk) return false;
    for (const SealEntry& e : idx.files) {
        const std::wstring full = dest + L"\\" + e.rel;
        const size_t s = full.find_last_of(L'\\');
        if (s != std::wstring::npos) SHCreateDirectoryExW(nullptr, full.substr(0, s).c_str(), nullptr);
        if (!sealWriteBytes(full, e.dataLen ? a.data() + e.dataOff : (const uint8_t*)"", e.dataLen)) return false;
    }
    return true;
}
static std::string treeSummary(const std::vector<std::pair<std::wstring, std::string>>& t) {
    std::string s;
    char buf[64];
    sprintf_s(buf, "%zu file(s): ", t.size());
    s = buf;
    for (const auto& e : t) {
        const std::string u8 = sealUtf8(e.first);
        sprintf_s(buf, "[%zuB] ", e.second.size());
        s += u8 + buf;
    }
    return s;
}

// An archive case: run all three, compare the TREE each leaves behind (not just the
// verdict — case A18 differs only in what got written before the refusal).
static void acheck(const char* name, const Bytes& a, int eReason, size_t eFiles,
                   Rel rel, const char* why) {
    const std::wstring dO = gBase + L"\\arc_o", dF = gBase + L"\\arc_f", dS = gBase + L"\\arc_s";
    rmTree(dO); rmTree(dF); rmTree(dS);
    SHCreateDirectoryExW(nullptr, dO.c_str(), nullptr);
    SHCreateDirectoryExW(nullptr, dF.c_str(), nullptr);
    SHCreateDirectoryExW(nullptr, dS.c_str(), nullptr);

    const bool oOk = oracle::sealExtractArchive(a, dO);
    const SealArchiveIndex fIdx = sentinelide::readSealArchive(a);
    const bool fOk = materialise(fIdx, a, dF);
    const SealArchiveIndex sIdx = sentinelReadArchive(a);
    const bool sOk = materialise(sIdx, a, dS);

    std::vector<std::pair<std::wstring, std::string>> tO, tF, tS;
    listTree(dO, L"", tO); listTree(dF, L"", tF); listTree(dS, L"", tS);

    const bool asserted = sIdx.reason == eReason && sIdx.files.size() == eFiles;
    const bool agree = fIdx.reason == sIdx.reason && fOk == sOk && tF == tS;
    const bool same = (oOk == sOk) && (tO == tS);
    const bool relOk = (rel == Rel::Parity) ? same : !same;
    const bool ok = asserted && agree && relOk;

    printf("[%s] %-52s %s\n", ok ? "PASS" : "FAIL", name,
           rel == Rel::Parity ? "(parity)" : "(DIVERGES)");
    if (!ok || rel == Rel::Diverges) {
        printf("        shipped C++ : extract=%s, wrote %s\n", oOk ? "OK" : "REFUSED", treeSummary(tO).c_str());
        printf("        Sentinel    : reason=%d entries=%zu, wrote %s\n",
               sIdx.reason, sIdx.files.size(), treeSummary(tS).c_str());
        if (rel == Rel::Diverges) printf("        why         : %s\n", why);
    }
    if (!ok) {
        if (!asserted) printf("        !! expected reason=%d entries=%zu\n", eReason, eFiles);
        if (!agree)    printf("        !! C++ fallback disagrees with Sentinel: reason=%d wrote %s\n",
                              fIdx.reason, treeSummary(tF).c_str());
        if (!relOk)    printf("        !! expected %s against the shipped C++, got the opposite\n",
                              rel == Rel::Parity ? "parity" : "a divergence");
    }
    if (ok) pass++; else fail++;
}

// ---- corpus builders --------------------------------------------------------
struct RawEnt {
    std::string path;
    uint32_t declaredPathLen;   // usually path.size()
    uint64_t declaredDataLen;   // usually data.size()
    std::string data;
};
static Bytes packRaw(uint32_t declaredCount, const std::vector<RawEnt>& es) {
    Bytes a; putU32(a, declaredCount);
    for (const RawEnt& e : es) {
        putU32(a, e.declaredPathLen);
        a.insert(a.end(), e.path.begin(), e.path.end());
        putU64(a, e.declaredDataLen);
        a.insert(a.end(), e.data.begin(), e.data.end());
    }
    return a;
}
static RawEnt ent(const std::string& path, const std::string& data) {
    return RawEnt{ path, (uint32_t)path.size(), (uint64_t)data.size(), data };
}
static Bytes pw(const char* s) { return Bytes((const uint8_t*)s, (const uint8_t*)s + strlen(s)); }
static void writeFile(const std::wstring& p, const std::string& d) {
    sealWriteBytes(p, (const uint8_t*)d.data(), d.size());
}

int main() {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    gBase = std::wstring(tmp) + L"sealxcheck";
    rmTree(gBase);
    const std::wstring src = gBase + L"\\src";
    SHCreateDirectoryExW(nullptr, (src + L"\\sub").c_str(), nullptr);
    writeFile(src + L"\\a.sentinel", "fn main() -> i64 { 42 }\r\n");
    writeFile(src + L"\\notes..txt", "a legitimate name with two dots\r\n");   // the phase-31 regression
    writeFile(src + L"\\sub\\b.txt", std::string(5000, 'x'));

    // The real corpus: a genuine v2 container, then bytes patched at named offsets —
    // exactly what tests/seal_test.cpp does, reused rather than reinvented.
    const std::wstring sealedPath = gBase + L"\\out.sealed";
    const SealResult sr = sealProject(src, sealedPath, pw("correct horse"));
    if (!sr.ok) { printf("[FAIL] could not build the corpus: %ls\n", sr.message.c_str()); return 1; }
    Bytes v2f; sealReadBytes(sealedPath, v2f);

    // A genuine v1 container, built the way the pre-v2 writer did (no slot_len, no AAD)
    // — the same reconstruction seal_test case 9 uses.
    Bytes v1f;
    {
        std::vector<std::pair<std::wstring, Bytes>> files;
        sealCollect(src, L"", files);
        const Bytes archive = sealPackArchive(files);
        Bytes comp; sealCompress(archive, comp);
        uint8_t dek[32], pn[12], pt[16], salt[16], kek[32], wn[12], wt[16];
        sealRng(dek, 32); sealRng(pn, 12); sealRng(salt, 16); sealRng(wn, 12);
        Bytes payload; sealAesGcm(true, dek, pn, comp, payload, pt);          // no AAD
        sealPbkdf2(pw("v1 pass"), salt, 16, kSealPbkdf2Iters, kek, 32);
        Bytes dekIn(dek, dek + 32), wrapped;
        sealAesGcm(true, kek, wn, dekIn, wrapped, wt);
        const char m1[8] = { 'S','N','T','S','E','A','L','1' };
        v1f.insert(v1f.end(), m1, m1 + 8);
        putU32(v1f, 1); putU32(v1f, 1); putU64(v1f, (uint64_t)archive.size());
        putU32(v1f, 1);                      // slot count
        putU32(v1f, 1);                      // slot type — NO slot_len in v1
        v1f.insert(v1f.end(), salt, salt + 16);
        putU64(v1f, kSealPbkdf2Iters);
        v1f.insert(v1f.end(), wn, wn + 12);
        v1f.insert(v1f.end(), wrapped.begin(), wrapped.end());
        v1f.insert(v1f.end(), wt, wt + 16);
        v1f.insert(v1f.end(), pn, pn + 12);
        putU64(v1f, (uint64_t)payload.size());
        v1f.insert(v1f.end(), payload.begin(), payload.end());
        v1f.insert(v1f.end(), pt, pt + 16);
        SecureZeroMemory(dek, 32); SecureZeroMemory(kek, 32);
    }

    printf("== header: the container as written ==\n");
    hcheck("H1  a real v2 file", v2f, kSealHdrOk, 1, Rel::Parity, "");
    hcheck("H2  a real v1 file", v1f, kSealHdrOk, 1, Rel::Parity, "");

    printf("== header: not a container at all ==\n");
    hcheck("H3  empty file", Bytes(), kSealHdrBadMagic, 0, Rel::Parity, "");
    { Bytes t(v2f.begin(), v2f.begin() + 4);
      hcheck("H4  four bytes", t, kSealHdrBadMagic, 0, Rel::Parity, ""); }
    { Bytes t(v2f.begin(), v2f.begin() + 8);
      hcheck("H5  magic and nothing else", t, kSealHdrTruncHeader, 0, Rel::Parity, ""); }
    { Bytes t = v2f; t[3] = 'X';
      hcheck("H6  wrong magic", t, kSealHdrBadMagic, 0, Rel::Parity, ""); }
    { Bytes t(v2f.begin(), v2f.begin() + 20);
      hcheck("H7  header cut mid-field", t, kSealHdrTruncHeader, 0, Rel::Parity, ""); }

    printf("== header: version and algorithm ==\n");
    { Bytes t = v2f; putU32At(t, OFF_VERSION, 3);
      hcheck("H8  version 3 under SNTSEAL2", t, kSealHdrBadVersion, 0, Rel::Parity, ""); }
    { Bytes t = v2f; putU32At(t, OFF_ALG, 2);
      hcheck("H9  aead_alg 2 (ChaCha, reserved)", t, kSealHdrBadVersion, 0, Rel::Parity, ""); }
    { Bytes t = v2f; putU32At(t, OFF_VERSION, 1);
      hcheck("H10 version 1 under SNTSEAL2", t, kSealHdrBadVersion, 0, Rel::Parity, ""); }

    printf("== header: archive_size that does not fit an i64 (TRAP A) ==\n");
    { Bytes t = v2f; putU64At(t, OFF_ARCHIVE_SIZE, 0xFFFFFFFFFFFFFFFFULL);
      hcheck("H11 archive_size = 18446744073709551615", t, kSealHdrArchiveSize, 0, Rel::Diverges,
             "the shipped reader accepted it and unsealProject handed it to sealDecompress as the "
             "output-buffer size — out.resize(2^64-1). Sentinel has no u64, so the value reads back "
             "NEGATIVE; rd_u64 refuses it rather than wrapping, and the file never reaches an "
             "allocation. (In a v2 file the AAD would also catch it; a v1 file has no AAD at all.)"); }
    { Bytes t = v2f; putU64At(t, OFF_ARCHIVE_SIZE, 0x8000000000000000ULL);
      hcheck("H12 archive_size = 2^63 exactly (the boundary)", t, kSealHdrArchiveSize, 0, Rel::Diverges,
             "the first value whose top bit is set; the shipped reader takes it, we refuse it"); }
    { Bytes t = v2f; putU64At(t, OFF_ARCHIVE_SIZE, 0x7FFFFFFFFFFFFFFFULL);
      hcheck("H13 archive_size = 2^63-1", t, kSealHdrArchiveSize, 0, Rel::Diverges,
             "fitting in an i64 was never evidence a size was REAL. The shipped reader "
             "accepts this and hands it to sealDecompress as an output-buffer size; a "
             "206-byte container claiming 64 GiB was measured committing 65,667 MB and 14 s "
             "before failing, and near the top of the range the bad_alloc escaped "
             "unsealProject. Authentication cannot help — the attacker is the SEALER. The "
             "size must now be plausible for the container it arrived in."); }
    { // The bound is a RATIO, not a constant, so prove both halves: a size that is
      // implausible for THIS container is refused even though it is small in absolute
      // terms, and a size within the floor is still accepted.
      Bytes t = v2f; putU64At(t, OFF_ARCHIVE_SIZE, 0x40000000ULL);   // 1 GiB
      hcheck("H13b archive_size = 1 GiB from a small container", t, kSealHdrArchiveSize, 0,
             Rel::Diverges,
             "no compressor turns a few hundred bytes into a gigabyte; the claim is out of "
             "step with the file it arrived in"); }
    { Bytes t = v2f; putU64At(t, OFF_ARCHIVE_SIZE, 1048576ULL);      // 1 MiB, under the floor
      hcheck("H13c archive_size = 1 MiB (under the 64 MiB floor, accepted)", t, kSealHdrOk, 1,
             Rel::Parity,
             ""); }

    printf("== header: the slot table (phase-31 defects a, b) ==\n");
    { // seal_test case 5's corpus: an unknown slot spliced in AFTER the password slot.
      Bytes t(v2f.begin(), v2f.begin() + OFF_SLOT0_BODY + kPasswordSlotLen);
      Bytes ns; putU32(ns, 99); putU32(ns, 12);
      for (int i = 0; i < 12; i++) ns.push_back((uint8_t)i);
      t.insert(t.end(), ns.begin(), ns.end());
      t.insert(t.end(), v2f.begin() + OFF_SLOT0_BODY + kPasswordSlotLen, v2f.end());
      putU32At(t, OFF_SLOT_COUNT, 2);
      hcheck("H14 unknown v2 slot AFTER the password slot", t, kSealHdrOk, 2, Rel::Parity, ""); }
    { // seal_test case 6's corpus: the unknown slot FIRST. v1 aborted here.
      Bytes t(v2f.begin(), v2f.begin() + OFF_SLOT0_TYPE);
      Bytes ns; putU32(ns, 99); putU32(ns, 12);
      for (int i = 0; i < 12; i++) ns.push_back((uint8_t)i);
      t.insert(t.end(), ns.begin(), ns.end());
      t.insert(t.end(), v2f.begin() + OFF_SLOT0_TYPE, v2f.end());
      putU32At(t, OFF_SLOT_COUNT, 2);
      hcheck("H15 unknown v2 slot BEFORE the password slot", t, kSealHdrOk, 2, Rel::Parity, ""); }
    { // A zero-length unknown slot: slot_len 0 must still step, not stall.
      Bytes t(v2f.begin(), v2f.begin() + OFF_SLOT0_TYPE);
      Bytes ns; putU32(ns, 77); putU32(ns, 0);
      t.insert(t.end(), ns.begin(), ns.end());
      t.insert(t.end(), v2f.begin() + OFF_SLOT0_TYPE, v2f.end());
      putU32At(t, OFF_SLOT_COUNT, 2);
      hcheck("H16 zero-length unknown slot is stepped", t, kSealHdrOk, 2, Rel::Parity, ""); }
    { Bytes t = v2f; putU32At(t, OFF_SLOT0_TYPE, 99);
      hcheck("H17 the only slot is an unknown v2 type", t, kSealHdrOk, 1, Rel::Parity,
             ""); }
    { // v1 has no slot_len, so an unknown type cannot be stepped over at all.
      Bytes t = v1f; putU32At(t, 28, 99);          // v1 slot0 type sits at 28
      hcheck("H18 v1 file with a non-password slot", t, kSealHdrSlotType, 0, Rel::Parity, ""); }
    { // slot_count lies by one. The second "slot header" is read out of the payload
      // nonce, so it parses as some type and length and then fails on its BODY — which
      // is the honest outcome: nothing distinguishes a slot header from the bytes that
      // follow the table except the count itself. Both readers land in the same place.
      Bytes t = v2f; putU32At(t, OFF_SLOT_COUNT, 2);
      hcheck("H19 slot_count 2, only one slot present", t, kSealHdrTruncSlotBody, 1, Rel::Parity, ""); }
    { // Cut so that four bytes remain where a v2 slot header needs eight.
      Bytes t(v2f.begin(), v2f.begin() + OFF_SLOT0_TYPE + 4);
      hcheck("H20 file ends inside a slot header", t, kSealHdrTruncSlots, 0, Rel::Parity, ""); }
    { Bytes t = v2f; putU32At(t, OFF_SLOT0_LEN, 0xFFFFFFF0u);
      hcheck("H21 slot_len larger than the file (TRAP B)", t, kSealHdrTruncSlotBody, 0, Rel::Parity,
             ""); }
    { Bytes t(v2f.begin(), v2f.begin() + OFF_SLOT0_BODY + 40);   // slot body cut in half
      hcheck("H22 truncated slot body", t, kSealHdrTruncSlotBody, 0, Rel::Parity, ""); }
    { Bytes t = v2f; putU32At(t, OFF_SLOT_COUNT, 0xFFFFFFFFu);
      const DWORD t0 = GetTickCount();
      hcheck("H23 slot_count = 4294967295", t, kSealHdrSlotCount, 0, Rel::Diverges,
             "the shipped reader walks slots until the bytes run out, which is safe for the "
             "PARSE but not for what follows it: unsealProject runs a full PBKDF2 per "
             "password-type slot until one unlocks, so a WRONG password runs them all. "
             "Measured: 200 slots in an 18 KB file took 18.7 s on the UI thread; a 1 MB file "
             "holds ~11,400, about 17.7 minutes, no progress and no cancel. Phase 31 bounded "
             "`iters` for exactly this reason; this was the same hang by another route.");
      const DWORD ms = GetTickCount() - t0;
      printf("        (four billion claimed slots, refused in %lu ms — now before the walk)\n", ms); }
    { Bytes t = v2f; putU32At(t, OFF_SLOT_COUNT, 64u);
      hcheck("H23b slot_count = 64 (the ceiling itself, still walked)", t, kSealHdrTruncSlotBody, 1,
             Rel::Parity,
             ""); }

    printf("== header: the payload framing ==\n");
    { Bytes t(v2f.begin(), v2f.begin() + OFF_SLOT0_BODY + kPasswordSlotLen);
      hcheck("H24 file ends after the slot table", t, kSealHdrTruncPayload, 1, Rel::Parity, ""); }
    { Bytes t = v2f;
      const size_t plenOff = OFF_SLOT0_BODY + kPasswordSlotLen + 12;
      putU64At(t, plenOff, 0xFFFFFFFFFFFFFFFFULL);
      hcheck("H25 payload_len = 18446744073709551615", t, kSealHdrTruncPayloadBody, 1, Rel::Parity,
             ""); }
    { Bytes t = v2f;
      const size_t plenOff = OFF_SLOT0_BODY + kPasswordSlotLen + 12;
      putU64At(t, plenOff, 0x8000000000000000ULL);
      hcheck("H26 payload_len = 2^63 exactly", t, kSealHdrTruncPayloadBody, 1, Rel::Parity, ""); }
    { Bytes t(v2f.begin(), v2f.end() - 8);   // eat half the tag
      hcheck("H27 payload tag truncated", t, kSealHdrTruncPayloadBody, 1, Rel::Parity, ""); }

    printf("\n== archive: the index as written ==\n");
    {
        std::vector<std::pair<std::wstring, Bytes>> files;
        sealCollect(src, L"", files);
        acheck("A1  the real archive (3 files)", sealPackArchive(files), kSealArcOk, 3, Rel::Parity, "");
    }
    acheck("A2  zero files", packRaw(0, {}), kSealArcOk, 0, Rel::Parity, "");
    acheck("A3  a file with zero-length data", packRaw(1, { ent("empty.txt", "") }),
           kSealArcOk, 1, Rel::Parity, "");
    acheck("A4  nested path with '/' separators",
           packRaw(2, { ent("sub/deep/x.txt", "hello"), ent("top.txt", "hi") }),
           kSealArcOk, 2, Rel::Parity, "");
    acheck("A5  multi-byte UTF-8 path components",
           packRaw(1, { ent("m\xc3\xa4ppe/\xe6\x96\x87\xe4\xbb\xb6.txt", "unicode") }),
           kSealArcOk, 1, Rel::Parity, "");

    printf("== archive: the '..' rule is PER COMPONENT (TRAP C) ==\n");
    acheck("A6  notes..txt is a FILE NAME, not a traversal",
           packRaw(1, { ent("notes..txt", "two dots") }), kSealArcOk, 1, Rel::Parity, "");
    acheck("A7  v1..2.md likewise", packRaw(1, { ent("v1..2.md", "x") }), kSealArcOk, 1, Rel::Parity, "");
    acheck("A8  sub/ok..name.txt likewise",
           packRaw(1, { ent("sub/ok..name.txt", "x") }), kSealArcOk, 1, Rel::Parity, "");
    acheck("A9  a real '..' component is refused",
           packRaw(1, { ent("..", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A10 ../evil.txt is refused",
           packRaw(1, { ent("../evil.txt", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A11 sub/../../x is refused",
           packRaw(1, { ent("sub/../../x", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A12 a backslash-spelled traversal is refused",
           packRaw(1, { ent("..\\evil.txt", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A13 rooted (leading backslash) is refused",
           packRaw(1, { ent("\\rooted.txt", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A14 rooted (leading slash) is refused",
           packRaw(1, { ent("/rooted.txt", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A15 drive-qualified is refused",
           packRaw(1, { ent("C:\\abs.txt", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");
    acheck("A16 an empty path is refused",
           packRaw(1, { ent("", "x") }), kSealArcUnsafePath, 0, Rel::Parity, "");

    printf("== archive: the colon (the one rule bytes and characters disagree on) ==\n");
    acheck("A17 an alternate-data-stream path is refused",
           packRaw(1, { ent("ab:c", "hidden") }), kSealArcUnsafePath, 0, Rel::Diverges,
           "the shipped guard only looked at position 1, so `ab:c` passed — and on NTFS that "
           "writes an ALTERNATE DATA STREAM `c` on a zero-length file `ab`, content a tree walk "
           "of the unsealed project never shows. The colon is now refused anywhere, which cannot "
           "cost a real file: ':' is illegal in a Windows filename and every archived path came "
           "from FindFirstFileW walking a Windows directory");
    acheck("A18 multi-byte first character, then a colon",
           packRaw(1, { ent("\xc3\xa9:foo", "x") }), kSealArcUnsafePath, 0, Rel::Parity,
           "");

    printf("== archive: lengths off disk (TRAPS A and B) ==\n");
    acheck("A19 archive shorter than the file count",
           Bytes{ 1, 0 }, kSealArcShort, 0, Rel::Parity, "");
    acheck("A20 file_count 5, one entry present",
           packRaw(5, { ent("a.txt", "x") }), kSealArcTrunc, 1, Rel::Diverges,
           "same divergence as A26 in its smallest form: the shipped extractor had already "
           "written a.txt before the second entry ran off the end of the archive, so a refused "
           "unseal still left a file behind. We write nothing");
    {
        RawEnt e = ent("a.txt", "x"); e.declaredPathLen = 1000;
        acheck("A21 path_len longer than the archive", packRaw(1, { e }), kSealArcTrunc, 0, Rel::Parity, "");
    }
    {
        RawEnt e = ent("a.txt", "x"); e.declaredDataLen = 1000;
        acheck("A22 data_len longer than the archive", packRaw(1, { e }), kSealArcTrunc, 0, Rel::Parity, "");
    }
    {
        RawEnt e = ent("a.txt", "x"); e.declaredDataLen = 0xFFFFFFFFFFFFFFFFULL;
        acheck("A23 data_len = 18446744073709551615", packRaw(1, { e }), kSealArcDataLen, 0, Rel::Parity,
               "");
    }
    {
        RawEnt e = ent("a.txt", "x"); e.declaredDataLen = 0x8000000000000000ULL;
        acheck("A24 data_len = 2^63 exactly", packRaw(1, { e }), kSealArcDataLen, 0, Rel::Parity, "");
    }
    {
        // 2^63-1 still reads as a length; it simply does not fit in the archive.
        RawEnt e = ent("a.txt", "x"); e.declaredDataLen = 0x7FFFFFFFFFFFFFFFULL;
        acheck("A25 data_len = 2^63-1 (a length, just too big)", packRaw(1, { e }),
               kSealArcTrunc, 0, Rel::Parity, "");
    }

    printf("== archive: nothing is written until everything is checked ==\n");
    acheck("A26 good files, then a traversal",
           packRaw(4, { ent("keep1.txt", "aaa"), ent("keep2.txt", "bbb"),
                        ent("sub/keep3.txt", "ccc"), ent("../evil.txt", "OWNED") }),
           kSealArcUnsafePath, 3, Rel::Diverges,
           "the shipped extractor parsed and wrote in one loop, so it had already dropped "
           "keep1/keep2/sub/keep3 into the destination before it refused the fourth entry — and "
           "MainWindow's failure cleanup is RemoveDirectoryW, which only removes an EMPTY "
           "directory, so they stayed. The parse now validates the whole index first, so a "
           "rejected archive writes nothing at all");

    rmTree(gBase);
    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
