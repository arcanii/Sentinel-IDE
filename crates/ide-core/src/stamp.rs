// SPDX-License-Identifier: GPL-3.0-or-later
//! External-change detection — "did this file change underneath the editor?". FileStamp.h
//! ported whole: its header already argued the question was portable and only the
//! syscalls were not, and that turned out to be exactly right.
//!
//! The reasoning, kept short here because FileStamp.h states it in full:
//!
//! * **(mtime, size) is the cheap filter; the digest is the answer.** `git checkout` of
//!   the branch you are on, a formatter that rewrites identical bytes, a backup tool
//!   touching timestamps — all move mtime without moving a byte, and a prompt that is
//!   usually wrong is one users learn to dismiss.
//! * **The digest is over disk bytes, never the editor's text**, so nothing here has to
//!   know how the editor spells a newline.
//! * **FNV-1a is not a security claim.** A collision means one external edit goes
//!   unnoticed — no worse than no detection at all. Signatures live in `signing`.

use std::fs::{self, File};
use std::io::{self, Read};
use std::path::Path;
use std::time::SystemTime;

/// What a file looked like the last time we read or wrote it. `digest` is 0 when the file
/// did not exist, and is only meaningful alongside `size`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct FileStamp {
    pub exists: bool,
    pub mtime: Option<SystemTime>,
    pub size: u64,
    pub digest: u64,
}

/// How a file on disk now differs from the stamp we hold for it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiskChange {
    /// Same content — nothing to tell the user.
    None,
    /// The bytes differ from what we last read or wrote.
    Modified,
    /// It was there and is not any more.
    Deleted,
    /// It exists but could not be read — ask again later.
    Unreadable,
}

pub const FNV_SEED: u64 = 1_469_598_103_934_665_603;

/// FNV-1a folded over one chunk. Split from the seed so the file digest can be built
/// incrementally and a test can compute the same value over a buffer — one primitive,
/// so the two cannot drift.
pub fn fnv1a_update(mut h: u64, bytes: &[u8]) -> u64 {
    for &b in bytes {
        h ^= u64::from(b);
        h = h.wrapping_mul(1_099_511_628_211);
    }
    h
}

pub fn fnv1a64(bytes: &[u8]) -> u64 {
    fnv1a_update(FNV_SEED, bytes)
}

/// Cheap half: mtime + size without opening the file. `exists` false when it is gone.
pub fn stat_file(path: &Path) -> FileStamp {
    match fs::metadata(path) {
        Ok(m) => FileStamp { exists: true, mtime: m.modified().ok(), size: m.len(), digest: 0 },
        Err(_) => FileStamp::default(),
    }
}

/// Full stamp: stat plus a digest of the bytes. The `bool` is false when the file exists
/// but could not be read — callers MUST NOT store such a stamp, or a file caught mid-write
/// would be recorded as its own truncated self and the real change never reported.
pub fn stamp_file(path: &Path) -> (FileStamp, bool) {
    // mtime is taken BEFORE the reads. If another process writes while we read, we hold an
    // mtime older than the bytes we digested, so the next cheap filter fires and we re-read.
    // The other order would record the newer mtime beside a torn digest and the filter would
    // then say "unchanged" forever. Neither is atomic; this one fails toward asking again.
    let mut s = stat_file(path);
    if !s.exists {
        return (s, true);
    }
    match digest(path) {
        Ok((size, h)) => {
            s.size = size; // the read is the authority; the stat may predate a write we raced
            s.digest = h;
            (s, true)
        }
        Err(_) => (s, false),
    }
}

// In 64 KiB chunks rather than materialising the file: this runs on every window
// activation and the editor will happily open a multi-megabyte source.
fn digest(path: &Path) -> io::Result<(u64, u64)> {
    let mut f = File::open(path)?;
    let mut buf = vec![0u8; 64 * 1024];
    let (mut h, mut total) = (FNV_SEED, 0u64);
    loop {
        let n = match f.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        };
        h = fnv1a_update(h, &buf[..n]);
        total += n as u64;
    }
    Ok((total, h))
}

/// Compare a held stamp against the file as it is now. `now` must come from
/// [`stamp_file`] — a stat-only value has no digest and would always compare as Modified.
pub fn changed_from(held: &FileStamp, now: &FileStamp, read_ok: bool) -> DiskChange {
    if !held.exists && !now.exists {
        return DiskChange::None;
    }
    if held.exists && !now.exists {
        return DiskChange::Deleted;
    }
    if !read_ok {
        return DiskChange::Unreadable;
    }
    // A file that appeared where we recorded none is a change like any other: the buffer
    // no longer matches disk, which is the only thing the caller acts on.
    if !held.exists {
        return DiskChange::Modified;
    }
    if held.size == now.size && held.digest == now.digest {
        DiskChange::None
    } else {
        DiskChange::Modified
    }
}

/// Does the cheap filter say it is worth reading the file at all? False means the
/// timestamp AND length are both unmoved — the overwhelmingly common case.
pub fn might_have_changed(held: &FileStamp, stat_now: &FileStamp) -> bool {
    if held.exists != stat_now.exists {
        return true;
    }
    if !held.exists {
        return false;
    }
    held.size != stat_now.size || held.mtime != stat_now.mtime
}
