// SPDX-License-Identifier: GPL-3.0-or-later
//
// Puts the Sentinel dylib on this crate's test binaries' rpath. sentinel-sys's own
// rustc-link-arg reaches only sentinel-sys's binaries, so it hands the directory over
// as `links` metadata instead (DEP_SENTINEL_PARSERS_DYLIB_DIR). `cargo test` would also
// find it through DYLD_FALLBACK_LIBRARY_PATH; the rpath is what lets a test binary run
// on its own. The app bundle uses @executable_path/../Frameworks instead.

fn main() {
    println!("cargo:rerun-if-env-changed=DEP_SENTINEL_PARSERS_DYLIB_DIR");
    if let Ok(dir) = std::env::var("DEP_SENTINEL_PARSERS_DYLIB_DIR") {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
    }
}
