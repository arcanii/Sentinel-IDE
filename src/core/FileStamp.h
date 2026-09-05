// SPDX-License-Identifier: GPL-3.0-or-later
// SentinelIDE external-change detection — "did this file change underneath the
// editor?". Header-only, and deliberately in core/ rather than the Win32 host:
// the QUESTION is portable (mtime, size, content digest) even though the syscalls
// here are not, so a macOS/Linux host reimplements stampFile and keeps changedFrom.
//
// WHY A DIGEST AND NOT JUST (mtime, size). A stat pair answers "was this file
// written", not "is its content different", and the two diverge constantly in a
// source tree: `git checkout` of the branch you were already on, a formatter that
// rewrites identical bytes, a backup tool touching timestamps. Every one of those
// would raise a prompt about a file whose content nobody changed, and a prompt that
// is usually wrong is one users learn to dismiss without reading -- which is exactly
// the prompt this exists to be. So the stat pair is the CHEAP FILTER and the digest
// is the ANSWER: the common path is two syscalls and no read at all, and a file is
// only read when its timestamp or length already moved.
//
// WHY THE DIGEST IS OVER DISK BYTES, NOT THE EDITOR'S TEXT. MainWindow.cpp's
// editorText() returns the buffer with LONE '\r' line breaks while the file on disk
// holds CRLF -- its own comment says the two "are never compared against each other"
// -- so a comparison that crossed that boundary would report every file as changed.
// Reading both sides off disk sidesteps the question entirely; nothing here has to
// know how the editor spells a newline.
//
// FNV-1a IS NOT A SECURITY CLAIM, and in a project that ships signature verification
// that is worth saying out loud. Nothing adversarial is being resisted: a collision
// means one external edit goes unnoticed, which is precisely today's behaviour with
// no detection at all, so the failure mode is "no worse than before" rather than
// "trusted the wrong bytes". The signing path (core/Signing.h) is where real digests
// live and it is untouched by this.
#pragma once
#include <windows.h>
#include <string>

namespace sentinelide {

// What a file looked like the last time we read or wrote it. `digest` is 0 when the
// file did not exist, and is only meaningful alongside `size`.
struct FileStamp {
    bool          exists = false;
    FILETIME      mtime{};
    unsigned long long size = 0;
    unsigned long long digest = 0;
};

// How a file on disk now differs from the stamp we hold for it.
enum class DiskChange {
    None,        // same content -- nothing to tell the user
    Modified,    // the bytes differ from what we last read or wrote
    Deleted,     // it was there and is not any more
    Unreadable,  // it exists but could not be opened (mid-write, locked) -- ask again later
};

constexpr unsigned long long kFnvSeed = 1469598103934665603ULL;

// FNV-1a folded over one chunk. Split from the seed so the file digest can be built
// incrementally and a test can compute the same value over a buffer without reading a
// file -- one primitive, so the two can't drift.
inline unsigned long long fnv1aUpdate(unsigned long long h, const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ULL; }
    return h;
}
inline unsigned long long fnv1a64(const char* p, size_t n) { return fnv1aUpdate(kFnvSeed, p, n); }

// Cheap half: mtime + size without opening the file. `exists` false when it is gone.
// GetFileAttributesExW does not open a handle, so this cannot fail on a file another
// process holds for writing -- which is the whole reason the check starts here.
inline FileStamp statFile(const std::wstring& path) {
    FileStamp s;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return s;
    s.exists = true;
    s.mtime  = fad.ftLastWriteTime;
    s.size   = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return s;
}

// Full stamp: stat plus a digest of the bytes. `ok` is false when the file exists but
// could not be read -- callers MUST NOT store such a stamp, or a file caught mid-write
// would be recorded as its own truncated self and the real change never reported.
inline FileStamp stampFile(const std::wstring& path, bool* ok = nullptr) {
    FileStamp s = statFile(path);
    if (ok) *ok = true;
    if (!s.exists) return s;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { if (ok) *ok = false; return s; }
    // Digest in chunks rather than materialising the file: the editor is happy to open a
    // multi-megabyte source and this runs every time the window is activated.
    //
    // NOTE THE ORDER: mtime was taken by statFile ABOVE, before these reads. If another
    // process writes the file while we are reading it, we end up holding an mtime older
    // than the bytes we digested -- so the next check's cheap filter fires again and we
    // re-read. The other order (stat after read) would record the NEWER mtime beside a
    // torn digest, and the cheap filter would then say "unchanged" forever. Neither
    // ordering is atomic; this one fails toward asking again.
    char buf[64 * 1024];
    unsigned long long h = kFnvSeed, total = 0;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf, sizeof(buf), &got, nullptr)) { CloseHandle(f); if (ok) *ok = false; return s; }
        if (got == 0) break;
        h = fnv1aUpdate(h, buf, got);
        total += got;
    }
    CloseHandle(f);
    s.size = total;   // the read is the authority; the stat may predate a write we raced
    s.digest = h;
    return s;
}

// Compare a held stamp against the file as it is now. `now` must come from stampFile
// (a statFile-only value has no digest and would always compare as Modified).
inline DiskChange changedFrom(const FileStamp& held, const FileStamp& now, bool readOk) {
    if (!held.exists && !now.exists) return DiskChange::None;
    if (held.exists && !now.exists)  return DiskChange::Deleted;
    if (!readOk)                     return DiskChange::Unreadable;
    // A file that appeared where we recorded none is a change like any other: the buffer
    // no longer matches disk, which is the only thing the caller acts on.
    if (!held.exists)                return DiskChange::Modified;
    return (held.size == now.size && held.digest == now.digest) ? DiskChange::None : DiskChange::Modified;
}

// Does the cheap filter say it is worth reading the file at all? False means the
// timestamp AND length are both unmoved, which for a real editor writing real files
// is the overwhelmingly common case.
inline bool mightHaveChanged(const FileStamp& held, const FileStamp& statNow) {
    if (held.exists != statNow.exists) return true;
    if (!held.exists) return false;
    return held.size != statNow.size ||
           CompareFileTime(&held.mtime, &statNow.mtime) != 0;
}

}  // namespace sentinelide
