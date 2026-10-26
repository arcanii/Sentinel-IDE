// SPDX-License-Identifier: GPL-3.0-or-later
//! Settings — the same `settings.ini` the Windows host keeps, in macOS's places for it:
//! `~/Library/Application Support/SentinelIDE/settings.ini`, and the log under
//! `~/Library/Logs/SentinelIDE/`.
//!
//! Windows reads and writes it through GetPrivateProfileString / WritePrivateProfileString.
//! [`Ini`] is the subset of those semantics this file relies on: section and key names
//! match case-insensitively, the first match wins, and a write edits the file in place so
//! comments and keys this build does not know survive. It is pure text-in/text-out, which
//! makes it a candidate to move into Sentinel beside the manifest reader.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::logger::LogLevel;

/// The recent-projects list cap.
pub const MAX_RECENTS: usize = 10;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Settings {
    pub editor_font: String,
    /// -1 follow the system, 0 light, 1 dark.
    pub theme_mode: i64,
    pub log_level: LogLevel,
    /// Empty means the default location.
    pub log_file: PathBuf,
    /// Optional snc override; empty means auto-detect.
    pub snc_path: PathBuf,
    pub line_numbers: bool,
    /// The appcast version the user chose to skip — never re-offered.
    pub update_skip_version: String,
    /// Recently opened project folders, most recent first.
    pub recents: Vec<PathBuf>,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            // Cascadia Code is the Windows default and is not on a stock Mac; Menlo is.
            editor_font: "Menlo".into(),
            theme_mode: -1,
            log_level: LogLevel::Info,
            log_file: PathBuf::new(),
            snc_path: PathBuf::new(),
            line_numbers: false,
            update_skip_version: String::new(),
            recents: Vec::new(),
        }
    }
}

impl Settings {
    /// Promote `folder` to the front of the recents list, de-duplicated and capped. The
    /// comparison ignores ASCII case, as lstrcmpiW did and as macOS's default
    /// case-insensitive volumes do.
    pub fn add_recent(&mut self, folder: &Path) {
        if folder.as_os_str().is_empty() {
            return;
        }
        let key = folder.to_string_lossy().to_ascii_lowercase();
        self.recents.retain(|r| r.to_string_lossy().to_ascii_lowercase() != key);
        self.recents.insert(0, folder.to_path_buf());
        self.recents.truncate(MAX_RECENTS);
    }

    /// Overlay what `ini` holds onto the defaults, key by key, as loadSettings does.
    pub fn from_ini(ini: &Ini) -> Settings {
        let mut s = Settings::default();
        if let Some(v) = ini.get("editor", "font") {
            s.editor_font = v.to_string();
        }
        s.theme_mode = ini.get_int("ui", "theme", s.theme_mode);
        s.log_level = LogLevel::from_int(ini.get_int("log", "level", s.log_level as i64));
        if let Some(v) = ini.get("log", "file") {
            s.log_file = v.into();
        }
        s.snc_path = ini.get("build", "snc").unwrap_or("").into();
        s.line_numbers = ini.get_int("editor", "line_numbers", 0) != 0;
        s.update_skip_version = ini.get("update", "skip_version").unwrap_or("").to_string();
        let count = ini.get_int("recents", "count", 0).clamp(0, MAX_RECENTS as i64);
        for i in 0..count {
            if let Some(v) = ini.get("recents", &format!("item{i}")).filter(|v| !v.is_empty()) {
                s.recents.push(v.into());
            }
        }
        s
    }

    /// Write every setting into `ini`, as saveSettings does — including dropping the
    /// `[recents]` section first so a shorter list leaves no stale `itemN` behind.
    pub fn write_to(&self, ini: &mut Ini) {
        ini.set("editor", "font", &self.editor_font);
        ini.set("ui", "theme", &self.theme_mode.to_string());
        ini.set("log", "level", &(self.log_level as i64).to_string());
        ini.set("log", "file", &self.log_file.to_string_lossy());
        ini.set("build", "snc", &self.snc_path.to_string_lossy());
        ini.set("editor", "line_numbers", if self.line_numbers { "1" } else { "0" });
        ini.set("update", "skip_version", &self.update_skip_version);
        ini.remove_section("recents");
        ini.set("recents", "count", &self.recents.len().to_string());
        for (i, r) in self.recents.iter().enumerate() {
            ini.set("recents", &format!("item{i}"), &r.to_string_lossy());
        }
    }
}

/// `~/Library/Application Support/SentinelIDE`.
pub fn app_data_dir() -> PathBuf {
    home().join("Library/Application Support/SentinelIDE")
}

/// `~/Library/Logs/SentinelIDE/sentinelide.log` — where Console.app looks.
pub fn default_log_file() -> PathBuf {
    home().join("Library/Logs/SentinelIDE/sentinelide.log")
}

pub fn settings_path() -> PathBuf {
    app_data_dir().join("settings.ini")
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/tmp"))
}

/// Load from `path`. A missing file is the defaults; so is an unreadable one, logged —
/// the IDE must start either way.
pub fn load_settings(path: &Path) -> Settings {
    let mut s = match fs::read(path) {
        Ok(b) => Settings::from_ini(&Ini::parse(&String::from_utf8_lossy(&b))),
        Err(e) => {
            if e.kind() != io::ErrorKind::NotFound {
                crate::logger::log(LogLevel::Warn, &format!("settings {}: {e}", path.display()));
            }
            Settings::default()
        }
    };
    if s.log_file.as_os_str().is_empty() {
        s.log_file = default_log_file();
    }
    s
}

/// Save to `path`, editing the existing file in place rather than replacing it.
pub fn save_settings(s: &Settings, path: &Path) -> io::Result<()> {
    let mut ini = match fs::read(path) {
        Ok(b) => Ini::parse(&String::from_utf8_lossy(&b)),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ini::default(),
        Err(e) => return Err(e),
    };
    s.write_to(&mut ini);
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    fs::write(path, ini.render())
}

// ---- Ini ----------------------------------------------------------------------------

/// An INI file held as its lines, so a write changes one line and leaves the rest alone.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Ini {
    /// The first entry is ALWAYS the preamble before any `[section]`, with an empty name —
    /// every lookup skips it, so an Ini without one would hide its first real section.
    sections: Vec<Section>,
}

impl Default for Ini {
    fn default() -> Self {
        Ini { sections: vec![Section::default()] }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
struct Section {
    name: String,
    /// Every line after the header, verbatim.
    lines: Vec<String>,
}

impl Ini {
    pub fn parse(text: &str) -> Ini {
        let mut sections = vec![Section::default()];
        for line in text.lines() {
            let t = line.trim();
            if let Some(rest) = t.strip_prefix('[') {
                if let Some(end) = rest.find(']') {
                    sections.push(Section { name: rest[..end].trim().to_string(), lines: Vec::new() });
                    continue;
                }
            }
            sections.last_mut().unwrap().lines.push(line.to_string());
        }
        Ini { sections }
    }

    pub fn render(&self) -> String {
        let mut out = String::new();
        for (i, s) in self.sections.iter().enumerate() {
            if i > 0 {
                out.push('[');
                out.push_str(&s.name);
                out.push_str("]\n");
            }
            for l in &s.lines {
                out.push_str(l);
                out.push('\n');
            }
        }
        out
    }

    fn key_of(line: &str) -> Option<(&str, &str)> {
        let t = line.trim_start();
        if t.starts_with(';') || t.starts_with('#') {
            return None;
        }
        let (k, v) = line.split_once('=')?;
        Some((k.trim(), v.trim()))
    }

    /// The first `key` in the first `[section]` holding it. Surrounding double quotes are
    /// stripped, as GetPrivateProfileString strips them.
    pub fn get(&self, section: &str, key: &str) -> Option<&str> {
        self.sections
            .iter()
            .skip(1)
            .filter(|s| s.name.eq_ignore_ascii_case(section))
            .flat_map(|s| s.lines.iter())
            .filter_map(|l| Self::key_of(l))
            .find(|(k, _)| k.eq_ignore_ascii_case(key))
            .map(
                |(_, v)| {
                    if v.len() >= 2 && v.starts_with('"') && v.ends_with('"') {
                        &v[1..v.len() - 1]
                    } else {
                        v
                    }
                },
            )
    }

    /// GetPrivateProfileInt's reading: leading digits (with an optional sign) or the default.
    pub fn get_int(&self, section: &str, key: &str, default: i64) -> i64 {
        let Some(v) = self.get(section, key) else { return default };
        let (neg, digits) = match v.strip_prefix('-') {
            Some(rest) => (true, rest),
            None => (false, v),
        };
        let n: String = digits.chars().take_while(|c| c.is_ascii_digit()).collect();
        match n.parse::<i64>() {
            Ok(x) if neg => -x,
            Ok(x) => x,
            Err(_) => default,
        }
    }

    pub fn set(&mut self, section: &str, key: &str, value: &str) {
        let idx = match self.sections.iter().skip(1).position(|s| s.name.eq_ignore_ascii_case(section)) {
            Some(i) => i + 1,
            None => {
                self.sections.push(Section { name: section.to_string(), lines: Vec::new() });
                self.sections.len() - 1
            }
        };
        let lines = &mut self.sections[idx].lines;
        let line = format!("{key}={value}");
        match lines.iter().position(|l| Self::key_of(l).is_some_and(|(k, _)| k.eq_ignore_ascii_case(key))) {
            Some(i) => lines[i] = line,
            None => {
                // After the section's last non-blank line, so a blank separator before the
                // next section stays where it was.
                let at = lines.iter().rposition(|l| !l.trim().is_empty()).map_or(0, |i| i + 1);
                lines.insert(at, line);
            }
        }
    }

    pub fn remove_section(&mut self, section: &str) {
        let mut first = true;
        self.sections.retain(|s| {
            let keep = first || !s.name.eq_ignore_ascii_case(section);
            first = false;
            keep
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trips_every_setting() {
        let mut s = Settings {
            editor_font: "SF Mono".into(),
            theme_mode: 1,
            log_level: LogLevel::Debug,
            log_file: "/tmp/x.log".into(),
            snc_path: "/opt/snc".into(),
            line_numbers: true,
            update_skip_version: "0.1.16.220".into(),
            recents: Vec::new(),
        };
        s.add_recent(Path::new("/p/one"));
        s.add_recent(Path::new("/p/two"));
        let mut ini = Ini::default();
        s.write_to(&mut ini);
        assert_eq!(Settings::from_ini(&Ini::parse(&ini.render())), s);
    }

    #[test]
    fn reads_a_windows_written_file() {
        // The shape WritePrivateProfileString leaves: CRLF, `key=value`, sections in the
        // order they were first written, and the [editor] d2d key Windows now deletes.
        let text = "[editor]\r\nfont=Cascadia Code\r\nline_numbers=1\r\nd2d=0\r\n[ui]\r\ntheme=-1\r\n\
                    [log]\r\nlevel=3\r\nfile=C:\\Users\\b\\sentinelide.log\r\n\
                    [recents]\r\ncount=2\r\nitem0=G:\\a\r\nitem1=G:\\b\r\n";
        let s = Settings::from_ini(&Ini::parse(text));
        assert_eq!(s.editor_font, "Cascadia Code");
        assert!(s.line_numbers);
        assert_eq!(s.theme_mode, -1);
        assert_eq!(s.log_level, LogLevel::Debug);
        assert_eq!(s.recents, vec![PathBuf::from("G:\\a"), PathBuf::from("G:\\b")]);
    }

    #[test]
    fn a_write_keeps_what_it_does_not_manage() {
        let mut ini = Ini::parse("; hand-written\n[editor]\nfont = Menlo\ncustom = 7\n\n[other]\nk=v\n");
        ini.set("EDITOR", "Font", "Monaco");
        ini.set("editor", "line_numbers", "1");
        let out = ini.render();
        assert_eq!(
            out,
            "; hand-written\n[editor]\nFont=Monaco\ncustom = 7\nline_numbers=1\n\n[other]\nk=v\n"
        );
    }

    #[test]
    fn a_shorter_recents_list_leaves_no_stale_items() {
        let mut ini = Ini::parse("[recents]\ncount=3\nitem0=/a\nitem1=/b\nitem2=/c\n");
        let s = Settings { recents: vec!["/z".into()], ..Settings::default() };
        s.write_to(&mut ini);
        assert_eq!(ini.get("recents", "item1"), None);
        assert_eq!(Settings::from_ini(&ini).recents, vec![PathBuf::from("/z")]);
    }

    #[test]
    fn recents_dedupe_ignoring_case_and_cap() {
        let mut s = Settings::default();
        for i in 0..12 {
            s.add_recent(Path::new(&format!("/p/{i}")));
        }
        s.add_recent(Path::new("/P/5"));
        assert_eq!(s.recents.len(), MAX_RECENTS);
        assert_eq!(s.recents[0], Path::new("/P/5"));
        assert_eq!(s.recents.iter().filter(|r| r.to_string_lossy().eq_ignore_ascii_case("/p/5")).count(), 1);
    }

    #[test]
    fn get_int_reads_like_get_private_profile_int() {
        let ini = Ini::parse("[a]\nx=42abc\ny=-3\nz=abc\n");
        assert_eq!(ini.get_int("a", "x", 0), 42);
        assert_eq!(ini.get_int("a", "y", 0), -3);
        assert_eq!(ini.get_int("a", "z", 9), 9);
        assert_eq!(ini.get_int("a", "missing", 5), 5);
    }
}
