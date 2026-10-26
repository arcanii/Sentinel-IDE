// SPDX-License-Identifier: GPL-3.0-or-later
//! Synchronous run-and-capture for short tools — `snc verify`, `snc help`, keygen/sign.
//! Proc.h's runCapture. The streaming build/run loop is the host's (it pumps lines live
//! into the Output pane) and is not this.
//!
//! The one real difference from Win32: a program and its arguments, never a command line.
//! CreateProcessW takes a single string that the child re-splits, which is why every path
//! in MainWindow.cpp is wrapped in hand-written quotes; there is nothing to quote here.

use std::ffi::OsStr;
use std::io::{self, Read};
use std::path::Path;
use std::process::{Command, ExitStatus, Stdio};

/// Strip ANSI/VT escape sequences (snc colours its diagnostics): `ESC [`, then anything
/// up to and including a final byte in `@`..=`~`.
pub fn strip_ansi(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut it = s.chars().peekable();
    while let Some(c) = it.next() {
        if c == '\u{1b}' && it.peek() == Some(&'[') {
            it.next();
            for f in it.by_ref() {
                if ('@'..='~').contains(&f) {
                    break;
                }
            }
        } else {
            out.push(c);
        }
    }
    out
}

pub struct Captured {
    pub status: ExitStatus,
    /// stdout and stderr merged in the order the child wrote them, decoded lossily and
    /// ANSI-stripped.
    pub output: String,
}

/// Run `program args…` in `dir` (the current directory if `None`), wait for it, and
/// capture its merged output. Blocks until the child exits — for short tools only.
pub fn run_capture<I, S>(program: &Path, args: I, dir: Option<&Path>) -> io::Result<Captured>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    // One pipe for both streams, so interleaving is the child's and not ours.
    let (mut reader, writer) = io::pipe()?;
    let mut cmd = Command::new(program);
    cmd.args(args).stdin(Stdio::null()).stdout(writer.try_clone()?).stderr(writer);
    if let Some(d) = dir {
        cmd.current_dir(d);
    }
    let mut child = cmd.spawn()?;
    // The Command still holds our copies of the write end; until they are gone the read
    // below never sees end-of-file.
    drop(cmd);
    let mut bytes = Vec::new();
    reader.read_to_end(&mut bytes)?;
    let status = child.wait()?;
    Ok(Captured { status, output: strip_ansi(&String::from_utf8_lossy(&bytes)) })
}

/// Run `program args…` in `dir` and hand each line of its merged output to `on_line` as it
/// arrives — the Build/Run loop (MainWindow.cpp's `worker`). Lines lose their `\n` and one
/// trailing `\r`, are decoded lossily and ANSI-stripped; a final line without a newline is
/// still delivered. Blocks until the child exits, so call it off the UI thread.
pub fn run_streaming<I, S>(
    program: &Path,
    args: I,
    dir: Option<&Path>,
    mut on_line: impl FnMut(String),
) -> io::Result<ExitStatus>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    let (mut reader, writer) = io::pipe()?;
    let mut cmd = Command::new(program);
    cmd.args(args).stdin(Stdio::null()).stdout(writer.try_clone()?).stderr(writer);
    if let Some(d) = dir {
        cmd.current_dir(d);
    }
    let mut child = cmd.spawn()?;
    drop(cmd);
    let mut emit = |raw: &[u8]| {
        let raw = raw.strip_suffix(b"\r").unwrap_or(raw);
        on_line(strip_ansi(&String::from_utf8_lossy(raw)));
    };
    let (mut pending, mut chunk) = (Vec::new(), [0u8; 4096]);
    loop {
        let n = match reader.read(&mut chunk) {
            Ok(0) => break,
            Ok(n) => n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(e);
            }
        };
        pending.extend_from_slice(&chunk[..n]);
        // Split on bytes, not chars: a multi-byte character can straddle two reads, and
        // decoding a half of it would turn one é into two U+FFFDs.
        while let Some(i) = pending.iter().position(|&b| b == b'\n') {
            emit(&pending[..i]);
            pending.drain(..=i);
        }
    }
    if !pending.is_empty() {
        emit(&pending);
    }
    child.wait()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn streams_lines_in_order_including_an_unterminated_last_one() {
        let mut lines = Vec::new();
        let status = run_streaming(
            Path::new("/bin/sh"),
            [
                "-c",
                "printf 'one\\r\\ntwo\\n'; printf 'caf\\303\\251 \\033[31mred\\033[0m\\n' >&2; printf 'tail'",
            ],
            None,
            |l| lines.push(l),
        )
        .unwrap();
        assert!(status.success());
        assert_eq!(lines, ["one", "two", "café red", "tail"]);
    }

    #[test]
    fn strips_colour_and_keeps_text() {
        assert_eq!(strip_ansi("\u{1b}[1;31merror\u{1b}[0m: x"), "error: x");
        assert_eq!(strip_ansi("plain"), "plain");
        // A lone ESC that does not start a CSI sequence is text, as in the C++.
        assert_eq!(strip_ansi("a\u{1b}b"), "a\u{1b}b");
        // An unterminated sequence swallows the rest, as in the C++.
        assert_eq!(strip_ansi("ok\u{1b}[12"), "ok");
    }

    #[test]
    fn merges_both_streams_and_reports_the_exit_code() {
        let c = run_capture(
            Path::new("/bin/sh"),
            ["-c", "printf 'out1\\n'; printf 'err\\n' >&2; printf 'out2\\n'; exit 3"],
            None,
        )
        .unwrap();
        assert_eq!(c.status.code(), Some(3));
        assert_eq!(c.output, "out1\nerr\nout2\n");
    }

    #[test]
    fn runs_in_the_given_directory() {
        let dir = std::env::temp_dir().canonicalize().unwrap();
        let c = run_capture(Path::new("/bin/pwd"), [""; 0], Some(&dir)).unwrap();
        assert_eq!(Path::new(c.output.trim()).canonicalize().unwrap(), dir);
    }

    #[test]
    fn a_missing_program_is_an_error_not_an_exit_code() {
        assert!(run_capture(Path::new("/nonexistent/snc"), ["help"], None).is_err());
    }
}
