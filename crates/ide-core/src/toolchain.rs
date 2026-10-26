// SPDX-License-Identifier: GPL-3.0-or-later
//! Finding `snc` — MainWindow.cpp's findSnc. There is no Toolchain.h counterpart: on
//! macOS snc links through the system `cc`, so there is no MSVC environment to capture.

use std::env;
use std::path::{Path, PathBuf};

use crate::signing::{snc_signing_caps, SncSigningCaps};

/// Where snc is looked for, in order: `$SNC`, then `target/{release,debug}/snc` under
/// `$SENTINEL_LANG` and under a `Sentinel-lang` checkout beside `near` (the folder holding
/// this repo, in development), then `extra` (a host passes the compiler it was built with),
/// then `snc` on PATH.
pub fn snc_candidates(near: Option<&Path>, extra: &[PathBuf]) -> Vec<PathBuf> {
    let mut c = Vec::new();
    if let Some(p) = env::var_os("SNC") {
        c.push(PathBuf::from(p));
    }
    let mut roots = Vec::new();
    if let Some(p) = env::var_os("SENTINEL_LANG") {
        roots.push(PathBuf::from(p));
    }
    if let Some(n) = near {
        roots.push(n.join("Sentinel-lang"));
    }
    for r in roots {
        for profile in ["release", "debug"] {
            c.push(r.join("target").join(profile).join("snc"));
        }
    }
    c.extend(extra.iter().cloned());
    if let Some(paths) = env::var_os("PATH") {
        c.extend(env::split_paths(&paths).map(|d| d.join("snc")));
    }
    c
}

/// The most ADR-0061-capable snc among the candidates: one that can sign beats one that
/// can only verify, which beats the first that merely exists. Release is listed before
/// debug, but capability wins over position (findSnc's rule).
pub fn find_snc(near: Option<&Path>, extra: &[PathBuf]) -> Option<(PathBuf, SncSigningCaps)> {
    let mut first: Option<(PathBuf, SncSigningCaps)> = None;
    let mut verify_only: Option<(PathBuf, SncSigningCaps)> = None;
    for c in snc_candidates(near, extra).into_iter().filter(|p| p.is_file()) {
        let caps = snc_signing_caps(&c);
        if caps.sign {
            return Some((c, caps));
        }
        if caps.verify && verify_only.is_none() {
            verify_only = Some((c.clone(), caps));
        }
        first.get_or_insert((c, caps));
    }
    verify_only.or(first)
}
