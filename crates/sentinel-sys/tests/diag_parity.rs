// SPDX-License-Identifier: GPL-3.0-or-later
//
// diag_parity — the Rust port of tests/diag_xcheck.cpp. Proves parse_diag (Sentinel, in
// src/sentinel/parsers.sentinel) agrees with the C++ parseDiag it replaced, on this
// platform and through this crate's decoder.
//
// The oracle below is a transcription of MainWindow.cpp's `#else` parseDiag. Two things
// had to be decided to transcribe it, and both are chosen to keep its meaning:
//
//   * It walked UTF-16 code units and reported UTF-16 offsets; this walks chars and
//     reports BYTE offsets, because Sentinel reports byte offsets and Rust strings are
//     UTF-8. The C++ test converted Sentinel's bytes to UTF-16 to compare; this compares
//     in bytes. Same question, other unit.
//   * `iswalnum` accepts non-ASCII letters, so the walk-back keeps `café/` whole; that is
//     `char::is_alphanumeric` here. Sentinel gets the same answer a different way — its
//     `is_path` accepts any byte >= 0x80.
//
// The line number accumulates into an `int`, as it did; the cases never overflow it.

use sentinel_sys::{parse_diag, Diag};

#[derive(Debug, PartialEq)]
struct Found {
    file: String,
    line: i64,
    col: i64,
    ts: usize,
    te: usize,
}

// ---- transcription of the C++ parseDiag (the oracle) ----
fn oracle(s: &str) -> Option<Found> {
    let b = s.as_bytes();
    let e = s.find(".sentinel")?;
    let p = e + 9;
    if p >= b.len() || b[p] != b':' {
        return None;
    }
    let mut q = p + 1;
    if q >= b.len() || !b[q].is_ascii_digit() {
        return None;
    }
    let mut ln: i32 = 0;
    while q < b.len() && b[q].is_ascii_digit() {
        ln = ln.wrapping_mul(10).wrapping_add((b[q] - b'0') as i32);
        q += 1;
    }
    let mut col: i32 = 1;
    if q < b.len() && b[q] == b':' {
        q += 1;
        let mut c: i32 = 0;
        let mut any = false;
        while q < b.len() && b[q].is_ascii_digit() {
            c = c.wrapping_mul(10).wrapping_add((b[q] - b'0') as i32);
            q += 1;
            any = true;
        }
        if any {
            col = c;
        }
    }
    let is_path = |ch: char| ch.is_alphanumeric() || matches!(ch, '_' | '.' | '\\' | '/' | ':' | '-' | ' ');
    let mut start = e;
    for (i, ch) in s[..e].char_indices().rev() {
        if !is_path(ch) || ch == '[' {
            break;
        }
        start = i;
    }
    let mut ts = start;
    while ts < b.len() && b[ts] == b' ' {
        ts += 1;
    }
    Some(Found { file: s[ts..e + 9].to_string(), line: ln as i64, col: col as i64, ts, te: q })
}

fn sentinel(s: &str) -> Option<Found> {
    let d = parse_diag(s.as_bytes()).expect("parse_diag record");
    d.map(|Diag { file, line, col, token }| Found { file, line, col, ts: token.start, te: token.end })
}

#[test]
fn diag_parity() {
    let cases = [
        "snc: error: crypto.sentinel:3:5: mismatched types",
        "src/main.sentinel:12: warning: unused",
        "no diagnostic on this line at all",
        "  G:\\proj\\src\\a.sentinel:7:2  something",
        "crypto.sentinel.sig:3:5 not a real diag (.sentinel not followed by colon)",
        "trailing.sentinel:99",                     // no col
        "[dependency] pulls in lib.sentinel:4:1",   // '[' stops the walk-back
        "multi a.sentinel:1:1 then b.sentinel:2:2", // first wins
        ".sentinel:5:5 degenerate empty-name",
        "exactly.sentinel:0:0 zeros",
        "café/app.sentinel:8:3 non-ascii dir", // UTF-8 multibyte in path
    ];
    let mut fail = 0;
    for line in cases {
        let (c, s) = (oracle(line), sentinel(line));
        let ok = c == s;
        println!("[{}] {line:?}", if ok { "PASS" } else { "FAIL" });
        if !ok {
            println!("      oracle   : {c:?}");
            println!("      Sentinel : {s:?}");
            fail += 1;
        }
    }
    println!("\n{} passed, {fail} failed", cases.len() - fail);
    assert_eq!(fail, 0, "parse_diag diverged from the C++ oracle");
}

// Not a parity case: pins what the decoder hands the Output pane for a link.
#[test]
fn token_is_a_byte_range_of_the_input() {
    let line = "café/app.sentinel:8:3 non-ascii dir";
    let d = parse_diag(line.as_bytes()).unwrap().unwrap();
    assert_eq!(&line[d.token.clone()], "café/app.sentinel:8:3");
    assert_eq!(d.file, "café/app.sentinel");
}
