// SPDX-License-Identifier: GPL-3.0-or-later
//! The portable half of Sentinel-IDE, in Rust: what `src/core/*.h` does for the Win32
//! host, minus Win32. No AppKit here either — a host links this and draws.
//!
//! The split with Sentinel is the one the Windows host already follows: Sentinel
//! interprets and produces bytes (every manifest, trust file and `.sig` goes through
//! `sentinel-sys`), this crate does the file I/O, the processes and the defaults around
//! them. Module by module:
//!
//! | module      | replaces             | notes                                          |
//! |-------------|----------------------|------------------------------------------------|
//! | `project`   | `Project.h`          | manifest via parse_manifest / save_manifest    |
//! | `signing`   | `Signing.h`          | trust + `.sig` via Sentinel, verify via `snc`  |
//! | `settings`  | `Settings.h`         | the same `settings.ini`, in Application Support |
//! | `stamp`     | `FileStamp.h`        | ported whole; the question was already portable |
//! | `proc`      | `Proc.h`             | merged stdout+stderr, ANSI-stripped            |
//! | `logger`    | `Logger.h`           | timestamps in UTC                              |
//! | `toolchain` | `findSnc` (MainWindow.cpp) | no MSVC environment on macOS             |
//!
//! Not here yet, on purpose: sealing (`Seal.h`'s crypto — milestone M4), file
//! associations and single-instance (the OS does both on macOS), and the editor.

use std::fmt;
use std::io;

pub mod logger;
pub mod proc;
pub mod project;
pub mod settings;
pub mod signing;
pub mod stamp;
pub mod toolchain;

pub use sentinel_sys::RecordError;

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    /// A Sentinel export returned a record that does not decode — a defect, not bad input.
    Record(RecordError),
    /// The folder holds neither a `*.sntproject` nor a `sentinel.toml`.
    NoManifest,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => e.fmt(f),
            Error::Record(e) => e.fmt(f),
            Error::NoManifest => f.write_str("no *.sntproject or sentinel.toml in this folder"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Io(e) => Some(e),
            Error::Record(e) => Some(e),
            Error::NoManifest => None,
        }
    }
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<RecordError> for Error {
    fn from(e: RecordError) -> Self {
        Error::Record(e)
    }
}

pub type Result<T> = std::result::Result<T, Error>;

/// A text file as the Windows host hands it to Sentinel: read raw, then decoded with
/// invalid UTF-8 replaced by U+FFFD (what `readUtf8` does through MultiByteToWideChar).
/// `Ok(None)` when the file is absent — for a trust manifest or a `.sig`, a normal state.
pub(crate) fn read_text_lossy(path: &std::path::Path) -> io::Result<Option<String>> {
    match std::fs::read(path) {
        Ok(b) => Ok(Some(String::from_utf8_lossy(&b).into_owned())),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(e),
    }
}
