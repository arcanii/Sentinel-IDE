// SPDX-License-Identifier: GPL-3.0-or-later
//
// file_stamp — the Rust port of tests/file_stamp_test.cpp: the "did this file change
// underneath the editor?" question, on real files on disk.
//
// THE FOUR ASSERTIONS THIS FILE EXISTS FOR, in the order they would hurt:
//
//   1. A REWRITE WITH IDENTICAL BYTES IS *NOT* A CHANGE (case 4) — `git checkout` of the
//      branch you are on moves the timestamp without moving a byte.
//   2. A SAME-LENGTH EDIT *IS* A CHANGE (case 3) — the digest is what makes the answer
//      independent of a filesystem's timestamp resolution.
//   3. THE DIGEST IS THE SAME ACROSS THE CHUNK BOUNDARY (case 7) — a mis-carried accumulator
//      between 64 KiB chunks would make every large file compare unequal to itself.
//   4. A FILE THAT CANNOT BE READ IS "Unreadable", NOT A NEW STAMP (case 6).
//
// Case 6 is the one that changed. On Windows it holds the file open with no sharing — a
// writer mid-save — and stampFile cannot open it. macOS has no mandatory locks, so an open
// file is always readable; the nearest real state that fails the read is a permission
// denial, which is what a file mid-replacement under a stricter umask looks like. It tests
// the same contract: the stat half still says the file exists, the read half says not ok.

use std::fs::{self, File};
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use ide_core::stamp::{
    changed_from, fnv1a64, might_have_changed, stamp_file, stat_file, DiskChange, FileStamp, FNV_SEED,
};

struct Run {
    pass: usize,
    fail: usize,
}

impl Run {
    fn check(&mut self, cond: bool, what: &str) {
        println!("  [{}] {what}", if cond { "PASS" } else { "FAIL" });
        if cond {
            self.pass += 1;
        } else {
            self.fail += 1;
        }
    }
}

fn put(path: &Path, bytes: &[u8]) -> bool {
    fs::write(path, bytes).is_ok()
}

// Set the mtime explicitly, so a case can pin the interesting half — identical bytes under
// a NEW mtime, and different bytes under the SAME one — instead of leaving it to the clock.
fn set_mtime(path: &Path, t: SystemTime) -> bool {
    File::options().write(true).open(path).and_then(|f| f.set_modified(t)).is_ok()
}

#[test]
fn file_stamp() {
    let dir: PathBuf = std::env::temp_dir().join(format!("sentinelide_stamp_test_{}", std::process::id()));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    let p = |n: &str| dir.join(n);
    let old = UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let newer = UNIX_EPOCH + Duration::from_secs(1_800_000_000);
    let mut r = Run { pass: 0, fail: 0 };

    println!("\n1. a file that is not there");
    {
        let (s, ok) = stamp_file(&p("nope.txt"));
        r.check(!s.exists, "stamp_file reports exists = false");
        r.check(ok, "...and ok stays true - absent is an answer, not a failure to read");
        r.check(
            changed_from(&FileStamp::default(), &s, ok) == DiskChange::None,
            "held-absent vs still-absent is None",
        );
    }

    println!("\n2. an untouched file");
    {
        let f = p("a.txt");
        r.check(put(&f, b"hello world\r\n"), "wrote the file");
        let (held, ok) = stamp_file(&f);
        r.check(held.exists && ok, "stamped it");
        r.check(held.size == 13, "size is the byte count, not a character count");
        r.check(!might_have_changed(&held, &stat_file(&f)), "the cheap filter says there is nothing to read");
        let (now, ok) = stamp_file(&f);
        r.check(changed_from(&held, &now, ok) == DiskChange::None, "and the full compare agrees");
    }

    println!("\n3. an edit that keeps the length");
    {
        let f = p("same-length.txt");
        r.check(put(&f, b"aaaa"), "wrote 4 bytes");
        r.check(set_mtime(&f, old), "pinned its mtime");
        let (held, _) = stamp_file(&f);
        r.check(put(&f, b"aaab"), "rewrote 4 DIFFERENT bytes");
        r.check(set_mtime(&f, old), "...under the SAME mtime, so only content differs");
        let (now, ok) = stamp_file(&f);
        r.check(now.size == held.size, "the sizes are equal, as intended");
        r.check(now.mtime == held.mtime, "and so are the timestamps");
        r.check(
            changed_from(&held, &now, ok) == DiskChange::Modified,
            "Modified anyway - the digest is what answers, not the stat pair",
        );
    }

    println!("\n4. a rewrite with IDENTICAL bytes (the false positive this exists to kill)");
    {
        let f = p("rewritten.txt");
        r.check(put(&f, b"unchanged content\r\n"), "wrote the file");
        r.check(set_mtime(&f, old), "pinned an old mtime");
        let (held, _) = stamp_file(&f);
        r.check(put(&f, b"unchanged content\r\n"), "rewrote the very same bytes");
        r.check(set_mtime(&f, newer), "...with a NEWER mtime, as a checkout would");
        let (now, ok) = stamp_file(&f);
        r.check(
            might_have_changed(&held, &stat_file(&f)),
            "the cheap filter DOES fire - the timestamp really moved",
        );
        r.check(
            changed_from(&held, &now, ok) == DiskChange::None,
            "but the answer is None: nobody changed a byte, so nobody is prompted",
        );
    }

    println!("\n5. deletion, appearance, and growth");
    {
        let f = p("gone.txt");
        r.check(put(&f, b"here"), "wrote the file");
        let (held, _) = stamp_file(&f);
        r.check(fs::remove_file(&f).is_ok(), "deleted it");
        let (now, ok) = stamp_file(&f);
        r.check(changed_from(&held, &now, ok) == DiskChange::Deleted, "Deleted");

        r.check(put(&f, b"back"), "wrote it again");
        let (now, ok) = stamp_file(&f);
        r.check(
            changed_from(&FileStamp::default(), &now, ok) == DiskChange::Modified,
            "a file appearing where none was held is Modified - the buffer is stale either way",
        );

        let (four, _) = stamp_file(&f);
        r.check(put(&f, b"back and then some"), "appended to it");
        let (more, ok) = stamp_file(&f);
        r.check(more.size > four.size, "it grew");
        r.check(changed_from(&four, &more, ok) == DiskChange::Modified, "Modified");
    }

    println!("\n6. a file that exists but cannot be read (see the header: macOS has no mandatory locks)");
    {
        let f = p("locked.txt");
        r.check(put(&f, b"before"), "wrote the file");
        let (held, _) = stamp_file(&f);
        r.check(
            fs::set_permissions(&f, fs::Permissions::from_mode(0o000)).is_ok(),
            "took away every permission",
        );
        let (now, ok_locked) = stamp_file(&f);
        r.check(!ok_locked, "stamp_file reports ok = false rather than a stamp it could not read");
        r.check(now.exists, "...while still reporting that the file EXISTS - the stat half worked");
        r.check(
            changed_from(&held, &now, ok_locked) == DiskChange::Unreadable,
            "Unreadable, not None - classifying it as unchanged would hide the real change",
        );
        r.check(fs::set_permissions(&f, fs::Permissions::from_mode(0o644)).is_ok(), "gave them back");
        let (now, ok) = stamp_file(&f);
        r.check(
            changed_from(&held, &now, ok) == DiskChange::None,
            "and once readable the same file reads as unchanged",
        );
    }

    println!("\n7. the 64 KiB chunk boundary");
    {
        // Bytes that vary, so a chunk digested with a reset seed or in the wrong order
        // cannot coincide. The same generator as the C++ (`(char)(i * 31 + (i >> 8))`).
        let big: Vec<u8> = (0..200_000u32).map(|i| (i.wrapping_mul(31).wrapping_add(i >> 8)) as u8).collect();
        let f = p("big.bin");
        r.check(put(&f, &big), "wrote 200,000 bytes (>3 chunks)");
        let (s, _) = stamp_file(&f);
        r.check(s.size == big.len() as u64, "the digest loop counted every byte");
        r.check(s.digest == fnv1a64(&big), "chunked digest == one-shot fnv1a64 over the same bytes");

        let mut bumped = big.clone();
        bumped[70_000] ^= 0x01;
        r.check(put(&f, &bumped), "flipped ONE bit at offset 70,000 (past the first chunk)");
        let (now, ok) = stamp_file(&f);
        r.check(now.size == s.size, "the length is unchanged");
        r.check(changed_from(&s, &now, ok) == DiskChange::Modified, "and it is still reported Modified");
    }

    println!("\n8. an empty file is a file");
    {
        let f = p("empty.txt");
        r.check(put(&f, b""), "wrote 0 bytes");
        let (s, ok) = stamp_file(&f);
        r.check(s.exists && ok, "it exists and read fine");
        r.check(s.size == 0, "size 0");
        r.check(s.digest == FNV_SEED, "digest is the bare seed - an empty file is not an absent one");
        r.check(
            changed_from(&FileStamp::default(), &s, ok) == DiskChange::Modified,
            "so absent -> empty is still a change",
        );
        r.check(put(&f, b"x"), "then wrote a byte into it");
        let (now, ok) = stamp_file(&f);
        r.check(changed_from(&s, &now, ok) == DiskChange::Modified, "empty -> non-empty is Modified");
    }

    // Tidy up: a stale file from an earlier run is how a later one passes for the wrong reason.
    fs::remove_dir_all(&dir).unwrap();
    println!("\n{} passed, {} failed", r.pass, r.fail);
    assert_eq!(r.fail, 0);
}
