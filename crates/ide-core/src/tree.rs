// SPDX-License-Identifier: GPL-3.0-or-later
//! What the project tree shows, and what opening a path means — MainWindow.cpp's
//! addDir / populateProject / openPathArg, as data a host can draw however it likes.

use std::fs;
use std::path::{Path, PathBuf};

use crate::project::{has_project, Project};

/// One row of the tree.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Node {
    pub label: String,
    pub kind: NodeKind,
    pub children: Vec<Node>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum NodeKind {
    /// The project itself (on Windows, a click opens Project Settings).
    Project,
    /// A file the editor can open.
    File(PathBuf),
    /// A folder on disk.
    Dir(PathBuf),
    /// A grouping with no file behind it ("Sources", "Targets").
    Group,
    /// One of the project's targets, by index; a click makes it the active target.
    Target(usize),
}

/// How deep the source walk goes, and what it never descends into — addDir's limits.
const MAX_DEPTH: usize = 8;
const SKIPPED_DIRS: [&str; 3] = ["build", "target", "node_modules"];

/// Folders, then `.sentinel` files, each sorted by name, recursively. Hidden folders and
/// build output are skipped; empty folders are kept, as the Windows tree keeps them.
pub fn source_tree(dir: &Path) -> Vec<Node> {
    walk(dir, 0)
}

fn walk(dir: &Path, depth: usize) -> Vec<Node> {
    if depth > MAX_DEPTH {
        return Vec::new();
    }
    let Ok(rd) = fs::read_dir(dir) else { return Vec::new() };
    let (mut dirs, mut files) = (Vec::new(), Vec::new());
    for e in rd.flatten() {
        let Ok(name) = e.file_name().into_string() else { continue };
        let Ok(ft) = e.file_type() else { continue };
        if ft.is_dir() {
            if !name.starts_with('.') && !SKIPPED_DIRS.contains(&name.as_str()) {
                dirs.push(name);
            }
        } else if is_sentinel_file(Path::new(&name)) {
            files.push(name);
        }
    }
    dirs.sort();
    files.sort();
    let mut out: Vec<Node> = dirs
        .into_iter()
        .map(|d| {
            let p = dir.join(&d);
            Node { label: d, children: walk(&p, depth + 1), kind: NodeKind::Dir(p) }
        })
        .collect();
    out.extend(files.into_iter().map(|f| Node {
        kind: NodeKind::File(dir.join(&f)),
        label: f,
        children: vec![],
    }));
    out
}

pub fn is_sentinel_file(p: &Path) -> bool {
    p.extension().is_some_and(|e| e.eq_ignore_ascii_case("sentinel"))
}

/// The Project view: the project node over its manifest, its trust file (when present),
/// its targets (when there is more than one) and its sources.
pub fn project_tree(p: &Project) -> Node {
    let mut children =
        vec![Node { label: p.manifest.clone(), kind: NodeKind::File(p.manifest_path()), children: vec![] }];
    let trust = p.resolve(&p.trust);
    if trust.is_file() {
        children.push(Node { label: p.trust.clone(), kind: NodeKind::File(trust), children: vec![] });
    }
    if p.targets.len() > 1 {
        let targets = p
            .targets
            .iter()
            .enumerate()
            .map(|(i, t)| Node {
                label: format!(
                    "{}  ({})",
                    if t.name.is_empty() { &p.name } else { &t.name },
                    t.r#type.name()
                ),
                kind: NodeKind::Target(i),
                children: vec![],
            })
            .collect();
        children.push(Node { label: "Targets".into(), kind: NodeKind::Group, children: targets });
    }
    children.push(Node { label: "Sources".into(), kind: NodeKind::Group, children: source_tree(&p.dir) });
    Node { label: p.name.clone(), kind: NodeKind::Project, children }
}

/// The Files view, for a folder that is not a project.
pub fn folder_tree(dir: &Path) -> Node {
    let label = dir.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    Node { label, kind: NodeKind::Dir(dir.to_path_buf()), children: source_tree(dir) }
}

/// What opening `path` means. A folder opens as itself; a file opens the nearest enclosing
/// project (walking up at most 16 levels), else its own folder, and is then shown — unless
/// it is the manifest, which just opens the project.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OpenRequest {
    pub root: PathBuf,
    pub file: Option<PathBuf>,
}

pub fn open_request(path: &Path) -> Option<OpenRequest> {
    let meta = fs::metadata(path).ok()?;
    if meta.is_dir() {
        return Some(OpenRequest { root: path.to_path_buf(), file: None });
    }
    let own = path.parent()?.to_path_buf();
    let root = own.ancestors().take(16).find(|d| has_project(d)).map(Path::to_path_buf).unwrap_or(own);
    let name = path.file_name()?.to_string_lossy();
    let is_manifest = name.eq_ignore_ascii_case("sentinel.toml")
        || path.extension().is_some_and(|e| e.eq_ignore_ascii_case("sntproject"));
    Some(OpenRequest { root, file: (!is_manifest).then(|| path.to_path_buf()) })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn scratch(tag: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("ide-core-tree-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&d);
        fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn the_source_walk_skips_what_add_dir_skips() {
        let d = scratch("walk");
        for sub in ["src/nested", ".git", "target/dev", "build", "node_modules", "empty"] {
            fs::create_dir_all(d.join(sub)).unwrap();
        }
        for f in [
            "b.sentinel",
            "A.SENTINEL",
            "notes.txt",
            "src/main.sentinel",
            "src/nested/x.sentinel",
            "target/dev/t.sentinel",
        ] {
            fs::write(d.join(f), "").unwrap();
        }
        let t = source_tree(&d);
        let labels: Vec<&str> = t.iter().map(|n| n.label.as_str()).collect();
        assert_eq!(labels, ["empty", "src", "A.SENTINEL", "b.sentinel"]);
        let src = &t[1];
        assert_eq!(
            src.children.iter().map(|n| n.label.as_str()).collect::<Vec<_>>(),
            ["nested", "main.sentinel"]
        );
        assert_eq!(src.children[0].children[0].kind, NodeKind::File(d.join("src/nested/x.sentinel")));
        fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn opening_a_file_finds_its_project() {
        let d = scratch("open");
        fs::create_dir_all(d.join("p/src/deep")).unwrap();
        fs::write(d.join("p/p.sntproject"), "[project]\n").unwrap();
        fs::write(d.join("p/src/deep/a.sentinel"), "").unwrap();
        fs::write(d.join("loose.sentinel"), "").unwrap();

        let r = open_request(&d.join("p/src/deep/a.sentinel")).unwrap();
        assert_eq!(r, OpenRequest { root: d.join("p"), file: Some(d.join("p/src/deep/a.sentinel")) });
        let r = open_request(&d.join("p/p.sntproject")).unwrap();
        assert_eq!(r, OpenRequest { root: d.join("p"), file: None }, "a manifest just opens the project");
        let r = open_request(&d.join("loose.sentinel")).unwrap();
        assert_eq!(r.root, d, "no project above it: its own folder");
        assert_eq!(open_request(&d.join("p")).unwrap(), OpenRequest { root: d.join("p"), file: None });
        assert!(open_request(&d.join("missing")).is_none());
        fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn the_example_project_tree() {
        let ex = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../examples");
        let p = crate::project::load_project(&ex).unwrap();
        let t = project_tree(&p);
        assert_eq!(t.label, "crypto-lib");
        let labels: Vec<&str> = t.children.iter().map(|n| n.label.as_str()).collect();
        assert_eq!(labels, ["sentinel.toml", "sentinel-trust.toml", "Targets", "Sources"]);
        assert_eq!(t.children[2].children[1].kind, NodeKind::Target(1));
        let sources: Vec<&str> = t.children[3].children.iter().map(|n| n.label.as_str()).collect();
        assert_eq!(sources, ["crypto.sentinel", "hello.sentinel"]);
    }
}
