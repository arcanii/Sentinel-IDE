// SPDX-License-Identifier: GPL-3.0-or-later
//! Sentinel-IDE for macOS — the AppKit host, in Rust.
//!
//!     sentinel-ide [<folder|file>] [--build] [--run] [--exit-when-done] [--snapshot <png>]
//!
//! The same command line as the Windows exe, plus `--exit-when-done`: quit when the build
//! (or run) finishes, with its exit code, after echoing the Output pane to stdout. That is
//! how the real app's build loop is exercised from a shell. And `--snapshot <png>`: when
//! that work is done, the app draws its own window into a PNG and quits.

mod controller;
mod palette;
mod ui;

use std::path::PathBuf;

use objc2::runtime::ProtocolObject;
use objc2_app_kit::{NSApplication, NSApplicationActivationPolicy};
use objc2_foundation::MainThreadMarker;

use controller::{Args, Controller};

fn parse_args() -> Args {
    let mut a = Args::default();
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        match arg.as_str() {
            "--build" => a.build = true,
            "--snapshot" => a.snapshot = it.next().map(PathBuf::from),
            "--run" => a.run = true,
            "--exit-when-done" => a.exit_when_done = true,
            // AppKit and Xcode inject single-dash arguments (-NSDocumentRevisionsDebugMode
            // YES, -psn_…); they are not paths.
            s if s.starts_with('-') => {}
            s if a.path.is_none() => a.path = Some(PathBuf::from(s)),
            _ => {}
        }
    }
    a
}

fn main() {
    let mtm = MainThreadMarker::new().expect("Sentinel-IDE must start on the main thread");
    let app = NSApplication::sharedApplication(mtm);
    app.setActivationPolicy(NSApplicationActivationPolicy::Regular);
    let controller = Controller::new(mtm, parse_args());
    controller::install(controller.clone());
    app.setDelegate(Some(ProtocolObject::from_ref(&*controller)));
    app.run();
}
