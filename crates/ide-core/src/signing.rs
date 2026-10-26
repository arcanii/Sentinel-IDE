// SPDX-License-Identifier: GPL-3.0-or-later
//! Signing & trust (ADR 0061) — Signing.h's. The trust manifest and the `.sig` carrier are
//! read by Sentinel (`parse_trust`, `parse_sig`); verification is `snc verify`, so the
//! decision about a signature is always the compiler's and never re-implemented here.

use std::ffi::OsString;
use std::path::{Path, PathBuf};

use crate::proc::run_capture;
use crate::{read_text_lossy, Result};

pub use sentinel_sys::TrustedKey;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum SignState {
    #[default]
    Unknown,
    Unsigned,
    Checking,
    Signed,
    Invalid,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct TrustManifest {
    /// True when the file declared at least one key — an absent file and a keyless one
    /// read the same, as in Signing.h.
    pub loaded: bool,
    pub keys: Vec<TrustedKey>,
}

/// Detached-signature fields, for display. Not a verification result.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SigInfo {
    pub present: bool,
    pub algorithm: String,
    pub key: String,
    pub grants: String,
}

pub fn load_trust(path: &Path) -> Result<TrustManifest> {
    let Some(text) = read_text_lossy(path)? else { return Ok(TrustManifest::default()) };
    if text.is_empty() {
        return Ok(TrustManifest::default());
    }
    let keys = sentinel_sys::parse_trust(text.as_bytes())?;
    Ok(TrustManifest { loaded: !keys.is_empty(), keys })
}

pub fn read_sig(sig_path: &Path) -> Result<SigInfo> {
    let Some(text) = read_text_lossy(sig_path)? else { return Ok(SigInfo::default()) };
    // Empty or missing → not present, without asking the parser (as readSig).
    if text.is_empty() {
        return Ok(SigInfo::default());
    }
    let s = sentinel_sys::parse_sig(text.as_bytes())?;
    Ok(SigInfo { present: s.present, algorithm: s.algorithm, key: s.key, grants: s.grants })
}

/// A short fingerprint for display: the first 16 hex characters and an ellipsis, as snc
/// prints them. Drops an `ed25519:` style prefix first.
pub fn short_key(key: &str) -> String {
    let k = key.split_once(':').map_or(key, |(_, rest)| rest);
    if k.chars().count() > 16 {
        format!("{}…", k.chars().take(16).collect::<String>())
    } else {
        k.to_string()
    }
}

/// `<file>.sig` — the suffix is appended to the whole name (`crypto.sentinel.sig`).
pub fn sig_path_for(file: &Path) -> PathBuf {
    let mut s: OsString = file.as_os_str().to_owned();
    s.push(".sig");
    PathBuf::from(s)
}

/// What ADR 0061 this snc can actually DO. Two capabilities, deliberately separate,
/// because they fail independently (Signing.h has the history):
///
/// * `verify` is implemented inside snc.
/// * `keygen`/`sign` shell out to `keygen_core` / `sign_core` beside the snc binary, so a
///   build can list them in its help and still fail at runtime. Probing the help text
///   alone is not evidence signing works; the helpers have to be there.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct SncSigningCaps {
    pub verify: bool,
    pub sign: bool,
}

pub fn snc_signing_caps(snc: &Path) -> SncSigningCaps {
    let Ok(help) = run_capture(snc, ["help"], None) else { return SncSigningCaps::default() };
    let out = help.output;
    let mut caps = SncSigningCaps { verify: out.contains("snc verify"), sign: false };
    if out.contains("snc keygen") && out.contains("snc sign") {
        let dir = snc.parent().unwrap_or(Path::new(""));
        caps.sign = dir.join("keygen_core").is_file() && dir.join("sign_core").is_file();
    }
    caps
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct VerifyResult {
    pub state: SignState,
    pub key: String,
    pub grants: String,
    pub message: String,
}

/// Verify `file` against `file.sig` with `snc verify`. Unsigned when there is no `.sig`.
pub fn verify_file(snc: &Path, file: &Path) -> VerifyResult {
    let sig = sig_path_for(file);
    if !sig.is_file() {
        return VerifyResult {
            state: SignState::Unsigned,
            message: "Unsigned — no .sig carrier.".into(),
            ..Default::default()
        };
    }
    let mut r = VerifyResult::default();
    match read_sig(&sig) {
        Ok(si) => {
            r.key = si.key;
            r.grants = si.grants;
        }
        // Display fields only; snc still decides below.
        Err(e) => crate::logger::log(crate::logger::LogLevel::Warn, &format!("read_sig: {e}")),
    }
    match run_capture(snc, [OsString::from("verify"), file.into(), "--sig".into(), sig.into()], None) {
        Ok(c) => {
            r.message = c.output.trim_matches([' ', '\t', '\r', '\n']).to_string();
            r.state = if c.status.success() { SignState::Signed } else { SignState::Invalid };
        }
        Err(e) => {
            r.message = format!("Could not run {}: {e}", snc.display());
            r.state = SignState::Invalid;
        }
    }
    r
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_key_matches_what_snc_prints() {
        let k = "58ad2d8cf5294de180a25c2cb8046f422d114dbb5c8a3a91f6483b0b9c476ca5";
        assert_eq!(short_key(k), "58ad2d8cf5294de1…");
        assert_eq!(short_key("ed25519:deadbeef"), "deadbeef");
        assert_eq!(short_key("0123456789abcdef"), "0123456789abcdef");
    }

    #[test]
    fn sig_path_appends_to_the_whole_name() {
        assert_eq!(sig_path_for(Path::new("a/crypto.sentinel")), Path::new("a/crypto.sentinel.sig"));
    }
}
