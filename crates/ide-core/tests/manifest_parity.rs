// SPDX-License-Identifier: GPL-3.0-or-later
//
// manifest_parity — the Rust port of tests/manifest_xcheck.cpp: parse_manifest (Sentinel)
// plus loadProject's defaults, over the same 14 manifests, including the flat-vs-target
// case-sensitivity asymmetry.
//
// THIS ONE CANNOT RUN ITS ORACLE HERE, and says so rather than pretending. The C++ oracle is
// loadProject's `#else` branch, which reads the flat fields through the Win32 profile API
// (GetPrivateProfileStringW) — an API with no macOS counterpart, whose semantics a Rust
// imitation could only guess at. So each case pins the Project instead: the values the
// Windows cross-check proved Sentinel and that API agree on. A Rust re-implementation of
// GetPrivateProfileString that agreed with Sentinel would prove nothing the pins do not.
//
// What it still proves on this platform: Sentinel's reading of each manifest, this crate's
// decoder, and ide-core's defaults and single-target fallback, end to end through a real
// file in a real folder (the folder's name is the default project name).

use std::fs;
use std::path::{Path, PathBuf};

use ide_core::project::{load_project, Project, ProjectType, Target, Tier};

fn dir() -> PathBuf {
    let d = std::env::temp_dir().join(format!("manixcheck-{}", std::process::id())).join("manixcheck");
    fs::create_dir_all(&d).unwrap();
    d
}

fn t(name: &str, entry: &str, ty: ProjectType, links: &[&str]) -> Target {
    Target {
        name: name.into(),
        entry: entry.into(),
        r#type: ty,
        links: links.iter().map(|s| s.to_string()).collect(),
    }
}

// The defaults loadProject applies when a manifest states nothing, for a folder named
// `manixcheck`. Each case below starts here and changes only what its manifest states.
fn defaults(d: &Path) -> Project {
    Project {
        dir: d.to_path_buf(),
        manifest: "sentinel.toml".into(),
        name: "manixcheck".into(),
        version: "0.1.0".into(),
        entry: String::new(),
        src_dir: "src".into(),
        icon: String::new(),
        r#type: ProjectType::Executable,
        default_tier: Tier::Experimental,
        lib_paths: vec![],
        links: vec![],
        sign_require: "off".into(),
        trust: "sentinel-trust.toml".into(),
        sign_output: false,
        targets: vec![t("manixcheck", "", ProjectType::Executable, &[])],
        explicit_targets: false,
    }
}

#[test]
fn manifest_parity() {
    use ProjectType::*;
    let d = dir();
    let base = defaults(&d);
    let with = |f: &dyn Fn(&mut Project)| {
        let mut p = base.clone();
        f(&mut p);
        p
    };
    let s = |v: &[&str]| v.iter().map(|x| x.to_string()).collect::<Vec<_>>();

    let cases: Vec<(&str, &str, Project)> = vec![
        (
            "real examples/sentinel.toml",
            "# Sentinel project manifest (read by Sentinel-IDE; snc parses the full TOML).\r\n\
             [project]\r\nname    = \"crypto-lib\"\r\nversion = \"0.1.0\"\r\ntype    = \"executable\"\r\n\
             entry   = \"crypto.sentinel\"\r\nicon    = \"../art/S2_icon.png\"\r\nauthors = [\"Bryan\"]\r\n\r\n\
             [build]\r\nsrc          = \".\"\r\nlib_paths    = []\r\nlinks        = []\r\ndefault_tier = \"experimental\"\r\n\r\n\
             [signing]\r\nrequire = \"warn\"\r\ntrust   = \"sentinel-trust.toml\"\r\nsign    = false\r\n\r\n\
             [[target]]\r\nname  = \"crypto\"\r\ntype  = \"executable\"\r\nentry = \"crypto.sentinel\"\r\n\r\n\
             [[target]]\r\nname  = \"hello\"\r\ntype  = \"executable\"\r\nentry = \"hello.sentinel\"\r\n",
            with(&|p| {
                p.name = "crypto-lib".into();
                p.entry = "crypto.sentinel".into();
                p.icon = "../art/S2_icon.png".into();
                p.src_dir = ".".into();
                p.sign_require = "warn".into();
                p.targets = vec![
                    t("crypto", "crypto.sentinel", Executable, &[]),
                    t("hello", "hello.sentinel", Executable, &[]),
                ];
                p.explicit_targets = true;
            }),
        ),
        (
            "minimal (defaults everywhere)",
            "[project]\r\nname = \"m\"\r\n",
            with(&|p| {
                p.name = "m".into();
                p.targets = vec![t("m", "", Executable, &[])];
            }),
        ),
        ("no sections at all (all defaults, single-target fallback)", "# just a comment\r\n", base.clone()),
        (
            "flat fields case-insensitive",
            "[PROJECT]\r\nNAME = \"cap\"\r\nVeRsIoN = \"9.9\"\r\n[BUILD]\r\nSRC = \"s2\"\r\n",
            with(&|p| {
                p.name = "cap".into();
                p.version = "9.9".into();
                p.src_dir = "s2".into();
                p.targets = vec![t("cap", "", Executable, &[])];
            }),
        ),
        (
            "target field keys are case-SENSITIVE (Name != name)",
            "[project]\r\nname = \"p\"\r\n[[target]]\r\nName = \"WrongCase\"\r\nentry = \"e.sentinel\"\r\n",
            with(&|p| {
                p.name = "p".into();
                p.targets = vec![t("", "e.sentinel", Executable, &[])];
                p.explicit_targets = true;
            }),
        ),
        (
            "lib_paths + links arrays",
            "[build]\r\nlib_paths = [\"a\", \"b/c\"]\r\nlinks = [ \"user32\" ,\"gdi32\"]\r\n",
            with(&|p| {
                p.lib_paths = s(&["a", "b/c"]);
                p.links = s(&["user32", "gdi32"]);
                p.targets = vec![t("manixcheck", "", Executable, &["user32", "gdi32"])];
            }),
        ),
        ("sign true/1/false", "[signing]\r\nsign = true\r\n", with(&|p| p.sign_output = true)),
        ("sign = 1", "[signing]\r\nsign = 1\r\n", with(&|p| p.sign_output = true)),
        (
            "library type",
            "[project]\r\ntype = \"library\"\r\nname = \"lib\"\r\n",
            with(&|p| {
                p.name = "lib".into();
                p.r#type = Library;
                p.targets = vec![t("lib", "", Library, &[])];
            }),
        ),
        (
            "shared/dll type",
            "[project]\r\ntype = \"dll\"\r\n",
            with(&|p| {
                p.r#type = Shared;
                p.targets = vec![t("manixcheck", "", Shared, &[])];
            }),
        ),
        ("tier stable/hard/dev", "[build]\r\ndefault_tier = \"hardened\"\r\n", with(&|p| p.default_tier = Tier::Hardened)),
        (
            "per-target links + type",
            "[[target]]\r\nname = \"t\"\r\nentry = \"t.sentinel\"\r\ntype = \"library\"\r\nlinks = [\"ws2_32\"]\r\n",
            with(&|p| {
                p.targets = vec![t("t", "t.sentinel", Library, &["ws2_32"])];
                p.explicit_targets = true;
            }),
        ),
        (
            "target closed by a normal section",
            "[[target]]\r\nname = \"t1\"\r\n[meta]\r\nname = \"ignored\"\r\n",
            with(&|p| {
                p.targets = vec![t("t1", "", Executable, &[])];
                p.explicit_targets = true;
            }),
        ),
        (
            "comments and blanks between keys",
            "[project]\r\n\r\n# c\r\nname = \"x\"\r\n\r\nversion = \"2.0\"\r\n",
            with(&|p| {
                p.name = "x".into();
                p.version = "2.0".into();
                p.targets = vec![t("x", "", Executable, &[])];
            }),
        ),
    ];

    let mut fail = 0;
    for (label, manifest, want) in &cases {
        fs::write(d.join("sentinel.toml"), manifest).unwrap();
        let got = load_project(&d).expect("load_project");
        let ok = &got == want;
        println!("[{}] {label}", if ok { "PASS" } else { "FAIL" });
        if !ok {
            println!("      pinned   : {want:?}");
            println!("      Sentinel : {got:?}");
            fail += 1;
        }
    }
    fs::remove_dir_all(d.parent().unwrap()).unwrap();
    println!("\n{} passed, {fail} failed", cases.len() - fail);
    assert_eq!(fail, 0, "parse_manifest + defaults diverged from the pinned Windows behaviour");
}

// The examples/ project as it is in the repo, through the real loader.
#[test]
fn loads_the_committed_example_project() {
    let d = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../examples");
    let p = load_project(&d).unwrap();
    assert_eq!(p.manifest, "sentinel.toml");
    assert_eq!(p.name, "crypto-lib");
    assert_eq!(p.sign_require, "warn");
    assert!(p.explicit_targets);
    let names: Vec<&str> = p.targets.iter().map(|t| t.name.as_str()).collect();
    assert_eq!(names, ["crypto", "hello"]);
}
