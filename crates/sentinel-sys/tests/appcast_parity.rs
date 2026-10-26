// SPDX-License-Identifier: GPL-3.0-or-later
//
// appcast_parity — the Rust/macOS port of tests/appcast_xcheck.cpp, the cross-check for
// the LAST parser to leave C++: the update-feed reader
// (src/sentinel/parsers.sentinel::parse_appcast), reached here through
// sentinel_sys::parse_appcast over libsentinel_parsers.dylib. It is also the only one of
// the six whose input arrives off the NETWORK, over deliberately unauthenticated HTTPS,
// so it is the one where "what does this do with bytes an attacker chose" is not a
// hypothetical.
//
// THERE IS NO WINDOWS HERE. The C++ test compiles its oracle and fallback next to
// parsers.lib; this one cannot compile Updater.cpp at all, so both are Rust
// TRANSCRIPTIONS of the C++ in tests/appcast_xcheck.cpp, statement for statement. Every
// case, every expectation and every reason string below is that test's. What running
// them here adds is the other half of the claim: parse_appcast built by snc for
// aarch64-apple-darwin, and decoded by sentinel-sys's own record reader rather than
// Updater.cpp's, reaches the same 25 verdicts the Windows build does.
//
// THREE implementations are run against every case and all three are asserted:
//
//   oracle::    a transcription of the C++ that shipped — Updater.cpp's parseVersion,
//               versionIsNewer and appcastVersion at b5a8d0b. Parity is measured
//               against what shipped, not against a tidied-up memory of it. The C++
//               accumulates into an `int` with no bound; signed overflow is undefined
//               there and MSVC wraps, so the oracle uses i32::wrapping_mul/wrapping_add.
//               Rust's plain `*` and `+` would panic in a test build, which would
//               measure Rust rather than what shipped — and cases 7, 8 and 18 print the
//               wrapped numbers, which must be the ones the C++ test prints.
//   fallback::  a transcription of Updater.cpp's `#else` branch, the C++ a build with
//               no parsers.lib compiles. A Mac build has no such branch (build.rs fails
//               loudly without snc), but the Windows sources still carry it, and it must
//               agree with Sentinel EVERYWHERE — the two are one behaviour with two
//               spellings, and this is what pins them. It is a copy, as the C++ test's
//               is: if that branch changes, both copies must change with it.
//   Sentinel    parse_appcast itself, through sentinel_sys::parse_appcast.
//
// WHERE THE ORACLE IS WRONG THIS TEST ASSERTS THE NEW BEHAVIOUR AND SAYS SO. Every case
// carries an explicit Parity/Diverges expectation plus the reason, and a Diverges case
// FAILS if the two ever agree again — so nobody can quietly restore the old behaviour
// and still be green. The two defects the port exists to close are exactly the
// divergences:
//
//   1. UNBOUNDED SIGNED OVERFLOW. `out[i] = out[i] * 10 + (*v - '0')` accumulated into
//      an `int` over however many digits the feed supplied. Cases 7, 8 and 18 print the
//      actual numbers the oracle computes.
//   2. NO VALIDATION. appcastVersion returned whatever sat between the quotes, verbatim,
//      and the host showed it, logged it, and on "Skip this version" wrote it into
//      settings.ini. Cases 3, 9, 10, 14, 15 and 16 are feeds where the oracle OFFERS an
//      update whose "version" is not a version.
//
// Run (needs snc — see build.rs for where it is looked for):
//     cargo test -p sentinel-sys --test appcast_parity -- --nocapture
// Without --nocapture the PASS/FAIL lines are still printed when the test fails. Cases
// 23-25 read the repo's real appcast.xml, two directories above this crate.

// ---------------------------------------------------------------------------
// std::string::find, which both copies of the C++ below are written against: the first
// index >= `pos` at which `needle` starts, or None for npos. A `pos` past the end is
// npos, as it is in C++. (The single-char overload is the same call with a 1-byte
// needle.)
// ---------------------------------------------------------------------------
fn find(hay: &[u8], needle: &[u8], pos: usize) -> Option<usize> {
    if pos > hay.len() {
        return None;
    }
    hay[pos..].windows(needle.len()).position(|w| w == needle).map(|i| i + pos)
}

// ---------------------------------------------------------------------------
// The oracle: src/host/win32/Updater.cpp at b5a8d0b, transcribed. DO NOT "fix" anything
// in here — its bugs are the measurement.
//
// The C++ walks a NUL-terminated `const char*`; here that pointer is an index into a
// byte slice, and `at` reads the byte under it, as `*v` would, with the end of the slice
// standing in for the terminator. A NUL inside the slice reads as 0 too, so this stops
// where c_str() would. MSVC's `char` is signed, so `at` yields i8 and every comparison
// is against an i8 '0'/'9'/'.', exactly as the C++ compares; for these comparisons the
// sign never changes an outcome (a byte >= 0x80 is below '0' signed and above '9'
// unsigned), but nothing here should have to be argued.
// ---------------------------------------------------------------------------
mod oracle {
    use super::find;

    fn at(v: &[u8], p: usize) -> i8 {
        v.get(p).map_or(0, |&b| b as i8)
    }

    // Parse "a.b.c.d" into four numbers. Missing components read as 0, so "0.1.6" == "0.1.6.0".
    pub fn parse_version(v: &[u8], out: &mut [i32; 4]) {
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        let mut p = 0usize; // the C++'s `v` pointer; `v &&` is always true for a c_str()
        let mut i = 0usize;
        while at(v, p) != 0 && i < 4 {
            while at(v, p) >= b'0' as i8 && at(v, p) <= b'9' as i8 {
                // `*v - '0'` promotes both chars to int before subtracting; `out[i] * 10`
                // and the `+` are each an int operation that overflows, and wraps.
                let digit = i32::from(at(v, p)) - i32::from(b'0' as i8);
                out[i] = out[i].wrapping_mul(10).wrapping_add(digit);
                p += 1;
            }
            if at(v, p) == b'.' as i8 {
                p += 1;
                i += 1;
            } else {
                break;
            }
        }
    }
    pub fn version_is_newer(candidate: &[u8], mine: &[u8]) -> bool {
        let mut a = [0i32; 4];
        let mut b = [0i32; 4];
        parse_version(candidate, &mut a);
        parse_version(mine, &mut b);
        for i in 0..4 {
            if a[i] != b[i] {
                return a[i] > b[i];
            }
        }
        false
    }

    // Pull sparkle:version="..." out of the feed. We publish this file, so a targeted match
    // beats dragging in an XML parser.
    pub fn appcast_version(body: &[u8]) -> Vec<u8> {
        let key: &[u8] = b"sparkle:version=\"";
        let Some(a) = find(body, key, 0) else { return Vec::new() };
        let Some(b) = find(body, b"\"", a + key.len()) else { return Vec::new() };
        body[a + key.len()..b].to_vec()
    }

    // The two shipped call sites both did exactly this: take the string, refuse an empty
    // one, and otherwise compare. Reproduced here so "what the oracle would have done"
    // means what the IDE would have done, not what the functions return in isolation.
    // (The C++ returns through two out-parameters; here it is a (version, offer) pair.)
    pub fn run(body: &[u8], mine: &[u8]) -> (Vec<u8>, bool) {
        let ver = appcast_version(body);
        let newer = !ver.is_empty() && version_is_newer(&ver, mine);
        (ver, newer)
    }
}

// ---------------------------------------------------------------------------
// The fallback: src/host/win32/Updater.cpp's `#else` branch (plus AppcastVerdict, which
// is declared above the #ifdef there), transcribed. This is the C++ a snc-less Windows
// build compiles. It must match Sentinel on every case below.
//
// Its `std::string`s are byte slices here. Its `char` comparisons are against '.', '0'
// and '9' only, where signed and unsigned bytes give the same answer, so they are
// written over u8 directly.
// ---------------------------------------------------------------------------
#[allow(clippy::manual_range_contains)] // the C++ spells its ranges out; so does the copy
mod fallback {
    use super::find;

    #[derive(Debug, Clone, Default, PartialEq, Eq)]
    pub struct AppcastVerdict {
        pub found: bool,      // sparkle:version="…" located (opening AND closing quote)
        pub valid: bool,      // ... and what sat between them is a well-formed version
        pub newer: bool,      // ... and it is strictly newer than the running build
        pub version: Vec<u8>, // the version — ONLY when valid; empty otherwise
    }

    // The version grammar, and the only one accepted: 1..4 components, 1..9 digits each,
    // single dots, nothing else. Nine digits caps a component at 999,999,999, so
    // acVersionComp's accumulate cannot overflow a long long — and a longer component
    // rejects the WHOLE version rather than wrapping or being truncated, because a number
    // the feed never stated is exactly what decides an update the wrong way.
    pub fn ac_version_valid(v: &[u8]) -> bool {
        if v.is_empty() || v.len() > 39 {
            return false;
        }
        let mut dots: i32 = 0;
        let mut digits: i32 = 0;
        for &c in v {
            if c == b'.' {
                // `if (digits == 0 || ++dots > 3) return false;` — the increment only
                // happens when the first test fails, as the || short-circuits.
                if digits == 0 {
                    return false; // empty component
                }
                dots += 1;
                if dots > 3 {
                    return false; // 5th component
                }
                digits = 0;
            } else if c >= b'0' && c <= b'9' {
                digits += 1;
                if digits > 9 {
                    return false; // the overflow bound
                }
            } else {
                return false; // not a digit or a dot
            }
        }
        digits > 0 // rejects a trailing '.'
    }
    // Component k (0-based); missing trailing components read as 0, so "0.1.6" == "0.1.6.0".
    // PRECONDITION: acVersionValid(v). The `digits < 9` guard is therefore unreachable and
    // is written anyway, so this cannot overflow for any input at all.
    pub fn ac_version_comp(v: &[u8], k: i32) -> i64 {
        let mut cur: i32 = 0;
        let mut digits: i32 = 0;
        let mut acc: i64 = 0;
        for &c in v {
            if c == b'.' {
                cur += 1;
                digits = 0;
            } else if cur == k && c >= b'0' && c <= b'9' && digits < 9 {
                acc = acc * 10 + i64::from(c - b'0');
                digits += 1;
            }
        }
        acc
    }
    pub fn read_appcast(body: &[u8], mine: &[u8]) -> AppcastVerdict {
        let mut v = AppcastVerdict::default();
        // FIRST match, not highest — see parse_appcast for why. Searching for the name
        // without the quote and then requiring the quote reproduces find()ing the whole
        // `sparkle:version="` literal: an unquoted occurrence is not a match and the scan
        // continues past it, but once the quote is seen the search stops either way.
        let key: &[u8] = b"sparkle:version=";
        let mut next = find(body, key, 0);
        while let Some(at) = next {
            // The for-loop's increment, `at = body.find(key, at + 1)`, taken up front so
            // that `continue` reaches it as it does in C++. Nothing below reads `next`.
            next = find(body, key, at + 1);
            if at + key.len() >= body.len() || body[at + key.len()] != b'"' {
                continue;
            }
            let s = at + key.len() + 1;
            let q = find(body, b"\"", s);
            if let Some(q) = q {
                v.found = true;
                v.version = body[s..q].to_vec();
            }
            break;
        }
        if !v.found {
            return v;
        }
        v.valid = ac_version_valid(&v.version);
        if !v.valid {
            v.version.clear(); // an invalid version is never handed back
            return v;
        }
        if !ac_version_valid(mine) {
            return v; // our own version unreadable -> nothing is newer
        }
        for i in 0..4 {
            let a = ac_version_comp(&v.version, i);
            let b = ac_version_comp(mine, i);
            if a != b {
                v.newer = a > b;
                break;
            }
        }
        v
    }
}

// ---- Sentinel side: the same call Updater.cpp's #ifdef branch makes ----------
// sentinel_sys decodes the record ([found][valid][newer][len][version bytes]) and keeps
// the version only when valid and non-empty — the rule the C++ decoder applies — so
// mapping None to an empty string gives the shape fallback::AppcastVerdict has. One
// difference from the C++ decoder: a record too short to read is an Err here where the
// C++ silently reads it as "nothing found"; this test reports that as a FAIL rather
// than let it pass as case 1's answer.
fn sentinel_read_appcast(body: &[u8], mine: &str) -> sentinel_sys::Result<fallback::AppcastVerdict> {
    let s = sentinel_sys::parse_appcast(body, mine)?;
    Ok(fallback::AppcastVerdict {
        found: s.found,
        valid: s.valid,
        newer: s.newer,
        version: s.version.map(String::into_bytes).unwrap_or_default(),
    })
}

// ---- harness ---------------------------------------------------------------
#[derive(Clone, Copy, PartialEq, Eq)]
enum Rel {
    Parity,
    Diverges,
}

// The C++'s `static int pass = 0, fail = 0;`.
#[derive(Default)]
struct Tally {
    pass: u32,
    fail: u32,
}

// A minimal but realistic appcast around one sparkle:version value.
fn feed(ver: &str) -> String {
    String::new()
        + "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        + "<rss version=\"2.0\" xmlns:sparkle=\"http://www.andymatuschak.org/xml-namespaces/sparkle\">\n"
        + "  <channel>\n"
        + "    <title>Sentinel-IDE Updates</title>\n"
        + "    <item>\n"
        + "      <title>Version "
        + ver
        + "</title>\n"
        + "      <enclosure url=\"https://example.invalid/Sentinel-IDE-setup.exe\"\n"
        + "                 sparkle:version=\""
        + ver
        + "\"\n"
        + "                 sparkle:edSignature=\"AAAA\"\n"
        + "                 length=\"1\"\n"
        + "                 type=\"application/octet-stream\"/>\n"
        + "    </item>\n"
        + "  </channel>\n"
        + "</rss>\n"
}

fn show(b: &[u8]) -> std::borrow::Cow<'_, str> {
    String::from_utf8_lossy(b)
}

impl Tally {
    // The C++ check(), argument for argument, so the cases below read as they do there.
    #[allow(clippy::too_many_arguments)]
    fn check(
        &mut self,
        name: &str,
        body: impl AsRef<[u8]>,
        mine: &str,
        e_found: bool,
        e_valid: bool,
        e_newer: bool,
        e_version: &str,
        rel: Rel,
        why: &str,
    ) {
        let body = body.as_ref();
        let s = match sentinel_read_appcast(body, mine) {
            Ok(s) => s,
            Err(e) => {
                println!("[FAIL] {name:<46} sentinel_sys::parse_appcast: {e}");
                self.fail += 1;
                return;
            }
        };
        let f = fallback::read_appcast(body, mine.as_bytes());
        let (ov, on) = oracle::run(body, mine.as_bytes());

        // (a) Sentinel does what this test says it must.
        let asserted = s.found == e_found
            && s.valid == e_valid
            && s.newer == e_newer
            && s.version == e_version.as_bytes();
        // (b) the C++ fallback is the same behaviour, not a second opinion.
        let agree = f.found == s.found && f.valid == s.valid && f.newer == s.newer && f.version == s.version;
        // (c) the relationship to the shipped C++ is the one we declared.
        let same = ov == s.version && on == s.newer;
        let rel_ok = if rel == Rel::Parity { same } else { !same };

        let ok = asserted && agree && rel_ok;
        println!(
            "[{}] {:<46} {}",
            if ok { "PASS" } else { "FAIL" },
            name,
            if rel == Rel::Parity { "(parity)" } else { "(DIVERGES)" }
        );
        if !ok || rel == Rel::Diverges {
            let mut oc = [0i32; 4];
            oracle::parse_version(&ov, &mut oc);
            println!(
                "        shipped C++ : version=\"{}\" -> [{},{},{},{}] offer={}",
                show(&ov),
                oc[0],
                oc[1],
                oc[2],
                oc[3],
                if on { "YES" } else { "no" }
            );
            println!(
                "        Sentinel    : found={} valid={} newer={} version=\"{}\"",
                u8::from(s.found),
                u8::from(s.valid),
                u8::from(s.newer),
                show(&s.version)
            );
            if rel == Rel::Diverges {
                println!("        why         : {why}");
            }
        }
        if !ok {
            if !asserted {
                println!(
                    "        !! expected found={} valid={} newer={} version=\"{}\"",
                    u8::from(e_found),
                    u8::from(e_valid),
                    u8::from(e_newer),
                    e_version
                );
            }
            if !agree {
                println!(
                    "        !! C++ fallback disagrees with Sentinel: found={} valid={} newer={} version=\"{}\"",
                    u8::from(f.found),
                    u8::from(f.valid),
                    u8::from(f.newer),
                    show(&f.version)
                );
            }
            if !rel_ok {
                println!(
                    "        !! expected {} against the shipped C++, got the opposite",
                    if rel == Rel::Parity { "parity" } else { "a divergence" }
                );
            }
        }
        if ok {
            self.pass += 1;
        } else {
            self.fail += 1;
        }
    }
}

// The last dotted component of a version, or -1 if there isn't a numeric one. Used only
// to build "one build older" / "one build newer" out of whatever the feed publishes.
// The accumulate is the C++'s unbounded `long long` one, wrapped rather than left to
// panic; the result is only ever used once acVersionValid has passed, which bounds the
// component to nine digits, so the wrap is never reached on a value that matters.
fn last_component(v: &[u8]) -> i64 {
    let tail = match v.iter().rposition(|&c| c == b'.') {
        None => v,
        Some(dot) => &v[dot + 1..],
    };
    if tail.is_empty() {
        return -1;
    }
    let mut n: i64 = 0;
    for &c in tail {
        if !c.is_ascii_digit() {
            return -1;
        }
        n = n.wrapping_mul(10).wrapping_add(i64::from(c - b'0'));
    }
    n
}
fn bump_last(v: &str, delta: i64) -> String {
    let head = match v.rfind('.') {
        None => "",
        Some(dot) => &v[..dot + 1],
    };
    format!("{}{}", head, last_component(v.as_bytes()) + delta)
}

// The repo's real, published feed. The C++ takes this path as argv[1] (ctest passes
// ${CMAKE_CURRENT_SOURCE_DIR}/appcast.xml); a cargo test has no argv of its own, so the
// path is fixed relative to this crate instead.
const REAL_APPCAST: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../appcast.xml");

// The cases keep the C++ table's layout — name, body, mine, then the expectation on one
// line and the reason under it — which rustfmt would explode into one argument per line.
#[rustfmt::skip]
#[test]
fn appcast_parity() {
    let mut t = Tally::default();
    let mine = "0.1.12.196"; // a real shipped SENTINEL_FILEVERSION_STR

    // ---- 1-2: nothing to read. Both refuse; nothing is offered. -------------
    t.check("1  no sparkle:version in the feed",
            "<rss><channel><item><title>Version 9</title></item></channel></rss>",
            mine, false, false, false, "", Rel::Parity, "");

    t.check("2  opening quote, no closing quote at all",
            "<enclosure sparkle:version=\"0.1.13.0", mine,
            false, false, false, "", Rel::Parity, "");

    // ---- 3: the same defect with a later quote in the document -------------
    // find('"') does not know what an attribute is, so it walks into the NEXT one.
    t.check("3  unterminated attribute, later quote in feed",
            "<enclosure sparkle:version=\"0.1.13.0 length=\"1\"/>", mine,
            true, false, false, "", Rel::Diverges,
            concat!("the shipped reader returns '0.1.13.0 length=' and OFFERS it: parseVersion ",
                    "stops at the space having read 0.1.13.0, and that XML fragment is what the ",
                    "dialog shows and 'Skip this version' writes into settings.ini"));

    // ---- 4: sparkle:version="" ---------------------------------------------
    t.check("4  empty value between the quotes", feed(""), mine,
            true, false, false, "", Rel::Parity, "");

    // ---- 5-6: THE trap. A string compare says 0.1.10 < 0.1.9. --------------
    t.check("5  0.1.10 is newer than 0.1.9", feed("0.1.10"), "0.1.9",
            true, true, true, "0.1.10", Rel::Parity, "");
    t.check("6  0.1.9 is NOT newer than 0.1.10", feed("0.1.9"), "0.1.10",
            true, true, false, "0.1.9", Rel::Parity, "");

    // ---- 7-8: defect 1, unbounded signed overflow ---------------------------
    t.check("7  14-digit component, single", feed("99999999999999"), mine,
            true, false, false, "", Rel::Diverges,
            concat!("14 digits overflow the shipped `int`: it computes 276447231 (from digits that ",
                    "state 99999999999999) and OFFERS the feed as newer"));
    t.check("8  10-digit component, wraps negative", feed("0.1.2147483648.0"), mine,
            true, false, false, "", Rel::Diverges,
            concat!("2147483648 wraps to -2147483648, so the shipped reader ranks a hugely higher ",
                    "version BELOW 0.1.12.196 and suppresses it — the same arithmetic, failing the ",
                    "other way"));

    // ---- 9-10: defect 2, no validation. Both of these get OFFERED. ---------
    t.check("9  version is prose", feed("99 red balloons"), mine,
            true, false, false, "", Rel::Diverges,
            concat!("parseVersion reads 99, stops at the space, and 99 > 0 — so the shipped reader ",
                    "offers an update called '99 red balloons' and can persist that string"));
    t.check("10 pre-release tag", feed("0.1.13-rc1"), mine,
            true, false, false, "", Rel::Diverges,
            concat!("reads 0.1.13, stops at '-', offers; the string shown and persisted is ",
                    "'0.1.13-rc1', which our feed never emits"));

    // ---- 11: a 200 that is not an appcast ----------------------------------
    t.check("11 GitHub 404 HTML page as the body",
            concat!("<!DOCTYPE html>\n<html lang=\"en\">\n<head><title>Page not found &middot; GitHub</title>\n",
                    "<meta name=\"viewport\" content=\"width=device-width\">\n</head>\n",
                    "<body class=\"px-2\"><h1>404</h1><p>File not found</p>\n",
                    "<p>The site configured at this address does not contain the requested file.</p>\n",
                    "</body></html>\n"),
            mine, false, false, false, "", Rel::Parity, "");

    // ---- 12-13: which occurrence wins --------------------------------------
    // FIRST, deliberately: our published feed carries exactly one <item>, first-match is
    // what shipped, and it bounds what a 256 KB body can steer us to.
    {
        let mut two = feed("0.1.13.200");
        two += &feed("9.9.9.9");
        t.check("12 two entries — the FIRST one wins", &two, mine,
                true, true, true, "0.1.13.200", Rel::Parity, "");
    }
    t.check("13 unquoted occurrence is skipped, next one wins",
            "<a sparkle:version=0.1.99.0 /><b sparkle:version=\"0.1.13.200\" />", mine,
            true, true, true, "0.1.13.200", Rel::Parity, "");

    // ---- 14-16: malformed shapes the oracle silently repairs ---------------
    t.check("14 trailing dot", feed("0.1.12."), "0.1.11.0",
            true, false, false, "", Rel::Diverges,
            "the shipped reader reads 0.1.12.0 and offers, showing '0.1.12.' as the version");
    t.check("15 empty component", feed("0.1..12"), "0.1.0.0",
            true, false, false, "", Rel::Diverges,
            "the shipped reader reads 0.1.0.12 and offers, showing '0.1..12'");
    t.check("16 five components", feed("0.1.12.196.7"), "0.1.12.195",
            true, false, false, "", Rel::Diverges,
            concat!("the shipped reader reads four and IGNORES the fifth, so '1.2.3.4.5' and ",
                    "'1.2.3.4' compare equal; we refuse rather than compare part of a version. ",
                    "make-appcast.ps1 enforces ^\\d+\\.\\d+\\.\\d+\\.\\d+$, so this cannot suppress ",
                    "a real release"));

    // ---- 17-18: the digit bound, from both sides ---------------------------
    t.check("17 nine digits is accepted (the bound)", feed("999999999.0.0.0"), mine,
            true, true, true, "999999999.0.0.0", Rel::Parity, "");
    t.check("18 ten digits is refused (the cost of the bound)", feed("1000000000.0.0.0"), mine,
            true, false, false, "", Rel::Diverges,
            concat!("1000000000 still fits an int, so the oracle is right here and we are stricter. ",
                    "This is the price of a bound that cannot be argued with; our build numbers are ",
                    "git commit counts, so a 10-digit component is not a version we can emit"));

    // ---- 19-21: missing trailing components read as 0 ----------------------
    t.check("19 0.1.6 == 0.1.6.0 (no update)", feed("0.1.6"), "0.1.6.0",
            true, true, false, "0.1.6", Rel::Parity, "");
    t.check("20 0.1.6.0 == 0.1.6 (no update, reversed)", feed("0.1.6.0"), "0.1.6",
            true, true, false, "0.1.6.0", Rel::Parity, "");
    t.check("21 0.1.6.1 is newer than 0.1.6", feed("0.1.6.1"), "0.1.6",
            true, true, true, "0.1.6.1", Rel::Parity, "");

    // ---- 22: our OWN version unreadable -> nothing is newer ----------------
    // Unreachable in the product (mine is SENTINEL_FILEVERSION_STR, a compile-time
    // constant), which is exactly why it is worth pinning.
    t.check("22 unparseable `mine` fails closed", feed("0.1.13.0"), "0.1.12.196.junk",
            true, true, false, "0.1.13.0", Rel::Diverges,
            concat!("the shipped reader parses four components out of our own bad version and ",
                    "offers; nothing is newer than a version we cannot read"));

    // ---- 23-25: the real published feed ------------------------------------
    // The published version is READ OUT of the feed, not pinned here as a literal. It
    // was pinned once, and the release that bumped appcast.xml to 0.1.13.202 turned this
    // test red without changing anything it is about — a maintenance trap that fires
    // every release. What these three cases actually assert is the RELATION: the
    // published version is well-formed, the same version is not an update, one build
    // older is, one build newer is not. The extraction is oracle::appcast_version, so the
    // string never comes from the code under test.
    match std::fs::read(REAL_APPCAST) {
        Err(e) => {
            println!("[FAIL] could not read the real appcast at \"{REAL_APPCAST}\": {e}");
            t.fail += 1;
        }
        Ok(body) => {
            let published = oracle::appcast_version(&body);
            let last = last_component(&published);
            if !fallback::ac_version_valid(&published) || last < 1 {
                println!(
                    "[FAIL] the published feed's sparkle:version is \"{}\" — not a version this \
                     build could ever offer, or a build number below 1",
                    show(&published)
                );
                t.fail += 1;
            } else {
                // Valid means digits and dots only, so this is ASCII and cannot fail.
                let p = String::from_utf8(published).expect("a valid version is ASCII");
                println!("       (published feed says {p})");
                t.check("23 real appcast.xml, same version (no update)", &body, &p,
                        true, true, false, &p, Rel::Parity, "");
                t.check("24 real appcast.xml, older build (update)", &body, &bump_last(&p, -1),
                        true, true, true, &p, Rel::Parity, "");
                t.check("25 real appcast.xml, newer build (no update)", &body, &bump_last(&p, 1),
                        true, true, false, &p, Rel::Parity, "");
            }
        }
    }

    println!("\n{} passed, {} failed", t.pass, t.fail);
    assert_eq!(t.fail, 0, "appcast parity: {} case(s) failed — see the [FAIL] lines above", t.fail);
}
