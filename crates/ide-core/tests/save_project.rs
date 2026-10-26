// SPDX-License-Identifier: GPL-3.0-or-later
//
// save_project — the Rust port of tests/saveproject_test.cpp: characterization tests for
// the comment-preserving manifest writer, end to end through a real file. Where
// saveproject_parity holds save_manifest to the C++ oracle one buffer at a time, this
// drives ide-core's load_project → mutate → save_project → read-back loop, which is what
// the Project Settings form will do.
//
// The writer rewrites the user's manifest in place, and its whole promise is that
// everything it does NOT manage — comments, blank lines, key alignment, unmodeled keys,
// [[target]] blocks — survives byte for byte. Two cases pin behaviour that is arguably
// WRONG (8's case-sensitivity asymmetry, 9's "[[ target ]]" spacing hole); they are
// recorded as they are, as on Windows, so a change to either is a decision and not drift.

use std::fs;
use std::path::{Path, PathBuf};

use ide_core::project::{load_project, save_project, Project};

struct Run {
    dir: PathBuf,
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
    fn manifest(&self) -> PathBuf {
        self.dir.join("sentinel.toml")
    }
    fn write(&self, text: &str) {
        fs::write(self.manifest(), text).unwrap();
    }
    fn read(&self) -> String {
        fs::read_to_string(self.manifest()).unwrap_or_else(|_| "<missing>".into())
    }
    // Load the manifest we just wrote, mutate via the model, save, return the new text.
    fn round_trip(&self, mutate: impl FnOnce(&mut Project)) -> String {
        let mut p = load_project(&self.dir).unwrap();
        mutate(&mut p);
        save_project(&p).unwrap();
        self.read()
    }
}

fn count(hay: &str, needle: &str) -> usize {
    hay.matches(needle).count()
}

fn all_crlf(s: &str) -> bool {
    s.char_indices().all(|(i, c)| c != '\n' || (i > 0 && s.as_bytes()[i - 1] == b'\r'))
}

#[test]
fn save_project_characterization() {
    let dir = std::env::temp_dir().join(format!("sntsave_{}", std::process::id()));
    fs::create_dir_all(&dir).unwrap();
    let mut r = Run { dir, pass: 0, fail: 0 };

    println!("\n1. a save that changes nothing preserves everything it does not manage");
    r.write(
        "# leading comment\r\n\r\n[project]\r\nname    = \"demo\"\r\nversion = \"0.1.0\"\r\n\
         type    = \"executable\"\r\nentry   = \"main.sentinel\"\r\n\
         icon    = \"../art/S2_icon.png\"   # unmodeled\r\nauthors = [\"Bryan\"]\r\n\r\n\
         # a comment inside build\r\n[build]\r\nsrc          = \".\"\r\nlib_paths    = []\r\n\
         links        = []\r\ndefault_tier = \"experimental\"\r\n\r\n[signing]\r\nrequire = \"warn\"\r\n\
         trust   = \"sentinel-trust.toml\"\r\nsign    = false\r\n",
    );
    let out = r.round_trip(|_| {});
    r.check(out.contains("# leading comment"), "leading comment survives");
    r.check(out.contains("# a comment inside build"), "in-section comment survives");
    r.check(
        out.contains("icon    = \"../art/S2_icon.png\"   # unmodeled"),
        "unmodeled key + its trailing comment survive verbatim",
    );
    r.check(out.contains("authors = [\"Bryan\"]"), "unmodeled array key survives");
    r.check(out.contains("name    = \"demo\""), "key alignment is preserved on rewrite");

    println!("\n2. changing one managed value rewrites only that value");
    let before = r.read();
    let out = r.round_trip(|p| p.name = "renamed".into());
    r.check(out.contains("name    = \"renamed\""), "new value written with the original alignment");
    r.check(!out.contains("\"demo\""), "old value gone");
    r.check(out.contains("version = \"0.1.0\""), "sibling managed key untouched");
    r.check(
        out.contains("icon    = \"../art/S2_icon.png\"   # unmodeled"),
        "unmodeled key still untouched after a real edit",
    );
    r.check(before != out, "the file did change");

    println!("\n3. a managed key absent from the file is inserted into its section");
    r.write("[project]\r\nname = \"demo\"\r\n\r\n[signing]\r\nrequire = \"off\"\r\n");
    let out = r.round_trip(|_| {});
    r.check(count(&out, "[project]") == 1, "no duplicate [project] header (TOML forbids it)");
    r.check(count(&out, "[signing]") == 1, "no duplicate [signing] header");
    r.check(out.contains("version ="), "missing project key inserted");
    r.check(out.contains("trust ="), "missing signing key inserted");
    r.check(out.contains("[build]"), "entirely absent section is created");
    r.check(out.contains("src ="), "keys for the created section are written");

    println!("\n4. [[target]] blocks are rewritten in order and never gain new keys");
    r.write(
        "[project]\r\nname  = \"demo\"\r\nentry = \"a.sentinel\"\r\n\r\n[[target]]\r\n# first target\r\n\
         name  = \"alpha\"\r\nentry = \"a.sentinel\"\r\ncustom = 42   # unmodeled per-target key\r\n\r\n\
         [[target]]\r\nname  = \"beta\"\r\nentry = \"b.sentinel\"\r\n",
    );
    let out = r.round_trip(|p| {
        if let Some(t) = p.targets.first_mut() {
            t.name = "alphaZ".into();
        }
    });
    r.check(out.contains("name  = \"alphaZ\""), "first target renamed in place");
    r.check(out.contains("name  = \"beta\""), "second target untouched");
    r.check(out.contains("# first target"), "per-target comment survives");
    r.check(
        out.contains("custom = 42   # unmodeled per-target key"),
        "unmodeled per-target key survives verbatim",
    );
    r.check(count(&out, "[[target]]") == 2, "target block count unchanged");
    // Scope "nothing inserted" to the first target BLOCK: `type` is a managed [project]
    // key, so it legitimately appears elsewhere in the file.
    let block = out
        .find("[[target]]")
        .map(|a| {
            let b = out[a + 1..].find("[[target]]").map_or(out.len(), |b| a + 1 + b);
            &out[a..b]
        })
        .unwrap_or("");
    r.check(!block.contains("type ="), "no key is INSERTED into a target block");
    r.check(!block.contains("links ="), "...including links, which targets can carry but this one lacks");

    println!("\n5. output normalises to CRLF with a single trailing newline");
    let out = r.read();
    r.check(out.ends_with("\r\n"), "ends with exactly one CRLF");
    r.check(!out.ends_with("\r\n\r\n"), "no blank line at end of file");
    r.check(all_crlf(&out), "no bare LF anywhere in the output");

    println!("\n6. an LF-only manifest is read and re-emitted as CRLF");
    r.write("[project]\nname = \"lf\"\n");
    let out = r.round_trip(|_| {});
    r.check(all_crlf(&out), "LF input re-emitted as CRLF");
    r.check(out.contains("name = \"lf\""), "value preserved across the normalisation");

    println!("\n7. section names are matched case-insensitively");
    r.write("[PROJECT]\r\nname = \"demo\"\r\n");
    let out = r.round_trip(|p| p.name = "cased".into());
    r.check(out.contains("name = \"cased\""), "value under [PROJECT] was rewritten");
    r.check(count(&out, "[PROJECT]") == 1, "the original header casing is preserved");
    r.check(!out.contains("[project]"), "no second lower-cased [project] section added");

    println!("\n8. key names are matched case-SENSITIVELY -- an asymmetry with case 7");
    r.write("[project]\r\nName = \"demo\"\r\n");
    let out = r.round_trip(|p| p.name = "newname".into());
    r.check(
        out.contains("Name = \"demo\""),
        "'Name' is NOT treated as the managed key 'name' -- left untouched",
    );
    r.check(
        out.contains("name = \"newname\""),
        "...and 'name' is inserted separately, so the file now has both",
    );

    println!("\n9. a spaced '[[ target ]]' header is not recognised as a target block");
    r.write("[project]\r\nname = \"demo\"\r\n\r\n[[ target ]]\r\nname = \"spaced\"\r\n");
    let out = r.round_trip(|p| {
        for t in &mut p.targets {
            t.name = "SHOULD_NOT_APPEAR".into();
        }
    });
    r.check(
        out.contains("name = \"spaced\""),
        "the spaced block is left entirely alone (header parses to '[ target')",
    );
    r.check(!out.contains("SHOULD_NOT_APPEAR"), "no target rewrite reached it");

    println!("\n10. saving over a manifest that does not exist yet");
    fs::remove_file(r.manifest()).unwrap();
    let mut p = Project::from_fields(&r.dir, "sentinel.toml", Default::default());
    p.name = "fresh".into();
    r.check(save_project(&p).is_ok(), "save_project reports success");
    let out = r.read();
    r.check(out.contains("[project]"), "a usable manifest is produced from nothing");
    r.check(out.contains("\"fresh\""), "the model's value is in it");

    fs::remove_dir_all(&r.dir).unwrap();
    println!("\n{} passed, {} failed", r.pass, r.fail);
    assert_eq!(r.fail, 0);
}

// Not in the C++: the paths a Windows-authored manifest may carry still resolve here.
#[test]
fn manifest_paths_resolve_with_either_separator() {
    let p = Project::from_fields(Path::new("/proj"), "x.sntproject", Default::default());
    assert_eq!(p.resolve("src/main.sentinel"), Path::new("/proj/src/main.sentinel"));
    assert_eq!(p.resolve("src\\main.sentinel"), Path::new("/proj/src/main.sentinel"));
    assert_eq!(p.manifest_path(), Path::new("/proj/x.sntproject"));
}
