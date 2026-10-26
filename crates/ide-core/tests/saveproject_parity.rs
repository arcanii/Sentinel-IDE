// SPDX-License-Identifier: GPL-3.0-or-later
//
// saveproject_parity — the Rust port of tests/saveproject_xcheck.cpp. Proves the Sentinel
// manifest WRITER (save_manifest) produces byte-identical output to the C++ saveProject it
// replaced, over the same 27 manifests and model changes.
//
// The oracle below is a transcription of Project.h's `#else` saveProject — pure text
// processing apart from its file I/O, so unlike manifest_parity this one really does run
// its oracle on macOS. Both sides render values with the same toml_str / toml_arr
// (ide_core::project), as both C++ sides shared tomlStr / tomlArr: two renderers that have
// to agree is the silent divergence this test exists to catch.
//
// Two differences from the C++ test, neither of which changes what is compared:
//   * The model is built by ide-core's load_project, i.e. through parse_manifest, where
//     the C++ test used the C++ loadProject. manifest_parity is what says those agree.
//   * The oracle works on Rust strings, not UTF-16. It splits and searches only on ASCII,
//     so every case — all valid UTF-8, see the note below — splits in the same places.
//
// KNOWN, DELIBERATE DIVERGENCE — not tested, as in the C++. save_manifest is byte-
// transparent; the C++ round-tripped through UTF-16 and rewrote invalid UTF-8 to U+FFFD.
// Every case feeds valid UTF-8, where the two agree exactly.

use std::fs;
use std::path::PathBuf;

use ide_core::project::{load_project, toml_arr, toml_str, Project, ProjectType};

// ---- oracle: transcription of the C++ saveProject (#else branch) ----
fn proj_trim(s: &str) -> &str {
    s.trim_matches([' ', '\t', '\r', '\n'])
}

fn oracle_save(text: &str, p: &Project) -> String {
    struct Kv {
        section: &'static str,
        key: &'static str,
        value: String,
        written: bool,
    }
    let kv = |section, key, value| Kv { section, key, value, written: false };
    let mut kv = vec![
        kv("project", "name", toml_str(&p.name)),
        kv("project", "version", toml_str(&p.version)),
        kv("project", "type", toml_str(p.r#type.name())),
        kv("project", "entry", toml_str(&p.entry)),
        kv("build", "src", toml_str(&p.src_dir)),
        kv("build", "lib_paths", toml_arr(&p.lib_paths)),
        kv("build", "links", toml_arr(&p.links)),
        kv("build", "default_tier", toml_str(p.default_tier.dir())),
        kv("signing", "require", toml_str(&p.sign_require)),
        kv("signing", "trust", toml_str(&p.trust)),
        kv("signing", "sign", if p.sign_output { "true" } else { "false" }.into()),
    ];

    // Parse into ordered sections (the first, name-empty section is the preamble).
    struct Section {
        name: String,
        lines: Vec<String>,
    }
    let mut secs = vec![Section { name: String::new(), lines: vec![] }];
    let mut push_line = |l: String| {
        let t = proj_trim(&l);
        if t.starts_with('[') {
            let name = match t.find(']') {
                Some(e) => proj_trim(&t[1..e]).to_lowercase(),
                None => String::new(),
            };
            secs.push(Section { name, lines: vec![] });
        }
        secs.last_mut().unwrap().lines.push(l);
    };
    let mut cur = String::new();
    for c in text.chars() {
        if c == '\n' {
            if cur.ends_with('\r') {
                cur.pop();
            }
            push_line(std::mem::take(&mut cur));
        } else {
            cur.push(c);
        }
    }
    if !cur.is_empty() {
        push_line(cur);
    }

    // Replace existing managed values in place (skip each section's header line).
    for sec in &mut secs {
        let from = if sec.name.is_empty() { 0 } else { 1 };
        for line in sec.lines.iter_mut().skip(from) {
            let t = proj_trim(line);
            if t.is_empty() || t.starts_with('#') || t.starts_with('[') {
                continue;
            }
            let Some(eq) = line.find('=') else { continue };
            let key = proj_trim(&line[..eq]).to_string();
            if let Some(e) = kv.iter_mut().find(|e| !e.written && e.section == sec.name && e.key == key) {
                *line = format!("{} {}", &line[..eq + 1], e.value);
                e.written = true;
            }
        }
    }

    // Insert any still-missing managed keys into their (existing or new) section.
    for sname in ["project", "build", "signing"] {
        let mut missing = Vec::new();
        for e in kv.iter_mut().filter(|e| !e.written && e.section == sname) {
            missing.push(format!("{} = {}", e.key, e.value));
            e.written = true;
        }
        if missing.is_empty() {
            continue;
        }
        let idx = match secs.iter().position(|s| s.name == sname) {
            Some(i) => i,
            None => {
                secs.push(Section { name: sname.into(), lines: vec![String::new(), format!("[{sname}]")] });
                secs.len() - 1
            }
        };
        secs[idx].lines.extend(missing);
    }

    // Rewrite values inside [[target]] blocks in place, matched by order. The double-bracket
    // header parses to the section name "[target"; only existing lines are updated.
    let mut ti = 0;
    for sec in secs.iter_mut().filter(|s| s.name == "[target") {
        if let Some(tg) = p.targets.get(ti) {
            for line in sec.lines.iter_mut().skip(1) {
                let t = proj_trim(line);
                if t.is_empty() || t.starts_with('#') || t.starts_with('[') {
                    continue;
                }
                let Some(eq) = line.find('=') else { continue };
                let val = match proj_trim(&line[..eq]) {
                    "name" => toml_str(&tg.name),
                    "entry" => toml_str(&tg.entry),
                    "type" => toml_str(tg.r#type.name()),
                    "links" => toml_arr(&tg.links),
                    _ => continue,
                };
                *line = format!("{} {val}", &line[..eq + 1]);
            }
        }
        ti += 1;
    }

    // Re-emit (CRLF, single trailing newline).
    let mut out = String::new();
    for sec in &secs {
        for line in &sec.lines {
            out.push_str(line);
            out.push_str("\r\n");
        }
    }
    while out.len() >= 4 && out.ends_with("\r\n\r\n") {
        out.truncate(out.len() - 2);
    }
    out
}

// ---- the cases ----
type Mutate = fn(&mut Project);

fn dir() -> PathBuf {
    let d = std::env::temp_dir().join(format!("sntxsave_{}", std::process::id()));
    fs::create_dir_all(&d).unwrap();
    d
}

// The first differing byte — a diff of two near-identical manifests is unreadable.
fn report(what: &str, a: &[u8], b: &[u8]) {
    let i = a.iter().zip(b).take_while(|(x, y)| x == y).count();
    let win = |s: &[u8]| {
        let lo = i.saturating_sub(12);
        let hi = (i + 12).min(s.len());
        String::from_utf8_lossy(&s[lo.min(s.len())..hi]).replace('\r', "\\r").replace('\n', "\\n")
    };
    println!("  [FAIL] {what}");
    println!("         oracle   {} bytes, sentinel {} bytes, first diff at {i}", a.len(), b.len());
    println!("         oracle   ...{}...", win(a));
    println!("         sentinel ...{}...", win(b));
}

#[test]
fn saveproject_parity() {
    let d = dir();
    let full = "# leading comment\r\n\r\n\
                [project]\r\n\
                name    = \"demo\"\r\n\
                version = \"0.1.0\"\r\n\
                type    = \"executable\"\r\n\
                entry   = \"main.sentinel\"\r\n\
                icon    = \"../art/S2_icon.png\"   # unmodeled\r\n\
                authors = [\"Bryan\"]\r\n\r\n\
                # comment inside build\r\n\
                [build]\r\n\
                src          = \".\"\r\n\
                lib_paths    = []\r\n\
                links        = []\r\n\
                default_tier = \"experimental\"\r\n\r\n\
                [signing]\r\n\
                require = \"warn\"\r\n\
                trust   = \"sentinel-trust.toml\"\r\n\
                sign    = false\r\n";
    let targets = "[project]\r\nname  = \"demo\"\r\nentry = \"a.sentinel\"\r\n\r\n\
                   [[target]]\r\n# first\r\nname  = \"alpha\"\r\nentry = \"a.sentinel\"\r\ncustom = 42\r\n\r\n\
                   [[target]]\r\nname  = \"beta\"\r\nentry = \"b.sentinel\"\r\n";

    let cases: Vec<(&str, &str, Option<Mutate>)> = vec![
        ("1  full manifest, no model change", full, None),
        ("2  renamed project", full, Some(|p| p.name = "renamed".into())),
        (
            "3  every managed scalar changed",
            full,
            Some(|p| {
                p.name = "n2".into();
                p.version = "9.9.9".into();
                p.entry = "other.sentinel".into();
                p.src_dir = "src".into();
                p.sign_require = "strict".into();
                p.trust = "t.toml".into();
                p.sign_output = true;
            }),
        ),
        (
            "4  arrays populated",
            full,
            Some(|p| {
                p.lib_paths = vec!["a".into(), "b".into()];
                p.links = vec!["ws2_32".into()];
            }),
        ),
        ("5  empty file", "", None),
        ("6  preamble only", "# just a comment\r\n", None),
        (
            "7  missing keys inserted into existing sections",
            "[project]\r\nname = \"demo\"\r\n\r\n[signing]\r\nrequire = \"off\"\r\n",
            None,
        ),
        ("8  absent sections created", "[project]\r\nname = \"demo\"\r\n", None),
        ("9  no trailing newline", "[project]\r\nname = \"demo\"", None),
        ("10 LF-only line endings", "[project]\nname = \"demo\"\n", None),
        ("11 trailing blank lines collapse", "[project]\r\nname = \"demo\"\r\n\r\n\r\n\r\n", None),
        ("12 section case-insensitive", "[PROJECT]\r\nname = \"demo\"\r\n", None),
        ("13 key case-SENSITIVE ('Name' is not 'name')", "[project]\r\nName = \"demo\"\r\n", None),
        (
            "14 duplicate key: only the first is rewritten",
            "[project]\r\nname = \"a\"\r\nname = \"b\"\r\n",
            None,
        ),
        (
            "15 duplicate section header",
            "[project]\r\nname = \"a\"\r\n\r\n[project]\r\nversion = \"1\"\r\n",
            None,
        ),
        ("16 alignment preserved (wide gutter)", "[project]\r\nname          = \"demo\"\r\n", None),
        ("17 header with no closing bracket", "[project\r\nname = \"demo\"\r\n", None),
        ("18 line that is only '='", "[project]\r\n=\r\nname = \"demo\"\r\n", None),
        (
            "19 managed key in the preamble is left alone",
            "name = \"stray\"\r\n[project]\r\nname = \"demo\"\r\n",
            None,
        ),
        ("20 two target blocks, unchanged", targets, None),
        (
            "21 first target renamed",
            targets,
            Some(|p| {
                if let Some(t) = p.targets.first_mut() {
                    t.name = "alphaZ".into();
                }
            }),
        ),
        (
            "22 both targets rewritten",
            targets,
            Some(|p| {
                for t in &mut p.targets {
                    t.entry = "z.sentinel".into();
                    t.r#type = ProjectType::Library;
                }
            }),
        ),
        ("23 more blocks than model targets", targets, Some(|p| p.targets.truncate(1))),
        (
            "24 spaced '[[ target ]]' is not a target block",
            "[project]\r\nname = \"demo\"\r\n\r\n[[ target ]]\r\nname = \"spaced\"\r\n",
            None,
        ),
        (
            "25 target block before [project]",
            "[[target]]\r\nname = \"t\"\r\n\r\n[project]\r\nname = \"demo\"\r\n",
            None,
        ),
        (
            "26 UTF-8 content in an unmodeled value",
            "[project]\r\nname = \"demo\"\r\nauthors = [\"Björn\"]\r\n",
            None,
        ),
        ("27 quotes and backslashes in a value", full, Some(|p| p.entry = "dir\\sub\\a.sentinel".into())),
    ];

    let mut fail = 0;
    for (what, input, mutate) in &cases {
        fs::write(d.join("sentinel.toml"), input).unwrap();
        let mut p = load_project(&d).expect("load_project");
        if let Some(m) = mutate {
            m(&mut p);
        }
        let oracle = oracle_save(input, &p);
        let sentinel =
            sentinel_sys::save_manifest(input.as_bytes(), &p.manifest_values()).expect("save_manifest");
        if oracle.as_bytes() == sentinel.as_slice() {
            println!("  [PASS] {what}");
        } else {
            report(what, oracle.as_bytes(), &sentinel);
            fail += 1;
        }
    }
    fs::remove_dir_all(&d).unwrap();
    println!("\n{} passed, {fail} failed", cases.len() - fail);
    assert_eq!(fail, 0, "save_manifest diverged from the C++ oracle");
}
