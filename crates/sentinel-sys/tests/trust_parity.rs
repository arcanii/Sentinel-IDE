// SPDX-License-Identifier: GPL-3.0-or-later
//
// trust_parity — the Rust port of tests/trust_xcheck.cpp. Proves parse_trust (Sentinel)
// agrees with the C++ loadTrust it replaced. This is a SECURITY-boundary parser — it
// decides which keys a build trusts — so lockstep matters doubly.
//
// The oracle is a transcription of the C++ projTrim / projUnq / parseInlineArr /
// loadTrust (text form). The C++ worked on UTF-16 and this works on UTF-8, but every
// delimiter it looks for is ASCII, so the two split every input in the same places.

use sentinel_sys::{parse_trust, TrustedKey};

// ---- oracle ----
fn proj_unq(s: &str) -> String {
    let s = s.trim_start_matches([' ', '\t']);
    if s.is_empty() {
        return String::new();
    }
    let s = s.trim_end_matches([' ', '\t', '\r']);
    if s.len() >= 2 && s.starts_with('"') && s.ends_with('"') {
        s[1..s.len() - 1].to_string()
    } else {
        s.to_string()
    }
}

fn proj_trim(s: &str) -> &str {
    s.trim_matches([' ', '\t', '\r', '\n'])
}

fn parse_inline_arr(s: &str) -> Vec<String> {
    let (mut v, mut cur, mut inq) = (Vec::new(), String::new(), false);
    for c in s.chars() {
        if c == '"' {
            inq = !inq;
            if !inq {
                v.push(std::mem::take(&mut cur));
            }
        } else if inq {
            cur.push(c);
        }
    }
    v
}

// The C++ splits on '\n', drops one trailing '\r', and skips an empty final line.
fn lines(text: &str) -> Vec<&str> {
    let mut out: Vec<&str> = text.split('\n').map(|l| l.strip_suffix('\r').unwrap_or(l)).collect();
    if out.last() == Some(&"") {
        out.pop();
    }
    out
}

fn load_trust_text(text: &str) -> Vec<TrustedKey> {
    let mut keys: Vec<TrustedKey> = Vec::new();
    let mut open = false; // is `keys.last()` the block being filled?
    for l in lines(text) {
        let t = proj_trim(l);
        if t.is_empty() || t.starts_with('#') {
            continue;
        }
        if t.starts_with("[[") {
            open = t.contains("keys");
            if open {
                keys.push(TrustedKey::default());
            }
            continue;
        }
        if t.starts_with('[') {
            open = false;
            continue;
        }
        if !open {
            continue;
        }
        let Some(eq) = t.find('=') else { continue };
        let (k, v) = (proj_trim(&t[..eq]), &t[eq + 1..]);
        let cur = keys.last_mut().unwrap();
        match k {
            "pubkey" => cur.pubkey = proj_unq(v),
            "name" => cur.name = proj_unq(v),
            "grants" => cur.grants = parse_inline_arr(v),
            _ => {}
        }
    }
    keys
}

#[test]
fn trust_parity() {
    let cases: &[(&str, &str)] = &[
        ("empty", ""),
        ("comments only", "# just a comment\r\n  # another\r\n"),
        (
            "one full key",
            "[[keys]]\r\nname = \"Sentinel-IDE demo key\"\r\n\
             pubkey = \"58ad2d8cf5294de180a25c2cb8046f422d114dbb5c8a3a91f6483b0b9c476ca5\"\r\n\
             grants = [\"secret\", \"constant_time\", \"alloc\"]\r\n",
        ),
        ("empty key (header, no fields)", "[[keys]]\r\n"),
        (
            "two keys",
            "[[keys]]\npubkey = \"aa\"\nname = \"one\"\n\
             [[keys]]\nname = \"two\"\ngrants = [\"x\"]\npubkey = \"bb\"\n",
        ),
        ("loose match [[keystore]] treated as keys", "[[keystore]]\npubkey = \"cc\"\n"),
        ("other table closes block", "[[keys]]\npubkey = \"dd\"\n[meta]\npubkey = \"ignored\"\n"),
        (
            "grants spacing + empty array",
            "[[keys]]\ngrants=[  \"a\"  ,\"b\"]\npubkey=\"ee\"\ngrants2=[\"nope\"]\n",
        ),
        ("no-quote value kept raw", "[[keys]]\npubkey = ff_no_quotes\n"),
        ("ed25519 prefix kept verbatim", "[[keys]]\npubkey = \"ed25519:deadbeef\"\n"),
        ("non-ascii name", "[[keys]]\nname = \"clé café\"\npubkey = \"aa\"\n"),
        ("crlf and trailing spaces", "[[keys]]   \r\n  pubkey  =  \"gg\"  \r\n"),
    ];
    let mut fail = 0;
    for (label, text) in cases {
        let c = load_trust_text(text);
        let s = parse_trust(text.as_bytes()).expect("parse_trust record");
        let ok = c == s;
        println!("[{}] {label}  ({} keys)", if ok { "PASS" } else { "FAIL" }, c.len());
        if !ok {
            println!("      oracle   : {c:?}");
            println!("      Sentinel : {s:?}");
            fail += 1;
        }
    }
    println!("\n{} passed, {fail} failed", cases.len() - fail);
    assert_eq!(fail, 0, "parse_trust diverged from the C++ oracle");
}
