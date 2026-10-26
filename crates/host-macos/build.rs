// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two rpaths and one baked-in path:
//
//   @executable_path/../Frameworks   where the app bundle carries libsentinel_parsers.dylib
//   DEP_SENTINEL_PARSERS_DYLIB_DIR   where `cargo run` finds it, straight out of OUT_DIR
//   SENTINEL_IDE_BUILD_SNC           the snc that built the library, offered as the last
//                                    resort when neither Settings nor the environment names one

fn main() {
    println!("cargo:rerun-if-env-changed=DEP_SENTINEL_PARSERS_DYLIB_DIR");
    println!("cargo:rerun-if-env-changed=DEP_SENTINEL_PARSERS_SNC");
    println!("cargo:rustc-link-arg=-Wl,-rpath,@executable_path/../Frameworks");
    if let Ok(dir) = std::env::var("DEP_SENTINEL_PARSERS_DYLIB_DIR") {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
    }
    if let Ok(snc) = std::env::var("DEP_SENTINEL_PARSERS_SNC") {
        println!("cargo:rustc-env=SENTINEL_IDE_BUILD_SNC={snc}");
    }
}
