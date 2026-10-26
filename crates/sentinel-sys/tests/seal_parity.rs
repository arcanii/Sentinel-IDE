// SPDX-License-Identifier: GPL-3.0-or-later
//
// seal_parity — the macOS port of tests/seal_xcheck.cpp, the cross-check for the
// SEALED-CONTAINER FRAMING (src/sentinel/parsers.sentinel::parse_seal_header /
// parse_seal_archive, which sentinel-sys builds into libsentinel_parsers.dylib). Same
// cases, same Parity/Diverges expectations, same reasons. What had to change for macOS
// is said where it changed; the one real change — the corpus — has its own section.
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
// back byte offsets; the host does every crypto call from those offsets. The crypto
// core stays out of Sentinel (docs/Sentinel-lang_request.md R1: no secure-zero for
// `[secret u8]`).
//
// THREE implementations are run against every case and all three are asserted:
//
//   oracle::    the C++ that shipped at 72a6b82 — unsealProject's parsing loop and
//               sealExtractArchive — as seal_xcheck.cpp restructured it, transcribed.
//               sealExtractArchive is VERBATIM but for the host calls (see "the host's
//               file I/O" below). The header parse records each slot's offsets where the
//               shipped loop ran PBKDF2, and sets a reason code where it set r.message;
//               every bound, every branch and their order are the shipped ones. Its gaps
//               are the measurement — do not "fix" anything in there.
//   fallback::  Seal.h's own C++ readSealHeader/readSealArchive — the `#else` branch, what
//               a snc-less Windows build compiles — transcribed. Seal.h itself cannot be
//               compiled here (windows.h, bcrypt.h, compressapi.h), so on macOS this pins
//               Sentinel against a faithful transcription; the C++ itself stays pinned by
//               seal_xcheck.exe on Windows. It must agree with Sentinel EVERYWHERE: they
//               are one behaviour with two spellings, and this is what pins them.
//   Sentinel    parse_seal_header / parse_seal_archive themselves, called through
//               sentinel-sys and decoded by ITS decoders — the ones the macOS host will
//               use. seal_xcheck.cpp kept a private copy of the record decoder so the test
//               could disagree with the header it checks; here the crate's decoder is part
//               of what is under test. It bounds-checks every read, so a malformed record
//               fails the case with the offset instead of being read out of range.
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
// Run (needs snc; see build.rs):
//     cargo test -p sentinel-sys --test seal_parity -- --nocapture
//
// Writes only under $TMPDIR/sealparity-<pid>, removed afterwards. Never touches examples/.

use std::fs;
use std::path::{Component, Path, PathBuf};
use std::sync::Mutex;
use std::time::Instant;

use sentinel_sys::SealArchiveReason as Arch;
use sentinel_sys::SealHeaderReason as Hdr;
use sentinel_sys::{
    parse_seal_archive, parse_seal_header, SealArchiveIndex, SealEntry, SealHeader, SealSlot,
};

// ---- Seal.h's format constants (src/core/Seal.h, the framing section), transcribed ----
const K_SEAL_VERSION1: u32 = 1; // legacy: no slot_len, no AAD (still read)
const K_SEAL_VERSION2: u32 = 2; // current writer
const K_AEAD_AES_GCM: u32 = 1; // aead_alg id (2 = ChaCha20-Poly1305, reserved)
const K_SLOT_PASSWORD: u32 = 1; // slot_type id
const K_PASSWORD_SLOT_LEN: u32 = 16 + 8 + 12 + 32 + 16; // salt|iters|nonce|wrapped|tag = 84
const K_MAX_UNLOCK_SLOTS: u32 = 64;
const K_SEAL_PBKDF2_ITERS: u64 = 600_000; // crypto policy, but sealProject writes it

// ---- little-endian (de)serialization, as Seal.h's putU32/putU64/getU32/getU64 ---------
fn put_u32(b: &mut Vec<u8>, v: u32) {
    b.extend_from_slice(&v.to_le_bytes());
}
fn put_u64(b: &mut Vec<u8>, v: u64) {
    b.extend_from_slice(&v.to_le_bytes());
}
fn get_u32(p: &[u8]) -> u32 {
    u32::from_le_bytes(p[..4].try_into().unwrap())
}
fn get_u64(p: &[u8]) -> u64 {
    u64::from_le_bytes(p[..8].try_into().unwrap())
}
fn put_u32_at(b: &mut [u8], off: usize, v: u32) {
    b[off..off + 4].copy_from_slice(&v.to_le_bytes());
}
fn put_u64_at(b: &mut [u8], off: usize, v: u64) {
    b[off..off + 8].copy_from_slice(&v.to_le_bytes());
}

// sealWide / sealUtf8. The C++ implementations run their guards on UTF-16, so the
// transcriptions do too: `p[1]` in the oracle's colon test is the second UTF-16 CODE
// UNIT, and A18 depends on exactly that. MultiByteToWideChar without
// MB_ERR_INVALID_CHARS and Rust's lossy decoder both map invalid UTF-8 to U+FFFD but can
// differ in how many a malformed run becomes; no case feeds invalid UTF-8, as no case in
// the C++ does.
fn seal_wide(p: &[u8]) -> Vec<u16> {
    String::from_utf8_lossy(p).encode_utf16().collect()
}
fn seal_utf8(w: &[u16]) -> String {
    String::from_utf16_lossy(w)
}
const BSLASH: u16 = b'\\' as u16;
const FSLASH: u16 = b'/' as u16;
const COLON: u16 = b':' as u16;
const DOTDOT: [u16; 2] = [b'.' as u16, b'.' as u16];

/// SealHeader's C++ default: `reason = kSealHdrBadMagic` and every number zero.
fn blank_header() -> SealHeader {
    SealHeader {
        reason: Hdr::BadMagic,
        version: 0,
        archive_size: 0,
        payload_nonce_off: 0,
        payload_off: 0,
        payload_len: 0,
        payload_tag_off: 0,
        slots: Vec::new(),
    }
}

// ---- the host's file I/O, on macOS ----------------------------------------------------
//
// `destRoot + L"\\" + rel`, SHCreateDirectoryExW on its parent, then sealWriteBytes: the
// three host calls every writer in this file makes. The archive's paths are `\`-separated
// on every platform; a macOS host materialises one by turning each `\` into `/` (for an
// index entry, by SealEntry::components), so that is what happens here.
//
// ONE THING THE C++ HARNESS DID NOT HAVE: a write that would land outside its destination
// — a `..` component or a rooted path surviving into the join — is NOT performed. It is
// recorded in ESCAPES and fails the case. On Windows the C++ would have written it
// wherever it pointed; nothing in this corpus reaches that branch on any implementation,
// so it changes no outcome. It only means a guard regression is reported here rather than
// scribbling somewhere above $TMPDIR — and on macOS a rooted path is the filesystem root,
// not the root of a drive.
static ESCAPES: Mutex<Vec<String>> = Mutex::new(Vec::new());

fn native_path<'a>(dest: &Path, components: impl Iterator<Item = &'a str>) -> Option<PathBuf> {
    let mapped = components.collect::<Vec<_>>().join("/");
    let tail = Path::new(&mapped);
    if tail.components().all(|c| matches!(c, Component::Normal(_) | Component::CurDir)) {
        Some(dest.join(tail))
    } else {
        ESCAPES.lock().unwrap().push(format!("{mapped:?} under {}", dest.display()));
        None
    }
}

/// SHCreateDirectoryExW(parent) + sealWriteBytes (CREATE_ALWAYS: create or truncate).
/// As in the C++, a directory that cannot be created is ignored and the write reports it.
fn write_at(full: &Path, data: &[u8]) -> bool {
    if let Some(parent) = full.parent() {
        let _ = fs::create_dir_all(parent);
    }
    fs::write(full, data).is_ok()
}

// ---------------------------------------------------------------------------------------
// The oracle: src/core/Seal.h at 72a6b82, as seal_xcheck.cpp restructured it.
// ---------------------------------------------------------------------------------------
mod oracle {
    use super::*;

    // unsealProject's parsing loop (Seal.h:316-360 at 72a6b82). Restructured only where it
    // had to be: the PBKDF2/GCM block becomes a record of the slot's offsets, and each
    // `r.message = L"…"; return r;` becomes the reason code that message now maps to.
    // `archive_size` is read and NOT range-checked, which is the shipped behaviour and the
    // thing cases H11/H12 measure.
    pub fn read_seal_header(f: &[u8]) -> SealHeader {
        let mut h = blank_header();
        let remaining = |pos: usize| f.len() - pos;
        let need = |pos: usize, n: usize| remaining(pos) >= n;

        if f.len() < 8 {
            h.reason = Hdr::BadMagic;
            return h;
        }
        let v2 = &f[..8] == b"SNTSEAL2";
        let v1 = &f[..8] == b"SNTSEAL1";
        if !v1 && !v2 {
            h.reason = Hdr::BadMagic;
            return h;
        }
        let mut pos = 8;
        if !need(pos, 20) {
            h.reason = Hdr::TruncHeader;
            return h;
        }
        h.version = get_u32(&f[pos..]);
        pos += 4;
        let alg = get_u32(&f[pos..]);
        pos += 4;
        let archive_size = get_u64(&f[pos..]); // <-- NOT range-checked. The gap.
        pos += 8;
        let slots = get_u32(&f[pos..]);
        pos += 4;
        if h.version != (if v2 { K_SEAL_VERSION2 } else { K_SEAL_VERSION1 }) || alg != K_AEAD_AES_GCM {
            h.reason = Hdr::BadVersion;
            return h;
        }
        // The shipped code held archive_size in a local until sealDecompress, so on the
        // version/alg path above it never escaped. Publishing it only here keeps the oracle
        // faithful to what a CALLER could see, not merely to the order of the statements.
        h.archive_size = archive_size;

        for _ in 0..slots {
            if !need(pos, if v2 { 8 } else { 4 }) {
                h.reason = Hdr::TruncSlots;
                return h;
            }
            let ty = get_u32(&f[pos..]);
            pos += 4;
            let mut slen = K_PASSWORD_SLOT_LEN;
            if v2 {
                slen = get_u32(&f[pos..]);
                pos += 4;
            } else if ty != K_SLOT_PASSWORD {
                h.reason = Hdr::SlotType;
                return h;
            }
            if !need(pos, slen as usize) {
                h.reason = Hdr::TruncSlotBody;
                return h;
            }
            h.slots.push(SealSlot { r#type: ty, body_off: pos, body_len: slen as usize });
            pos += slen as usize;
        }

        if !need(pos, 12 + 8) {
            h.reason = Hdr::TruncPayload;
            return h;
        }
        h.payload_nonce_off = pos;
        pos += 12;
        let plen = get_u64(&f[pos..]);
        pos += 8;
        if plen > remaining(pos) as u64 || remaining(pos) - (plen as usize) < 16 {
            h.reason = Hdr::TruncPayloadBody;
            return h;
        }
        h.payload_off = pos;
        h.payload_len = plen as usize;
        h.payload_tag_off = pos + plen as usize;
        h.reason = Hdr::Ok;
        h
    }

    // sealUnsafeRelPath + sealExtractArchive (Seal.h:205-241 at 72a6b82), VERBATIM. It
    // parses and WRITES in the same loop (cases A20/A26), and its colon test only looks at
    // position 1 (case A17).
    pub fn seal_unsafe_rel_path(p: &[u16]) -> bool {
        if p.is_empty() {
            return true;
        }
        if p[0] == BSLASH || p[0] == FSLASH {
            return true; // rooted
        }
        if p.len() > 1 && p[1] == COLON {
            return true; // drive-qualified
        }
        let mut start = 0;
        while start <= p.len() {
            let sep = p[start..].iter().position(|&c| c == BSLASH).map(|i| start + i);
            let end = sep.unwrap_or(p.len());
            if p[start..end] == DOTDOT {
                return true; // exactly ".."
            }
            match sep {
                None => break,
                Some(s) => start = s + 1,
            }
        }
        false
    }

    pub fn seal_extract_archive(a: &[u8], dest_root: &Path) -> bool {
        if a.len() < 4 {
            return false;
        }
        let mut pos = 0;
        let n = get_u32(&a[pos..]);
        pos += 4;
        for _ in 0..n {
            if a.len() - pos < 4 {
                return false;
            }
            let pl = get_u32(&a[pos..]) as usize;
            pos += 4;
            if a.len() - pos < pl {
                return false;
            }
            let mut rel = seal_wide(&a[pos..pos + pl]);
            pos += pl;
            if a.len() - pos < 8 {
                return false;
            }
            let dl = get_u64(&a[pos..]);
            pos += 8;
            if dl > (a.len() - pos) as u64 {
                return false;
            }
            for ch in rel.iter_mut() {
                if *ch == FSLASH {
                    *ch = BSLASH; // → native separators
                }
            }
            if seal_unsafe_rel_path(&rel) {
                return false;
            }
            // destRoot + L"\\" + rel → SHCreateDirectoryExW(parent) → sealWriteBytes. On
            // macOS each `\` becomes `/`; that is the whole of the adaptation.
            let rel8 = seal_utf8(&rel);
            let Some(full) = native_path(dest_root, rel8.split('\\')) else { return false };
            if !write_at(&full, &a[pos..pos + dl as usize]) {
                return false;
            }
            pos += dl as usize;
        }
        true
    }
}

// ---------------------------------------------------------------------------------------
// The C++ fallback: Seal.h's `#else` readSealHeader / readSealArchive / sealUnsafeRelPath.
// ---------------------------------------------------------------------------------------
mod fallback {
    use super::*;

    // Seal.h's sealUnsafeRelPath: the oracle's, with the colon refused ANYWHERE.
    pub fn seal_unsafe_rel_path(p: &[u16]) -> bool {
        if p.is_empty() {
            return true;
        }
        if p[0] == BSLASH || p[0] == FSLASH {
            return true; // rooted
        }
        if p.contains(&COLON) {
            return true; // drive-qualified / ADS
        }
        let mut start = 0;
        while start <= p.len() {
            let sep = p[start..].iter().position(|&c| c == BSLASH).map(|i| start + i);
            let end = sep.unwrap_or(p.len());
            if p[start..end] == DOTDOT {
                return true; // exactly ".."
            }
            match sep {
                None => break,
                Some(s) => start = s + 1,
            }
        }
        false
    }

    // The shipped parsing, restructured to hand back offsets, plus the refusals the port
    // added. Bounds are `remaining < want`, never `pos + want > size`: the lengths are u64
    // straight off disk, so the additive form can wrap and pass.
    #[allow(clippy::manual_clamp)] // the floor/cap are two ifs in Seal.h and in Sentinel
    pub fn read_seal_header(f: &[u8]) -> SealHeader {
        let mut h = blank_header();
        let remaining = |pos: usize| f.len() - pos;
        let need = |pos: usize, n: usize| remaining(pos) >= n;

        if f.len() < 8 {
            h.reason = Hdr::BadMagic;
            return h;
        }
        let v2 = &f[..8] == b"SNTSEAL2";
        let v1 = &f[..8] == b"SNTSEAL1";
        if !v1 && !v2 {
            h.reason = Hdr::BadMagic;
            return h;
        }
        let mut pos = 8;
        if !need(pos, 20) {
            h.reason = Hdr::TruncHeader;
            return h;
        }
        h.version = get_u32(&f[pos..]);
        pos += 4;
        let alg = get_u32(&f[pos..]);
        pos += 4;
        let archive_size = get_u64(&f[pos..]);
        pos += 8;
        let slots = get_u32(&f[pos..]);
        pos += 4;
        if h.version != (if v2 { K_SEAL_VERSION2 } else { K_SEAL_VERSION1 }) || alg != K_AEAD_AES_GCM {
            h.reason = Hdr::BadVersion;
            return h;
        }
        // The archive-size plausibility bound: refuse what does not fit an i64, and what
        // is out of step with the container it arrived in. The ratio does the work, the
        // floor keeps small real files working, the cap sits above any source tree.
        // Kept identical to parse_seal_header so the two stay comparable.
        const CAP_ABS: u64 = 2_147_483_648; // 2 GiB
        const CAP_FLOOR: u64 = 67_108_864; // 64 MiB whatever the ratio
        let mut allow = f.len() as u64 * 1024;
        if allow < CAP_FLOOR {
            allow = CAP_FLOOR;
        }
        if allow > CAP_ABS {
            allow = CAP_ABS;
        }
        if archive_size > i64::MAX as u64 || archive_size > allow {
            h.reason = Hdr::ArchiveSize;
            return h;
        }
        h.archive_size = archive_size;

        // The slot-count ceiling: a full PBKDF2 per password-type slot on a wrong password.
        if slots > K_MAX_UNLOCK_SLOTS {
            h.reason = Hdr::SlotCount;
            return h;
        }

        for _ in 0..slots {
            if !need(pos, if v2 { 8 } else { 4 }) {
                h.reason = Hdr::TruncSlots;
                return h;
            }
            let ty = get_u32(&f[pos..]);
            pos += 4;
            // v1 has no slot_len, so its only navigable slot type is the password one.
            let mut slen = K_PASSWORD_SLOT_LEN;
            if v2 {
                slen = get_u32(&f[pos..]);
                pos += 4;
            } else if ty != K_SLOT_PASSWORD {
                h.reason = Hdr::SlotType;
                return h;
            }
            if !need(pos, slen as usize) {
                h.reason = Hdr::TruncSlotBody;
                return h;
            }
            h.slots.push(SealSlot { r#type: ty, body_off: pos, body_len: slen as usize });
            pos += slen as usize; // ALWAYS advance, understood or not — v1 broke exactly here
        }

        if !need(pos, 12 + 8) {
            h.reason = Hdr::TruncPayload;
            return h;
        }
        h.payload_nonce_off = pos;
        pos += 12;
        let plen = get_u64(&f[pos..]);
        pos += 8;
        if plen > i64::MAX as u64 || plen > remaining(pos) as u64 || remaining(pos) - (plen as usize) < 16 {
            h.reason = Hdr::TruncPayloadBody;
            return h;
        }
        h.payload_off = pos;
        h.payload_len = plen as usize;
        h.payload_tag_off = pos + plen as usize;
        h.reason = Hdr::Ok;
        h
    }

    pub fn read_seal_archive(a: &[u8]) -> SealArchiveIndex {
        let mut idx = SealArchiveIndex { reason: Arch::Short, files: Vec::new() };
        if a.len() < 4 {
            idx.reason = Arch::Short;
            return idx;
        }
        let mut pos = 0;
        let n = get_u32(&a[pos..]);
        pos += 4;
        for _ in 0..n {
            if a.len() - pos < 4 {
                idx.reason = Arch::Trunc;
                return idx;
            }
            let pl = get_u32(&a[pos..]) as usize;
            pos += 4;
            if a.len() - pos < pl {
                idx.reason = Arch::Trunc;
                return idx;
            }
            let mut rel = seal_wide(&a[pos..pos + pl]);
            pos += pl;
            if a.len() - pos < 8 {
                idx.reason = Arch::Trunc;
                return idx;
            }
            let dl = get_u64(&a[pos..]);
            pos += 8;
            if dl > i64::MAX as u64 {
                idx.reason = Arch::DataLen;
                return idx;
            }
            if dl > (a.len() - pos) as u64 {
                idx.reason = Arch::Trunc;
                return idx;
            }
            for ch in rel.iter_mut() {
                if *ch == FSLASH {
                    *ch = BSLASH; // → native separators
                }
            }
            if seal_unsafe_rel_path(&rel) {
                idx.reason = Arch::UnsafePath;
                return idx;
            }
            idx.files.push(SealEntry { rel: seal_utf8(&rel), data_off: pos, data_len: dl as usize });
            pos += dl as usize;
        }
        idx.reason = Arch::Ok;
        idx
    }
}

// ---- harness --------------------------------------------------------------------------
#[derive(Clone, Copy, PartialEq, Eq)]
enum Rel {
    Parity,
    Diverges,
}

impl Rel {
    fn label(self) -> &'static str {
        match self {
            Rel::Parity => "(parity)",
            Rel::Diverges => "(DIVERGES)",
        }
    }
    fn opposite(self) -> &'static str {
        match self {
            Rel::Parity => "parity",
            Rel::Diverges => "a divergence",
        }
    }
}

// Offsets in a v2 file — the same named offsets tests/seal_test.cpp patches.
const OFF_ARCHIVE_SIZE: usize = 16; // magic(8)+ver(4)+alg(4)
const OFF_VERSION: usize = 8;
const OFF_ALG: usize = 12;
const OFF_SLOT_COUNT: usize = 24;
const OFF_SLOT0_TYPE: usize = 28;
const OFF_SLOT0_LEN: usize = 32;
const OFF_SLOT0_BODY: usize = 36;

fn describe_hdr(h: &SealHeader) -> String {
    format!(
        "reason={} ver={} arc={} slots={} pnonce={} poff={} plen={} ptag={}",
        h.reason as u8,
        h.version,
        h.archive_size,
        h.slots.len(),
        h.payload_nonce_off,
        h.payload_off,
        h.payload_len,
        h.payload_tag_off
    )
}

type Tree = Vec<(String, Vec<u8>)>;

// ---- directory helpers (all under $TMPDIR/sealparity-<pid>) -----------------------------
fn rm_tree(dir: &Path) {
    let _ = fs::remove_dir_all(dir);
}

// Every regular file under `root`, as (relative path, contents), sorted. Empty directories
// do not appear — exactly as FindFirstFileW's walk in the C++ left them out. Components are
// joined with `/`, the separator they actually have on this disk, so a name that kept a
// literal `\` would show as one.
fn list_tree(root: &Path) -> Tree {
    fn walk(dir: &Path, rel: &str, out: &mut Tree) {
        let Ok(rd) = fs::read_dir(dir) else { return };
        for e in rd.flatten() {
            let name = e.file_name().to_string_lossy().into_owned();
            let child = if rel.is_empty() { name } else { format!("{rel}/{name}") };
            if e.file_type().map(|t| t.is_dir()).unwrap_or(false) {
                walk(&e.path(), &child, out);
            } else {
                out.push((child, fs::read(e.path()).unwrap_or_default()));
            }
        }
    }
    let mut out = Vec::new();
    walk(root, "", &mut out);
    out.sort();
    out
}

// Write out an already-parsed index — the loop sealExtractArchive keeps after the parse
// moved out of it. Each entry goes through SealEntry::components, the way the macOS host
// will split it.
fn materialise(idx: &SealArchiveIndex, a: &[u8], dest: &Path) -> bool {
    if idx.reason != Arch::Ok {
        return false;
    }
    for e in &idx.files {
        let Some(full) = native_path(dest, e.components()) else { return false };
        let data: &[u8] = if e.data_len == 0 {
            &[]
        } else {
            // An entry pointing outside the archive it came from would be a decoder or
            // parser defect. The C++ reads out of bounds there; this refuses the write,
            // which surfaces as a disagreement between the trees.
            match e.data_off.checked_add(e.data_len).and_then(|end| a.get(e.data_off..end)) {
                Some(d) => d,
                None => return false,
            }
        };
        if !write_at(&full, data) {
            return false;
        }
    }
    true
}

fn tree_summary(t: &Tree) -> String {
    let mut s = format!("{} file(s): ", t.len());
    for (p, d) in t {
        s += &format!("{p}[{}B] ", d.len());
    }
    s
}

struct Harness {
    pass: usize,
    fail: usize,
    base: PathBuf,
}

impl Harness {
    fn verdict(&mut self, ok: bool, name: &str, rel: Rel) {
        println!("[{}] {:<52} {}", if ok { "PASS" } else { "FAIL" }, name, rel.label());
        if ok {
            self.pass += 1;
        } else {
            self.fail += 1;
        }
    }

    // A header case: assert the Sentinel result, that the C++ fallback agrees with it, and
    // that the relationship to the shipped C++ is the one we declared. sameHdr is
    // SealHeader's derived PartialEq: the same seven numbers and the same slot triples.
    fn hcheck(&mut self, name: &str, f: &[u8], e_reason: Hdr, e_slots: usize, rel: Rel, why: &str) {
        let s = match parse_seal_header(f) {
            Ok(s) => s,
            Err(e) => {
                self.verdict(false, name, rel);
                println!("        !! Sentinel's record did not decode: {e}");
                return;
            }
        };
        let b = fallback::read_seal_header(f);
        let o = oracle::read_seal_header(f);

        let asserted = s.reason == e_reason && s.slots.len() == e_slots;
        let agree = s == b;
        let same = s == o;
        let rel_ok = if rel == Rel::Parity { same } else { !same };
        let ok = asserted && agree && rel_ok;

        self.verdict(ok, name, rel);
        if !ok || rel == Rel::Diverges {
            println!("        shipped C++ : {}", describe_hdr(&o));
            println!("        Sentinel    : {}", describe_hdr(&s));
            if rel == Rel::Diverges {
                println!("        why         : {why}");
            }
        }
        if !ok {
            if !asserted {
                println!("        !! expected reason={} slots={}", e_reason as u8, e_slots);
            }
            if !agree {
                println!("        !! C++ fallback disagrees with Sentinel: {}", describe_hdr(&b));
            }
            if !rel_ok {
                println!("        !! expected {} against the shipped C++, got the opposite", rel.opposite());
            }
        }
    }

    // An archive case: run all three, compare the TREE each leaves behind (not just the
    // verdict — cases A20 and A26 differ only in what got written before the refusal).
    fn acheck(&mut self, name: &str, a: &[u8], e_reason: Arch, e_files: usize, rel: Rel, why: &str) {
        let (d_o, d_f, d_s) = (self.base.join("arc_o"), self.base.join("arc_f"), self.base.join("arc_s"));
        for d in [&d_o, &d_f, &d_s] {
            rm_tree(d);
            fs::create_dir_all(d).expect("create a destination under $TMPDIR");
        }
        ESCAPES.lock().unwrap().clear();

        let o_ok = oracle::seal_extract_archive(a, &d_o);
        let f_idx = fallback::read_seal_archive(a);
        let f_ok = materialise(&f_idx, a, &d_f);
        let s_idx = match parse_seal_archive(a) {
            Ok(s) => s,
            Err(e) => {
                self.verdict(false, name, rel);
                println!("        !! Sentinel's record did not decode: {e}");
                return;
            }
        };
        let s_ok = materialise(&s_idx, a, &d_s);

        let (t_o, t_f, t_s) = (list_tree(&d_o), list_tree(&d_f), list_tree(&d_s));
        let escaped = std::mem::take(&mut *ESCAPES.lock().unwrap());

        let asserted = s_idx.reason == e_reason && s_idx.files.len() == e_files;
        let agree = f_idx.reason == s_idx.reason && f_ok == s_ok && t_f == t_s;
        let same = o_ok == s_ok && t_o == t_s;
        let rel_ok = if rel == Rel::Parity { same } else { !same };
        let contained = escaped.is_empty();
        let ok = asserted && agree && rel_ok && contained;

        self.verdict(ok, name, rel);
        if !ok || rel == Rel::Diverges {
            println!(
                "        shipped C++ : extract={}, wrote {}",
                if o_ok { "OK" } else { "REFUSED" },
                tree_summary(&t_o)
            );
            println!(
                "        Sentinel    : reason={} entries={}, wrote {}",
                s_idx.reason as u8,
                s_idx.files.len(),
                tree_summary(&t_s)
            );
            if rel == Rel::Diverges {
                println!("        why         : {why}");
            }
        }
        if !ok {
            if !asserted {
                println!("        !! expected reason={} entries={}", e_reason as u8, e_files);
            }
            if !agree {
                println!(
                    "        !! C++ fallback disagrees with Sentinel: reason={} wrote {}",
                    f_idx.reason as u8,
                    tree_summary(&t_f)
                );
            }
            if !rel_ok {
                println!("        !! expected {} against the shipped C++, got the opposite", rel.opposite());
            }
            if !contained {
                println!("        !! tried to write outside its destination: {}", escaped.join("; "));
            }
        }
    }
}

// ---- corpus builders --------------------------------------------------------------------
struct RawEnt {
    path: Vec<u8>,
    declared_path_len: u32, // usually path.len()
    declared_data_len: u64, // usually data.len()
    data: Vec<u8>,
}

fn pack_raw(declared_count: u32, es: &[RawEnt]) -> Vec<u8> {
    let mut a = Vec::new();
    put_u32(&mut a, declared_count);
    for e in es {
        put_u32(&mut a, e.declared_path_len);
        a.extend_from_slice(&e.path);
        put_u64(&mut a, e.declared_data_len);
        a.extend_from_slice(&e.data);
    }
    a
}

fn ent(path: impl AsRef<[u8]>, data: impl AsRef<[u8]>) -> RawEnt {
    let (path, data) = (path.as_ref().to_vec(), data.as_ref().to_vec());
    RawEnt { declared_path_len: path.len() as u32, declared_data_len: data.len() as u64, path, data }
}

// sealCollect + sealPackArchive (Seal.h:182-209): walk the project, `/`-join the relative
// paths, skip build/VCS output and anything already sealed, then pack
//   file_count:u32 | { path_len:u32 | path(utf8, '/'-separated) | data_len:u64 | data } *
// FindFirstFileW on NTFS hands back a directory in name order; read_dir promises no order
// at all, so entries are sorted to stand in for it. Nothing below depends on the order (A1
// counts files and compares trees), but the archive bytes should not vary run to run.
fn seal_collect(root: &Path, rel: &str, out: &mut Vec<(String, Vec<u8>)>) {
    let dir = if rel.is_empty() { root.to_path_buf() } else { root.join(rel) };
    let Ok(rd) = fs::read_dir(&dir) else { return };
    let mut entries: Vec<_> = rd.flatten().collect();
    entries.sort_by_key(|e| e.file_name());
    for e in entries {
        let name = e.file_name().to_string_lossy().into_owned();
        let child = if rel.is_empty() { name.clone() } else { format!("{rel}/{name}") };
        if e.file_type().map(|t| t.is_dir()).unwrap_or(false) {
            if ["target", "build", ".git", "node_modules"].contains(&name.as_str()) {
                continue; // build/VCS output
            }
            seal_collect(root, &child, out);
        } else {
            if name.to_ascii_lowercase().ends_with(".sealed") {
                continue; // don't seal a seal
            }
            if let Ok(data) = fs::read(e.path()) {
                out.push((child, data));
            }
        }
    }
}

fn seal_pack_archive(files: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut a = Vec::new();
    put_u32(&mut a, files.len() as u32);
    for (p, d) in files {
        put_u32(&mut a, p.len() as u32);
        a.extend_from_slice(p.as_bytes());
        put_u64(&mut a, d.len() as u64);
        a.extend_from_slice(d);
    }
    a
}

// ---- the corpus: SYNTHESISED here, where the C++ seals it -------------------------------
//
// WHY THIS DIFFERS FROM THE C++. seal_xcheck.cpp builds its corpus by sealing a three-file
// tree with the real sealProject — CNG AES-256-GCM over a Windows Compression API LZMS
// stream — and then patching bytes at named offsets. Neither CNG nor LZMS exists on macOS,
// and the crypto deliberately did not move to Sentinel (R1), so there is no sealProject to
// call. Nothing under test needs one: the three header readers never authenticate,
// decrypt or decompress. They read the FRAMING — magic, version, alg, the sizes, the slot
// table, the payload length — and hand back offsets; not one line of oracle, fallback or
// parse_seal_header looks INSIDE the salt, the nonces, the wrapped DEK, the ciphertext or
// the tags.
//
// So the containers are laid down byte by byte from the layout at the top of Seal.h, with
// every framing field exactly what sealProject writes (archive_size is the real size of the
// real archive of the real tree, iters is kSealPbkdf2Iters, slot_len is 84) and fixed,
// recognisable filler where sealProject writes random or encrypted bytes. The named
// offsets land on the fields they name; that is asserted before any case runs, because a
// patch that hit the wrong bytes would quietly measure nothing.
//
// TWO PLACES WHERE THE FILLER IS NOT ARBITRARY, and why:
//
//   * THE PAYLOAD NONCE. H19, H23 and H23b claim more slots than the file holds, so the
//     second "slot header" is read out of the payload nonce: type from its bytes 0-3,
//     slot_len from bytes 4-7. With a random nonce that slot_len is larger than the file
//     with probability 1 - size/2^32, which is what the C++ silently relied on. Here the
//     nonce counts up from 0xB0, so slot_len is 0xB7B6B5B4 (about 3 GB) — the same outcome,
//     made certain instead of overwhelmingly likely.
//   * THE PAYLOAD LENGTH. No case reads a byte of the "ciphertext", and the one property
//     of its LENGTH that any case uses is that the whole container is well under 1 MiB, so
//     that 1 GiB is out of ratio for it (H13b; the floor makes the bound 64 MiB for any
//     container under 64 KiB). 200 bytes keeps it at a few hundred bytes, which is what
//     H13b's reason says it is.
//
// The ARCHIVE corpus needs no such treatment: A1 is the real sealPackArchive of the real
// tree, written to disk and walked back, and A2-A26 are hand-packed in both languages.
const SYNTH_PAYLOAD_LEN: usize = 200;

/// `n` fixed bytes counting up from `seed`: the stand-in for a random or encrypted field.
fn filler(n: usize, seed: u8) -> Vec<u8> {
    (0..n).map(|i| seed.wrapping_add(i as u8)).collect()
}

// v2 is sealProject's layout. v1 is the pre-v2 writer's, as seal_xcheck.cpp reconstructs
// it: no slot_len, and (not that a framing parser could tell) no AAD. The C++ v1 was sealed
// under a different password with fresh randomness; with filler for every secret there is
// nothing for that to change.
fn synth_container(v2: bool, archive_size: u64) -> Vec<u8> {
    let mut f = Vec::new();
    f.extend_from_slice(if v2 { b"SNTSEAL2" } else { b"SNTSEAL1" });
    put_u32(&mut f, if v2 { K_SEAL_VERSION2 } else { K_SEAL_VERSION1 });
    put_u32(&mut f, K_AEAD_AES_GCM);
    put_u64(&mut f, archive_size); // v2: the AAD prefix ends here
    put_u32(&mut f, 1); // slot count
    put_u32(&mut f, K_SLOT_PASSWORD); // slot 0 type
    if v2 {
        put_u32(&mut f, K_PASSWORD_SLOT_LEN); // slot 0 length — NO slot_len in v1
    }
    f.extend(filler(16, 0x10)); // salt
    put_u64(&mut f, K_SEAL_PBKDF2_ITERS); // iters
    f.extend(filler(12, 0x20)); // wrap nonce
    f.extend(filler(32, 0x30)); // wrapped DEK
    f.extend(filler(16, 0x50)); // wrap tag
    f.extend(filler(12, 0xB0)); // payload nonce — see "the payload nonce" above
    put_u64(&mut f, SYNTH_PAYLOAD_LEN as u64);
    f.extend(filler(SYNTH_PAYLOAD_LEN, 0x60)); // GCM(LZMS(archive)) stand-in
    f.extend(filler(16, 0xD0)); // payload tag
    f
}

/// Removes the per-process directory however the test ends.
struct Scratch(PathBuf);
impl Drop for Scratch {
    fn drop(&mut self) {
        rm_tree(&self.0);
    }
}

#[test]
fn seal_parity() {
    let base = std::env::temp_dir().join(format!("sealparity-{}", std::process::id()));
    rm_tree(&base);
    let _scratch = Scratch(base.clone());
    let src = base.join("src");
    fs::create_dir_all(src.join("sub")).expect("create the source tree under $TMPDIR");
    fs::write(src.join("a.sentinel"), "fn main() -> i64 { 42 }\r\n").unwrap();
    // notes..txt is the phase-31 regression: a legal name the substring guard refused.
    fs::write(src.join("notes..txt"), "a legitimate name with two dots\r\n").unwrap();
    fs::write(src.join("sub").join("b.txt"), vec![b'x'; 5000]).unwrap();

    let mut files = Vec::new();
    seal_collect(&src, "", &mut files);
    let archive = seal_pack_archive(&files);
    assert_eq!(files.len(), 3, "the source tree should collect to three files");

    // The corpus (see the section above): a v2 container as sealProject lays it out, then
    // bytes patched at named offsets — exactly what tests/seal_test.cpp does.
    let v2f = synth_container(true, archive.len() as u64);
    // A v1 container, laid out the way the pre-v2 writer did (no slot_len) — the same
    // reconstruction seal_test case 9 uses.
    let v1f = synth_container(false, archive.len() as u64);

    let plen_off = OFF_SLOT0_BODY + K_PASSWORD_SLOT_LEN as usize + 12;
    assert_eq!(&v2f[..8], b"SNTSEAL2");
    assert_eq!(get_u32(&v2f[OFF_VERSION..]), K_SEAL_VERSION2);
    assert_eq!(get_u32(&v2f[OFF_ALG..]), K_AEAD_AES_GCM);
    assert_eq!(get_u64(&v2f[OFF_ARCHIVE_SIZE..]), archive.len() as u64);
    assert_eq!(get_u32(&v2f[OFF_SLOT_COUNT..]), 1);
    assert_eq!(get_u32(&v2f[OFF_SLOT0_TYPE..]), K_SLOT_PASSWORD);
    assert_eq!(get_u32(&v2f[OFF_SLOT0_LEN..]), K_PASSWORD_SLOT_LEN);
    assert_eq!(get_u64(&v2f[OFF_SLOT0_BODY + 16..]), K_SEAL_PBKDF2_ITERS);
    assert_eq!(get_u64(&v2f[plen_off..]), SYNTH_PAYLOAD_LEN as u64);
    assert_eq!(v2f.len(), plen_off + 8 + SYNTH_PAYLOAD_LEN + 16);
    assert_eq!(get_u32(&v1f[28..]), K_SLOT_PASSWORD); // v1 slot0 type sits at 28
    assert_eq!(v1f.len(), v2f.len() - 4);

    let mut t = Harness { pass: 0, fail: 0, base: base.clone() };

    println!("== header: the container as written ==");
    t.hcheck("H1  a v2 file as sealProject lays it out", &v2f, Hdr::Ok, 1, Rel::Parity, "");
    t.hcheck("H2  a v1 file as the pre-v2 writer laid it out", &v1f, Hdr::Ok, 1, Rel::Parity, "");

    println!("== header: not a container at all ==");
    t.hcheck("H3  empty file", &[], Hdr::BadMagic, 0, Rel::Parity, "");
    t.hcheck("H4  four bytes", &v2f[..4], Hdr::BadMagic, 0, Rel::Parity, "");
    t.hcheck("H5  magic and nothing else", &v2f[..8], Hdr::TruncHeader, 0, Rel::Parity, "");
    {
        let mut f = v2f.clone();
        f[3] = b'X';
        t.hcheck("H6  wrong magic", &f, Hdr::BadMagic, 0, Rel::Parity, "");
    }
    t.hcheck("H7  header cut mid-field", &v2f[..20], Hdr::TruncHeader, 0, Rel::Parity, "");

    println!("== header: version and algorithm ==");
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_VERSION, 3);
        t.hcheck("H8  version 3 under SNTSEAL2", &f, Hdr::BadVersion, 0, Rel::Parity, "");
    }
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_ALG, 2);
        t.hcheck("H9  aead_alg 2 (ChaCha, reserved)", &f, Hdr::BadVersion, 0, Rel::Parity, "");
    }
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_VERSION, 1);
        t.hcheck("H10 version 1 under SNTSEAL2", &f, Hdr::BadVersion, 0, Rel::Parity, "");
    }

    println!("== header: archive_size that does not fit an i64 (TRAP A) ==");
    {
        let mut f = v2f.clone();
        put_u64_at(&mut f, OFF_ARCHIVE_SIZE, 0xFFFF_FFFF_FFFF_FFFF);
        t.hcheck(
            "H11 archive_size = 18446744073709551615",
            &f,
            Hdr::ArchiveSize,
            0,
            Rel::Diverges,
            "the shipped reader accepted it and unsealProject handed it to sealDecompress as the \
             output-buffer size — out.resize(2^64-1). Sentinel has no u64, so the value reads back \
             NEGATIVE; rd_u64 refuses it rather than wrapping, and the file never reaches an \
             allocation. (In a v2 file the AAD would also catch it; a v1 file has no AAD at all.)",
        );
    }
    {
        let mut f = v2f.clone();
        put_u64_at(&mut f, OFF_ARCHIVE_SIZE, 0x8000_0000_0000_0000);
        t.hcheck(
            "H12 archive_size = 2^63 exactly (the boundary)",
            &f,
            Hdr::ArchiveSize,
            0,
            Rel::Diverges,
            "the first value whose top bit is set; the shipped reader takes it, we refuse it",
        );
    }
    {
        let mut f = v2f.clone();
        put_u64_at(&mut f, OFF_ARCHIVE_SIZE, 0x7FFF_FFFF_FFFF_FFFF);
        t.hcheck(
            "H13 archive_size = 2^63-1",
            &f,
            Hdr::ArchiveSize,
            0,
            Rel::Diverges,
            "fitting in an i64 was never evidence a size was REAL. The shipped reader \
             accepts this and hands it to sealDecompress as an output-buffer size; a \
             206-byte container claiming 64 GiB was measured committing 65,667 MB and 14 s \
             before failing, and near the top of the range the bad_alloc escaped \
             unsealProject. Authentication cannot help — the attacker is the SEALER. The \
             size must now be plausible for the container it arrived in.",
        );
    }
    {
        // The bound is a RATIO, not a constant, so prove both halves: a size that is
        // implausible for THIS container is refused even though it is small in absolute
        // terms, and a size within the floor is still accepted.
        let mut f = v2f.clone();
        put_u64_at(&mut f, OFF_ARCHIVE_SIZE, 0x4000_0000); // 1 GiB
        t.hcheck(
            "H13b archive_size = 1 GiB from a small container",
            &f,
            Hdr::ArchiveSize,
            0,
            Rel::Diverges,
            "no compressor turns a few hundred bytes into a gigabyte; the claim is out of \
             step with the file it arrived in",
        );
    }
    {
        let mut f = v2f.clone();
        put_u64_at(&mut f, OFF_ARCHIVE_SIZE, 1_048_576); // 1 MiB, under the floor
        t.hcheck(
            "H13c archive_size = 1 MiB (under the 64 MiB floor, accepted)",
            &f,
            Hdr::Ok,
            1,
            Rel::Parity,
            "",
        );
    }

    println!("== header: the slot table (phase-31 defects a, b) ==");
    let slot0_end = OFF_SLOT0_BODY + K_PASSWORD_SLOT_LEN as usize;
    {
        // seal_test case 5's corpus: an unknown slot spliced in AFTER the password slot.
        let mut f = v2f[..slot0_end].to_vec();
        put_u32(&mut f, 99);
        put_u32(&mut f, 12);
        f.extend(0u8..12);
        f.extend_from_slice(&v2f[slot0_end..]);
        put_u32_at(&mut f, OFF_SLOT_COUNT, 2);
        t.hcheck("H14 unknown v2 slot AFTER the password slot", &f, Hdr::Ok, 2, Rel::Parity, "");
    }
    {
        // seal_test case 6's corpus: the unknown slot FIRST. v1 aborted here.
        let mut f = v2f[..OFF_SLOT0_TYPE].to_vec();
        put_u32(&mut f, 99);
        put_u32(&mut f, 12);
        f.extend(0u8..12);
        f.extend_from_slice(&v2f[OFF_SLOT0_TYPE..]);
        put_u32_at(&mut f, OFF_SLOT_COUNT, 2);
        t.hcheck("H15 unknown v2 slot BEFORE the password slot", &f, Hdr::Ok, 2, Rel::Parity, "");
    }
    {
        // A zero-length unknown slot: slot_len 0 must still step, not stall.
        let mut f = v2f[..OFF_SLOT0_TYPE].to_vec();
        put_u32(&mut f, 77);
        put_u32(&mut f, 0);
        f.extend_from_slice(&v2f[OFF_SLOT0_TYPE..]);
        put_u32_at(&mut f, OFF_SLOT_COUNT, 2);
        t.hcheck("H16 zero-length unknown slot is stepped", &f, Hdr::Ok, 2, Rel::Parity, "");
    }
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_SLOT0_TYPE, 99);
        t.hcheck("H17 the only slot is an unknown v2 type", &f, Hdr::Ok, 1, Rel::Parity, "");
    }
    {
        // v1 has no slot_len, so an unknown type cannot be stepped over at all.
        let mut f = v1f.clone();
        put_u32_at(&mut f, 28, 99); // v1 slot0 type sits at 28
        t.hcheck("H18 v1 file with a non-password slot", &f, Hdr::SlotType, 0, Rel::Parity, "");
    }
    {
        // slot_count lies by one. The second "slot header" is read out of the payload
        // nonce, so it parses as some type and length and then fails on its BODY — which
        // is the honest outcome: nothing distinguishes a slot header from the bytes that
        // follow the table except the count itself. Both readers land in the same place.
        // (The synthesised nonce makes that length 0xB7B6B5B4 — see the corpus section.)
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_SLOT_COUNT, 2);
        t.hcheck("H19 slot_count 2, only one slot present", &f, Hdr::TruncSlotBody, 1, Rel::Parity, "");
    }
    // Cut so that four bytes remain where a v2 slot header needs eight.
    t.hcheck(
        "H20 file ends inside a slot header",
        &v2f[..OFF_SLOT0_TYPE + 4],
        Hdr::TruncSlots,
        0,
        Rel::Parity,
        "",
    );
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_SLOT0_LEN, 0xFFFF_FFF0);
        t.hcheck("H21 slot_len larger than the file (TRAP B)", &f, Hdr::TruncSlotBody, 0, Rel::Parity, "");
    }
    // Slot body cut in half.
    t.hcheck("H22 truncated slot body", &v2f[..OFF_SLOT0_BODY + 40], Hdr::TruncSlotBody, 0, Rel::Parity, "");
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_SLOT_COUNT, 0xFFFF_FFFF);
        let t0 = Instant::now();
        t.hcheck(
            "H23 slot_count = 4294967295",
            &f,
            Hdr::SlotCount,
            0,
            Rel::Diverges,
            "the shipped reader walks slots until the bytes run out, which is safe for the \
             PARSE but not for what follows it: unsealProject runs a full PBKDF2 per \
             password-type slot until one unlocks, so a WRONG password runs them all. \
             Measured: 200 slots in an 18 KB file took 18.7 s on the UI thread; a 1 MB file \
             holds ~11,400, about 17.7 minutes, no progress and no cancel. Phase 31 bounded \
             `iters` for exactly this reason; this was the same hang by another route.",
        );
        let ms = t0.elapsed().as_millis();
        println!("        (four billion claimed slots, refused in {ms} ms — now before the walk)");
    }
    {
        let mut f = v2f.clone();
        put_u32_at(&mut f, OFF_SLOT_COUNT, 64);
        t.hcheck(
            "H23b slot_count = 64 (the ceiling itself, still walked)",
            &f,
            Hdr::TruncSlotBody,
            1,
            Rel::Parity,
            "",
        );
    }

    println!("== header: the payload framing ==");
    t.hcheck("H24 file ends after the slot table", &v2f[..slot0_end], Hdr::TruncPayload, 1, Rel::Parity, "");
    {
        let mut f = v2f.clone();
        put_u64_at(&mut f, plen_off, 0xFFFF_FFFF_FFFF_FFFF);
        t.hcheck("H25 payload_len = 18446744073709551615", &f, Hdr::TruncPayloadBody, 1, Rel::Parity, "");
    }
    {
        let mut f = v2f.clone();
        put_u64_at(&mut f, plen_off, 0x8000_0000_0000_0000);
        t.hcheck("H26 payload_len = 2^63 exactly", &f, Hdr::TruncPayloadBody, 1, Rel::Parity, "");
    }
    // Eat half the tag.
    t.hcheck("H27 payload tag truncated", &v2f[..v2f.len() - 8], Hdr::TruncPayloadBody, 1, Rel::Parity, "");

    println!("\n== archive: the index as written ==");
    t.acheck("A1  the real archive (3 files)", &archive, Arch::Ok, 3, Rel::Parity, "");
    t.acheck("A2  zero files", &pack_raw(0, &[]), Arch::Ok, 0, Rel::Parity, "");
    t.acheck(
        "A3  a file with zero-length data",
        &pack_raw(1, &[ent("empty.txt", "")]),
        Arch::Ok,
        1,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A4  nested path with '/' separators",
        &pack_raw(2, &[ent("sub/deep/x.txt", "hello"), ent("top.txt", "hi")]),
        Arch::Ok,
        2,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A5  multi-byte UTF-8 path components",
        // "mäppe/文件.txt", spelled as the C++ spells it.
        &pack_raw(1, &[ent(b"m\xc3\xa4ppe/\xe6\x96\x87\xe4\xbb\xb6.txt", "unicode")]),
        Arch::Ok,
        1,
        Rel::Parity,
        "",
    );

    println!("== archive: the '..' rule is PER COMPONENT (TRAP C) ==");
    t.acheck(
        "A6  notes..txt is a FILE NAME, not a traversal",
        &pack_raw(1, &[ent("notes..txt", "two dots")]),
        Arch::Ok,
        1,
        Rel::Parity,
        "",
    );
    t.acheck("A7  v1..2.md likewise", &pack_raw(1, &[ent("v1..2.md", "x")]), Arch::Ok, 1, Rel::Parity, "");
    t.acheck(
        "A8  sub/ok..name.txt likewise",
        &pack_raw(1, &[ent("sub/ok..name.txt", "x")]),
        Arch::Ok,
        1,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A9  a real '..' component is refused",
        &pack_raw(1, &[ent("..", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A10 ../evil.txt is refused",
        &pack_raw(1, &[ent("../evil.txt", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A11 sub/../../x is refused",
        &pack_raw(1, &[ent("sub/../../x", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A12 a backslash-spelled traversal is refused",
        &pack_raw(1, &[ent("..\\evil.txt", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    // On macOS a leading `\` becomes `/` — the filesystem ROOT, not a drive's — so the
    // rooted refusals matter more here, not less.
    t.acheck(
        "A13 rooted (leading backslash) is refused",
        &pack_raw(1, &[ent("\\rooted.txt", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A14 rooted (leading slash) is refused",
        &pack_raw(1, &[ent("/rooted.txt", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    // A drive letter means nothing to macOS (mapped, `C:\abs.txt` would be a directory
    // `C:` holding `abs.txt`), but the guard is byte-level and platform-independent, and a
    // sealed file can travel back to a Windows host — so it is refused here too.
    t.acheck(
        "A15 drive-qualified is refused",
        &pack_raw(1, &[ent("C:\\abs.txt", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );
    t.acheck(
        "A16 an empty path is refused",
        &pack_raw(1, &[ent("", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );

    println!("== archive: the colon (the one rule bytes and characters disagree on) ==");
    // ON macOS THE ORACLE'S WRITE LOOKS DIFFERENT, AND THE DIVERGENCE STILL HOLDS. ':' is
    // a legal filename character on APFS (Finder shows it as '/'), so where Windows wrote an
    // alternate data stream that a tree walk shows as a zero-length `ab`, the oracle here
    // writes a plain six-byte file named `ab:c`. Either way the shipped extractor said OK
    // and left a file behind, and Sentinel refuses and leaves nothing. The reason below is
    // the C++'s, about NTFS, and it is still why the rule exists.
    t.acheck(
        "A17 an alternate-data-stream path is refused",
        &pack_raw(1, &[ent("ab:c", "hidden")]),
        Arch::UnsafePath,
        0,
        Rel::Diverges,
        "the shipped guard only looked at position 1, so `ab:c` passed — and on NTFS that \
         writes an ALTERNATE DATA STREAM `c` on a zero-length file `ab`, content a tree walk \
         of the unsealed project never shows. The colon is now refused anywhere, which cannot \
         cost a real file: ':' is illegal in a Windows filename and every archived path came \
         from FindFirstFileW walking a Windows directory",
    );
    // "é:foo": the first CHARACTER is two UTF-8 bytes, so the colon is the second UTF-16
    // unit (the oracle's p[1]) but the third byte. Every implementation refuses it.
    t.acheck(
        "A18 multi-byte first character, then a colon",
        &pack_raw(1, &[ent(b"\xc3\xa9:foo", "x")]),
        Arch::UnsafePath,
        0,
        Rel::Parity,
        "",
    );

    println!("== archive: lengths off disk (TRAPS A and B) ==");
    t.acheck("A19 archive shorter than the file count", &[1, 0], Arch::Short, 0, Rel::Parity, "");
    t.acheck(
        "A20 file_count 5, one entry present",
        &pack_raw(5, &[ent("a.txt", "x")]),
        Arch::Trunc,
        1,
        Rel::Diverges,
        "same divergence as A26 in its smallest form: the shipped extractor had already \
         written a.txt before the second entry ran off the end of the archive, so a refused \
         unseal still left a file behind. We write nothing",
    );
    {
        let mut e = ent("a.txt", "x");
        e.declared_path_len = 1000;
        t.acheck("A21 path_len longer than the archive", &pack_raw(1, &[e]), Arch::Trunc, 0, Rel::Parity, "");
    }
    {
        let mut e = ent("a.txt", "x");
        e.declared_data_len = 1000;
        t.acheck("A22 data_len longer than the archive", &pack_raw(1, &[e]), Arch::Trunc, 0, Rel::Parity, "");
    }
    {
        let mut e = ent("a.txt", "x");
        e.declared_data_len = 0xFFFF_FFFF_FFFF_FFFF;
        t.acheck(
            "A23 data_len = 18446744073709551615",
            &pack_raw(1, &[e]),
            Arch::DataLen,
            0,
            Rel::Parity,
            "",
        );
    }
    {
        let mut e = ent("a.txt", "x");
        e.declared_data_len = 0x8000_0000_0000_0000;
        t.acheck("A24 data_len = 2^63 exactly", &pack_raw(1, &[e]), Arch::DataLen, 0, Rel::Parity, "");
    }
    {
        // 2^63-1 still reads as a length; it simply does not fit in the archive.
        let mut e = ent("a.txt", "x");
        e.declared_data_len = 0x7FFF_FFFF_FFFF_FFFF;
        t.acheck(
            "A25 data_len = 2^63-1 (a length, just too big)",
            &pack_raw(1, &[e]),
            Arch::Trunc,
            0,
            Rel::Parity,
            "",
        );
    }

    println!("== archive: nothing is written until everything is checked ==");
    t.acheck(
        "A26 good files, then a traversal",
        &pack_raw(
            4,
            &[
                ent("keep1.txt", "aaa"),
                ent("keep2.txt", "bbb"),
                ent("sub/keep3.txt", "ccc"),
                ent("../evil.txt", "OWNED"),
            ],
        ),
        Arch::UnsafePath,
        3,
        Rel::Diverges,
        "the shipped extractor parsed and wrote in one loop, so it had already dropped \
         keep1/keep2/sub/keep3 into the destination before it refused the fourth entry — and \
         MainWindow's failure cleanup is RemoveDirectoryW, which only removes an EMPTY \
         directory, so they stayed. The parse now validates the whole index first, so a \
         rejected archive writes nothing at all",
    );

    println!("\n{} passed, {} failed", t.pass, t.fail);
    assert_eq!(t.fail, 0, "{} of {} seal parity cases failed", t.fail, t.pass + t.fail);
}
