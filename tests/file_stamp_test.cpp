// SPDX-License-Identifier: GPL-3.0-or-later
// file_stamp_test — src/core/FileStamp.h, the "did this file change underneath the
// editor?" question, on real files on disk.
//
// Build+run:  cmake --build build --target file_stamp_test && build\file_stamp_test.exe
// or via ctest:  ctest --test-dir build -R file_stamp
//
// THE FOUR ASSERTIONS THIS FILE EXISTS FOR, in the order they would hurt:
//
//   1. A REWRITE WITH IDENTICAL BYTES IS *NOT* A CHANGE (case 4). This is the entire
//      reason the stamp carries a digest instead of just (mtime, size): `git checkout` of
//      the branch you are already on, a formatter, a backup tool — all move the timestamp
//      without moving a byte. Report those and the user gets a prompt that is usually
//      wrong, which is how a prompt becomes something people dismiss unread. That would
//      break the one below, which is a real either/or.
//   2. A SAME-LENGTH EDIT *IS* A CHANGE (case 3). The cheap filter alone would miss it
//      only if mtime were also unmoved, but the digest is what makes the answer independent
//      of a filesystem's timestamp resolution — and this repo lives on a network share.
//   3. THE DIGEST IS THE SAME ACROSS THE CHUNK BOUNDARY (case 7). stampFile reads in 64 KiB
//      chunks; a seed reset or a mis-carried accumulator between chunks would make every
//      file over 64 KiB compare unequal to itself, i.e. an unstoppable reload prompt on
//      exactly the large files where a spurious reload costs the most.
//   4. A FILE CAUGHT MID-WRITE IS "Unreadable", NOT A NEW STAMP (case 6). If a locked file
//      classified as unchanged, the change would never be reported at all — the caller is
//      required not to store such a stamp, and the signal it needs for that is `ok`.
#include "core/FileStamp.h"

#include <windows.h>
#include <cstdio>
#include <string>

using namespace sentinelide;

static int gPass = 0, gFail = 0;
static void check(bool cond, const char* what) {
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) gPass++; else gFail++;
}

static std::wstring gDir;
static std::wstring p(const wchar_t* name) { return gDir + L"\\" + name; }

static bool put(const std::wstring& path, const std::string& bytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    const BOOL ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
    CloseHandle(f);
    return ok && wrote == bytes.size();
}

// Timestamps on this repo's own drive are a network share's, so "did the mtime move" is not
// something to leave to luck: set it explicitly. This also lets a case pin the interesting
// half — identical bytes under a NEW mtime, and different bytes under the SAME one.
static bool setMtime(const std::wstring& path, ULONGLONG hundredNs) {
    HANDLE f = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    FILETIME ft{ (DWORD)(hundredNs & 0xFFFFFFFFULL), (DWORD)(hundredNs >> 32) };
    const BOOL ok = SetFileTime(f, nullptr, nullptr, &ft);
    CloseHandle(f);
    return ok != 0;
}

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    gDir = std::wstring(tmp) + L"sentinelide_stamp_test";
    RemoveDirectoryW(gDir.c_str());
    if (!CreateDirectoryW(gDir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        printf("could not create %ls\n", gDir.c_str());
        return 1;
    }

    printf("\n1. a file that is not there\n");
    {
        const std::wstring miss = p(L"nope.txt");
        DeleteFileW(miss.c_str());
        bool ok = false;
        const FileStamp s = stampFile(miss, &ok);
        check(!s.exists, "stampFile reports exists = false");
        check(ok, "...and ok stays true - absent is an answer, not a failure to read");
        check(changedFrom(FileStamp{}, s, ok) == DiskChange::None, "held-absent vs still-absent is None");
    }

    printf("\n2. an untouched file\n");
    {
        const std::wstring f = p(L"a.txt");
        check(put(f, "hello world\r\n"), "wrote the file");
        bool ok = false;
        const FileStamp held = stampFile(f, &ok);
        check(held.exists && ok, "stamped it");
        check(held.size == 13, "size is the byte count, not a character count");
        check(!mightHaveChanged(held, statFile(f)), "the cheap filter says there is nothing to read");
        check(changedFrom(held, stampFile(f, &ok), ok) == DiskChange::None, "and the full compare agrees");
    }

    printf("\n3. an edit that keeps the length\n");
    {
        const std::wstring f = p(L"same-length.txt");
        check(put(f, "aaaa"), "wrote 4 bytes");
        check(setMtime(f, 132000000000000000ULL), "pinned its mtime");
        bool ok = false;
        const FileStamp held = stampFile(f, &ok);
        check(put(f, "aaab"), "rewrote 4 DIFFERENT bytes");
        check(setMtime(f, 132000000000000000ULL), "...under the SAME mtime, so only content differs");
        const FileStamp now = stampFile(f, &ok);
        check(now.size == held.size, "the sizes are equal, as intended");
        check(CompareFileTime(&now.mtime, &held.mtime) == 0, "and so are the timestamps");
        check(changedFrom(held, now, ok) == DiskChange::Modified,
              "Modified anyway - the digest is what answers, not the stat pair");
    }

    printf("\n4. a rewrite with IDENTICAL bytes (the false positive this exists to kill)\n");
    {
        const std::wstring f = p(L"rewritten.txt");
        check(put(f, "unchanged content\r\n"), "wrote the file");
        check(setMtime(f, 132000000000000000ULL), "pinned an old mtime");
        bool ok = false;
        const FileStamp held = stampFile(f, &ok);
        check(put(f, "unchanged content\r\n"), "rewrote the very same bytes");
        check(setMtime(f, 133000000000000000ULL), "...with a NEWER mtime, as a checkout would");
        const FileStamp now = stampFile(f, &ok);
        check(mightHaveChanged(held, statFile(f)),
              "the cheap filter DOES fire - the timestamp really moved");
        check(changedFrom(held, now, ok) == DiskChange::None,
              "but the answer is None: nobody changed a byte, so nobody is prompted");
    }

    printf("\n5. deletion, appearance, and growth\n");
    {
        const std::wstring f = p(L"gone.txt");
        check(put(f, "here"), "wrote the file");
        bool ok = false;
        const FileStamp held = stampFile(f, &ok);
        check(DeleteFileW(f.c_str()) != 0, "deleted it");
        check(changedFrom(held, stampFile(f, &ok), ok) == DiskChange::Deleted, "Deleted");

        check(put(f, "back"), "wrote it again");
        check(changedFrom(FileStamp{}, stampFile(f, &ok), ok) == DiskChange::Modified,
              "a file appearing where none was held is Modified - the buffer is stale either way");

        const FileStamp four = stampFile(f, &ok);
        check(put(f, "back and then some"), "appended to it");
        const FileStamp more = stampFile(f, &ok);
        check(more.size > four.size, "it grew");
        check(changedFrom(four, more, ok) == DiskChange::Modified, "Modified");
    }

    printf("\n6. a file held open exclusively, i.e. one being written right now\n");
    {
        const std::wstring f = p(L"locked.txt");
        check(put(f, "before"), "wrote the file");
        bool ok = false;
        const FileStamp held = stampFile(f, &ok);
        HANDLE lock = CreateFileW(f.c_str(), GENERIC_WRITE, 0 /* no sharing */, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(lock != INVALID_HANDLE_VALUE, "took an exclusive handle on it");
        bool okLocked = true;
        const FileStamp now = stampFile(f, &okLocked);
        check(!okLocked, "stampFile reports ok = false rather than a stamp it could not read");
        check(now.exists, "...while still reporting that the file EXISTS - the stat half worked");
        check(changedFrom(held, now, okLocked) == DiskChange::Unreadable,
              "Unreadable, not None - classifying it as unchanged would hide the real change");
        CloseHandle(lock);
        check(changedFrom(held, stampFile(f, &ok), ok) == DiskChange::None,
              "and once the handle is gone the same file reads as unchanged");
    }

    printf("\n7. the 64 KiB chunk boundary\n");
    {
        // stampFile reads in 64 KiB chunks. Straddle the boundary with a payload whose bytes
        // vary, so a chunk digested with a reset seed or in the wrong order cannot coincide.
        std::string big;
        big.reserve(200000);
        for (int i = 0; i < 200000; i++) big.push_back((char)(i * 31 + (i >> 8)));
        const std::wstring f = p(L"big.bin");
        check(put(f, big), "wrote 200,000 bytes (>3 chunks)");
        bool ok = false;
        const FileStamp s = stampFile(f, &ok);
        check(s.size == big.size(), "the digest loop counted every byte");
        check(s.digest == fnv1a64(big.data(), big.size()),
              "chunked digest == one-shot fnv1a64 over the same bytes");

        // One byte, changed on the far side of the first boundary.
        std::string bumped = big;
        bumped[70000] = (char)(bumped[70000] ^ 0x01);
        check(put(f, bumped), "flipped ONE bit at offset 70,000 (past the first chunk)");
        const FileStamp now = stampFile(f, &ok);
        check(now.size == s.size, "the length is unchanged");
        check(changedFrom(s, now, ok) == DiskChange::Modified, "and it is still reported Modified");
    }

    printf("\n8. an empty file is a file\n");
    {
        const std::wstring f = p(L"empty.txt");
        check(put(f, ""), "wrote 0 bytes");
        bool ok = false;
        const FileStamp s = stampFile(f, &ok);
        check(s.exists && ok, "it exists and read fine");
        check(s.size == 0, "size 0");
        check(s.digest == kFnvSeed, "digest is the bare seed - an empty file is not an absent one");
        check(changedFrom(FileStamp{}, s, ok) == DiskChange::Modified,
              "so absent -> empty is still a change");
        check(put(f, "x"), "then wrote a byte into it");
        check(changedFrom(s, stampFile(f, &ok), ok) == DiskChange::Modified, "empty -> non-empty is Modified");
    }

    // Tidy up: leaving files in %TEMP% named after this test is how a later run picks up a
    // stale one and passes for the wrong reason.
    for (const wchar_t* n : { L"a.txt", L"same-length.txt", L"rewritten.txt", L"gone.txt",
                              L"locked.txt", L"big.bin", L"empty.txt" })
        DeleteFileW(p(n).c_str());
    RemoveDirectoryW(gDir.c_str());

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
