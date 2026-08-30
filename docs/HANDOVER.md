# Sentinel-IDE — Handover

_Last updated: 2026-08-30._

> **Naming:** the product/exe is **`Sentinel-IDE`** (matching `Sentinel-lang` / `Sentinel-learning`);
> the build output is `build\Sentinel-IDE.exe`. **Internal identifiers stay `SentinelIDE`** by design —
> the window class `SentinelIDEMainWindow`, the `sentinelide` C++ namespace, the settings dir
> `%LOCALAPPDATA%\SentinelIDE`, the CMake target id, and the local folder `G:\SentinelIDE`. So scripts
> that match the **process name** use `Sentinel-IDE`, but `-Class SentinelIDEMainWindow` is unchanged.

**Sentinel-IDE** is a native, Windows-first IDE for the **Sentinel** language, intended to
eventually be built *in* Sentinel (thin native host shrinking over time). Two workstreams
exist so far:

1. **UX design spines** (BMad) — `DESIGN.md` + `EXPERIENCE.md`, status **draft**.
2. **A working Win32 C++ prototype** — phases 1–41 built and verified.

---

## TL;DR — current state

- The prototype **builds and runs**: `scripts\build.bat` → `build\Sentinel-IDE.exe`.
- It's a real dark/coral Win32 IDE: themed shell with **dark popup/context menus**, dark
  TreeView + RichEdit editor (syntax highlighting, line gutter, dirty `●`/Save, undo/redo),
  `snc` build/run with live streamed Output (**clickable `file:line:col`**) + a Problems
  triad, a configurable logfile + Settings dialog, a **Sentinel project model**
  (a `*.sntproject` file, or legacy `sentinel.toml`) with **multiple build targets**, an
  **Xcode-style target ▾ · tier ▾ scheme selector**, a **Project/Files explorer**,
  **New / Open / Close Project + Recent Projects + New File**, a **structured Project Settings
  editor with per-target editing**, and a **Signing & Trust panel** driving *real* ADR-0061
  `snc keygen`/`sign`/`verify` with a live status-bar trust chip.
- Unsaved edits are **never discarded silently**: closing a file, the project, the window, or an
  update install all route through one **Save / Don't Save / Cancel** prompt (phase 39).
- Also: **sealed projects** (password-encrypted, `core/Seal.h`), **file associations**
  (double-click `.sntproject`/`.sentinel`), an **About box with lines-of-code badges** whose
  total is counted by **`tools/loc.sentinel`** — the first piece written *in* Sentinel — and a
  **Windows installer** (Inno Setup → `build/installer/`).
- **It is a git repo** (branch `main`, GPL-3.0 `LICENSE`, `README.md`, `.gitignore`,
  `.gitattributes`), pushed to **`arcanii/Sentinel-IDE`** (`https://github.com/arcanii/Sentinel-IDE`);
  `main` tracks `origin/main`. **PUBLIC since phase 32 — and it must stay that way:** WinSparkle
  fetches the update appcast over unauthenticated HTTPS, so going private returns 404 and silently
  disables auto-update for every installed client.
- **Released.** Latest is **v0.1.7 (build 164)**; eight releases so far. Every file reader/parser in
  the IDE runs in Sentinel. Auto-update is live (WinSparkle + Ed25519-signed `appcast.xml`), but read
  phases 40–41 before trusting it: v0.1.0–v0.1.4 offered updates that **could never install**, and
  0.1.5 fixed only the **manual** ≡ ▸ Check for Updates… path. Since **phase 41** the background path
  works too, on **our own timer** — WinSparkle's periodic check is switched off because its prompt is
  the broken one. See **Releases** below and `docs/RELEASING.md` for the cut procedure.
- It is built **from the UX spines + the SQLTerminal-Win32 visual reference**, and it has
  empirically reproduced real toolchain gaps (see *Known gaps*).

---

## Repo layout (`G:\SentinelIDE`)

| Path | What |
|---|---|
| `CMakeLists.txt` | Build (MSVC/Ninja, C++17; links comctl32, dwmapi, uxtheme, …) |
| `src/core/` | **Portable-intended logic** (the reuse layer): Project, Signing, Seal, Settings, Toolchain, FileAssoc, Proc, Logger. Win32-coupled today; the layer that trends toward a Sentinel C-ABI lib. |
| `src/host/win32/` | **The thin Win32 native host** (per-platform). A macOS/Linux port adds `src/host/<os>/` against the same core. |
| `src/host/win32/WinMain.cpp` | Entry → `runApp` |
| `src/host/win32/MainWindow.cpp` | The whole app (window, toolbar, tree, editor, dock, build/run, project) |
| `src/host/win32/Theme.h` | Palette — **code embodiment of DESIGN.md** (dark/coral, OS follow, diag + trust + dialog helpers) |
| `src/host/win32/SettingsDialog.{h,cpp}` | Themed modal Settings dialog |
| `src/host/win32/ProjectSettingsDialog.{h,cpp}` | Themed modal **Project Settings** form over `sentinel.toml` |
| `src/host/win32/SigningDialog.{h,cpp}` | Themed modal **Signing & Trust** panel (ADR-0061 keygen/sign/verify + trust viewer) |
| `src/host/win32/AboutDialog.{h,cpp}` | Themed modal **About** box (S2 shield + name/version/tagline + lines-of-code shields.io badges) |
| `src/host/win32/PasswordDialog.{h,cpp}` | Themed modal password prompt (one field, or two with a match check for sealing) |
| `src/host/win32/UpdateDialog.{h,cpp}` | Themed modal **update offer** (class `SentinelUpdateDlg`, Skip/Install/Later) raised by the updater's own appcast poll — WinSparkle's equivalent prompt leads to the install path that does nothing (phase 41). |
| `src/host/win32/SaveChangesDialog.{h,cpp}` | Themed modal **Save / Don't Save / Cancel** prompt for unsaved edits (class `SentinelSaveDlg`). Driven by `MainWindow.cpp::confirmSaveIfDirty` — the single choke point every discarding path goes through (phase 39). |
| `src/core/Seal.h` | **Project sealing** (ADR-style): archive → LZMS-compress → AES-256-GCM under a random DEK, wrapped per password slot (PBKDF2-HMAC-SHA256). LUKS-like extensible unlock slots — **format v2** (`SNTSEAL2`): slots carry `slot_len` so unknown types are skipped, and the 24-byte header prefix is AEAD-bound as AAD. Reads v1. Native CNG; the AEAD+KDF core is a Sentinel-rewrite target. |
| `tests/seal_test.cpp` | Tests the `.sealed` format: one case per defect + a v1 back-compat case (25 assertions). `cmake --build build --target seal_test` or `ctest`. |
| `src/sentinel/` | **Product logic written *in* Sentinel, compiled into the binary** (phases 35–38). `parsers.sentinel` = four parsers (diagnostic, trust manifest, `.sig` carrier, project manifest), built to `build/generated/parsers.lib` (one C-ABI lib, ADR 0059) and called from `parseDiag` / `loadTrust` / `readSig` / `loadProject`. Every file reader in the IDE. About box shows the % in Sentinel. |
| `tests/*_xcheck.cpp` | Prove each Sentinel parser stays byte-identical to its C++ oracle: `diag` (11), `trust` (12), `sig` (12), `manifest` (14). Plus `seal_test`. `ctest --test-dir build`. |
| `src/core/FileAssoc.h` | Per-user (`HKCU\Software\Classes`) file associations for `.sntproject`/`.sentinel` → open in this exe (`registerFileAssociations`; ≡ ▸ Register File Associations…). |
| `src/core/Proc.h` | `runCapture` (synchronous run-and-capture) + `stripAnsi` |
| `src/core/Signing.h` | Trust manifest (`[[keys]]`) + `.sig` parsers, `verifyFile`, and `sncSigningCaps` — which reports **verify** and **keygen/sign** as separate capabilities (they fail independently; see phase 30) |
| `src/core/Toolchain.h` | `findVcvars` (auto-detect MSVC env) + `captureMsvcEnv` (vcvars → env block for builds) |
| `src/core/Logger.h` | Thread-safe append logfile (level + location) |
| `src/core/Settings.h` | `settings.ini` in `%LOCALAPPDATA%\SentinelIDE` — font/theme/log/toolchain + the **`[recents]`** project list (`addRecent`, capped `kMaxRecents`) |
| `src/core/Project.h` | manifest parsing (`findManifest`: prefers `*.sntproject`, else `sentinel.toml`) + tiers + **`[[target]]`** + `saveProject` (surgical writer → the loaded manifest; preserves comments + unmodeled keys incl. `[[target]]`) |
| `packaging/` | `app.manifest` (ComCtl v6, per-monitor-v2 DPI), `SentinelIDE.rc`, `app.ico` (res 100), `file.ico` (res 101 — `.sentinel` tree icon) |
| `scripts/build.bat` | Configure + build (needs VS 2026 — path is hard-coded inside). **Derives the build number from `git rev-list --count HEAD`** (+ a `BUILDBASE` offset) and writes `build/generated/Version.h` (consumed by the C++ sources + the `.rc`). Warns `BUILD_DIRTY` when the tree doesn't match HEAD. |
| `scripts/launch.ps1` | Launch the exe **detached** (WMI) so it survives the shell |
| `scripts/capture.ps1` | Screenshot the window → `build\shot.png` (DPI-aware PrintWindow; `-Class` for dialogs) |
| `scripts/convert-icon.ps1` | PNG → multi-size letterboxed `.ico`. Defaults `art/S2_icon.png`→`packaging/app.ico`; `-Src/-Dst` for `file.ico`. (Letterboxes — S2 is 554×657, not square. After rerun, touch the `.rc` so ninja relinks; the per-size Win icon cache may need `ie4uinit -ClearIconCache` + reboot to refresh large/extra-large.) |
| `scripts/loc.ps1` | Counts the IDE's source by language → `build/generated/Loc.h` (About-box badges); builds the corpus and runs **`tools/loc.sentinel`** via `snc` for the Sentinel-verified total. Called by `build.bat`. |
| `packaging/Sentinel-IDE.iss` | **Inno Setup** installer script → per-user `setup.exe` (Start-Menu shortcut, file associations mirroring `FileAssoc.h`, uninstall). |
| `scripts/make-installer.bat` | Build the app, then compile the installer (needs Inno Setup 6: `winget install JRSoftware.InnoSetup`) → `build/installer/`. |
| `src/host/win32/Updater.{h,cpp}` | **Auto-update** over WinSparkle — EdDSA-signed appcast, `initUpdater`/`checkForUpdates`/`shutdownUpdater`. Inactive until a real public key replaces the placeholder (phase 32). |
| `third_party/winsparkle/` | Vendored WinSparkle 0.9.3 x64 (DLL + import lib + headers, MIT). CMake copies the DLL beside the exe; the installer ships it. |
| `scripts/make-appcast.ps1` | Generate `appcast.xml` for a release (takes the signature; never touches the private key). |
| `docs/RELEASING.md` | Update-signing key setup + the per-release procedure. **Read before cutting a release.** |
| `docs/Sentinel-lang_request.md` | **Prioritised capability requests to the Sentinel-lang team** (R1–R15), each reproduced + measured on this machine. Also the authoritative record of what `snc`/`std` can actually do — it corrects several long-standing false claims in *this* file. |
| `THIRD-PARTY-NOTICES.txt` | WinSparkle MIT text + the SQLTerminal-Win32 GPL lineage note. |
| `tools/loc.sentinel` | **The first part of Sentinel-IDE written *in* Sentinel** — a whole-file line counter (read_file/write_file). Counts toward the "Sentinel" LOC badge. |
| `examples/` | Sample project: `sentinel.toml` (+ `[[target]]`s), `sentinel-trust.toml`, `crypto.sentinel`(+`.sig`), `hello.sentinel` |
| `art/` | `S2_icon.png` (app icon — metallic shield), `A_simple_clean…827808.png` (the `.sentinel` file icon — page + blue S + padlock), plus earlier iteration drafts (`…721412/818278.png`, `Remove_the_drop_shadow…726263.png`) |
| `docs/` | `prototype.md`, `sentinel-project.md`, this file, `screenshots/phase1..15*.png` |
| `LICENSE` | GPL-3.0-or-later (verbatim; the Win32 shell derives from GPL-3.0 SQLTerminal-Win32) |
| `README.md` | Public-facing readme (status, features, build, installer, license) |
| `.gitignore` / `.gitattributes` | Ignores `build/`, `target/`, artifacts, `*.sealed`/`*.key`, `.claude/`, `_bmad/`. Attributes force `eol=crlf` on text and mark `*.sig`/`*.ico`/`*.png` **binary** so the signed demo stays byte-exact. |
| `_bmad-output/planning-artifacts/` | UX spines, PRD, brief (BMad) — tracked |
| `build/` | CMake/Ninja output (`Sentinel-IDE.exe`, `installer/`) — regenerated (gitignored) |

## Reference repos (read-only, on the same machine)

- **`G:\SQLTerminal-Win32`** — the Win32 **visual/architecture reference** (same author/house
  style). Source of `Theme.h`, themed dialogs, tree + splitters, worker threads. **GPL-3.0.**
- **`G:\Sentinel-lang`** — the language + **`snc` compiler** + ADRs/docs. Two binaries,
  `target\release\snc.exe` and `target\debug\snc.exe`. **As of 2026-07 BOTH carry the ADR-0061
  subcommands** (`keygen`/`sign`/`verify` + `--require-signatures`/`--trust`) — the old note that
  the release was a stale signing-less C1.0b is **no longer true**, and `target\release\
  sentinel_runtime.lib` now exists too. `findSnc()` tries release first and returns the first
  signing-capable binary, so **the IDE now uses the release build**. Note `scripts\build.bat`
  still hard-codes the **debug** snc for the LOC step — so the build and the IDE can use
  different binaries; harmless today, but don't assume they match.
  Key docs: `docs/decisions/0061-code-signing-and-trust.md` (signing/trust — **v1 IMPLEMENTED,
  Windows-verified**), `docs/TIERED_RELEASES.md` (build tiers), ADR 0059 (C-ABI lib/shared),
  ADR 0037 (modules / `--lib-path`).

## Build / run / capture

```
scripts\build.bat                       :: → build\Sentinel-IDE.exe   (BUILD_OK on success)
build\Sentinel-IDE.exe [<file|folder>] [--build] [--settings] [--project-settings] [--signing] [--tier <name>]
powershell -File scripts\launch.ps1 "G:\SentinelIDE\examples" --build --tier hardened
powershell -File scripts\capture.ps1                  :: main window → build\shot.png (then Read that PNG)
powershell -File scripts\capture.ps1 -Class SentinelProjectDlg   :: a modal dialog by window class
```

- `launch.ps1` now takes the open path + flags as **separate** positional args
  (`launch.ps1 "<folder>" --build`); it collects them via `ValueFromRemainingArguments`.
  (The old `[string]$Args` param collided with PowerShell's automatic `$Args` and silently
  dropped every argument — that's why a launch could come up with no folder open.)

- **Versioning:** marketing version is **`0.1.3`** (`MKT`/`MKTRC` at the top of `build.bat`; was
  `0.1.0` through the first release, bumped for v0.1.1). The build number is
  **`git rev-list --count HEAD` + `BUILDBASE`** (phase 34). Same commit → same version, so any
  released artifact can be rebuilt from its tag. It only advances when you **commit**, so repeated
  builds of one commit are indistinguishable by version — deliberate. `build.bat` prints
  `BUILD_DIRTY <n>` when the tree doesn't match HEAD (the binary is then stamped with a commit
  whose contents it lacks — fine while developing, never ship one), and **fails** if git can't
  answer rather than stamping a wrong version.
  `BUILDBASE` (=100) exists only to stay above the **retired** `packaging/build_number.txt`
  counter's high-water mark of 38 — the commit count was 26 at the switch, so a raw count would
  have moved the version *backwards*, which WinSparkle can never recover from. Don't lower it.
  `build.bat` composes `build/generated/Version.h` (`SENTINEL_VERSION_DISPLAY_W` = `L"0.1.1 (build N)"`,
  `SENTINEL_FILEVERSION` = `0,1,1,N`, etc.), included by `MainWindow.cpp` (status bar), `AboutDialog.cpp`,
  and `SentinelIDE.rc` (the exe's **FileVersion** = `0.1.1.N`; **ProductVersion** stays hard-coded
  `0.1.0.0` in the `.rc` — which is why the installer reads FileVersion, not ProductVersion; see
  phase 33). CMake writes a fallback `Version.h` (build 0) so a raw `cmake --build` without
  `build.bat` still compiles.
- **Build env:** Visual Studio 2026 Community (MSVC + bundled CMake/Ninja). The exact path is
  hard-coded in `scripts\build.bat` — change it if VS moves. Drive builds through `cmd /c
  scripts\build.bat` (it calls `vcvars64.bat`). Note: a sandbox guard rejects `Remove-Item`
  in the **same** PowerShell call as `cmd /c` — keep them in separate calls.
- **Screenshots:** the app isn't an installed app, so the screenshot MCP can't allowlist it.
  Use `scripts\capture.ps1` (WMI-detached launch + DPI-aware `PrintWindow`).

## Environment gotchas (this machine)

Small, non-obvious frictions that cost real time when rediscovered. None is a defect in the project.

- **Never edit a `.bat` with `sed`, a heredoc, or anything else that writes LF.** `cmd.exe` needs
  CRLF and fails with nonsense like `'t' is not recognized`. Use the `Edit` tool, which preserves
  line endings. **This overrides any harness preference for shell-based edits.** The same care
  applies to `examples/crypto.sentinel` — rewriting it with LF invalidates its committed `.sig`,
  which `.gitattributes` marks binary precisely to prevent.
- **`G:` is a mapped network share** (`\truenas.local\hkshare01\...`). Ordinary binaries run from it
  fine — `launch.ps1` runs the app straight off the share, and the `ctest` binaries run in place — but
  an **Inno installer** launched from `build\installer\` fails with **"Access is denied"**. Copy the
  setup exe to local disk before running it. Git also needs `safe.directory` here (already configured
  globally).
- **A `Remove-Item` in the same PowerShell call as a `cmd /c` is rejected** by a sandbox guard
  ("path is protected from removal"). Put them in separate calls.
- **Bash heredocs strip one level of backslash escaping** on the way to the interpreter, so a Python
  one-liner matching C++ text like `L"\r\n"` needs `\\r\\n` in the heredoc. If a search string
  mysteriously fails to match, that is usually why.

## Prototype status — phases 1–41 (all done; screenshots cover 1–11, 13, 15 — see note below)

1. **Themed shell** — DWM dark titlebar, `≡` popup menu, dark/coral identity, status bar.
2. **Real controls** — dark `WC_TREEVIEW` + RichEdit editor, draggable splitter, Open Project (`IFileOpenDialog`).
3. **Syntax highlighting** — Sentinel keywords (incl. `secret`/`declassify`), strings, numbers, comments per `Theme.h`.
4. **Build/Run loop** — `snc.exe` on a worker thread, live-streamed Output (ANSI-stripped) + exit code; miette `file:line:col` → clickable Problems list.
5. **Logging + Settings** — configurable logfile (`%LOCALAPPDATA%\SentinelIDE\logs`) + themed Settings dialog (font, theme, log level/location).
6. **Project model + icon** — `sentinel.toml`; `art/S_icon.png` → app icon. *(That icon was replaced by `art/S2_icon.png` in phase 12; `S_icon.png` no longer exists.)*
7. **Tier scheme selector** — Xcode-style top selector: target · **tier** ▾ · output; tiers = Development/Experimental/Stable/Hardened (TIERED_RELEASES.md); output → `target/<tier>/`.
8. **Explorer views** — **Project** (Sentinel-icon root, `sentinel.toml`/`sentinel-trust.toml`/Sources) and **Files** (raw tree), via tabs.
9. **Project Settings editor** — a themed modal form over `sentinel.toml` (name/version/type/entry, src/lib_paths/links/tier, ADR-0061 signing require/trust/sign). Opened by clicking the **project root node** or **≡ ▸ Project Settings…**; Save persists via `saveProject` (preserves comments + unmodeled keys like `icon`/`authors` **and `[[target]]` blocks**), then reloads. Clicking the `sentinel.toml` child still opens the **raw** manifest; opening a project lands in the **active target's entry source**. `docs/screenshots/phase9-project-settings.png`.
10. **Signing & Trust (ADR-0061, real)** — a status-bar **trust chip** (✓ Signed / ⊘ Unsigned / ⚠ Signature invalid) computed by an async `snc verify` of the open file; clicking it (or **≡ ▸ Signing & Trust…**) opens a panel that runs *real* `snc keygen`/`sign` (with `--grant` capabilities)/`verify` and views the consumer trust manifest (dependency · key · policy · grants · forbids), plus "Import current key as trusted". `examples/crypto.sentinel.sig` is a committed signed demo. `docs/screenshots/phase10-signing.png`.
11. **Multiple targets** — `[[target]]` array-of-tables in the manifest (a project with none gets one synthesized target — backward compatible). The scheme selector is now **`target ▾ · tier ▾`**; the Project tree gains a **Targets** group; Build/Run/output follow the active target. Switch via the selector dropdown or a target tree node. `docs/screenshots/phase11-targets.png`.
12. **New Project + `.sntproject`** — the IDE recognizes a folder as a project by a **`*.sntproject`** file (preferred — the native IDE project file) **or** legacy `sentinel.toml` (`findManifest`; `loadProject`/`saveProject`/the tree all use the discovered manifest). **≡ ▸ New Project…** opens a Save dialog, then `createNewProject` makes the chosen dir **+ any missing parent dirs** (`SHCreateDirectoryExW`), a `src/` folder with a starter `main.sentinel`, and the `<name>.sntproject` manifest, then opens it. App icon swapped to the cleaner **`art/S2_icon.png`**.
13. **Build toolchain in Settings + working builds** — Settings → **BUILD TOOLCHAIN** adds **Sentinel (snc)** and **MSVC env** fields (Browse + auto-detect hints; blank = auto). The IDE auto-detects `vcvars64.bat` and **injects the MSVC environment** into builds so `snc`'s `link.exe` step works — closing the build→link gap. Verified: `examples` builds at exit 0 → a runnable `crypto.exe` (exit 42). `docs/screenshots/phase13-settings-build.png`.
14. **Open Project + New File** — **≡ ▸ Open Project…** is a manifest file picker (`*.sntproject` / `sentinel.toml`) that loads the containing folder (replacing the old folder picker). **≡ ▸ New File…** creates a new `.sentinel` source via a Save dialog (defaulting to the project's `src/`), then opens it + refreshes the tree.
15. **Polish — themed About + S2 icon + smooth splitter.** App icon/tree-node/About all use `art/S2_icon.png`. **≡ ▸ About** is now a dark/coral themed dialog (S2 shield via `DrawIconEx`), not a classic MessageBox (`docs/screenshots/phase15-about.png`). Splitter drag is smoothed to match SQLTerminal's GDI technique: `layout()` batches pane moves with `BeginDeferWindowPos`; the drag repaints synchronously via `RedrawWindow(RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN|RDW_NOERASE)` (full-window invalidate so the parent-painted bands — tab strip, dock header, gutter — don't trail; skips no-op moves); `onPaint` reuses a **cached back-buffer** (no per-frame bitmap alloc); and the editor is **no-wrap** (`EM_SETTARGETDEVICE`) so width changes don't reflow. (True GPU-smoothness still wants the planned Direct2D editor — Sentinel-IDE is GDI + RichEdit; SQLTerminal uses Direct2D.)

16. **`.sentinel` file icon + icon polish.** `.sentinel` nodes in the explorer tree now use a custom icon (a page + blue **S** + padlock — `packaging/file.ico`, resource 101, loaded into the tree image list for `IMG_FILE`; falls back to the shell icon). `convert-icon.ps1` is parameterized (`-Src/-Dst`) and **letterboxes** to preserve aspect (S2 is 554×657). The app `.ico` is verified-correct at all sizes (256/48/32/16 PNG frames); stale large/extra-large icons in Explorer are the **Windows per-size icon cache** (`ie4uinit -ClearIconCache` + reboot to refresh), not the art.
17. **Editor: edit/save + line numbers + error highlight.** Edits set a **dirty** flag (`●` in the tab + a **toolbar Save button** that lights coral when dirty); **Save** (`Ctrl+S` / `≡ ▸ Save` / toolbar) writes UTF-8+CRLF; **Build auto-saves** the open file first. (Save bug fixed: the read buffer was sized by `GTL_NUMCHARS` but `GT_USECRLF` expands each `\r`→`\r\n`, so output overflowed and truncated — now sized `n*2+16`.) A **line-number gutter** toggles via `≡ ▸ Line Numbers` / `Ctrl+L` (persisted in settings; painted from `EM_GETFIRSTVISIBLELINE`/`EM_POSFROMCHAR`, repainted on `ENM_SCROLL`). **Error lines** from a build are tinted (`CFM_BACKCOLOR`) in the open file and cleared on edit. A real **accelerator table** now backs the menu shortcuts (Ctrl+S/N/O/Shift+N/Shift+B, F5, Ctrl+L, Ctrl+,). Gotcha: `EM_SETCHARFORMAT` raises `EN_CHANGE`, so highlight/error-tint ops set `g.highlighting` to avoid a spurious dirty flag. Also fixed the Problems **File** column (it rendered garbage glyphs): `ListView_SetItemText` is a macro that assigns `pszText` then `SendMessage`s in *separate* statements, so a temporary `baseName(d.file).c_str()` dangled before the send — now held in a named local.

18. **Undo / redo memory.** RichEdit has native multi-level undo, but the syntax highlighter was **polluting** it — `highlight()`/`markErrorLines()`/`clearErrorMarks()`/the base `styleEditor()` reformat call `EM_SETCHARFORMAT`, and each format landed on the undo stack, so `Ctrl+Z` undid a *color change* not the edit. Fix: a cached `ITextDocument` (TOM) fetched lazily via `EM_GETOLEINTERFACE` → `QueryInterface(__uuidof(ITextDocument))` (`#include <tom.h>`/`<richole.h>`; released in `WM_DESTROY`); `suspendUndo()`/`resumeUndo()` wrap each programmatic-format site with `ITextDocument::Undo(tomSuspend/tomResume)` so only real text edits are recorded. **Ctrl+Z → `EM_UNDO`**, **Ctrl+Y → `EM_REDO`** are wired in the accelerator table + the `≡` menu (Undo/Redo items, grayed via `EM_CANUNDO`/`EM_CANREDO`). Undo/redo go through `WM_COMMAND ID_UNDO`/`ID_REDO` → editor, then `SetFocus`. `EM_UNDO`/`EM_REDO` raise `EN_CHANGE` → `onEditChanged` re-highlights (undo-suspended) and marks **dirty** — so any undo/redo leaves `●` set (RichEdit doesn't track the saved point; accepted as simplest). Verified by driving `WM_CHAR`+`WM_COMMAND` against the live window: type 3 chars (429), one undo reverts *text* (428, not a no-op color undo), redo restores (429). **Toolbar Undo/Redo buttons** (`↶`/`↷` glyphs, `rUndo`/`rRedo`, between Save and the scheme selector) mirror the menu/accelerator paths; they gray out when `EM_CANUNDO`/`EM_CANREDO` is false and repaint only when availability changes (`refreshUndoButtons` — any invalidate forces a full-window repaint, so it skips the steady-state no-op; called from `onEditChanged`, which all edits/undo/redo reach via `EN_CHANGE`). Screenshot-verified: grayed at open → `↶` lights on first edit → `↷` lights after an undo.

19. **Dark popup menus + tree context menu.** Popup/context menus were classic light Win32 (clashing with the dark shell). `Theme.h::applyMenuDarkMode()` calls the undocumented uxtheme ordinals (135 `SetPreferredAppMode` → ForceDark/ForceLight per `currentTheme().dark`, 136 `FlushMenuThemes`) — the same technique SQLTerminal uses; guarded, so menus just stay light if the exports are missing. Called at startup and in `applyTheme` (theme changes). A **right-click context menu** on the explorer tree (`WM_CONTEXTMENU` → `showTreeMenu`, also keyboard Shift+F10) offers **New File…** (the headline action) + New/Open Project, routing to the existing command IDs. Screenshot-verified dark.

20. **Clickable `file:line:col` in Output.** As build output streams, the `path.sentinel:line:col` token in each diagnostic is marked `CFE_LINK` (`outAppend` now returns the insertion char-pos — CRLF-safe via `EM_EXGETSEL` clamp-to-end — and `outLinkify` selects `[base+ts,base+te)` from `parseDiag`'s reported span). `ENM_LINK` is enabled on the Output pane; `EN_LINK` (WM_LBUTTONUP) extracts the link text via `EM_GETTEXTRANGE`, re-parses it, and calls the same `gotoLineCol` the Problems list uses. Screenshot-verified: link renders underlined; clicking jumped the editor to Ln 3.

21. **Lines-of-code dogfood — the first piece of Sentinel-IDE written *in* Sentinel.** `tools/loc.sentinel` reads `loc_corpus.txt` (whole-file `read_file`), counts newlines, and `write_file`s the total to `loc_total.txt` — Sentinel has no argv/stdin/dir-walk (ADR 0035), so the native host hands it a fixed corpus and reads back the result (the thin-host / Sentinel-core split). `scripts/loc.ps1` (run by `build.bat` under the vcvars env) enumerates the IDE's own source by language (C++ / Sentinel / Build), writes the corpus, compiles+runs `loc.sentinel` via `snc` for the **Sentinel-verified grand total** (falls back to its own count if snc is absent — never blocks a build), and emits `build/generated/Loc.h` (`SENTINEL_LOC_*`). The **About box** renders them as shields.io-style flat badge pills (`drawBadge`: rounded clip region, dark label half + colored value half) — **C++** (blue), **Sentinel** (coral — the new one), **Build** (gray), **Total** (green) — under the caption "Lines of code · total counted by loc.sentinel". Snapshot at build 8: C++ 3307 · Sentinel 67 · Build 354 · Total 3728. (Gotcha learned: Sentinel has **no `%` operator** — use `v - (v/10)*10`; and `if` is an *expression*, so a bare `if cond { stmt; }` is a parse error — use `x + (if cond {1} else {0})`.)

22. **Per-target editing in Project Settings.** A **TARGETS** section (shown only when the manifest declares real `[[target]]` blocks — `SentinelProject::explicitTargets`) adds a target selector combo + editable **Name / Entry / Type** for the selected target; switching targets (`CBN_SELCHANGE`) commits the current one and loads the next, and Save commits the visible one. `saveProject` gained a **non-destructive `[[target]]` writer**: it rewrites existing `name/entry/type/links` values in place (matched by block order; the `[[` header parses to section name `"[target"`), never inserting — so comments and unmodeled per-target keys survive. Round-trip verified: renaming target 1 to `cryptoZ` and saving changed only that block; target 2 (`hello`) and all comments were untouched.

23. **Signing follow-on — sign the built artifact.** On a **successful** project build, when `signing.sign = true` and a `sentinel.key` exists in the project, the IDE runs `snc sign <artifact> --key sentinel.key` (the same key convention as the Signing panel) and reports `[signed · <name>.sig]` in Output — making the project's "Sign the built artifact" checkbox real. (Build-gating on `require = warn/strict` was already wired in `composeBuild` via `--require-signatures`/`--trust`; the stale "snc C1.0b doesn't accept it" note in the Project Settings form is corrected.) Verified end-to-end: keygen → sign=true → build `examples` → `crypto.exe.sig` produced + `[signed]` in Output.

24. **Recent Projects + Close Project.** `Settings` gained a `recents` list (most-recent-first, capped at `kMaxRecents`=10, persisted under `[recents]` in `settings.ini`; `addRecent` de-dupes case-insensitively and promotes to front). `openFolderPath` records a recent whenever a real project loads (so New/Open/CLI/recent all feed it). The `≡` menu gains a **Recent Projects ▸** submenu (`buildRecentsMenu` — `name⟶parent` per entry, `&`-escaped, then **Clear Recent Projects**; items are `ID_RECENT_BASE+i`); a missing folder is dropped on click. **Close Project** (in the `≡` menu + tree context menu, grayed when nothing's open) returns to the welcome state: auto-saves a dirty file first (like Build), then clears the project/file/tree/problems/output, hides the tree+editor, and resets the title/chip. Verified: opening two projects ordered the persisted recents correctly; opening recent #1 reopened the right project + its entry file; Close reset to the welcome screen. (Also fixed a pre-existing cosmetic bug — "Signing & Trust…" rendered as "Signing _Trust…" because the literal `&` was a menu mnemonic; now `&&`.)

25. **Sealed projects — encrypt so only the developer can open.** `src/core/Seal.h` (header-only, native CNG + Windows Compression API) seals a project: **archive folder → LZMS-compress → AES-256-GCM encrypt under a random master key (DEK)**. The DEK is wrapped per **unlock slot** (LUKS-style) — v1 = one **password** slot: PBKDF2-HMAC-SHA256(password, 16-B salt, 600k iters) → KEK → AES-256-GCM key-wrap of the DEK. More unlock methods (key file, Ed25519/smartcard, TPM) become new slot types that wrap the *same* DEK — no re-encryption, and a project can carry several at once. The `.sealed` format (magic `SNTSEAL1`, version, AEAD-alg id, archive size, slots, payload nonce/ct/tag) records algorithm ids so a future **ChaCha20-Poly1305** slot/payload coexists with AES files. Extraction sanitizes paths (rejects `..`/absolute); GCM auth detects tampering; archiving skips `target`/`build`/`.git`/`node_modules` and any `.sealed`. UI: **≡ ▸ Seal Project…** (themed `PasswordDialog`, double-entry) → writes `<parent>\<name>.sealed` (non-destructive — the plaintext is left in place), reports in Output. **≡ ▸ Open Sealed Project…** → file picker → password → decrypts to a sibling `<name>-unsealed\` and opens it; wrong password / corruption → a clear message. **This is the headline Sentinel-rewrite target:** the AEAD + KDF core maps onto `std/security` (machine-verified constant-time ChaCha20-Poly1305 + SHA-256); the native host keeps archive/compress/dir-walk (~~Sentinel has no dir traversal~~ — **this was wrong; re-measured 2026-07-19, dir traversal works through `extern "C"` and a full recursive archiver has been written in 100% Sentinel. See `docs/Sentinel-lang_request.md` R9**). **Verified** end-to-end: a standalone harness round-trips (seal→unseal byte-identical, wrong-password rejected, 1-bit tamper caught by GCM); and the IDE's own seal of `examples` (7 files · 3906 B → 2134 compressed → 2286 sealed) unseals to byte-identical sources. (A test-harness gotcha, not in the engine: building a key from two separate `"literal"` expressions spans different string objects when pooling is off → use one named buffer.)

26. **File associations — double-click `.sntproject` / `.sentinel` → opens the IDE.** `src/core/FileAssoc.h` registers per-user associations under `HKCU\Software\Classes` (no admin, effective immediately via `SHChangeNotify`): ProgIDs `SentinelIDE.Project` / `SentinelIDE.Source` with `shell\open\command = "<thisexe>" "%1"` and `DefaultIcon = "<exe>",-100/-101` (the app + `.sentinel` file icons, by negative resource id). **≡ ▸ Register File Associations…** writes them and confirms (with a note that an existing per-extension *UserChoice* would still win — by design). The exe already accepts a path on argv; `openPathArg` was improved so a double-clicked file opens its **nearest enclosing project** (walks up to the first folder with a manifest), and a manifest opens the project landing in its entry source rather than the raw file. Verified: registry keys point at `build\Sentinel-IDE.exe`; a pure shell-association launch (`Start-Process crypto.sentinel`, no exe path) opened the IDE with the crypto-lib project + `crypto.sentinel`. (Follow-up: single-instance/IPC so a double-click reuses an open window instead of spawning a new one; today each opens its own.)

27. **Rename to "Sentinel-IDE" + git/repo prep.** Display name → **Sentinel-IDE** (titlebar/`kAppName`, About box + caption, `.rc` ProductName/FileDescription/InternalName/OriginalFilename, `app.manifest` identity) and the **exe → `Sentinel-IDE.exe`** via CMake `OUTPUT_NAME` (the CMake target id, window class `SentinelIDEMainWindow`, `sentinelide` namespace, `%LOCALAPPDATA%\SentinelIDE` settings dir, and the `G:\SentinelIDE` folder stay "SentinelIDE" — so saved settings + the `-Class` capture path keep working). `launch.ps1`/`capture.ps1` now match the **process name** `Sentinel-IDE`; the file associations were re-registered to the new exe. **Repo prepped (to stay private initially):** added `LICENSE` (verbatim GPL-3.0, from SQLTerminal-Win32), a public `README.md`, `.gitignore` (excludes `build/`, `target/`, `*.o`/`*.obj`/`*.exe`/`*.pdb`/`*.lib`, `*.sealed`/`*.key`, `.claude/`, `_bmad/`), and `.gitattributes` (`* text=auto eol=crlf` so the signed demo keeps CRLF; `*.sig`/`*.ico`/`*.png` `binary` so the committed signature isn't mangled). `git init -b main` → clean initial commit **`e5f8386`, 85 files** (source/docs/examples/packaging/scripts/art/tools + the `_bmad-output` design docs; **no build artifacts, secrets, or tooling**). Gotchas handled: the repo sits on a network share (git needed `safe.directory`), and a first attempt committed `examples/target/**` build artifacts — history was rebuilt clean before any push. **Pushed 2026-07-19** to the private repo **`arcanii/Sentinel-IDE`** via `gh repo create Sentinel-IDE --private --source=. --remote=origin --push` (`gh` 2.96.0 *is* installed now — the earlier "not installed" note is stale). 87 files, history verified free of key material and build artifacts. The local folder stays `G:\SentinelIDE`; only the GitHub repo + product/exe are "Sentinel-IDE".

28. **Windows installer (Inno Setup).** `packaging/Sentinel-IDE.iss` + `scripts/make-installer.bat` → a **per-user `setup.exe`** (no admin): the exe + `examples/` + README/LICENSE, a Start-Menu shortcut (optional desktop icon), the `.sntproject`/`.sentinel` associations declared in `[Registry]` under `HKA` (mirroring `FileAssoc.h`, icons by negative resource id `-100`/`-101`), and a full uninstall that reverses them. `ChangesAssociations=yes` refreshes the shell. **Built and verified**: Inno Setup 6 installed per-user via winget → `ISCC` compiled it → `build/installer/Sentinel-IDE-0.1.0-setup.exe` (~2.6 MB). `make-installer.bat` probes `Program Files (x86)`, then `%LOCALAPPDATA%\Programs\Inno Setup 6`, then bare `ISCC.exe`. ~~Caveat: the `.iss` hard-codes `AppVersion 0.1.0` (it does *not* pick up the build number), and `AppUrl` is still a placeholder.~~ **Both fixed in phase 33** — the version is now read from the built exe's FileVersion resource and `AppUrl` points at the repo. WiX/MSI or MSIX (Store) remain the heavier alternatives.

29. **Trust manifest wired to a real fingerprint — and a real schema bug fixed.** Investigating "put the real key in `sentinel-trust.toml`" uncovered that **the shipped schema was fiction**: snc's parser (`crates/sentinel-trust/src/trust_model.rs`, `#[serde(deny_unknown_fields)]`) accepts only `[[keys]]` tables with a **bare 64-hex `pubkey`** (plus optional `name`, `grants`). The old `[dependencies.<name>]` / `sig` / `policy` / `forbids` shape is a **hard TOML parse error that aborts the build in BOTH `warn` and `strict`** — and since `MainWindow.cpp` passes `--require-signatures --trust` whenever `[signing] require != "off"`, the example would have broken IDE-driven builds the moment anyone changed that setting. Worse, an `ed25519:` prefix *parses* but never matches, silently yielding `UNTRUSTED` (configured-looking, enforcing nothing). Fixed all three sides so the IDE and the compiler agree: `examples/sentinel-trust.toml` rewritten to `[[keys]]` with the demo's real key `58ad2d8c…`; `core/Signing.h` (`TrustDep`→**`TrustedKey`**, `deps`→`keys`, parses `[[keys]]`/`pubkey`/`name`/`grants`); and `SigningDialog`'s importer now **writes** that schema (bare hex, dedup by key) with the viewer's columns reduced to **Name · Trusted key · Grants (ceiling)** (`policy`/`forbids` don't exist in v1). **Verified end-to-end**: `snc build … --require-signatures strict --trust …` → `trust: 'crypto.sentinel' verified — key 58ad2d8cf5294de1…` (exit 0), a one-nibble-altered key → `UNTRUSTED … build refused` (exit 1), and the Signing panel now lists the trusted key next to the matching file signature. **Honest scope:** identity + byte-integrity are genuinely enforced; the `grants` ceiling is parsed and intersected for real but v1's capability extractor only ever detects `ffi` (from `extern` blocks), so `secret`/`constant_time`/`alloc` are recorded intent, not an enforced gate, and `forbids` is unimplemented.

30. **Signing capability split + trust gate re-armed.** Three live defects, found by auditing the
    repo against the toolchain rather than against its own docs. (a) `sncSupportsSigning` treated a
    `snc help` match as proof signing worked; `keygen`/`sign` actually shell out to
    `keygen_core.exe`/`sign_core.exe`, present only beside `target\debug\`. Since `findSnc` listed
    release first and the probe passed, **the Signing panel enabled Generate Key / Sign on a binary
    that could not do either**, failing at runtime. Replaced with `SncSigningCaps{verify, sign}`
    (`core/Signing.h`), where `sign` additionally requires the helpers on disk; `findSnc` now ranks
    by capability, not list order. Splitting the flag also avoids the inverse bug — `verify` works on
    a verify-only snc, so it stays enabled while only keygen/sign grey out. (b) `docs/sentinel-project.md`
    and `docs/prototype.md` still published the pre-phase-29 `[dependencies.…]`/`policy`/`forbids`
    schema — a build-breaking parse error for anyone who copied it; the docs are the **fourth**
    lockstep site the seed prompt didn't name. (c) `examples/sentinel.toml` had `require = "off"` on
    a rationale ("snc C1.0b does not accept those flags") that phase 29 had already falsified, so
    **no ordinary build ever exercised the trust manifest**. Now `warn` — verified that a signed
    target verifies, the unsigned `hello` target warns, and the old schema fails at exit 1 with no
    artifact, so an ordinary build regression-tests phase 29's fix. Also recorded: `snc build
    --lib`/`--shared` never invoke the trust gate, so Library/Shared targets enforce nothing
    regardless of `require` (upstream).

31. **Seal format v2 + the repo's first test.** The `.sealed` container was still unshipped (no
    `.sealed` file existed anywhere; the installer excludes them from shipped examples), so breaking
    it was free *exactly once* — taken now rather than after a user seals something. Four v1 defects:
    (a) the slot loop `for (i = 0; i < slots && !unlocked; i++)` exited on success **without
    advancing `pos`** past the remaining slots, so the payload read began inside the next slot body —
    latent only because v1 always wrote one slot, and it would have fired on the first multi-slot
    file, i.e. the moment the headline sealing follow-on landed; (b) unknown slot types **aborted**
    the unseal, contradicting the header's own promise that keyfile/Ed25519/TPM slots could be added
    freely — v2 adds `slot_len` so readers skip what they don't parse; (c) `archive_size` was
    unauthenticated yet fed straight to `sealDecompress` as the output-buffer size, so flipping 8
    bytes in a file you couldn't decrypt still steered a multi-GB allocation in the victim's process —
    v2 binds the 24-byte header prefix as GCM **AAD**; (d) `iters` — the one field neither
    authenticated nor self-checking — went straight into PBKDF2, so a crafted file could name 2^64
    iterations and hang the app behind a password prompt; now range-checked.
    **The AAD deliberately covers only the fixed prefix, not `slot_count` or the slot bodies** —
    authenticating the slot table would tie the payload tag to the current slot set, so adding an
    unlock method would force re-encrypting the payload and destroy the very LUKS property the format
    exists to have. Slots defend themselves (each wrapped DEK carries its own tag).
    Also fixed while reading: `sealExtractArchive` rejected any path *containing* `..`, killing the
    whole unseal over a legitimate `notes..txt`; and length bounds were `pos + n > size`, which can
    wrap since the lengths are u64 off disk. **v1 files are still read** — only the writer moved.
    `tests/seal_test.cpp` (the repo's **first test**, 25 assertions, all passing) has a case per
    defect; two of them splice an extra slot in *before* and *after* the password slot and confirm
    the payload still opens, which is what proves the no-re-encryption property is real.

32. **WinSparkle auto-update (Sparkle-style), modelled on `G:\RabbitEars`.** WinSparkle 0.9.3
    (vendored prebuilt x64, MIT — `third_party/winsparkle/`) reads an **Ed25519-signed appcast**
    at `raw.githubusercontent.com/arcanii/Sentinel-IDE/main/appcast.xml`; the Inno installer is the
    update artifact. `src/host/win32/Updater.{h,cpp}` keeps a three-function surface
    (`initUpdater(HWND)` / `checkForUpdates(HWND)` / `shutdownUpdater()`) so a macOS host could back
    the same names with Sparkle. Reachable from **≡ ▸ Check for Updates…** *and* a **Check for
    Updates…** button in the **About box** (beside the version it reports — the conventional home,
    and the dialog a user is in when they wonder about their version); `initUpdater` runs after the
    window exists (WinSparkle's shutdown request needs a HWND) and also starts the periodic
    background check. `shutdownUpdater()` is called **after `runApp`'s message loop, not in
    `WM_DESTROY`** — `win_sparkle_cleanup()` joins WinSparkle's worker threads and the
    shutdown-request callback runs *on* one of them, so tearing down from `WM_DESTROY` races it.
    **The repo being PUBLIC is now load-bearing** — WinSparkle fetches over *unauthenticated* HTTPS,
    so a private repo answers 404 and every check silently reports "no updates". Verified both ways:
    the raw URL 404'd while private, returns 200 now. Making it private again breaks updates with
    **no visible symptom**; see `docs/RELEASING.md`.
    **Ships inactive on purpose:** `kEdDsaPublicKey` is the placeholder `@@SENTINEL_IDE_…@@`, and
    while it is, the updater refuses to init, logs a warning, and the menu item is **hidden** (not
    greyed) — an updater that cannot verify a signature must not look like it works. Activate by
    generating a **dedicated** key pair with `winsparkle-tool.exe generate-key --file <path>` (from
    the WinSparkle 0.9.3 zip — 0.9.3 no longer ships the older `generate_keys.exe`/`sign_update.exe`)
    and pasting the public half. **The private key is a FILE**, not a credential-store entry — keep
    it outside the repo; `.gitignore`'s `*.key` is only a backstop. See `docs/RELEASING.md`.
    **Also fixed a real hang in all five modal dialogs** (not just the one hosting the menu item):
    each ran `while (!st.done && GetMessageW(...) > 0)`, and `GetMessageW` returns 0 for `WM_QUIT`
    **and consumes it** — so when WinSparkle posts `WM_CLOSE` to install, the nested loop ate the
    quit and `runApp`'s outer loop blocked forever: window gone, process alive, **exe locked, install
    fails**. The loops now re-post `WM_QUIT` (the actual fix); a 3-s force-exit watchdog in
    `Updater.cpp` is the backstop. RabbitEars hit this via its About box and shipped watchdog-only;
    it's worse here because *background* checks can request shutdown while **any** dialog is open.
    Verified end-to-end with a temporary key: init reported `0.1.0.25`, the menu item appeared, and a
    check performed a real HTTPS fetch and surfaced WinSparkle's error UI for the unpublished
    appcast. Temporary key reverted; absence re-confirmed by searching the tree **and the built exe**.
    (WinSparkle's own UI is native/light — it does not follow the dark theme. Cosmetic.)

33. **Installer version derived from the build.** `packaging/Sentinel-IDE.iss` hard-coded
    `AppVersion "0.1.0"`, so `Sentinel-IDE-0.1.0-setup.exe` was the filename for *every* build —
    two different binaries shipping under one name, which matters much more now that WinSparkle
    compares versions. It now reads the built exe's **FileVersion** via
    `GetVersionNumbersString(SourcePath + "\..\build\" + AppExe)` → `Sentinel-IDE-0.1.0.28-setup.exe`,
    with a `#if !FileExists` → `#error` guard so a forgotten build stops ISCC instead of yielding a
    mis-versioned installer. **It must be FileVersion, not ProductVersion:** `SentinelIDE.rc`
    hard-codes `PRODUCTVERSION 0,1,0,0`, so `GetFileProductVersion` would have silently pinned every
    installer to `0.1.0.0` — the same bug wearing a fix's clothes. Verified: exe `0.1.0.28` →
    filename `0.1.0.28` → setup's own ProductVersion `0.1.0.28`. `AppUrl` also now points at the repo.
    **Found while testing:** the installer was shipping `examples\crypto` and `examples\hello` — the
    *extensionless* PE binaries `snc` drops beside the source, which `Excludes: *.exe` misses for
    exactly the reason `.gitignore` missed them. Added to `Excludes` (−52 KB). `*.sig` stays
    deliberately **un**excluded, or the installed demo loses its ✓ Signed chip.
    ⚠ **Consequence to resolve:** the build number now names a shipped artifact but is not
    reproducible — see *Installer follow-ons*.

    **Also: the installer was putting an x64 app in `C:\Program Files (x86)`.** The `.iss` set
    neither architecture directive, and Inno's `Setup.exe` is itself a **32-bit process** — so
    without `ArchitecturesInstallIn64BitMode` it runs in 32-bit install mode and `{commonpf}`
    resolves through WOW64 to the x86 Program Files, whatever the payload's bitness. (Per-user
    installs were unaffected: `{autopf}` → `{localappdata}\Programs` either way, so this only
    showed after choosing "install for all users" in the `PrivilegesRequiredOverridesAllowed`
    dialog.) Now sets **`ArchitecturesAllowed=x64compatible`** (refuse where the exe can't run)
    and **`ArchitecturesInstallIn64BitMode=x64compatible`** (put Setup in 64-bit mode) — two
    directives doing different jobs; the second is the one that fixes the path. `x64compatible`
    rather than the deprecated `x64` so the build is still offered on ARM64 under emulation.
    Proved with a throwaway probe `.iss` that aborts in `InitializeSetup` after dumping the
    constants: without the directive `Is64BitInstallMode=0`, `{commonpf}=C:\Program Files (x86)`;
    with it `Is64BitInstallMode=1`, `{commonpf}=C:\Program Files`. Secondary benefit: 32-bit
    install mode also redirects `[Registry]` writes under HKLM into `Wow6432Node`, so a
    per-machine install's file associations were landing where 64-bit Explorer may not look.

34. **Build number derived from git.** `packaging/build_number.txt` incremented on every
    `build.bat` run — including failed ones, since the stamp preceded cmake — was tied to nothing
    reproducible, and dirtied a tracked file each build. Tolerable while it only fed an About-box
    badge; not once it names shipped installers *and* drives WinSparkle's version comparison, where
    it meant a released `Sentinel-IDE-0.1.0.<n>-setup.exe` could **never be rebuilt**.
    Now `git rev-list --count HEAD` + `BUILDBASE`: same commit → same version, verified by building
    twice and getting 126 both times. `build.bat` **fails** if git can't answer, rather than
    stamping a wrong version, and prints `BUILD_DIRTY <n>` when the tree doesn't match HEAD (the
    binary would otherwise claim a commit whose contents it lacks).
    **`BUILDBASE` (=100) is load-bearing:** the commit count was **26** against a counter already at
    **38**, so a raw switch would have moved the version *backwards* — the one versioning mistake
    WinSparkle cannot recover from, since it only ever offers a *higher* `sparkle:version`. Safe to
    renumber at all only because nothing had been released (no appcast, no GitHub releases —
    checked). `packaging/build_number.txt` deleted; references updated across README, `.iss`,
    RELEASING and this file.

35. **First product logic in Sentinel — the diagnostic parser.** The About box has long claimed
    *"Interpretation of untrusted bytes: Sentinel"* while the shipped binary held **zero** Sentinel
    code (`tools/loc.sentinel` is a build-time tool). Now true: `src/sentinel/diag.sentinel` parses
    `snc`'s `path.sentinel:line:col` diagnostics — genuinely untrusted bytes — and is compiled to a
    **C-ABI static lib** (`snc build --lib`, ADR 0059) linked into the exe. `build.bat` builds
    `build/generated/diag.lib` (release snc, under the vcvars env it already has); CMake links it +
    the **R8** Windows system libs (`legacy_stdio_definitions ntdll userenv ws2_32 dbghelp`) and
    defines `SENTINELIDE_SENTINEL_DIAG`, under which `MainWindow.cpp::parseDiag` calls the Sentinel
    export. The **C++ `parseDiag` stays as an `#else` fallback** for a snc-less build; if the lib is
    absent CMake prints `diagnostic parser = C++ fallback` and the app still builds. The two are held
    byte-identical by **`tests/diag_xcheck.cpp`** (11 cross-checked cases — the trailing-colon quirk,
    the `[` walk-back stop, first-of-multiple, a non-ASCII UTF-8 path). **Verified live:** an
    undefined-function build surfaced the diagnostic in Problems with the correct file/line and the
    editor line tinted, having passed through Sentinel. Design points: uses the **`-> [u8]` return
    ABI** (not `&mut [u8]`) to sidestep R3; the host converts UTF-8 byte offsets back to UTF-16 char
    offsets so link spans stay correct on non-ASCII paths; **calling** a Sentinel lib (vs merely
    linking an unused one) pulls in the runtime — the exe grew **~1.05 MB** (1.68→2.73 MB, Debug),
    the honest cost the R-doc's "+512 bytes" (unreferenced) did not show. **(Phase 36 renamed `diag.sentinel`→`parsers.sentinel` / `diag.lib`→`parsers.lib` and the gate `SENTINELIDE_SENTINEL_DIAG`→`SENTINELIDE_SENTINEL` when the trust parser joined the same lib.)** `loc.ps1` now counts
    `src/**.sentinel`, so the About box's **Sentinel LOC badge went 67 → 228**.
    **New build-time fact:** a normal `build.bat` now expects release `snc` at
    `G:\Sentinel-lang\target\release\snc.exe` to produce the lib (graceful C++ fallback if absent),
    so the default build genuinely depends on the Sentinel toolchain.

36. **Trust-manifest parser in Sentinel — first security-boundary port + a thesis meter.**
    `loadTrust` (the code deciding which Ed25519 keys the build trusts) now runs in Sentinel:
    `parse_trust` in `src/sentinel/parsers.sentinel`, a faithful port of `Signing.h::loadTrust`
    (`[[keys]]` tables, `name`/`pubkey` via projUnq, `grants` via parseInlineArr, block-closing
    tables, the loose `[[keystore]]` match quirk preserved). `tests/trust_xcheck.cpp` holds it
    byte-identical to the C++ oracle (12 cases). **Architecture change:** `diag.sentinel` +
    `trust.sentinel` were **merged into one lib** (`parsers.sentinel` → `parsers.lib`) because each
    Sentinel static lib bundles the whole runtime — two libs would collide on `sentinel_free_bytes`
    / the allocator. The payoff is the thesis working: the second parser grew the exe by **only
    ~8 KB** (the first paid the ~1 MB runtime cost; every further port is nearly free). The gate is
    now `SENTINELIDE_SENTINEL` (both parsers); `MainWindow.cpp::parseDiag` and `Signing.h::loadTrust`
    both call in, both keep C++ `#else` fallbacks. **Verified live:** the Signing & Trust panel lists
    the demo key (`58ad2d8c…`, grants secret/constant_time/alloc) parsed from `sentinel-trust.toml`
    by the Sentinel `loadTrust`.
    Also (per Bryan's request): the **About box now shows a "built in Sentinel" progress bar** — a
    coral fill + `X.X% written in Sentinel`, Sentinel's share of Sentinel+C++ LOC (build tooling
    excluded). It moves with every port: ~1.4% → 4.9% (diag) → **8.3%** (trust). Sentinel LOC badge
    is now **408**.

37. **`.sig` carrier parser in Sentinel — third port.** `Signing.h::readSig` now runs in Sentinel:
    `parse_sig` in `parsers.sentinel`, a faithful port of the flat `key: value` carrier parser (split
    on the first `:`, both halves projTrim'd, `algorithm`/`key`/`grants` last-wins). Byte-identical
    to the C++ oracle via `tests/sig_xcheck.cpp` (12 cases — value-contains-colon, colon-with-no-key,
    empty value, whitespace-only `present`, unknown keys ignored, non-ASCII, no trailing newline).
    **Verified live:** the Signing panel's FILE SIGNATURE section shows Key/Grants parsed from
    `crypto.sentinel.sig` by the Sentinel `readSig`. All **4** tests pass via ctest (seal, diag,
    trust, sig). The one-lib economics held again — **+~8 KB** (2,741,760 → 2,749,952). Now every
    parser on the signing/trust path is Sentinel; the About-box Sentinel bar moved **8.3% → 9.5%**
    (Sentinel LOC 408 → 479). The only file parser still in C++ is the manifest parser (`Project.h`),
    whose comment-preserving *writer* is the hard part and would stay native for now.

38. **Project manifest reader in Sentinel — fourth port; every file parser is now Sentinel.**
    `Project.h::loadProject` reads the manifest in Sentinel: `parse_manifest` in `parsers.sentinel`
    does **both** the flat `[section] key=value` fields (replacing the Win32 profile API,
    `projIni`/`projArr`) **and** the `[[target]]` array-of-tables (replacing `parseTargets`) in one
    pass. The subtlety: those two had **different** semantics, both reproduced — flat fields are
    **case-insensitive** (GetPrivateProfileString; empirically probed via a scratchpad harness) so
    `emit_scalar`/`emit_array` use `lit_ieq`; `[[target]]` field keys are **case-sensitive**
    (`parseTargets`) so `emit_targets` uses `lit_eq`. A `Name`-vs-`name` target test guards the split.
    Defaults / `typeFromName` / `tierFromName` / single-target fallback stay host-side; Sentinel
    returns raw found-flag + strings. `tests/manifest_xcheck.cpp` holds it byte-identical to the C++
    `loadProject` across **14** cases (it deliberately does *not* define `SENTINELIDE_SENTINEL`, so
    its `Project.h` is the C++ oracle, and links `parsers.lib` for `parse_manifest`). **Verified
    live:** opening `examples/` loads crypto-lib with both `[[target]]`s, the Experimental tier, and
    the entry file — all via the Sentinel reader. All **5** tests pass.
    **The comment-preserving WRITER (`saveProject`) stays C++** — a surgical, structure-preserving
    TOML rewrite is the genuinely hard part and out of scope. So every file *reader/parser* in the
    IDE is now Sentinel; the manifest *writer* is the one file-touching path still in C++.
    About-box figure **9.5% → 14.5%** (Sentinel LOC 479 → 784 — the biggest jump; parse_manifest is
    ~200 lines). Sentinel language notes learned here, worth keeping: a **by-value `[u8]` param is
    moved on first use inside a loop** — pass `&[u8]` for anything reused across iterations; and
    **`&"literal"` is rejected** ("cannot borrow a non-lvalue") — bind literals to a `let` first,
    then borrow.

39. **Unsaved-changes guard — the editor stops discarding work silently.** Three paths threw away
    the editor buffer with no prompt, and a fourth wrote to disk without being asked.
    (a) `openFile` overwrote the RichEdit contents unconditionally, so clicking another file in the
    tree (or a Problems entry, or a `file:line:col` link in Output) lost every unsaved edit;
    (b) **there was no `WM_CLOSE` handler at all** — closing the window discarded the buffer, and
    `≡ ▸ Exit` called `DestroyWindow` directly, bypassing even that; (c) **an auto-update install did
    the same, unattended** — WinSparkle's shutdown request posts `WM_CLOSE`, which fell straight
    through `DefWindowProc`; (d) Build, Close Project and Seal each *silently auto-saved*, which
    never lost data but wrote the user's file on commands that say nothing about saving.
    Now a themed three-answer modal (`src/host/win32/SaveChangesDialog.{h,cpp}`, window class
    `SentinelSaveDlg`, built from `PasswordDialog`'s shape — a MessageBox would be light-themed, cf.
    phase 15) asks **Save / Don't Save / Cancel**, and `confirmSaveIfDirty(hwnd, action)` is the one
    choke point: it returns false only on Cancel, and **every caller must then abort changing nothing**.
    `openFile` was split — `loadFileIntoEditor` is the raw loader, `openFile` = guard + load — so
    internal reloads (the project's entry source, the raw-manifest refresh after Project Settings)
    never prompt while every user-reachable path does. `openFolderPath`, `openFile` and
    `setActiveTarget` now return `bool`.
    Ordering matters in three places, each a bug if done the obvious way: `setActiveTarget` asks
    **before** assigning `g.target`, or a cancel would leave the selector on a target whose source
    never opened; New Project and Open Sealed ask **after** their picker but **before**
    `createNewProject` / the unseal writes, or a cancel would strand a scaffolded project or a
    decrypted copy on disk; and `openProjectSettings` asks when the dirty file *is* the manifest,
    because the form's Save rewrites that file from the model and the raw edits are gone the moment
    it writes. `TVN_SELCHANGEDW` fires **after** the tree selection has already moved, so a cancelled
    open would leave the tree highlighting a file that isn't in the editor — the handler puts it back
    via `itemOld.hItem`, guarded by `g.restoringTreeSel` against the re-entrant notification.
    **The updater is the subtle one.** `onShutdownRequest` posts `WM_CLOSE` and arms a 3-second
    `ExitProcess` watchdog, so prompting there would show a dialog nobody answers and then get killed
    *with the edits still only in the buffer* — worse than the old behaviour. `Updater` now exposes
    **`updaterShutdownPending()`** (an `std::atomic<bool>` set on the WinSparkle worker thread), and
    `WM_CLOSE` auto-saves without asking in that case only. `ID_EXIT` was rerouted to `WM_CLOSE` so
    there is a single exit path. **Build keeps its silent auto-save on purpose** — "save before build"
    is the conventional behaviour and the build is meaningless without it.
    **Verified live** by driving the running window: the prompt appears for close / close-project /
    target-switch / tree-click / seal with the right per-action wording; **Cancel** leaves the app
    open, the file still dirty, and the tree selection restored; **Don't Save** exits with the file
    byte-identical on disk; **Save** writes then exits; and Cancel at the Seal prompt aborts before
    the password dialog, writing no `.sealed`. All 5 tests still pass.
    `docs/screenshots/phase39-save-changes.png`.
    Like every other modal here the new dialog re-posts `WM_QUIT` in its loop (phase 32's rule — a
    nested `GetMessageW` that swallows the quit wedges `runApp` forever); it is now the **sixth**
    dialog that has to.
    **Four defects an adversarial review of the first cut found, all fixed before commit** — worth
    keeping because three of them are the same trap in different clothes: *a guard that runs twice
    is not idempotent*. (i) **"Don't Save" deliberately does NOT clear `g.dirty`** (the buffer really
    is still unsaved until something replaces it), so the early guards in `newProject` /
    `openSealedProject` did not stop `openFolderPath`'s own guard from re-prompting for the same file
    *after* the scaffold or the decrypt had already hit disk — and both callers dropped the `false`
    return, so cancelling the second prompt left a stranded project / plaintext copy while the status
    bar still read "Created project X" / "[unsealed]". Fixed structurally, not by clearing the flag:
    `openFolderPath` was split into **`loadFolderPath`** (raw) + `openFolderPath` (guard + load), the
    same shape as `loadFileIntoEditor`/`openFile`, so **every user command asks exactly once**.
    Clearing `g.dirty` on Discard would have been the tempting fix and is wrong — after Seal or a
    failed New Project the buffer is still modified, and it would have dropped the `●` that is the
    only thing standing between those edits and a later silent loss. (ii) `newFile` wrote the file
    and rebuilt the tree *before* prompting, then ignored the cancel and reported "Created …"; it now
    asks straight after the picker. (iii) The tree-selection restore was gated on
    `tv->itemOld.hItem`, which is **NULL after every `populateTree()`** (project open, Project
    Settings save, New File, sidebar tab switch) — so the very case it was written for, the first
    click in a fresh tree, was the one it skipped. Restoring to `NULL` (= no selection) is the
    correct prior state. (iv) The header static lacked **`SS_NOPREFIX`**, so a file called
    `notes & drafts.sentinel` was shown as `notes  drafts.sentinel` — the *same* `&`-as-mnemonic bug
    phase 24 hit in the ≡ menu. All four re-verified live.
    **Follow-up landed later:** `g.dirty` was a one-way latch, so undo-to-original still showed `●`
    and still prompted (phase 18's wart, which the guard made user-visible). It is now
    `g.dirty = (editorText() != g.savedText)`, with `g.savedText` snapshotted in `loadFileIntoEditor`
    and `saveFile`. Costs nothing measurable: `highlight()` already did the identical whole-buffer
    `EM_GETTEXTEX` on every keystroke and then re-colorized the lot, so the compare is lost in the
    noise — and `highlight()` now shares the same `editorText()` helper rather than duplicating it.
    The comparison is deliberately in the control's own representation (`GT_DEFAULT`, lone CR);
    `saveFile` re-fetches with `GT_USECRLF` for the on-disk form and the two are never compared.
    Verified: a genuinely modified buffer still prompts; undo back to the loaded text does not; after
    a save the saved point moves, so typing-then-undoing back to the *saved* text is clean too.
    (Harness note for next time: a cross-process `SendMessage(WM_COMMAND)` to one of these dialogs
    sets `done` but leaves its `GetMessageW` blocked — sent messages don't wake a queue wait. **Post**
    the message instead, or the dialog looks wedged when the code is fine.)

40. **Auto-update actually installs — it never had.** Every release v0.1.0–v0.1.4 offered updates,
    downloaded them, verified the Ed25519 signature, shut the app down, and then installed **nothing**.
    On next launch the user was on the old version and was offered the same update again, forever. It
    was invisible because **WinSparkle reports failures only through callbacks we had never
    registered**, so not one line was ever logged. The prior "verified end-to-end" claim was true only
    of the *offer* path, which is all anyone had ever tested.
    **Diagnosis (measured, not inferred).** Wiring `win_sparkle_set_error_callback` plus the lifecycle
    callbacks showed the error callback **never fires**. `win_sparkle_set_user_run_installer_callback`
    — which runs immediately before WinSparkle would execute the payload, and is handed its path — was
    the hook that cracked it: on the `check_update_with_ui()` (prompt-then-install) path it is called
    **~0.5 s after "update found"**, far too fast to have fetched 3.5 MB, with an **empty path**, and
    is never called again. WinSparkle's own launch therefore had nothing to launch. Ruled out along the
    way: **Mark-of-the-Web** (no `Zone.Identifier` on the payload), SmartScreen/Defender/AppLocker (no
    block events), and **our own shutdown logic** — WinSparkle's header states the installer is
    launched *before* the shutdown callback, so neither `shutdownUpdater()` nor the 3-second watchdog
    was implicated. The artifact was never at fault: the downloaded payload is **byte-identical
    (SHA-256)** to the published installer, and running it by hand from WinSparkle's own temp dir
    installs perfectly.
    **The fix needs both halves.** (a) We **run the installer ourselves** from
    `onUserRunInstaller`, returning 1 for "handled" — WinSparkle's broken execute step is out of the
    loop. *No security cost:* WinSparkle verifies the signature against the compiled-in public key
    **before** calling us and does not call us if it fails; an empty path returns 0 and logs an error
    rather than pretending. (b) `checkForUpdates` uses **`win_sparkle_check_update_with_ui_and_install()`**,
    because only that variant downloads first and passes a real path. It skips the "do you want to
    update?" prompt — acceptable, since the user got there by choosing Check for Updates.
    **Two traps found the hard way, both now in comments.** The install **scope must be explicit**:
    `Sentinel-IDE.iss` sets `PrivilegesRequiredOverridesAllowed=dialog`, so a bare `/SILENT` stops dead
    on Inno's *"Select Setup Install Mode"* dialog — a silent launch sitting on a modal question, i.e.
    a hang, which is exactly where the first cut of this fix stalled. We now pass `/CURRENTUSER` or
    `/ALLUSERS` based on whether the running exe lives under `%LOCALAPPDATA%`, which also stops an
    update installing a *second* copy into the other scope. And `SEE_MASK_NOASYNC` is required, because
    WinSparkle asks us to quit the instant the callback returns and the shell call would otherwise be
    abandoned as the process dies.
    **Verified end-to-end:** a `0.1.2.153` client with the cache cleared was offered `0.1.4.151` from
    the live feed, downloaded it, launched it per-user, and was running `0.1.4.151` **within ten
    seconds** — the first completed auto-update in the project's history. The published 0.1.5 artifact
    was then downloaded from GitHub, installed, and confirmed to launch and report *no update
    available* against the live feed.
    **The diagnostic callbacks stay in.** An updater that fails silently is indistinguishable from one
    that works, which is precisely how this survived four releases.
    ⚠ **Clients on v0.1.4 or older cannot auto-install 0.1.5** — the broken updater is the thing being
    fixed. They need one manual install.
    ⚠ **Only the MANUAL check installs. The automatic/background check is STILL BROKEN** — verified
    afterwards, and the reason this entry exists in two halves. `win_sparkle_init()` also starts a
    periodic check, and that check raises WinSparkle's *own* prompt, i.e. the same
    `check_update_with_ui()` flow whose payload path is empty. Measured: with `CheckForUpdates=1` and
    `LastCheckTime=0` forced in `HKCU\Software\Sentinel\Sentinel-IDE\WinSparkle`, a 0.1.2.153 client
    raised the update prompt unprompted after 7.8 s, and clicking Install logged
    `payload ready — []  exists=NO` and installed nothing. `checkForUpdates()` is the only entry point
    we control, so the fix reaches the menu item and the About-box button and nothing else.
    **Do not claim auto-update "just works"** — it worked only when the user asked for it, until
    phase 41 replaced the background path too.

41. **Background auto-update, on our own timer.** Phase 40 fixed only the manual check, because
    `checkForUpdates()` is the sole entry point we control; WinSparkle's periodic check raised its own
    prompt and installed nothing. Now `initUpdater` calls
    **`win_sparkle_set_automatic_check_for_updates(0)`** — silencing that prompt entirely, so there is
    never a second, dud offer — and starts a detached thread that sleeps 90 s, GETs the appcast over
    WinINet, pulls `sparkle:version` out of it, and compares against `SENTINEL_FILEVERSION_STR`. If it
    is newer the thread posts **`WM_APP_UPDATE_AVAILABLE`** and returns (one offer per run). The host
    shows a themed `UpdateDialog` (**Skip this version · Install now · Later**), and accepting calls
    `checkForUpdates()` — the entry point that works.
    **Only the decision to OFFER is ours.** The download, the Ed25519 verification against the
    compiled-in key, and the install all remain WinSparkle's, so a tampered feed can at worst provoke
    an offer WinSparkle then refuses to install. We deliberately did not reimplement any of that.
    **Three defects an adversarial review caught in the first cut, all real, all fixed:**
    (i) the offer is posted **asynchronously**, and every modal in this app pumps with a
    `GetMessageW(&msg, nullptr, 0, 0)` — a null filter — so the message was dispatched *into their
    nested loop* and the offer opened **on top of** e.g. the unsaved-changes prompt; worse, closing it
    ran an unconditional `EnableWindow(owner, TRUE)`, re-enabling the main window with that prompt
    still pending, which is a route back to discarding edits. The handler now refuses to open while
    `!IsWindowEnabled(hwnd)`, parks the version in `g.pendingUpdate` and retries on a 4 s timer; and
    the dialog restores the owner's *prior* enabled state rather than TRUE.
    (ii) `Install now` was the focused `BS_DEFPUSHBUTTON`, copied from `SaveChangesDialog` — fine
    there because the user asked for that dialog, wrong here because this one arrives uninvited while
    they are typing, so a stray Enter would install and quit the IDE mid-sentence. **`Later` is now
    the default and holds focus**, and `IDOK` is ignored for 700 ms after the dialog appears.
    (iii) a declined version was re-offered on every launch. **Skip this version** persists to
    `settings.ini` (`[update] skip_version`) and is checked before offering.
    **Verified live, each point separately:** the offer appears unattended at ~91 s and no WinSparkle
    prompt appears alongside it; accepting took a 0.1.5.162 client to 0.1.6.160 in under 10 s; with a
    Settings modal open the offer correctly did **not** appear, then arrived 3.3 s after that modal
    closed; Skip wrote `skip_version=0.1.6.160` and a relaunch was not re-offered; and an
    already-current client saw no prompt at all in 125 s.
    **Known and deliberate:** the poll runs 90 s after *every* launch (a ~1 KB GET) rather than
    persisting a daily last-check, and `Later` means "ask again next launch" — Skip is the durable no.

See `docs/prototype.md` and `docs/sentinel-project.md` for detail; `docs/RELEASING.md` for the
release + update-signing procedure.

**Screenshot coverage is partial, despite "screenshot-verified" above:** `docs/screenshots/` holds
14 PNGs covering phases 1–11, 13, 15 and 39. Phases 12, 14 and 16–38 were verified live during their
sessions but no image was committed — treat their screenshots as absent, not lost.

## Releases

Public releases on GitHub (`arcanii/Sentinel-IDE/releases`), each an EdDSA-signed Inno installer that
WinSparkle auto-updates to. Every release is **built from a clean tree and tagged at the build
commit**, so `git checkout <tag> && scripts\build.bat` reproduces that build number exactly.

| Tag | Build | Installer | Carries |
|---|---|---|---|
| `v0.1.0` | 127 | `Sentinel-IDE-0.1.0.127-setup.exe` | First release. The whole phase 1–32 IDE + live auto-update. **No Sentinel in the binary yet** (C++ parsers). |
| `v0.1.1` | 135 | `Sentinel-IDE-0.1.1.135-setup.exe` | **First release with Sentinel in the binary** — the diagnostic + trust-manifest parsers run in Sentinel; About-box "built in Sentinel" progress bar; installer x64/`Program Files` fix; git-derived build number. |
| `v0.1.2` | 140 | `Sentinel-IDE-0.1.2.140-setup.exe` | Patch. The `.sig`-carrier parser (`readSig`) now runs in Sentinel too — every signing/trust-path parser is Sentinel. About figure 8.3% → 9.5%. Behavior-identical to 0.1.1 otherwise. |
| `v0.1.3` | 145 | `Sentinel-IDE-0.1.3.145-setup.exe` | Patch. The project-manifest reader (`loadProject`) now runs in Sentinel — **every file reader/parser in the IDE is Sentinel**; only the manifest writer (`saveProject`) remains C++. About figure 9.5% → 14.5%. |
| `v0.1.4` | 151 | `Sentinel-IDE-0.1.4.151-setup.exe` | Patch. The **unsaved-changes guard** (phase 39), and — found by a pre-flight audit of this very release — **the first build that runs on a machine without Visual Studio**: v0.1.0–v0.1.3 all shipped a Debug `/MDd` binary importing the non-redistributable debug CRT. Now Release + static CRT; exe 2.65 → 0.78 MB. |
| `v0.1.5` | 154 | `Sentinel-IDE-0.1.5.154-setup.exe` | Patch. **The first release whose auto-update actually installs** — v0.1.0–v0.1.4 offered, downloaded and verified updates, then silently installed nothing (phase 40). The app now runs the verified payload itself, and the updater logs what it does. **Clients ≤0.1.4 must install this one by hand.** |
| `v0.1.6` | 160 | `Sentinel-IDE-0.1.6.160-setup.exe` | Patch. Saved-point dirty tracking: undoing back to the loaded or last-saved text now clears the `●` and the unsaved-changes prompt, instead of latching on at the first keystroke. **First release verified by a real released client auto-updating to it** — a shipped 0.1.5 install went to 0.1.6.160 in under 10 s via ≡ ▸ Check for Updates…. |
| `v0.1.7` | 164 | `Sentinel-IDE-0.1.7.164-setup.exe` | Patch. **Automatic update checking works** (phase 41) — WinSparkle's periodic check is disabled and our own timer polls the appcast and offers via a themed Skip/Install/Later dialog. Verified against the live feed both ways: a published 0.1.6 upgraded via the manual check, and a probe carrying this code raised the background offer unattended at 92 s and installed. **0.1.6 and earlier need one manual check to reach it.** |

**Every release before 0.1.4 was dead on arrival for anyone without Visual Studio.** `scripts/build.bat`
is the only build script and it configured `-DCMAKE_BUILD_TYPE=Debug`, so v0.1.0–v0.1.3 shipped a
`/MDd` binary importing `MSVCP140D.dll` / `VCRUNTIME140D.dll` / `VCRUNTIME140_1D.dll` / `ucrtbased.dll`
— DLLs that ship **only with Visual Studio** and are **not licensed for redistribution**, against an
installer that bundles no CRT. Install succeeded; launch failed with *"VCRUNTIME140D.dll was not
found"*. It went unnoticed for four releases because every machine that ever ran it had VS installed.
Fixed in 0.1.4 with `Release` + `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (static — the installer is
per-user and needs no admin, so bundling the VC redist would have meant elevation or a merge module).
`dumpbin /DEPENDENTS` is the check, and it is now worth running on any release candidate. The
Sentinel `parsers.lib` links fine under `/MT /O2` (`SENTINEL_PARSERS_OK`), and `seal_test`'s 25
assertions over the real AEAD/KDF path pass under the optimizer.

To cut the next one: `docs/RELEASING.md` (bump `MKT`/`MKTRC` in `build.bat` if the marketing version
moves → clean commit → `make-installer.bat` → `sign-release.ps1 -Appcast` → tag the build commit →
`gh release create` with the installer → commit+push `appcast.xml` → verify the feed + enclosure
resolve). **Verified for v0.1.1:** signature validates against the compiled-in key, the feed serves
`0.1.1.135`, the enclosure resolves, and a live `0.1.0.126` client was offered the update across the
marketing bump. **Known cosmetic:** WinSparkle's release-notes panel renders the GitHub page with
full site chrome — a standalone HTML notes file per release would fix it (unbuilt).

## Key decisions

- **Identity:** dark-primary Claude-desktop coral, OS light/dark follow, sourced verbatim from
  SQLTerminal `Theme.h`. `Theme.h` in this repo is that palette in code.
- **Editor:** RichEdit for now; the Direct2D/DirectWrite GPU editor (as in the reference) is
  the eventual native-perf target. Editor font = Cascadia Code, user-overridable in Settings.
- **Signing = Sentinel-native ADR-0061** (Ed25519 keys via `snc keygen`/`sign`/`verify`,
  `sentinel-trust.toml` declaring trusted **`[[keys]]`** with a bare 64-hex `pubkey` and an optional
  `grants` ceiling, gated by `--require-signatures`). This **supersedes the earlier Authenticode
  framing**. Note there is no `policy` and no `forbids` in v1 — both are parse errors, not features.
- **Build tiers** (not debug/release): D/E/S/H per `TIERED_RELEASES.md`.
- **Project model:** `sentinel.toml` (executable | library | later shared/dll; `lib_paths`/`links`; `default_tier`; `[signing]`).
- **Accessibility:** backlogged; v1 relies on native Win32/Common-Controls defaults.

## Known toolchain gaps (the IDE is the forcing function — PRD FR-16)

- **build→link — RESOLVED via MSVC-env injection (phase 13).** The "`link.exe` not found" / link
  failures came from snc shelling out to the MSVC linker without a Developer-Command-Prompt
  environment. The IDE now auto-detects `vcvars64.bat` (Settings → MSVC env, else `findVcvars`/
  vswhere) and **injects that environment into the build process** (`captureMsvcEnv` →
  `CreateProcess` env block). **Verified:** `examples` builds at **exit 0**, producing a working
  `target/experimental/crypto.exe` that runs (exit 42) — so **Run works now too**. If no MSVC env
  is found, Build warns and still fails at link — set it in Settings → MSVC env.
- **Signing — RESOLVED, but the capability is SPLIT (re-measured 2026-07-19).** ADR-0061 is
  v1-implemented and *both* snc builds now carry the subcommands and accept
  `--require-signatures`/`--trust` — the old "release is a signing-less C1.0b" note is **obsolete**.
  They are still not interchangeable: `verify` is implemented *inside* snc, but `keygen`/`sign`
  **shell out to `keygen_core.exe` / `sign_core.exe`** (themselves Sentinel programs) which exist
  only beside `target\debug\`:

  | | `verify` | `keygen` | `sign` |
  |---|---|---|---|
  | `target\release\` | works | **fails** — ``signing tool `keygen_core` not found`` | **fails** |
  | `target\debug\` | works | works | works |

  So **`snc help` is not evidence that signing works** — that mistake shipped a Signing panel whose
  Generate Key / Sign errored at runtime (fixed in phase 30). `core/Signing.h::sncSigningCaps`
  reports `{verify, sign}` separately and checks for the helpers on disk; `findSnc` ranks by
  capability rather than list order and lands on the debug build.
- **`snc build --lib`/`--shared` skip the trust gate entirely.** `run_trust_gate` is called only
  from snc's *executable* build path; the library path discards `--require-signatures`/`--trust`
  without an error. **Library and Shared targets display "signing: strict" in the IDE and enforce
  nothing.** Upstream (Sentinel-lang), not fixable here.
- **No tier/opt flag:** `snc` always builds `-O0` (TIERED_RELEASES is post-1.0). Tiers only set
  the output dir today.

## UX spines (BMad) — status: DRAFT

`_bmad-output/planning-artifacts/ux-designs/ux-SentinelIDE-2026-06-27/`
- `DESIGN.md` + `EXPERIENCE.md` (cross-reference DESIGN tokens by `{path}`), `mockups/`,
  `.memlog.md` (canonical decision log — append via `_bmad/scripts/memlog.py`).
- **Pending:** Bryan's sign-off on `[ASSUMPTION]`s (diag palette, shapes/spacing, keybindings),
  then finalize; **reconcile to ADR-0061 signing + the project/tier model** (the spines + PRD
  still assume Authenticode and debug/release).
- **PRD:** `prds/prd-SentinelIDE-2026-06-27/` — has signing FR-19..21 + UJ-5 (Authenticode-framed)
  and an adversarial review with open HIGH findings (timestamping, UJ-5 climax overclaim,
  sign-confirmation spec). PRD reconciliation was **parked** by Bryan.

## What's next (open options)

- ~~Undo / redo memory~~ (phase 18), ~~dark popup menus + right-click New File~~ (phase 19),
  ~~clickable `file:line` in Output~~ (phase 20), ~~LOC dogfood + About badges~~ (phase 21),
  ~~per-target editing~~ (phase 22), ~~sign the built artifact~~ (phase 23), ~~recents + close project~~
  (phase 24), ~~sealed projects (password)~~ (phase 25), ~~file associations~~ (phase 26),
  ~~unsaved-changes guard~~ (phase 39) — all **done**.
- **Single-instance / IPC:** a double-click (or a 2nd launch) currently spawns a new window; route the
  path to an existing instance (named pipe / `WM_COPYDATA` to a `FindWindow` of the app class) so the
  open project gains a file/tab instead. Also: drag-drop files onto the window; a shell "New ▸ Sentinel
  Project" entry. **Unblocked now** — both swap the open file, and phase 39 landed the dirty-guard
  they needed (`confirmSaveIfDirty`; route any new open through `openFile`, not `loadFileIntoEditor`).
- **Unsaved-changes follow-ons** (guard shipped phase 39): ~~undo-to-original still leaves `●` set~~
  **fixed** — `g.dirty` is now a comparison against `g.savedText` (a snapshot taken at load and at
  save) rather than a one-way latch, so undoing back to the loaded *or* saved text clears the dot and
  the prompt. Still open: there is no "reload from disk" prompt when a file changes underneath the
  editor.
- **Installer follow-ons** (installer shipped phase 28; ~~build-number pickup~~ + ~~real `AppUrl`~~
  done phase 33; ~~non-reproducible build number~~ done phase 34 — now `git rev-list --count HEAD`
  + `BUILDBASE`, so a released `Sentinel-IDE-0.1.0.<n>-setup.exe` can be rebuilt from its tag).
  Still open:
  code-signing the `setup.exe` (needs a cert — see `docs/RELEASING.md`); WiX/MSI or MSIX (Store)
  if enterprise/Store distribution is ever needed.
- **Trust/signing follow-ons** (phase 29 wired the manifest): v1 only ever detects the `ffi`
  capability, so `grants` ceilings are recorded intent rather than an enforced gate and `forbids` is
  unimplemented — revisit when snc's capability extractor grows. Also still open: surfacing
  capability-bound verify failures as Problems, and a Settings field for a default signing key.
- **Cross-platform = one repo, layered (decided direction).** Keep a single repo (the IDE is one
  product); split into a **portable core** (project model, manifest/format parsers, the `.sealed`
  format + crypto, signing/trust, the snc driver) and a **per-platform native host** (`src/host/win32/` = the
  Win32 host today; macOS/Linux hosts would be Cocoa / GTK-or-Qt rewrites — *not* a shared GUI). The
  reuse layer is **Sentinel itself**: the core's logic (esp. the sealing AEAD+KDF, which `std/security`
  already provides cross-platform + constant-time) becomes a Sentinel C-ABI lib each host links — the
  project's own thesis. **Do NOT scaffold empty `macos/`/`linux/` trees yet** (dead weight); add a host
  when a port actually starts. Today the `src/core/*.h` "core" is still Win32-coupled (wchar_t, BCrypt,
  Compression API, profile API) — step 1 of any port is pulling that logic behind a portable seam.
- **Sealing follow-ons:** more **unlock slots** (key file, Ed25519/smartcard, TPM — each wraps the
  same DEK, no re-encryption; **format v2 / phase 31 makes this actually work** — slots are
  skip-by-length and the AAD excludes the slot table, both covered by `tests/seal_test.cpp`);
  a "**remove plaintext after sealing**" option (with confirmation; today seal is non-destructive —
  and note this would be the app's only irreversible operation, so gate it behind a verify-after-seal
  round-trip); **re-seal in place** / "lock" of an unsealed working copy; show the `.sealed` in the
  tree and unseal on click (careful: `sealCurrentProject` writes to the project's **parent** dir,
  and `TVN_SELCHANGEDW` would happily load a `.sealed` as UTF-8 text into RichEdit); upgrade the KDF
  to **Argon2id** — **blocked**, no Argon2/BLAKE2 anywhere in CNG or Sentinel's stdlib (verified
  twice; note `Sentinel-lang/docs/SENTINEL_DESIGN2.md:388` *claims* the stdlib exposes Argon2id and
  that is false against its own source — filed as R15);
  open the sealed payload **into memory** rather than to a plaintext working dir.
- **Writing more of the IDE in Sentinel** (the project's destination). **Re-measured 2026-07-19 —
  the old capability list here was substantially WRONG; see `docs/Sentinel-lang_request.md` for the
  evidence.** Verdicts, each checked by compiling a probe rather than by reading docs:

  | Claimed gap | Reality |
  |---|---|
  | no argv | **PRESENT** — `arg_count()` / `arg(i)` are real builtins |
  | no stdin | **PARTIAL** — `stdin_recv()` reads `i64` frames, not text |
  | no directory traversal / `stat` / streaming+seek | missing as *builtins*, but **fully reachable via `extern "C"`** — a complete recursive archiver was written in 100% Sentinel and round-tripped a 47-file tree byte-identically |
  | no recoverable I/O errors | true of the `read_file`/`write_file` **builtins** (they abort), but `CreateFileW` + `GetLastError` over FFI returns real codes with the process alive |
  | no closures | **PARTIAL** — non-capturing `Fn<T,R>` works (ADR 0070); *captures* are missing |
  | no `for`, no tuples, no `%` | confirmed missing (cosmetic — `while`, `struct`, `v-(v/10)*10`) |

  So the old "host hands a fixed file in, reads a fixed file out" framing is **no longer the
  constraint** — that was a limit of the builtin surface, not of the language. **Top candidate is
  still the seal crypto core**, but the shape of the blocker changed: the C-ABI static-lib path is
  production-viable *today* (verified linking into this exact GUI/Debug/CRT configuration, +512
  bytes of binary), and PBKDF2-600k runs in **390 ms–1.2 s**, not the ~8 s previously recorded here.
  What actually blocks it is **P0-R1: no way to secure-zero a `[secret u8]`**, which would make the
  port a net security *regression* versus the CNG code it replaces. The **diagnostic `file:line:col`
  parser** was the first low-risk/file-driven candidate and is **DONE (phase 35)** — it proved the
  whole C-ABI integration spine (build lib → emit header → link with R8 libs → call → verify),
  end-to-end in the live app. The **trust-manifest validator** (`Signing.h::loadTrust`) followed as
  the second port (phase 36) — a real security boundary, and it proved the "one lib, further ports
  nearly free" economics (+8 KB). The `.sig` carrier parser (`Signing.h::readSig`) followed as the third port (phase 37), so every
  signing/trust file parser is now Sentinel. The manifest reader (`Project.h::loadProject`) followed as the fourth port (phase 38), so **every
  file reader/parser in the IDE is now Sentinel**. The one file-touching path still in C++ is the
  comment-preserving manifest *writer* (`saveProject`) — a surgical structure-preserving TOML rewrite,
  the genuinely hard part, deliberately left native. The crypto core stays blocked on R1 (secure-zero).
- **Signing follow-ons (remaining):** surface capability-bound verify failures as Problems; an
  editable trust manifest (policy/grants) beyond add+import; a Settings field for a default signing
  key (today post-build signing uses `sentinel.key` in the project dir).
- **Targets follow-ons (remaining):** per-target `lib_paths`; a definable output dir; add/remove
  `[[target]]` blocks from the form (today it edits existing blocks' name/entry/type in place).
- **Undo/redo follow-up:** track the saved point so undo-to-clean clears `●`; toolbar button hover states.
- **The Direct2D editor** (GPU-perf target, as in SQLTerminal); dark **title-bar menu bar**; a
  project-templates picker (lib/exe/multi-target) for New Project.
- **Reconcile the spines/PRD** to ADR-0061 signing + the project/tier model (un-park PRD work).

---

## Seed prompt for a new session

_Keep this SHORT. It is the agent's first message, not a second copy of the handover — its only
jobs are to point at the reference, and to pre-empt the mistakes and wrong priors that would happen
in the first few tool calls, before the handover has been read. Everything look-up-able once you've
read the handover belongs in the handover body, not here._

> You're continuing **Sentinel-IDE** (`G:\SentinelIDE`) — a native Win32 IDE for the **Sentinel**
> language, whose thesis is a thin C++ host that shrinks as logic moves *into* Sentinel. It is a
> **released, public, auto-updating** product: eight releases, latest **v0.1.7 (build 164)**, and real
> Sentinel code runs in the shipped binary (the four file parsers). Treat `main` as shippable.
>
> **If no task follows this prompt, read the handover and then ASK before changing anything.** Do not
> pick something off "What's next" and start — this is live software with users on auto-update.
>
> **Read `docs/HANDOVER.md` first — it is the current state** (phase list 1–41, Environment gotchas,
> the Releases table, What's next, per-area detail; ~780 lines, so skim `grep -n '^## '` for the
> section map). Then as needed: `docs/RELEASING.md` (cutting a release) and
> `docs/Sentinel-lang_request.md` (what the Sentinel toolchain can actually do — measured, not
> folklore). `docs/prototype.md` + `docs/sentinel-project.md` are older narrative detail.
>
> **One rule that overrides your defaults:** never edit a `.bat` with `sed`, a heredoc, or any tool
> that writes LF — `cmd` needs CRLF and dies with `'t' is not recognized`. Use `Edit`. If your harness
> tells you to prefer shell edits, this beats it. Same for `examples/crypto.sentinel`: rewriting it
> with LF invalidates its committed `.sig`.
>
> **Four more things that bite early** (all expanded under *Environment gotchas* and the phase list):
> 1. **Don't "fix" the naming.** Product/exe = `Sentinel-IDE`; internal identifiers (window class
>    `SentinelIDEMainWindow`, `sentinelide` namespace, `%LOCALAPPDATA%\SentinelIDE`, `SentinelIDE.rc`,
>    the CMake target, the folder) deliberately stay `SentinelIDE`. Changing them breaks saved
>    settings and the `-Class` screenshot path.
> 2. **Build through `cmd /c scripts\build.bat`**, never raw `cmake` — the batch stamps the version,
>    selects Release + static CRT, and builds the Sentinel parser lib. `BUILD_DIRTY` means the tree
>    doesn't match HEAD: fine while developing, never ship one.
> 3. **Commit/push only when asked.** The repo is **public and must stay public** — auto-update fetches
>    the appcast over anonymous HTTPS, and a private repo 404s every client silently.
> 4. **Don't trust your priors about Sentinel's limits.** Much "Sentinel can't do X" folklore is false
>    (argv, non-capturing closures, dir-walk via FFI all work); the crypto-core port is *blocked*
>    (R1, secure-zero) despite older notes calling it the top candidate.
>    `src/sentinel/parsers.sentinel` is the worked example for the language's quirks (no `%`, `if` is
>    an expression, no early `return`, `[u8]` params move in loops so pass `&[u8]`, `&"literal"` is
>    illegal).
>
> **One habit this project earned the hard way.** Three defects shipped across v0.1.0–v0.1.4: unsaved
> edits silently discarded, a Debug binary that could not launch without Visual Studio, and an
> auto-updater that offered updates it could never install. The last is the cautionary one — the docs
> called auto-update "verified end-to-end" when only the *offer* path had ever been tested, and the
> install had never once worked. So: when something here says "verified", check **what** was verified,
> and prefer running the thing over reading that it works. (Auto-update is still only half-fixed —
> the manual check installs, the background check does not. See phase 40.)
>
> That's the seed. The phase history, build/screenshot commands and window classes, the
> trust-manifest four-site lockstep rule, the release procedure and the full gotcha list are all in
> `docs/HANDOVER.md`. Read there; don't expect it duplicated here.
