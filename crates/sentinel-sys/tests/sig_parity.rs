// SPDX-License-Identifier: GPL-3.0-or-later
//
// sig_parity — the Rust port of tests/sig_xcheck.cpp. Proves parse_sig (Sentinel) agrees
// with the C++ readSig it replaced, over the human-readable `.sig` carrier.
//
// The oracle is a transcription of the C++ projTrim + readSig (text form). Every
// delimiter it looks for is ASCII, so splitting UTF-8 here and UTF-16 there agree.

use sentinel_sys::{parse_sig, SigFields};

// ---- oracle ----
fn proj_trim(s: &str) -> &str {
    s.trim_matches([' ', '\t', '\r', '\n'])
}

fn read_sig_text(text: &str) -> SigFields {
    let mut s = SigFields::default();
    if text.is_empty() {
        return s;
    }
    s.present = true;
    let mut lines: Vec<&str> = text.split('\n').map(|l| l.strip_suffix('\r').unwrap_or(l)).collect();
    if lines.last() == Some(&"") {
        lines.pop();
    }
    for l in lines {
        let t = proj_trim(l);
        let Some(c) = t.find(':') else { continue };
        let (k, v) = (proj_trim(&t[..c]), proj_trim(&t[c + 1..]));
        match k {
            "algorithm" => s.algorithm = v.to_string(),
            "key" => s.key = v.to_string(),
            "grants" => s.grants = v.to_string(),
            _ => {}
        }
    }
    s
}

#[test]
fn sig_parity() {
    let cases: &[(&str, &str)] = &[
        (
            "real crypto.sentinel.sig",
            "sentinel-signature v1\r\nalgorithm: ed25519\r\n\
             key: 58ad2d8cf5294de180a25c2cb8046f422d114dbb5c8a3a91f6483b0b9c476ca5\r\n\
             grants: secret,constant_time,alloc\r\n\
             signature: af13dd7c1d4306c8\r\n",
        ),
        ("empty (present=false)", ""),
        ("whitespace only (present=true, no fields)", "  \r\n\t\n"),
        ("first line no colon is skipped", "header line\nkey: abc\n"),
        ("value contains a colon (split on first)", "key: ed25519:deadbeef\n"),
        ("unknown keys ignored", "foo: bar\nsignature: xyz\nversion: 9\n"),
        ("last-wins", "key: first\nkey: second\nalgorithm: a\n"),
        ("extra whitespace around key and value", "   algorithm   :    ed448   \n"),
        ("no trailing newline", "key: nolinefeed"),
        ("non-ascii grant value", "grants: café,secret\nkey: aa\n"),
        ("empty value", "key:\nalgorithm: x\n"),
        ("colon with no key", ": orphan\nkey: k\n"),
    ];
    let mut fail = 0;
    for (label, text) in cases {
        let c = read_sig_text(text);
        let s = parse_sig(text.as_bytes()).expect("parse_sig record");
        let ok = c == s;
        println!("[{}] {label}", if ok { "PASS" } else { "FAIL" });
        if !ok {
            println!("      oracle   : {c:?}");
            println!("      Sentinel : {s:?}");
            fail += 1;
        }
    }
    println!("\n{} passed, {fail} failed", cases.len() - fail);
    assert_eq!(fail, 0, "parse_sig diverged from the C++ oracle");
}

// The committed demo carrier, read from disk — what verifyFile shows beside a signature.
#[test]
fn reads_the_committed_demo_signature() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../../examples/crypto.sentinel.sig");
    let s = parse_sig(&std::fs::read(path).unwrap()).unwrap();
    assert!(s.present);
    assert_eq!(s.algorithm, "ed25519");
    assert!(s.key.starts_with("58ad2d8cf5294de1"), "key = {}", s.key);
    assert_eq!(s.grants, "secret,constant_time,alloc");
}
