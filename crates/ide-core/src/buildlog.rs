// SPDX-License-Identifier: GPL-3.0-or-later
//! Reading `snc build` output as it streams: what colour each line is, and which lines are
//! Problems. MainWindow.cpp's WM_APP_LINE handler, without the window.
//!
//! snc reports through miette, which puts the message on one line and the location on a
//! later one:
//!
//! ```text
//!   × type mismatch: expected i64, found bool
//!    ╭─[bad.sentinel:2:18]
//! ```
//!
//! So a `×` line is remembered, and the next line holding a `file.sentinel:line:col` becomes a
//! Problem carrying that message. A location with no `×` before it keeps the whole line as
//! its message, as on Windows.

use std::ops::Range;

use sentinel_sys::Diag;

/// How a line of build output is shown.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LineKind {
    /// Contains `×` or `error`.
    Error,
    /// Contains `warning`.
    Warning,
    /// The `> snc build …` echo of the command.
    Command,
    Plain,
}

pub fn classify(line: &str) -> LineKind {
    if line.contains('×') || line.contains("error") {
        LineKind::Error
    } else if line.contains("warning") {
        LineKind::Warning
    } else if line.starts_with('>') {
        LineKind::Command
    } else {
        LineKind::Plain
    }
}

/// One entry in the Problems list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Problem {
    pub message: String,
    pub file: String,
    pub line: i64,
    pub col: i64,
}

/// What one line contributed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LineReport {
    pub kind: LineKind,
    /// Byte range of the `file:line:col` token to show as a link, and the Problem it opens.
    pub problem: Option<(Range<usize>, Problem)>,
}

#[derive(Debug, Default)]
pub struct BuildLog {
    pending: Option<String>,
    pub problems: Vec<Problem>,
}

impl BuildLog {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn push(&mut self, line: &str) -> LineReport {
        if let Some(x) = line.find('×') {
            self.pending = Some(line[x + '×'.len_utf8()..].trim_start_matches(' ').to_string());
        }
        let problem = match sentinel_sys::parse_diag(line.as_bytes()) {
            Ok(Some(Diag { file, line: ln, col, token })) => {
                let message =
                    self.pending.take().filter(|m| !m.is_empty()).unwrap_or_else(|| line.to_string());
                let p = Problem { message, file, line: ln, col };
                self.problems.push(p.clone());
                Some((token, p))
            }
            Ok(None) => None,
            Err(e) => {
                crate::logger::log(crate::logger::LogLevel::Warn, &format!("{e}"));
                None
            }
        };
        LineReport { kind: classify(line), problem }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_miette_report_becomes_one_problem_with_its_message() {
        let mut log = BuildLog::new();
        let lines = [
            "sentinel::types::mismatch",
            "",
            "  × type mismatch: expected i64, found bool",
            "   ╭─[bad.sentinel:2:18]",
            " 1 │ fn main() -> i64 {",
        ];
        let reports: Vec<LineReport> = lines.iter().map(|l| log.push(l)).collect();
        assert_eq!(reports[2].kind, LineKind::Error);
        let (token, p) = reports[3].problem.clone().expect("the location line is a problem");
        assert_eq!(&lines[3][token], "bad.sentinel:2:18");
        assert_eq!(p.message, "type mismatch: expected i64, found bool");
        assert_eq!((p.file.as_str(), p.line, p.col), ("bad.sentinel", 2, 18));
        assert_eq!(log.problems.len(), 1);
        assert!(reports.iter().filter(|r| r.problem.is_some()).count() == 1);
    }

    #[test]
    fn a_bare_location_keeps_its_line_as_the_message() {
        let mut log = BuildLog::new();
        let r = log.push("src/main.sentinel:12: warning: unused");
        assert_eq!(r.kind, LineKind::Warning);
        assert_eq!(r.problem.unwrap().1.message, "src/main.sentinel:12: warning: unused");
    }

    #[test]
    fn the_message_is_used_once() {
        let mut log = BuildLog::new();
        log.push("  × first");
        log.push("[a.sentinel:1:1]");
        let second = log.push("[b.sentinel:2:2]").problem.unwrap().1;
        assert_eq!(second.message, "[b.sentinel:2:2]");
    }

    #[test]
    fn classifies_like_the_windows_output_pane() {
        assert_eq!(classify("> snc build x"), LineKind::Command);
        assert_eq!(classify("trust: 'x' verified"), LineKind::Plain);
        assert_eq!(classify("warning: unsigned"), LineKind::Warning);
        assert_eq!(classify("error: link failed"), LineKind::Error);
    }
}
