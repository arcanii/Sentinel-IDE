// SPDX-License-Identifier: GPL-3.0-or-later
//! The project model — a folder with a `*.sntproject` (preferred) or legacy
//! `sentinel.toml` manifest. Project.h's.
//!
//! The same split as Windows: Sentinel reads the manifest (`parse_manifest`) and rewrites
//! it (`save_manifest`); this module applies the defaults, the single-target fallback,
//! TOML rendering of the values it writes back, and the `snc build` command line.

use std::ffi::OsString;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use sentinel_sys::{ManifestFields, ManifestValues, TargetValues};

use crate::{read_text_lossy, Error, Result};

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum ProjectType {
    #[default]
    Executable,
    Library,
    Shared,
}

impl ProjectType {
    pub fn name(self) -> &'static str {
        match self {
            ProjectType::Executable => "executable",
            ProjectType::Library => "library",
            ProjectType::Shared => "shared",
        }
    }

    /// Loose, as typeFromName is: any value containing `lib` is a library, then `shared`
    /// or `dll` is shared, and everything else — including nonsense — is an executable.
    pub fn from_name(s: &str) -> Self {
        if s.contains("lib") {
            ProjectType::Library
        } else if s.contains("shared") || s.contains("dll") {
            ProjectType::Shared
        } else {
            ProjectType::Executable
        }
    }
}

/// Build tiers (Sentinel-lang's TIERED_RELEASES.md). Output goes to `target/<dir>/`.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum Tier {
    Development,
    #[default]
    Experimental,
    Stable,
    Hardened,
}

impl Tier {
    pub const ALL: [Tier; 4] = [Tier::Development, Tier::Experimental, Tier::Stable, Tier::Hardened];

    pub fn name(self) -> &'static str {
        match self {
            Tier::Development => "Development",
            Tier::Experimental => "Experimental",
            Tier::Stable => "Stable",
            Tier::Hardened => "Hardened",
        }
    }

    pub fn dir(self) -> &'static str {
        match self {
            Tier::Development => "dev",
            Tier::Experimental => "experimental",
            Tier::Stable => "stable",
            Tier::Hardened => "hardened",
        }
    }

    /// Substring match in tierFromName's order; anything unrecognised is Experimental.
    pub fn from_name(s: &str) -> Self {
        if s.contains("dev") {
            Tier::Development
        } else if s.contains("stable") {
            Tier::Stable
        } else if s.contains("hard") {
            Tier::Hardened
        } else {
            Tier::Experimental
        }
    }
}

/// One build artifact. A project declares one or more as `[[target]]` blocks; a project
/// with none gets one synthesised from its `[project]`/`[build]` fields.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Target {
    pub name: String,
    pub entry: String,
    pub r#type: ProjectType,
    /// Per-target native links; when empty, the project's `[build] links` apply.
    pub links: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Project {
    pub dir: PathBuf,
    /// The manifest's file name within `dir`.
    pub manifest: String,
    pub name: String,
    pub version: String,
    pub entry: String,
    pub src_dir: String,
    pub icon: String,
    pub r#type: ProjectType,
    pub default_tier: Tier,
    pub lib_paths: Vec<String>,
    pub links: Vec<String>,
    /// `off` | `warn` | `strict` (ADR 0061).
    pub sign_require: String,
    /// The consumer trust manifest, relative to `dir`.
    pub trust: String,
    /// `snc sign` the built artifact.
    pub sign_output: bool,
    /// Never empty once loaded.
    pub targets: Vec<Target>,
    /// The manifest declared real `[[target]]` blocks (the Project Settings form shows the
    /// per-target editor only then).
    pub explicit_targets: bool,
}

impl Project {
    /// The defaults and the single-target fallback over what the manifest stated — the
    /// half of loadProject that is not parsing. Pure, so it can be tested without a file.
    pub fn from_fields(dir: &Path, manifest: &str, f: ManifestFields) -> Project {
        let base = dir.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        let name = f.name.unwrap_or(base);
        let r#type = ProjectType::from_name(f.r#type.as_deref().unwrap_or("executable"));
        let entry = f.entry.unwrap_or_default();
        let sign = f.sign.unwrap_or_else(|| "false".into());
        let mut targets: Vec<Target> = f
            .targets
            .into_iter()
            .map(|t| Target {
                name: t.name,
                entry: t.entry,
                r#type: ProjectType::from_name(&t.r#type),
                links: t.links,
            })
            .collect();
        let explicit_targets = !targets.is_empty();
        if targets.is_empty() {
            targets.push(Target { name: name.clone(), entry: entry.clone(), r#type, links: f.links.clone() });
        }
        Project {
            dir: dir.to_path_buf(),
            manifest: manifest.to_string(),
            name,
            version: f.version.unwrap_or_else(|| "0.1.0".into()),
            entry,
            src_dir: f.src.unwrap_or_else(|| "src".into()),
            icon: f.icon.unwrap_or_default(),
            r#type,
            default_tier: Tier::from_name(f.default_tier.as_deref().unwrap_or("experimental")),
            lib_paths: f.lib_paths,
            links: f.links,
            sign_require: f.require.unwrap_or_else(|| "off".into()),
            trust: f.trust.unwrap_or_else(|| "sentinel-trust.toml".into()),
            sign_output: sign == "true" || sign == "1",
            targets,
            explicit_targets,
        }
    }

    pub fn manifest_path(&self) -> PathBuf {
        let m = if self.manifest.is_empty() { "sentinel.toml" } else { &self.manifest };
        self.dir.join(m)
    }

    /// A manifest-relative path, resolved against the project folder. Manifests travel
    /// between Windows and macOS, and Windows accepts either separator, so a `\` here is a
    /// separator and never part of a name.
    pub fn resolve(&self, rel: &str) -> PathBuf {
        let mut p = self.dir.clone();
        for part in rel.split(['/', '\\']).filter(|s| !s.is_empty()) {
            p.push(part);
        }
        p
    }

    /// The managed values, rendered as TOML for save_manifest — encodeSaveModel's job.
    pub fn manifest_values(&self) -> ManifestValues {
        ManifestValues {
            name: toml_str(&self.name),
            version: toml_str(&self.version),
            r#type: toml_str(self.r#type.name()),
            entry: toml_str(&self.entry),
            src: toml_str(&self.src_dir),
            lib_paths: toml_arr(&self.lib_paths),
            links: toml_arr(&self.links),
            default_tier: toml_str(self.default_tier.dir()),
            require: toml_str(&self.sign_require),
            trust: toml_str(&self.trust),
            sign: if self.sign_output { "true" } else { "false" }.into(),
            targets: self
                .targets
                .iter()
                .map(|t| TargetValues {
                    name: toml_str(&t.name),
                    entry: toml_str(&t.entry),
                    r#type: toml_str(t.r#type.name()),
                    links: toml_arr(&t.links),
                })
                .collect(),
        }
    }
}

/// A TOML basic string: `\` and `"` escaped, nothing else touched.
pub fn toml_str(s: &str) -> String {
    let mut o = String::with_capacity(s.len() + 2);
    o.push('"');
    for c in s.chars() {
        if c == '\\' || c == '"' {
            o.push('\\');
        }
        o.push(c);
    }
    o.push('"');
    o
}

pub fn toml_arr(v: &[String]) -> String {
    let items: Vec<String> = v.iter().map(|s| toml_str(s)).collect();
    format!("[{}]", items.join(", "))
}

/// The manifest file name in `folder`: a `*.sntproject` if there is one, else
/// `sentinel.toml`, else `None`. Windows' FindFirstFileW returns whichever `.sntproject` the
/// directory lists first; here several are ordered by name, so the choice is stable.
pub fn find_manifest(folder: &Path) -> Option<String> {
    let mut found: Vec<String> = fs::read_dir(folder)
        .ok()?
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().is_ok_and(|t| t.is_file()))
        .filter_map(|e| e.file_name().into_string().ok())
        // Case-insensitive, as the Windows pattern match is.
        .filter(|n| n.to_ascii_lowercase().ends_with(".sntproject"))
        .collect();
    found.sort();
    if let Some(n) = found.into_iter().next() {
        return Some(n);
    }
    folder.join("sentinel.toml").is_file().then(|| "sentinel.toml".into())
}

pub fn has_project(folder: &Path) -> bool {
    find_manifest(folder).is_some()
}

pub fn load_project(folder: &Path) -> Result<Project> {
    let manifest = find_manifest(folder).ok_or(Error::NoManifest)?;
    let text = read_text_lossy(&folder.join(&manifest))?.unwrap_or_default();
    let fields = sentinel_sys::parse_manifest(text.as_bytes())?;
    Ok(Project::from_fields(folder, &manifest, fields))
}

/// Write the managed values back into the manifest, surgically: comments, blank lines,
/// unmodeled keys and `[[target]]` blocks survive. Raw bytes in and out — save_manifest is
/// byte-transparent, so a Latin-1 comment comes back as it was.
pub fn save_project(p: &Project) -> Result<()> {
    let path = p.manifest_path();
    let before = match fs::read(&path) {
        Ok(b) => b,
        Err(e) if e.kind() == io::ErrorKind::NotFound => Vec::new(),
        Err(e) => return Err(e.into()),
    };
    let after = sentinel_sys::save_manifest(&before, &p.manifest_values())?;
    fs::write(&path, after)?;
    Ok(())
}

/// What `snc build` is run with for one target and tier — composeBuild, as arguments.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BuildPlan {
    pub args: Vec<OsString>,
    /// `<project>/target/<tier>/` — the caller creates it before running.
    pub out_dir: PathBuf,
    /// What a successful build leaves behind; the file the signing step signs.
    pub artifact: PathBuf,
}

/// The arguments after the `snc` program itself. macOS artifact names: an executable has
/// no extension, a shared library is `.dylib`, a static one `.a`.
pub fn build_plan(p: &Project, t: &Target, tier: Tier) -> BuildPlan {
    let out_dir = p.dir.join("target").join(tier.dir());
    let name = if t.name.is_empty() { &p.name } else { &t.name };
    let entry = p.resolve(&t.entry);
    let mut a: Vec<OsString> = vec!["build".into()];
    let artifact = match t.r#type {
        ProjectType::Library | ProjectType::Shared => {
            let (flag, ext) =
                if t.r#type == ProjectType::Library { ("--lib", "a") } else { ("--shared", "dylib") };
            let artifact = out_dir.join(format!("{name}.{ext}"));
            a.extend([flag.into(), entry.into(), "-o".into(), artifact.clone().into()]);
            a.extend(["--emit-header".into(), out_dir.join(format!("{name}.h")).into()]);
            artifact
        }
        ProjectType::Executable => {
            let artifact = out_dir.join(name);
            a.extend([entry.into(), "-o".into(), artifact.clone().into()]);
            artifact
        }
    };
    for lp in &p.lib_paths {
        a.extend(["--lib-path".into(), p.resolve(lp).into()]);
    }
    for ln in if t.links.is_empty() { &p.links } else { &t.links } {
        a.extend(["--link".into(), ln.into()]);
    }
    if p.sign_require != "off" {
        a.extend(["--require-signatures".into(), p.sign_require.clone().into()]);
        a.extend(["--trust".into(), p.resolve(&p.trust).into()]);
    }
    BuildPlan { args: a, out_dir, artifact }
}
