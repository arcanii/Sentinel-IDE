// SPDX-License-Identifier: GPL-3.0-or-later
//! A small, thread-safe, append-only file logger with a configurable level and location
//! (set from Settings). Logger.h's, line for line, with one change: timestamps are UTC
//! and say so (`…Z`). Local time needs the C library's `localtime_r`, and a log read
//! after a timezone change or beside a CI log is easier to line up in UTC anyway.

use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

/// Same numbering as Logger.h, because `settings.ini` stores it as an integer.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
    Trace = 4,
}

impl LogLevel {
    pub fn name(self) -> &'static str {
        match self {
            LogLevel::Error => "ERROR",
            LogLevel::Warn => "WARN ",
            LogLevel::Info => "INFO ",
            LogLevel::Debug => "DEBUG",
            LogLevel::Trace => "TRACE",
        }
    }

    /// The integer form `settings.ini` holds; anything out of range is Trace, as the C++
    /// `levelName` default branch reads it.
    pub fn from_int(v: i64) -> Self {
        match v {
            0 => LogLevel::Error,
            1 => LogLevel::Warn,
            2 => LogLevel::Info,
            3 => LogLevel::Debug,
            _ => LogLevel::Trace,
        }
    }
}

struct State {
    file: PathBuf,
    level: LogLevel,
}

pub struct Logger {
    state: Mutex<State>,
}

impl Logger {
    pub const fn new() -> Self {
        Logger { state: Mutex::new(State { file: PathBuf::new(), level: LogLevel::Info }) }
    }

    pub fn configure(&self, file: impl Into<PathBuf>, level: LogLevel) {
        let mut s = self.lock();
        s.file = file.into();
        s.level = level;
    }

    pub fn set_level(&self, level: LogLevel) {
        self.lock().level = level;
    }

    pub fn level(&self) -> LogLevel {
        self.lock().level
    }

    pub fn file(&self) -> PathBuf {
        self.lock().file.clone()
    }

    pub fn log(&self, level: LogLevel, msg: &str) {
        let s = self.lock();
        if level > s.level || s.file.as_os_str().is_empty() {
            return;
        }
        let line = format!("{} [{}] {msg}\n", utc_timestamp(SystemTime::now()), level.name());
        // A logger that fails must not take the IDE with it; a lost line is the cost.
        let _ = append(&s.file, line.as_bytes());
    }

    // A panic while holding the lock poisons it; the state is two plain fields, so the
    // logger keeps working rather than failing every later call.
    fn lock(&self) -> std::sync::MutexGuard<'_, State> {
        self.state.lock().unwrap_or_else(|p| p.into_inner())
    }
}

impl Default for Logger {
    fn default() -> Self {
        Logger::new()
    }
}

fn append(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    OpenOptions::new().create(true).append(true).open(path)?.write_all(bytes)
}

/// The process-wide logger, as Logger.h's `logger()`.
pub fn logger() -> &'static Logger {
    static G: OnceLock<Logger> = OnceLock::new();
    G.get_or_init(Logger::new)
}

pub fn log(level: LogLevel, msg: &str) {
    logger().log(level, msg);
}

/// `2026-10-25 21:04:07.123Z`.
pub fn utc_timestamp(t: SystemTime) -> String {
    let d = t.duration_since(UNIX_EPOCH).unwrap_or_default();
    let secs = d.as_secs() as i64;
    let (days, rem) = (secs.div_euclid(86_400), secs.rem_euclid(86_400));
    let (y, m, dd) = civil_from_days(days);
    format!(
        "{y:04}-{m:02}-{dd:02} {:02}:{:02}:{:02}.{:03}Z",
        rem / 3600,
        rem / 60 % 60,
        rem % 60,
        d.subsec_millis()
    )
}

// Days since 1970-01-01 → (year, month, day), proleptic Gregorian. Howard Hinnant's
// `civil_from_days`, which is exact over the whole i64 range we can be handed.
fn civil_from_days(z: i64) -> (i64, u32, u32) {
    let z = z + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (yoe + era * 400 + i64::from(m <= 2), m, d)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    #[test]
    fn timestamps_are_utc_and_exact() {
        assert_eq!(utc_timestamp(UNIX_EPOCH), "1970-01-01 00:00:00.000Z");
        // 2026-09-05 07:42:31 UTC — the pubDate of the 0.1.15 appcast entry.
        let t = UNIX_EPOCH + Duration::from_millis(1_788_594_151_250);
        assert_eq!(utc_timestamp(t), "2026-09-05 07:42:31.250Z");
        // A leap day, to keep the month arithmetic honest.
        let t = UNIX_EPOCH + Duration::from_secs(951_782_400);
        assert_eq!(utc_timestamp(t), "2000-02-29 00:00:00.000Z");
    }

    #[test]
    fn filters_by_level_and_appends() {
        let dir = std::env::temp_dir().join(format!("ide-core-log-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let file = dir.join("t.log");
        let _ = std::fs::remove_file(&file);
        let l = Logger::new();
        l.configure(&file, LogLevel::Warn);
        l.log(LogLevel::Error, "kept");
        l.log(LogLevel::Info, "dropped");
        l.log(LogLevel::Warn, "kept too");
        let text = std::fs::read_to_string(&file).unwrap();
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines.len(), 2, "{text}");
        assert!(lines[0].ends_with("Z [ERROR] kept"), "{}", lines[0]);
        assert!(lines[1].ends_with("Z [WARN ] kept too"), "{}", lines[1]);
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
