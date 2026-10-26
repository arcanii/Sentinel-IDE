// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds src/sentinel/parsers.sentinel into the dylib this crate links.
//
// Two steps, and the second is the reason this file exists:
//
//   1. `snc build --lib` — the same command scripts\build.bat runs on Windows. It emits a
//      static archive holding the Sentinel exports AND the whole Sentinel runtime, which
//      is a Rust staticlib with its own copy of Rust's std.
//   2. Re-link that archive into a dylib that exports ONLY the C functions the header
//      declares. Linking the archive straight into a Rust binary puts two Rust stds in
//      one image — measured: ld reports a duplicate `_rust_eh_personality`, and which of
//      the two panic runtimes wins is not something to leave to link order. Inside a
//      dylib the runtime's std is private (two-level namespace), so it cannot collide
//      with ours. Measured 2026-10-25: 463 KB, nine exported symbols.
//
// Finding snc: $SNC, else $SENTINEL_LANG/target/{release,debug}/snc, else a sibling
// ../Sentinel-lang checkout, else `snc` on PATH. Release first, as on Windows. There is
// no C++-style fallback: a Mac build without snc has no parsers, so it fails, loudly.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

const LIB: &str = "sentinel_parsers";

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo = manifest_dir.join("../..").canonicalize().expect("repo root");
    let source = repo.join("src/sentinel/parsers.sentinel");
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());

    println!("cargo:rerun-if-changed={}", source.display());
    for var in ["SNC", "SENTINEL_LANG", "SNC_LIB_PATH", "CC", "MACOSX_DEPLOYMENT_TARGET"] {
        println!("cargo:rerun-if-env-changed={var}");
    }

    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("macos") {
        panic!(
            "sentinel-sys builds on macOS only. The Windows host links parsers.lib through \
             CMake (scripts\\build.bat); there is no Linux recipe yet."
        );
    }

    let snc = find_snc(&repo).unwrap_or_else(|| {
        panic!(
            "snc not found. Set SNC to the compiler binary, or SENTINEL_LANG to a Sentinel-lang \
             checkout with a built target/, or put snc on PATH."
        )
    });
    println!("cargo:rerun-if-changed={}", snc.display());

    // 1. snc → static archive + header.
    let archive = out.join("libparsers_static.a");
    let header = out.join("sentinel_parsers.h");
    let run = Command::new(&snc)
        .arg("build")
        .arg("--lib")
        .arg(&source)
        .arg("-o")
        .arg(&archive)
        .arg("--emit-header")
        .arg(&header)
        .output()
        .unwrap_or_else(|e| panic!("could not run {}: {e}", snc.display()));
    if !run.status.success() || !archive.exists() {
        // snc's own output is mostly libtool's "has no symbols" chatter, so it is only
        // worth showing when the build actually failed.
        panic!(
            "snc build --lib failed ({}):\n{}{}",
            run.status,
            String::from_utf8_lossy(&run.stdout),
            String::from_utf8_lossy(&run.stderr)
        );
    }

    // 2. The exported-symbols list comes from the header snc just wrote, so a new
    //    `export "C"` fn in parsers.sentinel is exported without editing this file.
    let exports = exports_from_header(&fs::read_to_string(&header).expect("read header"));
    if !exports.iter().any(|s| s == "sentinel_free_bytes") {
        panic!("{} declares no sentinel_free_bytes — has the ABI changed?", header.display());
    }
    let list = out.join("exports.txt");
    fs::write(&list, exports.iter().map(|s| format!("_{s}\n")).collect::<String>())
        .expect("write exports list");

    let dylib = out.join(format!("lib{LIB}.dylib"));
    let cc = env::var("CC").unwrap_or_else(|_| "cc".into());
    let mut link = Command::new(&cc);
    link.arg("-dynamiclib").arg("-o").arg(&dylib);
    if let Ok(min) = env::var("MACOSX_DEPLOYMENT_TARGET") {
        link.arg(format!("-mmacosx-version-min={min}"));
    }
    link.arg("-Wl,-all_load")
        .arg(&archive)
        .arg(format!("-Wl,-exported_symbols_list,{}", list.display()))
        .arg("-Wl,-dead_strip")
        .arg(format!("-Wl,-install_name,@rpath/lib{LIB}.dylib"));
    let linked = link.output().unwrap_or_else(|e| panic!("could not run {cc}: {e}"));
    if !linked.status.success() {
        panic!(
            "re-linking the Sentinel archive as a dylib failed ({}):\n{}",
            linked.status,
            String::from_utf8_lossy(&linked.stderr)
        );
    }

    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=dylib={LIB}");
    // This crate's own tests find the dylib through this rpath; dependents read the
    // directory back as DEP_SENTINEL_PARSERS_DYLIB_DIR and add the same rpath themselves.
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", out.display());
    println!("cargo:dylib_dir={}", out.display());
    // And the compiler that built it (DEP_SENTINEL_PARSERS_SNC), so a host can offer it as
    // a last-resort snc when nothing in the environment names one.
    println!("cargo:snc={}", snc.display());
}

fn find_snc(repo: &Path) -> Option<PathBuf> {
    if let Ok(p) = env::var("SNC") {
        return Some(PathBuf::from(p)).filter(|p| p.is_file());
    }
    let mut roots = Vec::new();
    if let Ok(p) = env::var("SENTINEL_LANG") {
        roots.push(PathBuf::from(p));
    }
    roots.push(repo.join("../Sentinel-lang"));
    for root in roots {
        for profile in ["release", "debug"] {
            let p = root.join("target").join(profile).join("snc");
            if p.is_file() {
                return p.canonicalize().ok();
            }
        }
    }
    env::var_os("PATH")
        .and_then(|paths| env::split_paths(&paths).map(|d| d.join("snc")).find(|p| p.is_file()))
}

// `void parse_diag(const uint8_t*, ...);` → "parse_diag". Every declaration snc emits is
// one line of that shape; the comments and preprocessor lines around them are skipped.
fn exports_from_header(h: &str) -> Vec<String> {
    h.lines()
        .map(str::trim)
        .filter(|l| l.ends_with(");") && !l.starts_with('#') && !l.starts_with("/*"))
        .filter_map(|l| {
            let name = l.split('(').next()?.split_whitespace().last()?;
            Some(name.trim_start_matches('*').to_string())
        })
        .collect()
}
