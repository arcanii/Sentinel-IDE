// SPDX-License-Identifier: GPL-3.0-or-later
// Seal format v2 tests — one case per defect v2 was introduced to fix, plus a
// backward-compatibility case proving pre-v2 files still open.
//
// Build+run:  cmake --build build --target seal_test && build\seal_test.exe
// Exit code is 0 only if every case passes, so it works as a CI gate.
//
// This deliberately reaches into Seal.h's internals (slot offsets, the v1 writer is
// reconstructed by hand in case 9) because the thing under test is an ON-DISK FORMAT.
// Testing it only through sealProject/unsealProject would pass just as happily if
// both sides were wrong in the same way.
#include "Seal.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace sentinelide;

static int gPass = 0, gFail = 0;
static void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

static Bytes pw(const char* s) { return Bytes((const uint8_t*)s, (const uint8_t*)s + strlen(s)); }

static void writeFile(const std::wstring& p, const std::string& data) {
    sealWriteBytes(p, (const uint8_t*)data.data(), data.size());
}
static std::string readFileStr(const std::wstring& p) {
    Bytes b; if (!sealReadBytes(p, b)) return "<missing>";
    return std::string((const char*)b.data(), b.size());
}

// Offsets in a v2 file.
static const size_t OFF_ARCHIVE_SIZE = 16;   // magic(8)+ver(4)+alg(4)
static const size_t OFF_SLOT_COUNT   = 24;
static const size_t OFF_SLOT0_TYPE   = 28;
static const size_t OFF_SLOT0_LEN    = 32;
static const size_t OFF_SLOT0_BODY   = 36;
static const size_t OFF_SLOT0_ITERS  = OFF_SLOT0_BODY + 16;

static void putU32At(Bytes& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + i] = (uint8_t)(v >> (8 * i));
}
static void putU64At(Bytes& b, size_t off, uint64_t v) {
    for (int i = 0; i < 8; i++) b[off + i] = (uint8_t)(v >> (8 * i));
}

int main() {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    const std::wstring base = std::wstring(tmp) + L"sealv2test";
    const std::wstring src  = base + L"\\src";
    const std::wstring out  = base + L"\\out.sealed";

    SHCreateDirectoryExW(nullptr, src.c_str(), nullptr);
    SHCreateDirectoryExW(nullptr, (src + L"\\sub").c_str(), nullptr);
    writeFile(src + L"\\a.sentinel", "fn main() -> i64 { 42 }\r\n");
    writeFile(src + L"\\notes..txt", "a legitimate name with two dots\r\n");   // v1 rejected this
    writeFile(src + L"\\sub\\b.txt", std::string(5000, 'x'));

    printf("== 1. round-trip ==\n");
    SealResult s = sealProject(src, out, pw("correct horse"));
    check(s.ok, "seal succeeds");
    if (!s.ok) { printf("     %ls\n", s.message.c_str()); return 1; }
    Bytes sealed; sealReadBytes(out, sealed);
    check(memcmp(sealed.data(), "SNTSEAL2", 8) == 0, "writes SNTSEAL2 magic");

    const std::wstring dst = base + L"\\unsealed";
    SealResult u = unsealProject(out, dst, pw("correct horse"));
    check(u.ok, "unseal succeeds");
    check(readFileStr(dst + L"\\a.sentinel") == "fn main() -> i64 { 42 }\r\n", "a.sentinel byte-identical");
    check(readFileStr(dst + L"\\sub\\b.txt") == std::string(5000, 'x'), "sub/b.txt byte-identical");
    check(readFileStr(dst + L"\\notes..txt") == "a legitimate name with two dots\r\n",
          "'notes..txt' survives (v1 rejected the whole archive)");

    printf("== 2. wrong password ==\n");
    SealResult w = unsealProject(out, base + L"\\wrong", pw("wrong password"));
    check(!w.ok, "wrong password rejected");

    printf("== 3. payload tamper ==\n");
    {
        Bytes t = sealed;
        t[t.size() - 32] ^= 0x01;                       // flip a bit inside the payload
        sealWriteBytes(base + L"\\tamper.sealed", t.data(), t.size());
        SealResult r = unsealProject(base + L"\\tamper.sealed", base + L"\\t1", pw("correct horse"));
        check(!r.ok, "payload bit-flip caught by GCM");
    }

    printf("== 4. header tamper (the v2 AAD property) ==\n");
    {
        Bytes t = sealed;
        putU64At(t, OFF_ARCHIVE_SIZE, 0x40000000ULL);   // claim a 1 GiB archive
        sealWriteBytes(base + L"\\hdr.sealed", t.data(), t.size());
        SealResult r = unsealProject(base + L"\\hdr.sealed", base + L"\\t2", pw("correct horse"));
        check(!r.ok, "tampered archive_size rejected (v1 would have honoured it)");
        check(r.message.find(L"authentication") != std::wstring::npos,
              "...and rejected as an AUTH failure, before any allocation");
    }

    printf("== 5. extra slot appended (LUKS property: no re-encryption) ==\n");
    {
        // Splice a second, unknown-type slot in after the password slot. The AAD
        // deliberately excludes the slot table, so the payload tag must still verify.
        Bytes t(sealed.begin(), sealed.begin() + OFF_SLOT0_BODY + kPasswordSlotLen);
        Bytes newSlot;
        putU32(newSlot, 99);              // unknown slot_type (pretend: TPM)
        putU32(newSlot, 12);              // slot_len
        for (int i = 0; i < 12; i++) newSlot.push_back((uint8_t)i);
        t.insert(t.end(), newSlot.begin(), newSlot.end());
        t.insert(t.end(), sealed.begin() + OFF_SLOT0_BODY + kPasswordSlotLen, sealed.end());
        putU32At(t, OFF_SLOT_COUNT, 2);
        sealWriteBytes(base + L"\\slot2.sealed", t.data(), t.size());
        SealResult r = unsealProject(base + L"\\slot2.sealed", base + L"\\t3", pw("correct horse"));
        check(r.ok, "unknown slot AFTER the password slot is skipped, payload still opens");
    }

    printf("== 6. unknown slot FIRST (v1's reader bug) ==\n");
    {
        // Unknown slot before the real one: the reader must step over it by slot_len
        // and keep going. v1 aborted here; and even on success v1 failed to advance
        // past trailing slots, so the payload read began mid-slot.
        Bytes t(sealed.begin(), sealed.begin() + OFF_SLOT0_TYPE);
        Bytes newSlot;
        putU32(newSlot, 99); putU32(newSlot, 12);
        for (int i = 0; i < 12; i++) newSlot.push_back((uint8_t)i);
        t.insert(t.end(), newSlot.begin(), newSlot.end());
        t.insert(t.end(), sealed.begin() + OFF_SLOT0_TYPE, sealed.end());
        putU32At(t, OFF_SLOT_COUNT, 2);
        sealWriteBytes(base + L"\\slot1.sealed", t.data(), t.size());
        SealResult r = unsealProject(base + L"\\slot1.sealed", base + L"\\t4", pw("correct horse"));
        check(r.ok, "unknown slot BEFORE the password slot is skipped, payload still opens");
    }

    printf("== 7. absurd iteration count ==\n");
    {
        Bytes t = sealed;
        putU64At(t, OFF_SLOT0_ITERS, 0xFFFFFFFFFFFFFFFFULL);
        sealWriteBytes(base + L"\\iters.sealed", t.data(), t.size());
        DWORD t0 = GetTickCount();
        SealResult r = unsealProject(base + L"\\iters.sealed", base + L"\\t5", pw("correct horse"));
        DWORD ms = GetTickCount() - t0;
        check(!r.ok, "u64-max iters rejected");
        check(ms < 2000, "...and rejected promptly rather than executed");
        printf("     (elapsed %lu ms)\n", ms);
    }

    printf("== 8. path traversal ==\n");
    {
        check(sealUnsafeRelPath(L"..\\evil.txt"),        "rejects ..\\evil.txt");
        check(sealUnsafeRelPath(L"sub\\..\\..\\x"),      "rejects sub\\..\\..\\x");
        check(sealUnsafeRelPath(L"C:\\abs.txt"),         "rejects drive-absolute");
        check(sealUnsafeRelPath(L"\\rooted.txt"),        "rejects rooted");
        check(!sealUnsafeRelPath(L"notes..txt"),         "ALLOWS notes..txt");
        check(!sealUnsafeRelPath(L"v1..2.md"),           "ALLOWS v1..2.md");
        check(!sealUnsafeRelPath(L"sub\\ok..name.txt"),  "ALLOWS sub\\ok..name.txt");
        check(!sealUnsafeRelPath(L"a\\b\\c.txt"),        "ALLOWS a\\b\\c.txt");
    }

    printf("== 9. v1 backward compatibility ==\n");
    {
        // Build a genuine v1 file the way the old writer did: SNTSEAL1, no slot_len,
        // and NO AAD on the payload. Proves the claim that pre-v2 seals still open.
        std::vector<std::pair<std::wstring, Bytes>> files;
        sealCollect(src, L"", files);
        Bytes archive = sealPackArchive(files);
        uint64_t archiveSize = archive.size();
        Bytes comp; sealCompress(archive, comp);

        uint8_t dek[32], pn[12], pt[16], salt[16], kek[32], wn[12], wt[16];
        sealRng(dek, 32); sealRng(pn, 12); sealRng(salt, 16); sealRng(wn, 12);
        Bytes payload; sealAesGcm(true, dek, pn, comp, payload, pt);          // <-- no AAD
        sealPbkdf2(pw("v1 pass"), salt, 16, kSealPbkdf2Iters, kek, 32);
        Bytes dekIn(dek, dek + 32), wrapped;
        sealAesGcm(true, kek, wn, dekIn, wrapped, wt);

        Bytes v1;
        const char m1[8] = { 'S','N','T','S','E','A','L','1' };
        v1.insert(v1.end(), m1, m1 + 8);
        putU32(v1, 1); putU32(v1, 1); putU64(v1, archiveSize);
        putU32(v1, 1);                       // slot count
        putU32(v1, 1);                       // slot type — note: NO slot_len in v1
        v1.insert(v1.end(), salt, salt + 16);
        putU64(v1, kSealPbkdf2Iters);
        v1.insert(v1.end(), wn, wn + 12);
        v1.insert(v1.end(), wrapped.begin(), wrapped.end());
        v1.insert(v1.end(), wt, wt + 16);
        v1.insert(v1.end(), pn, pn + 12);
        putU64(v1, payload.size());
        v1.insert(v1.end(), payload.begin(), payload.end());
        v1.insert(v1.end(), pt, pt + 16);
        sealWriteBytes(base + L"\\legacy.sealed", v1.data(), v1.size());

        SealResult r = unsealProject(base + L"\\legacy.sealed", base + L"\\t6", pw("v1 pass"));
        check(r.ok, "a v1 (SNTSEAL1) file still unseals");
        check(readFileStr(base + L"\\t6\\a.sentinel") == "fn main() -> i64 { 42 }\r\n",
              "...with byte-identical contents");
        SealResult rb = unsealProject(base + L"\\legacy.sealed", base + L"\\t7", pw("nope"));
        check(!rb.ok, "...and still rejects a wrong password");
    }

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
