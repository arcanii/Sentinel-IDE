# SentinelIDE — Win32 prototype

Native Windows IDE for the Sentinel language, built from the UX spines
(`_bmad-output/planning-artifacts/ux-designs/ux-SentinelIDE-2026-06-27/DESIGN.md` +
`EXPERIENCE.md`) and the SQLTerminal-Win32 visual reference.
Win32 + Common Controls v6, MSVC/Ninja, C++17.

## Build & run
- `scripts\build.bat` — configures + builds with the VS-bundled CMake/Ninja + MSVC.
- Output: `build\SentinelIDE.exe`.
- Requires Visual Studio 2026 (MSVC) at the path set in `scripts\build.bat`.

## Layout
- `CMakeLists.txt` — build (links comctl32, dwmapi, uxtheme, gdi32, …).
- `packaging/app.manifest` + `SentinelIDE.rc` — Common Controls v6, per-monitor-v2 DPI, UTF-8.
- `src/ui/Theme.h` — the palette: **code embodiment of DESIGN.md** (dark/coral, OS light/dark
  follow, `diag-*` + `trust-verified` tokens). Change colors here.
- `src/WinMain.cpp` — entry point.
- `src/ui/MainWindow.{h,cpp}` — the window: DWM dark titlebar, owner-drawn toolbar with the
  `≡` popup menu, three regions (tree | editor | bottom dock), status bar + signing chip.
- `src/ui/SettingsDialog.{h,cpp}` / `src/ui/ProjectSettingsDialog.{h,cpp}` / `src/ui/SigningDialog.{h,cpp}` / `src/ui/AboutDialog.{h,cpp}`
  — the themed modal dialogs (app Settings; the structured Project Settings form; the Signing & Trust panel; the About box).
- `src/core/Project.h` — `sentinel.toml` model (incl. `[[target]]`) + `loadProject`/`saveProject`
  (the writer rewrites only managed values and preserves comments + unmodeled keys, including target blocks).
- `src/core/Proc.h` — synchronous `runCapture` + `stripAnsi`.
- `src/core/Signing.h` — ADR-0061 trust-manifest + `.sig` parsers, `verifyFile`, `sncSupportsSigning`.
- `src/core/Toolchain.h` — `findVcvars` (auto-detect MSVC env) + `captureMsvcEnv` (vcvars → build env block).

## Status — Phase 5: DONE · prototype roadmap (1–5) complete
A real, native Win32 IDE built from the UX spines + `Theme.h`:
1. **Themed shell** — DWM dark titlebar, `≡` popup menu, dark/coral identity, status bar (signing chip).
2. **Real controls** — dark `WC_TREEVIEW` + RichEdit editor, draggable splitter, Open Project.
3. **Syntax highlighting** — Sentinel keywords (incl. `secret`/`declassify`), strings, numbers, comments per `Theme.h`.
4. **Build/Run loop** — `snc.exe` on a worker thread; live-streamed Output (ANSI-stripped) with the exact command + exit code; miette diagnostics (`file:line:col`) parsed into a clickable Problems list. Building `examples\crypto.sentinel` reproduces the real Windows link gap (PRD §14 / gap #1 / UJ-3).
5. **Logging + Settings** — a configurable logfile (level + location) under `%LOCALAPPDATA%\SentinelIDE\logs\`; a themed Settings dialog (editor font, theme follow/light/dark, log level + location + Reveal) persisted to `settings.ini` and applied live.

6. **Project model + app icon** — a Sentinel **project** (`sentinel.toml`): executable/library (later shared/dll), `lib_paths`/`links`, signing via `sentinel-trust.toml` (native ADR-0061 Ed25519 trust), and an **Xcode-style scheme selector** at the top: target · **tier** ▾ · output path. Tiers are Sentinel's **release tiers** (TIERED_RELEASES.md) — **Development / Experimental / Stable / Hardened** — selecting one sets `target/<tier>/`. Open Folder auto-detects a project; Build composes the right `snc` command per type+tier. App icon from `art/S_icon.png` (→ `packaging/app.ico`). See [sentinel-project.md](sentinel-project.md).

7. **Tier scheme selector** — Xcode-style top selector (target · tier ▾ · output); tiers set `target/<tier>/`.
8. **Explorer views** — **Project** + **Files** tabs over a themed `WC_TREEVIEW`.
9. **Project Settings editor** — a themed modal form over `sentinel.toml` (name/version/type/entry,
   src/lib_paths/links/tier, ADR-0061 signing require/trust/sign). Opened from the **project root node**
   or **≡ ▸ Project Settings…**; Save persists via `saveProject` (comment-preserving, keeps `[[target]]`) then reloads.
10. **Signing & Trust (ADR-0061, real)** — a status-bar **trust chip** (✓ Signed / ⊘ Unsigned / ⚠ invalid)
    from an async `snc verify` of the open file; clicking it (or **≡ ▸ Signing & Trust…**) opens a panel running
    *real* `snc keygen`/`sign` (`--grant` caps)/`verify` + a trust-manifest viewer (dep · key · policy · grants · forbids).
11. **Multiple targets** — `[[target]]` array-of-tables (single-target fallback if none); the scheme selector
    becomes **`target ▾ · tier ▾`**; a **Targets** tree group; Build/Run/output follow the active target.
12. **New Project + `.sntproject`** — a folder is a project via a `*.sntproject` file (preferred) or legacy
    `sentinel.toml` (`findManifest`). **≡ ▸ New Project…** → Save dialog → `createNewProject` makes the dir
    (+ missing parents), `src/main.sentinel`, and `<name>.sntproject`, then opens it. App icon → `art/S2_icon.png`.
13. **Build toolchain + working builds** — Settings → BUILD TOOLCHAIN adds **snc** + **MSVC env** fields
    (Browse + auto-detect). The IDE auto-detects `vcvars64.bat` and injects the MSVC environment into builds,
    so `snc`'s `link.exe` step works — `examples` builds at exit 0 → a runnable `crypto.exe`. Run works too.
14. **Open Project + New File** — **≡ ▸ Open Project…** picks a manifest (`*.sntproject`/`sentinel.toml`) and
    loads its folder; **≡ ▸ New File…** creates a new `.sentinel` source (Save dialog, defaults to `src/`) and opens it.
15. **Polish** — themed **About** dialog (S2 shield, replacing the classic MessageBox); S2 icon for app/tree/about;
    smoother splitter (`BeginDeferWindowPos` + synchronous `RedrawWindow` + cached back-buffer + no-wrap editor).
16. **`.sentinel` file icon** — custom tree icon (`packaging/file.ico`, res 101) for `.sentinel` nodes.
17. **Editor edit/save + line numbers + error highlight** — dirty flag (`●` tab) + **Save** (Ctrl+S) + build auto-save;
    a toggleable **line-number gutter** (Ctrl+L, persisted); **error lines** tinted after a build; a real accelerator table.

Each phase verified by build + run + screenshot (`docs/screenshots/phase1..15*.png`).
CLI: `SentinelIDE.exe <file|folder> [--build] [--settings] [--project-settings] [--signing] [--tier <name>]`.

## Follow-ups (beyond the prototype roadmap)
- **Signing** — gate Build on `require = strict` end-to-end; sign the *built artifact* once the link gap closes; editable trust policy/grants beyond add+import; a default-key setting.
- **Targets** — per-target editing in the Project Settings form (today it edits project-level fields and preserves `[[target]]`); per-target `lib_paths`; a definable output dir.
- **Project Settings polish** — `entry`-exists validation; surface `icon`/`authors` in the form.
- **Dark popup menu** — the `≡` menu is the OS-light popup; theme it dark.
- **Clickable Output `file:line`** — currently navigation is via the Problems list (double-click); add direct Output-line clicks.
- **Direct2D editor** — swap RichEdit for a GPU text editor (the native-perf target, as in the SQLTerminal reference).
- **Accessibility** — backlogged per the UX decision; add the Settings build-command field.
