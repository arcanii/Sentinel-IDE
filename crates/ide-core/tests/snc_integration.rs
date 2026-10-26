// SPDX-License-Identifier: GPL-3.0-or-later
//
// snc_integration — ide-core against the real compiler, on this Mac. Not a port of a C++
// test: on Windows the same ground was covered by hand, phase by phase (HANDOVER phases 13,
// 23 and 29). It needs `snc`; so does building this workspace at all (sentinel-sys compiles
// parsers.sentinel with it), so a missing compiler is a failure here, not a skip.
//
// What it pins:
//   * snc discovery finds a compiler that can at least verify;
//   * verify_file says Signed for the committed demo, Invalid for a one-byte change to it,
//     and Unsigned when there is no .sig — the three states the trust chip shows;
//   * build_plan's arguments drive a real `snc build` of the example project, under its
//     `require = "warn"` signature gate, to binaries that run and answer as they should.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use ide_core::proc::run_capture;
use ide_core::project::{build_plan, load_project, Tier};
use ide_core::signing::{verify_file, SignState};
use ide_core::toolchain::find_snc;

fn repo() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..").canonicalize().unwrap()
}

fn snc() -> PathBuf {
    let (p, caps) = find_snc(repo().parent()).expect("no snc found — set SNC or SENTINEL_LANG");
    assert!(caps.verify, "{} cannot verify signatures", p.display());
    p
}

// A private copy of examples/, so a build or a tamper never touches the committed files.
fn scratch_examples(tag: &str) -> PathBuf {
    let d = std::env::temp_dir().join(format!("ide-core-{tag}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&d);
    fs::create_dir_all(&d).unwrap();
    for f in
        ["sentinel.toml", "sentinel-trust.toml", "crypto.sentinel", "crypto.sentinel.sig", "hello.sentinel"]
    {
        fs::copy(repo().join("examples").join(f), d.join(f)).unwrap();
    }
    d
}

#[test]
fn verify_reports_signed_invalid_and_unsigned() {
    let snc = snc();
    let ex = repo().join("examples");

    let ok = verify_file(&snc, &ex.join("crypto.sentinel"));
    assert_eq!(ok.state, SignState::Signed, "{}", ok.message);
    assert!(ok.key.starts_with("58ad2d8cf5294de1"), "key = {}", ok.key);
    assert_eq!(ok.grants, "secret,constant_time,alloc");

    // One byte of the signed source changed, signature left alone.
    let d = scratch_examples("tamper");
    let src = d.join("crypto.sentinel");
    let mut bytes = fs::read(&src).unwrap();
    // Any byte would do; a digit keeps the file valid UTF-8 and readable in a diff.
    let at = bytes.iter().position(|&b| b == b'4').expect("a '4' to change");
    bytes[at] = b'5';
    fs::write(&src, bytes).unwrap();
    let bad = verify_file(&snc, &src);
    assert_eq!(bad.state, SignState::Invalid, "{}", bad.message);

    let none = verify_file(&snc, &ex.join("hello.sentinel"));
    assert_eq!(none.state, SignState::Unsigned);
    fs::remove_dir_all(&d).unwrap();
}

#[test]
fn build_plan_builds_the_example_project() {
    let snc = snc();
    let d = scratch_examples("build");
    let p = load_project(&d).unwrap();
    assert!(p.explicit_targets);

    // crypto exits 42 (its secret bytes compare equal); hello prints 100 and exits 0.
    let expect = [("crypto", Some(42), ""), ("hello", Some(0), "100")];
    for (t, (name, code, stdout)) in p.targets.iter().zip(expect) {
        assert_eq!(t.name, name);
        let plan = build_plan(&p, t, Tier::Experimental);
        fs::create_dir_all(&plan.out_dir).unwrap();
        let built = run_capture(&snc, &plan.args, Some(&p.dir)).unwrap();
        assert!(built.status.success(), "snc build {name} failed:\n{}", built.output);
        assert_eq!(plan.artifact, d.join("target/experimental").join(name));
        assert!(plan.artifact.is_file(), "{} not produced", plan.artifact.display());

        let ran = Command::new(&plan.artifact).output().unwrap();
        assert_eq!(ran.status.code(), code, "{name} exit status");
        assert_eq!(String::from_utf8_lossy(&ran.stdout).trim(), stdout, "{name} stdout");
    }
    fs::remove_dir_all(&d).unwrap();
}
