// SPDX-License-Identifier: GPL-3.0-or-later
//! Safe Rust over Sentinel-IDE's Sentinel library — `src/sentinel/parsers.sentinel`,
//! built by `snc build --lib` and re-linked as `libsentinel_parsers.dylib` (see build.rs).
//!
//! Each export takes bytes and returns an owned record that the caller frees with
//! `sentinel_free_bytes`. This crate owns that contract on the Rust side: one function
//! per export, each decoding its record into a typed value. Record formats are
//! documented above each export in parsers.sentinel; the decoders here are the Rust
//! counterparts of the C++ ones in Signing.h, Project.h, Seal.h, Updater.cpp and
//! MainWindow.cpp, and they read the records the same way.
//!
//! Two deliberate differences from the C++ decoders:
//!
//! * **Every read is bounds-checked.** The C++ trusts the record and reads length
//!   prefixes without checking them against the buffer. Here a record that does not
//!   fit is a [`RecordError`], never an out-of-bounds read. A Sentinel export that
//!   produced one would be a defect, so the error names the export and the offset.
//! * **Strings come back as `String`, decoded lossily** — invalid UTF-8 becomes U+FFFD,
//!   which is what `MultiByteToWideChar` without `MB_ERR_INVALID_CHARS` does on the
//!   Windows side. The one byte-transparent path, `save_manifest`, returns `Vec<u8>`.
//!
//! What this crate cannot protect against: a Sentinel runtime fault (an out-of-bounds
//! index inside an export) aborts the process. There is no hook to intercept it yet —
//! that is Sentinel-lang request R2 in docs/Sentinel-lang_request.md.

use std::fmt;
use std::ops::Range;

mod ffi {
    // The declarations snc writes to sentinel_parsers.h, transcribed. build.rs exports
    // exactly the symbols that header declares, so a mismatch here fails at link time.
    extern "C" {
        pub fn parse_diag(p: *const u8, n: i64, out: *mut *mut u8, out_n: *mut i64);
        pub fn parse_trust(p: *const u8, n: i64, out: *mut *mut u8, out_n: *mut i64);
        pub fn parse_sig(p: *const u8, n: i64, out: *mut *mut u8, out_n: *mut i64);
        pub fn parse_manifest(p: *const u8, n: i64, out: *mut *mut u8, out_n: *mut i64);
        pub fn save_manifest(
            a: *const u8,
            an: i64,
            b: *const u8,
            bn: i64,
            out: *mut *mut u8,
            out_n: *mut i64,
        );
        pub fn parse_appcast(
            a: *const u8,
            an: i64,
            b: *const u8,
            bn: i64,
            out: *mut *mut u8,
            out_n: *mut i64,
        );
        pub fn parse_seal_header(p: *const u8, n: i64, out: *mut *mut u8, out_n: *mut i64);
        pub fn parse_seal_archive(p: *const u8, n: i64, out: *mut *mut u8, out_n: *mut i64);
        pub fn sentinel_free_bytes(p: *mut u8);
    }
}

// ---- errors -------------------------------------------------------------------

/// A record that does not decode: shorter than its own length prefixes say, a negative
/// length, or a code outside the documented set. Always a defect on one side of the
/// FFI, never a property of the input the export was given.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordError {
    pub export: &'static str,
    pub offset: usize,
    pub what: String,
}

impl fmt::Display for RecordError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: malformed record at byte {}: {}", self.export, self.offset, self.what)
    }
}

impl std::error::Error for RecordError {}

pub type Result<T> = std::result::Result<T, RecordError>;

// ---- calling an export ----------------------------------------------------------

/// The buffer an export returned. Freed through the runtime that allocated it.
struct Owned {
    ptr: *mut u8,
    len: usize,
}

impl Owned {
    fn bytes(&self) -> &[u8] {
        if self.ptr.is_null() || self.len == 0 {
            &[]
        } else {
            // SAFETY: the export handed back `len` initialised bytes at `ptr`, owned by
            // us until sentinel_free_bytes, which only Drop calls.
            unsafe { std::slice::from_raw_parts(self.ptr, self.len) }
        }
    }
}

impl Drop for Owned {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            // SAFETY: allocated by the Sentinel runtime and not yet freed.
            unsafe { ffi::sentinel_free_bytes(self.ptr) }
        }
    }
}

type Export1 = unsafe extern "C" fn(*const u8, i64, *mut *mut u8, *mut i64);
type Export2 = unsafe extern "C" fn(*const u8, i64, *const u8, i64, *mut *mut u8, *mut i64);

fn finish(export: &'static str, ptr: *mut u8, n: i64) -> Result<Owned> {
    // Take ownership first, so the buffer is freed even when the length is unusable.
    let mut owned = Owned { ptr, len: 0 };
    owned.len = usize::try_from(n).map_err(|_| RecordError {
        export,
        offset: 0,
        what: format!("negative record length {n}"),
    })?;
    Ok(owned)
}

fn call1(export: &'static str, f: Export1, a: &[u8]) -> Result<Owned> {
    let mut ptr = std::ptr::null_mut();
    let mut n = 0i64;
    // SAFETY: `a` is a live slice for the duration of the call; an empty slice's pointer
    // is non-null and dangling, which is fine with a zero length.
    unsafe { f(a.as_ptr(), a.len() as i64, &mut ptr, &mut n) };
    finish(export, ptr, n)
}

fn call2(export: &'static str, f: Export2, a: &[u8], b: &[u8]) -> Result<Owned> {
    let mut ptr = std::ptr::null_mut();
    let mut n = 0i64;
    // SAFETY: as call1, for both slices.
    unsafe { f(a.as_ptr(), a.len() as i64, b.as_ptr(), b.len() as i64, &mut ptr, &mut n) };
    finish(export, ptr, n)
}

// ---- reading a record -------------------------------------------------------------

/// A cursor over a record. Integers are 8-byte little-endian i64 (Sentinel has no u64);
/// strings are an i64 length followed by that many bytes.
struct Rec<'a> {
    b: &'a [u8],
    pos: usize,
    export: &'static str,
}

impl<'a> Rec<'a> {
    fn new(export: &'static str, b: &'a [u8]) -> Self {
        Rec { b, pos: 0, export }
    }

    fn err(&self, what: impl Into<String>) -> RecordError {
        RecordError { export: self.export, offset: self.pos, what: what.into() }
    }

    fn remaining(&self) -> usize {
        self.b.len() - self.pos
    }

    fn peek(&self) -> Option<u8> {
        self.b.get(self.pos).copied()
    }

    fn u8(&mut self, what: &str) -> Result<u8> {
        let v = self.peek().ok_or_else(|| self.err(format!("{what}: record ends")))?;
        self.pos += 1;
        Ok(v)
    }

    fn i64(&mut self, what: &str) -> Result<i64> {
        let raw = self.take(8, what)?;
        Ok(i64::from_le_bytes(raw.try_into().unwrap()))
    }

    /// A count, offset or length: an i64 that must not be negative.
    fn size(&mut self, what: &str) -> Result<usize> {
        let at = self.pos;
        let v = self.i64(what)?;
        usize::try_from(v).map_err(|_| RecordError {
            export: self.export,
            offset: at,
            what: format!("{what}: negative value {v}"),
        })
    }

    fn take(&mut self, n: usize, what: &str) -> Result<&'a [u8]> {
        if self.remaining() < n {
            return Err(self.err(format!("{what}: needs {n} bytes, {} left", self.remaining())));
        }
        let s = &self.b[self.pos..self.pos + n];
        self.pos += n;
        Ok(s)
    }

    /// A length-prefixed string, decoded lossily.
    fn lp(&mut self, what: &str) -> Result<String> {
        let n = self.size(what)?;
        Ok(String::from_utf8_lossy(self.take(n, what)?).into_owned())
    }

    /// `{ byte 1, length-prefixed }*` and then one terminator byte if any remain — the
    /// list shape trust grants, manifest arrays and target links all use. The C++
    /// decoders accept any byte other than 1 as the terminator; so does this.
    fn lp_list(&mut self, what: &str) -> Result<Vec<String>> {
        let mut v = Vec::new();
        while self.peek() == Some(1) {
            self.pos += 1;
            v.push(self.lp(what)?);
        }
        if self.peek().is_some() {
            self.pos += 1;
        }
        Ok(v)
    }

    fn flag(&mut self, what: &str) -> Result<bool> {
        Ok(self.u8(what)? == 1)
    }

    fn rest(&mut self) -> &'a [u8] {
        let s = &self.b[self.pos..];
        self.pos = self.b.len();
        s
    }
}

// ---- parse_diag -------------------------------------------------------------------

/// A `path.sentinel:line[:col]` diagnostic found in one line of `snc build` output.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Diag {
    pub file: String,
    /// `line` and `col` are whatever digits the line held, accumulated without a bound —
    /// and Sentinel's i64 arithmetic WRAPS rather than trapping (measured 2026-10-25:
    /// `a.sentinel:9223372036854775808:1` reads back as line i64::MIN). Not a crash, but
    /// not a line number either: clamp both to the buffer before navigating.
    pub line: i64,
    pub col: i64,
    /// Byte range of the `file:line:col` token within the input line — what the Output
    /// pane underlines as a link.
    pub token: Range<usize>,
}

/// Find the first diagnostic in `line`. `Ok(None)` when the line holds none.
///
/// Record: `[matched][line][col][token_start][token_end][file bytes …]`.
pub fn parse_diag(line: &[u8]) -> Result<Option<Diag>> {
    let out = call1("parse_diag", ffi::parse_diag, line)?;
    let mut r = Rec::new("parse_diag", out.bytes());
    if !r.flag("matched")? {
        return Ok(None);
    }
    let ln = r.i64("line")?;
    let col = r.i64("col")?;
    let ts = r.size("token start")?;
    let te = r.size("token end")?;
    if ts > te || te > line.len() {
        return Err(r.err(format!("token {ts}..{te} outside a {}-byte line", line.len())));
    }
    let file = String::from_utf8_lossy(r.rest()).into_owned();
    Ok(Some(Diag { file, line: ln, col, token: ts..te }))
}

// ---- parse_trust ------------------------------------------------------------------

/// One `[[keys]]` entry of a consumer trust manifest (ADR 0061).
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TrustedKey {
    pub name: String,
    pub pubkey: String,
    pub grants: Vec<String>,
}

/// The trusted keys a manifest declares, in file order.
///
/// Record: per key `1, name, pubkey, { 1, grant }*, terminator`; a final 0 ends the keys.
pub fn parse_trust(text: &[u8]) -> Result<Vec<TrustedKey>> {
    let out = call1("parse_trust", ffi::parse_trust, text)?;
    let mut r = Rec::new("parse_trust", out.bytes());
    let mut keys = Vec::new();
    while r.peek() == Some(1) {
        r.pos += 1;
        let name = r.lp("name")?;
        let pubkey = r.lp("pubkey")?;
        let grants = r.lp_list("grants")?;
        keys.push(TrustedKey { name, pubkey, grants });
    }
    Ok(keys)
}

// ---- parse_sig ----------------------------------------------------------------------

/// The fields of a detached-signature (`.sig`) carrier.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SigFields {
    pub present: bool,
    pub algorithm: String,
    pub key: String,
    pub grants: String,
}

/// Record: `[present]` then length-prefixed algorithm, key, grants.
pub fn parse_sig(text: &[u8]) -> Result<SigFields> {
    let out = call1("parse_sig", ffi::parse_sig, text)?;
    let mut r = Rec::new("parse_sig", out.bytes());
    let present = r.flag("present")?;
    Ok(SigFields { present, algorithm: r.lp("algorithm")?, key: r.lp("key")?, grants: r.lp("grants")? })
}

// ---- parse_manifest -----------------------------------------------------------------

/// What a project manifest states, before any default is applied: `None` means the key
/// is absent. Defaults and the single-target fallback are the project model's business
/// (ide-core), exactly as they are host-side in Project.h.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ManifestFields {
    pub name: Option<String>,
    pub version: Option<String>,
    pub r#type: Option<String>,
    pub entry: Option<String>,
    pub icon: Option<String>,
    pub src: Option<String>,
    pub default_tier: Option<String>,
    pub require: Option<String>,
    pub trust: Option<String>,
    pub sign: Option<String>,
    pub lib_paths: Vec<String>,
    pub links: Vec<String>,
    pub targets: Vec<TargetFields>,
}

/// One `[[target]]` block. Missing keys come back empty, as parseTargets left them.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TargetFields {
    pub name: String,
    pub entry: String,
    pub r#type: String,
    pub links: Vec<String>,
}

/// Record: ten `[found][string]` scalars in the order of [`ManifestFields`], the
/// `lib_paths` and `links` lists, then per target `1, name, entry, type, { 1, link }*,
/// terminator`.
pub fn parse_manifest(text: &[u8]) -> Result<ManifestFields> {
    let out = call1("parse_manifest", ffi::parse_manifest, text)?;
    let mut r = Rec::new("parse_manifest", out.bytes());
    let mut scalar = |what: &str| -> Result<Option<String>> {
        let found = r.flag(what)?;
        let v = r.lp(what)?;
        Ok(found.then_some(v))
    };
    let mut m = ManifestFields {
        name: scalar("name")?,
        version: scalar("version")?,
        r#type: scalar("type")?,
        entry: scalar("entry")?,
        icon: scalar("icon")?,
        src: scalar("src")?,
        default_tier: scalar("default_tier")?,
        require: scalar("require")?,
        trust: scalar("trust")?,
        sign: scalar("sign")?,
        ..Default::default()
    };
    m.lib_paths = r.lp_list("lib_paths")?;
    m.links = r.lp_list("links")?;
    while r.peek() == Some(1) {
        r.pos += 1;
        m.targets.push(TargetFields {
            name: r.lp("target name")?,
            entry: r.lp("target entry")?,
            r#type: r.lp("target type")?,
            links: r.lp_list("target links")?,
        });
    }
    Ok(m)
}

// ---- save_manifest ------------------------------------------------------------------

/// The managed values the manifest writer puts back, each ALREADY RENDERED as TOML text
/// (`"quoted"`, `["a", "b"]`, `true`). Rendering stays with the caller so quoting rules
/// live in one language, as encodeSaveModel keeps them in C++ on Windows.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ManifestValues {
    pub name: String,
    pub version: String,
    pub r#type: String,
    pub entry: String,
    pub src: String,
    pub lib_paths: String,
    pub links: String,
    pub default_tier: String,
    pub require: String,
    pub trust: String,
    pub sign: String,
    pub targets: Vec<TargetValues>,
}

/// One target's rendered values, matched to the manifest's `[[target]]` blocks by order.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TargetValues {
    pub name: String,
    pub entry: String,
    pub r#type: String,
    pub links: String,
}

impl ManifestValues {
    /// The model bytes save_manifest reads: the eleven values in field order, an 8-byte
    /// target count, then four values per target; each value an 8-byte LE length and
    /// its UTF-8 bytes. Byte-for-byte what Project.h's encodeSaveModel produces.
    pub fn encode(&self) -> Vec<u8> {
        fn put(m: &mut Vec<u8>, s: &str) {
            m.extend_from_slice(&(s.len() as u64).to_le_bytes());
            m.extend_from_slice(s.as_bytes());
        }
        let mut m = Vec::new();
        for v in [
            &self.name,
            &self.version,
            &self.r#type,
            &self.entry,
            &self.src,
            &self.lib_paths,
            &self.links,
            &self.default_tier,
            &self.require,
            &self.trust,
            &self.sign,
        ] {
            put(&mut m, v);
        }
        m.extend_from_slice(&(self.targets.len() as u64).to_le_bytes());
        for t in &self.targets {
            for v in [&t.name, &t.entry, &t.r#type, &t.links] {
                put(&mut m, v);
            }
        }
        m
    }
}

/// Rewrite `before` (the manifest as it is on disk, raw bytes) with `values`, touching
/// only the managed keys. Byte-transparent: bytes on lines it does not rewrite come
/// back exactly as they went in, valid UTF-8 or not.
pub fn save_manifest(before: &[u8], values: &ManifestValues) -> Result<Vec<u8>> {
    save_manifest_raw(before, &values.encode())
}

/// save_manifest over an already-encoded model, for callers that hold the bytes.
pub fn save_manifest_raw(before: &[u8], model: &[u8]) -> Result<Vec<u8>> {
    let out = call2("save_manifest", ffi::save_manifest, before, model)?;
    if out.ptr.is_null() {
        return Err(RecordError { export: "save_manifest", offset: 0, what: "no output buffer".into() });
    }
    Ok(out.bytes().to_vec())
}

// ---- parse_appcast ------------------------------------------------------------------

/// What the update feed offers, relative to the running build.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct AppcastVerdict {
    /// A `sparkle:version="…"` attribute with both quotes was located.
    pub found: bool,
    /// … and the text between the quotes is a well-formed version.
    pub valid: bool,
    /// … and it is strictly newer than `mine`.
    pub newer: bool,
    /// The version — only when `valid`, so unvalidated feed bytes cannot reach a dialog,
    /// a log line or the settings file.
    pub version: Option<String>,
}

/// Record: `[found][valid][newer][len][version bytes]`. The flags nest: newer implies
/// valid implies found.
pub fn parse_appcast(body: &[u8], mine: &str) -> Result<AppcastVerdict> {
    let out = call2("parse_appcast", ffi::parse_appcast, body, mine.as_bytes())?;
    let mut r = Rec::new("parse_appcast", out.bytes());
    let found = r.flag("found")?;
    let valid = r.flag("valid")?;
    let newer = r.flag("newer")?;
    let n = r.size("version length")?;
    let bytes = r.take(n, "version")?;
    let version = (valid && n > 0).then(|| String::from_utf8_lossy(bytes).into_owned());
    Ok(AppcastVerdict { found, valid, newer, version })
}

// ---- parse_seal_header ----------------------------------------------------------------

/// Why a sealed container's header was refused — or `Ok`. The numbering is the record's
/// and Seal.h's `SealHeaderReason`; Seal.h maps each code to its user-facing message.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum SealHeaderReason {
    Ok = 0,
    /// Not 8 bytes, or not `SNTSEAL1`/`SNTSEAL2`.
    BadMagic = 1,
    /// version / alg / archive_size / slot_count do not fit.
    TruncHeader = 2,
    /// Version disagrees with the magic, or an unknown aead_alg.
    BadVersion = 3,
    /// A slot header does not fit.
    TruncSlots = 4,
    /// v1 only: a slot type with no slot_len to step over.
    SlotType = 5,
    /// A slot body does not fit.
    TruncSlotBody = 6,
    /// Payload nonce + length do not fit.
    TruncPayload = 7,
    /// Payload + tag do not fit, or payload_len >= 2^63.
    TruncPayloadBody = 8,
    /// archive_size is not plausible for the container it arrived in.
    ArchiveSize = 9,
    /// More unlock slots than the ceiling — each costs a full KDF on a wrong password.
    SlotCount = 10,
}

impl SealHeaderReason {
    pub fn from_code(c: u8) -> Option<Self> {
        use SealHeaderReason::*;
        Some(match c {
            0 => Ok,
            1 => BadMagic,
            2 => TruncHeader,
            3 => BadVersion,
            4 => TruncSlots,
            5 => SlotType,
            6 => TruncSlotBody,
            7 => TruncPayload,
            8 => TruncPayloadBody,
            9 => ArchiveSize,
            10 => SlotCount,
            _ => return None,
        })
    }
}

/// One unlock slot: its type and where its body sits in the file.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct SealSlot {
    pub r#type: u32,
    pub body_off: usize,
    pub body_len: usize,
}

/// Byte offsets into a sealed file. Nothing here is secret: it is where to look, not
/// what is there. Fields past `reason` are meaningful only when `reason` is `Ok`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SealHeader {
    pub reason: SealHeaderReason,
    pub version: u32,
    pub archive_size: u64,
    pub payload_nonce_off: usize,
    pub payload_off: usize,
    pub payload_len: usize,
    pub payload_tag_off: usize,
    pub slots: Vec<SealSlot>,
}

/// Record: `[reason][version][archive_size][slot_count][payload_nonce_off][payload_off]
/// [payload_len][payload_tag_off]` (57 bytes), then per slot `[type][body_off][body_len]`.
pub fn parse_seal_header(file: &[u8]) -> Result<SealHeader> {
    let out = call1("parse_seal_header", ffi::parse_seal_header, file)?;
    let mut r = Rec::new("parse_seal_header", out.bytes());
    let code = r.u8("reason")?;
    let reason = SealHeaderReason::from_code(code).ok_or_else(|| r.err(format!("reason code {code}")))?;
    let version = r.i64("version")?;
    let version = u32::try_from(version).map_err(|_| r.err(format!("version {version}")))?;
    let archive_size = r.size("archive_size")? as u64;
    let nslots = r.size("slot_count")?;
    let payload_nonce_off = r.size("payload_nonce_off")?;
    let payload_off = r.size("payload_off")?;
    let payload_len = r.size("payload_len")?;
    let payload_tag_off = r.size("payload_tag_off")?;
    if nslots > r.remaining() / 24 {
        return Err(r.err(format!("{nslots} slots in {} bytes", r.remaining())));
    }
    let mut slots = Vec::with_capacity(nslots);
    for _ in 0..nslots {
        let t = r.i64("slot type")?;
        let t = u32::try_from(t).map_err(|_| r.err(format!("slot type {t}")))?;
        slots.push(SealSlot {
            r#type: t,
            body_off: r.size("slot body_off")?,
            body_len: r.size("slot body_len")?,
        });
    }
    Ok(SealHeader {
        reason,
        version,
        archive_size,
        payload_nonce_off,
        payload_off,
        payload_len,
        payload_tag_off,
        slots,
    })
}

// ---- parse_seal_archive -----------------------------------------------------------------

/// Why a decrypted archive index was refused — or `Ok`. The UI has one outcome for all
/// of these ("bad archive"); they are kept apart so a test can name the bound that fired.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum SealArchiveReason {
    Ok = 0,
    /// Fewer than the 4 bytes of file_count.
    Short = 1,
    /// A path or a data run runs off the end.
    Trunc = 2,
    /// Rooted, drive-qualified, empty, or a `..` component.
    UnsafePath = 3,
    /// data_len >= 2^63.
    DataLen = 4,
}

impl SealArchiveReason {
    pub fn from_code(c: u8) -> Option<Self> {
        use SealArchiveReason::*;
        Some(match c {
            0 => Ok,
            1 => Short,
            2 => Trunc,
            3 => UnsafePath,
            4 => DataLen,
            _ => return None,
        })
    }
}

/// One file in the archive: its relative path and where its bytes sit in the archive.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SealEntry {
    /// `\`-separated, as the archive index is on every platform, and already through the
    /// traversal guard. A POSIX host splits it on `\` — see [`SealEntry::components`].
    pub rel: String,
    pub data_off: usize,
    pub data_len: usize,
}

impl SealEntry {
    /// The path's components. The guard has already refused empty, rooted and `..`
    /// paths, so each component is a plain name.
    pub fn components(&self) -> impl Iterator<Item = &str> {
        self.rel.split('\\')
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SealArchiveIndex {
    pub reason: SealArchiveReason,
    pub files: Vec<SealEntry>,
}

/// Record: `[reason][file_count]` then per file `[data_off][data_len][path_len][path]`.
pub fn parse_seal_archive(archive: &[u8]) -> Result<SealArchiveIndex> {
    let out = call1("parse_seal_archive", ffi::parse_seal_archive, archive)?;
    let mut r = Rec::new("parse_seal_archive", out.bytes());
    let code = r.u8("reason")?;
    let reason = SealArchiveReason::from_code(code).ok_or_else(|| r.err(format!("reason code {code}")))?;
    let n = r.size("file_count")?;
    if n > r.remaining() / 24 {
        return Err(r.err(format!("{n} files in {} bytes", r.remaining())));
    }
    let mut files = Vec::with_capacity(n);
    for _ in 0..n {
        let data_off = r.size("data_off")?;
        let data_len = r.size("data_len")?;
        let rel = r.lp("path")?;
        files.push(SealEntry { rel, data_off, data_len });
    }
    Ok(SealArchiveIndex { reason, files })
}
