# Sentinel-IDE — Handover

_Last updated: 2026-09-05._

> **Naming:** the product/exe is **`Sentinel-IDE`** (matching `Sentinel-lang` / `Sentinel-learning`);
> the build output is `build\Sentinel-IDE.exe`. **Internal identifiers stay `SentinelIDE`** by design —
> the window class `SentinelIDEMainWindow`, the `sentinelide` C++ namespace, the settings dir
> `%LOCALAPPDATA%\SentinelIDE`, the CMake target id, and the local folder `G:\SentinelIDE`. So scripts
> that match the **process name** use `Sentinel-IDE`, but `-Class SentinelIDEMainWindow` is unchanged.

**Sentinel-IDE** is a native, Windows-first IDE for the **Sentinel** language, intended to
eventually be built *in* Sentinel (thin native host shrinking over time). Two workstreams
exist so far:

1. **UX design spines** (BMad) — `DESIGN.md` + `EXPERIENCE.md`, status **draft**.
2. **A working Win32 C++ prototype** — phases 1–51 built and verified.

---

## TL;DR — current state

- The prototype **builds and runs**: `scripts\build.bat` → `build\Sentinel-IDE.exe`.
- It's a real dark/coral Win32 IDE: themed shell with **dark popup/context menus**, dark
  TreeView + a **Direct2D code editor** (syntax highlighting, line gutter, dirty `●`/Save,
  undo/redo, and since phase 49 **Find/Replace** — Ctrl+F, Ctrl+H, F3/Shift+F3, wrap-around, a
  live match count, Match case / Whole word, and a Replace All that is ONE undo step)
  — RichEdit is still the **Output pane**, and only that, since phase 46's last slice,
  `snc` build/run with live streamed Output (**clickable `file:line:col`**) + a Problems
  triad, a configurable logfile + Settings dialog, a **Sentinel project model**
  (a `*.sntproject` file, or legacy `sentinel.toml`) with **multiple build targets**, an
  **Xcode-style target ▾ · tier ▾ scheme selector**, a **Project/Files explorer**,
  **New / Open / Close Project + Recent Projects + New File**, a **structured Project Settings
  editor with per-target editing**, and a **Signing & Trust panel** driving *real* ADR-0061
  `snc keygen`/`sign`/`verify` with a live status-bar trust chip.
- Since phase 51 the editor also notices when a file changes **underneath** it: coming back to the
  window re-checks the open file, reloads it silently when nothing is unsaved, and raises a
  **Reload / Keep my edits** prompt when something is. An identical rewrite under a newer timestamp
  (what a `git checkout` looks like) raises nothing.
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
- **Released.** Latest is **v0.1.15 (build 213)**; sixteen releases so far. Every file reader/parser in
  the IDE runs in Sentinel — including, since phase 47, the **update appcast**, which is the only one
  of them fed off the network. Auto-update is live (WinSparkle + Ed25519-signed `appcast.xml`), but read
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
| `src/host/win32/ReloadDialog.{h,cpp}` | Themed modal **Reload / Keep my edits** prompt for a file that changed on disk under unsaved edits (class `SentinelReloadDlg`). Raised only from `MainWindow.cpp::checkExternalChange`, and only when the buffer is dirty — a clean one reloads silently (phase 51). **Keep is the default**, including for a stray Enter: Reload is the answer that destroys work. |
| `src/core/Seal.h` | **Project sealing** (ADR-style): archive → LZMS-compress → AES-256-GCM under a random DEK, wrapped per password slot (PBKDF2-HMAC-SHA256). LUKS-like extensible unlock slots — **format v2** (`SNTSEAL2`): slots carry `slot_len` so unknown types are skipped, and the 24-byte header prefix is AEAD-bound as AAD. Reads v1. Native CNG; the AEAD+KDF core is a Sentinel-rewrite target. |
| `tests/seal_test.cpp` | Tests the `.sealed` format: one case per defect + a v1 back-compat case (25 assertions). `cmake --build build --target seal_test` or `ctest`. |
| `src/sentinel/` | **Product logic written *in* Sentinel, compiled into the binary** (phases 35–38, 44, 47, 48). `parsers.sentinel` = **eight** `export "C"` entry points — seven readers (diagnostic, trust manifest, `.sig` carrier, project manifest, **update appcast**, **sealed-container header**, **sealed archive index**) and the manifest writer — built to `build/generated/parsers.lib` (one C-ABI lib, ADR 0059) and called from `parseDiag` / `loadTrust` / `readSig` / `loadProject` / `saveProject` / `Updater.cpp::readAppcast` / `Seal.h::readSealHeader` / `Seal.h::readSealArchive`. Every file reader in the IDE, the one NETWORK reader, and the one fully attacker-chosen BINARY container. The seal CRYPTO is deliberately NOT here (R1, secure-zero). About box shows the % in Sentinel. |
| `tests/*_xcheck.cpp` | Prove each Sentinel parser stays byte-identical to its C++ oracle: `diag` (11), `trust` (12), `sig` (12), `manifest` (14). **`appcast` (25) is the exception** — its oracle had two real defects (an unbounded `int` accumulate, no validation at all), so it asserts the NEW behaviour where the old C++ was wrong and declares each divergence; a parity test there would have pinned the bugs. Plus `seal_test`. `ctest --test-dir build`. |
| `src/core/FileAssoc.h` | Per-user (`HKCU\Software\Classes`) file associations for `.sntproject`/`.sentinel` → open in this exe (`registerFileAssociations`; ≡ ▸ Register File Associations…). |
| `src/core/FileStamp.h` | **External-change detection** — has the open file changed underneath the editor? A stat pair (mtime+size) as the cheap filter, an FNV-1a digest of the disk bytes as the answer, so an identical rewrite under a new timestamp raises nothing. Portable question, Win32 syscalls; a non-Windows host reimplements `stampFile` and keeps `changedFrom` (phase 51). |
| `tests/file_stamp_test.cpp` | Tests `FileStamp.h` against real files (48 assertions), including the 64 KiB chunk boundary and a file held open exclusively. `ctest -R file_stamp`. |
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
| `src/host/win32/Updater.{h,cpp}` | **Auto-update** over WinSparkle — EdDSA-signed appcast, `initUpdater`/`checkForUpdates`/`shutdownUpdater`. Inactive until a real public key replaces the placeholder (phase 32). Since phase 47 the FETCH is all that is C++ here: `fetchAppcast` (WinINet) stays, and `parseVersion`/`versionIsNewer`/`appcastVersion` are gone — `readAppcast` crosses into `parse_appcast`. |
| `third_party/winsparkle/` | Vendored WinSparkle 0.9.3 x64 (DLL + import lib + headers, MIT). CMake copies the DLL beside the exe; the installer ships it. |
| `scripts/make-appcast.ps1` | Generate `appcast.xml` for a release (takes the signature; never touches the private key). |
| `docs/RELEASING.md` | Update-signing key setup + the per-release procedure. **Read before cutting a release.** |
| `docs/Sentinel-lang_request.md` | **Prioritised capability requests to the Sentinel-lang team** (R1–R15), each reproduced + measured on this machine. Also the authoritative record of what `snc`/`std` can actually do — it corrects several long-standing false claims in *this* file. |
| `THIRD-PARTY-NOTICES.txt` | WinSparkle MIT text + the SQLTerminal-Win32 GPL lineage note. |
| `tools/loc.sentinel` | **The first part of Sentinel-IDE written *in* Sentinel** — a whole-file line counter (read_file/write_file). Counts toward the "Sentinel" LOC badge. |
| `examples/` | Sample project: `sentinel.toml` (+ `[[target]]`s), `sentinel-trust.toml`, `crypto.sentinel`(+`.sig`), `hello.sentinel` |
| `art/` | `S2_icon.png` (app icon — metallic shield), `A_simple_clean…827808.png` (the `.sentinel` file icon — page + blue S + padlock), plus earlier iteration drafts (`…721412/818278.png`, `Remove_the_drop_shadow…726263.png`) |
| `docs/` | `prototype.md`, `sentinel-project.md`, this file, `release-notes-*.md`, `screenshots/phase1..15*.png` + `phase46-editor.png` (the README hero, 0.1.12) |
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
- **`capture.ps1` works on the Direct2D editor — WITH ONE CONDITION: the window must be
  FOREGROUND.** Verified in phase 46 slice 2 by capturing the live demo in the foreground:
  dark ground, real text, **zero** white pixels in the client area (`windowBg` 70,545 sampled
  / `pureWhite` 0). A magenta-cleared `ID2D1HwndRenderTarget` probe also captures as magenta.
  `PrintWindow` **can** see Direct2D; that is settled and re-measured.
  **The condition is the part that keeps being lost, and it is not "try again".** A D2D swap
  chain that has never been composited to the screen has nothing for `PrintWindow` to copy, so
  a background or minimised window gives a **blank client area every time** — a reviewer got
  blank 3/3 that way. The house rule is *launch GUI apps in the background*, and a background
  process **cannot** bring a window to the foreground (`SetForegroundWindow` is refused unless
  the caller owns the foreground). So an automated/agent run genuinely cannot capture this
  control, and re-running changes nothing: either a human puts the window in front first, or
  the capture is the wrong tool. (The frame-not-yet-presented case is real too, but it is a
  first-capture-after-launch race, not the usual cause of a blank shot.)
  *(A slice-2 session went the other way and wrote down a "PrintWindow cannot capture D2D" law
  in four files from a single blank capture, plus an accusation that two reviewers had
  fabricated their pixel readings. That was wrong and is retracted — the reviewers were right.
  A single observation is not a mechanism; neither is a single retraction.)*
- **For testing the renderer, use `ctest -R d2d_render` — it ASSERTS.** The offscreen WIC path
  (also `build\d2d_editor_demo.exe --render out.png <file>`) needs no window, no foreground,
  no desktop and no human, and it fails by itself instead of inviting you to look. Anything a
  screenshot would have told you about whether the control drew, that test already checks.
  `ctest -R d2d_dialect` is the equivalent for the message dialect. Reach for `capture.ps1`
  only to LOOK at something (layout, theme, a visual judgement call), from a session that can
  actually put the window in front.

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

## Prototype status — phases 1–51 (all done; screenshots cover 1–11, 13, 15 — see note below)

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
    ⚠ **As shipped in 0.1.5/0.1.6, only the MANUAL check installed; the automatic/background check
    was still broken** — verified afterwards, and the reason this entry exists in two halves.
    **Superseded by phase 41**, which replaced the background path; this paragraph is kept because it
    records *why* the split existed and how the background failure was measured. `win_sparkle_init()` also starts a
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
    **Cadence:** a 10 s startup settle (only to keep the first network call off the window-creation
    path), then **hourly** — so a release published mid-session is noticed within the hour. The
    request is a ~1 KB GET, so frequency is not the cost. Nothing is persisted across restarts, so a
    launch always checks. `Later` means "ask again next launch" and the thread stops after one offer,
    so hourly polling never becomes hourly nagging; **Skip this version** is the durable no.

42. **Single instance + drag-drop.** A double-click on an associated `.sentinel`/`.sntproject` used to
    spawn a whole second IDE; now the second process hands its path to the running one and exits, and
    files dropped on the window open too. Both converge on **one** deferred, guarded choke point:
    `requestOpenPath` normalises the path and *posts* `WM_APP_OPEN_PATH`, and the handler opens
    nothing while the UI is busy — it parks the path in `g.pendingOpenPath` and retries on a 4 s timer.
    "Busy" is `uiIsBusy()`: `!IsWindowEnabled` (the seven modals) **plus** `GetCapture()` **plus**
    `GUI_INMENUMODE|GUI_POPUPMENUMODE` — because `TrackPopupMenu` does **not** disable its owner, so
    the phase-41 `IsWindowEnabled` test alone would let an open interrupt the ≡ menu or a tier ▾
    dropdown mid-selection. The pending path is cleared **before** opening, or the nested modal loop
    dispatches the retry timer and the file opens twice.
    **Transport: named mutex + `WM_COPYDATA`, not a pipe.** One path, a single-UI-thread receiver, and
    `SendMessageTimeoutW` gives a definite delivered/not-delivered answer for free where a pipe needs
    an explicit ack and a whole connect/worker/ACL lifecycle. `WM_COPYDATA` is a *sent* message
    dispatched inside every modal's null-filter `GetMessageW` while the sender blocks, so the handler
    only copies, acknowledges and posts. **Drag-drop is `WM_DROPFILES`, not `IDropTarget`** — we never
    need `DROPEFFECT`, non-file formats or drop highlighting, and `IDropTarget` would force
    `CoInitializeEx` to become `OleInitialize` plus a COM object with revoke-before-destroy lifetime.
    Only the first dropped file opens: each open replaces the single editor buffer, so five files
    would mean four guard prompts and only the last surviving.
    **Two environment traps cost most of the time here.** (i) `FindWindowW` returns **NULL** for
    `SentinelIDEMainWindow` on this machine while `EnumWindows` finds that very window by that very
    class name — so the lookup enumerates instead, which is wanted anyway to pick the right window when
    a stray build is also running. (ii) **Comparing exe path *strings* is wrong on a mapped drive:**
    `GetModuleFileNameW` reports the `G:` form while `QueryFullProcessImageNameW` reports the UNC
    `\truenas.local\...` form for the same file, so the "is that window my build?" check rejected a
    legitimate sibling and every hand-off silently fell back to a second window. It now compares
    **file identity** (volume serial + file index via `GetFileInformationByHandle`), keeping the string
    compare as a fast path.
    The mutex is keyed on a hash of the lowercased exe path, so a dev build in `build\` and an
    installed copy stay separate instances — without that, testing a local build would hand its argv to
    whatever release the user has installed. Any failure falls through to a normal launch: an extra
    window is a nuisance, a swallowed double-click is a bug. Only the **path** travels; `--settings`
    and friends are ignored on a second launch, since honouring them would mean opening a modal in
    response to an asynchronous message — exactly the phase-41 hazard.
    **Verified:** a second launch with a different file leaves **one** process and opens the file in
    the running instance; with unsaved edits it raises the unsaved-changes prompt rather than
    discarding them; a synthesized `WM_DROPFILES` opens the dropped file. `ctest` 5/5.

44. **The manifest writer runs in Sentinel — every file-touching path now does.** `saveProject` was
    the last piece of C++ that reads or writes a file, deliberately deferred since phase 38 because a
    surgical, structure-preserving TOML rewrite is the genuinely hard one: it must change only the
    values it manages and leave comments, blank lines, key alignment, unmodeled keys (`icon`,
    `authors`) and `[[target]]` blocks byte-for-byte intact.
    **A characterization test came first, not the port.** `saveProject` had **zero** coverage while
    being the one piece of code that overwrites the user's manifest in place, so
    `tests/saveproject_test.cpp` (38 assertions, 10 cases) pinned the C++ behaviour before anything
    moved. Two of its cases pin behaviour that is arguably *wrong* — section names match
    case-INSENSITIVELY but key names case-SENSITIVELY, so `Name` is not the managed `name` and a save
    leaves the old key *and* inserts a new one; and `[[ target ]]` with inner spaces parses to the
    section name `"[ target"`, never matches `"[target"`, and is neither rewritten nor counted. Pinned
    on purpose: a characterization test describes what the code *does* so a port can be proven
    equivalent before anyone argues about what it *should* do.
    **The design that made it tractable.** The C++ builds a `vector<Section>{ vector<wstring> lines }`
    and mutates lines in place — a shape Sentinel cannot express, since `Vec<[u8]>` is unsupported and
    a non-Copy `Vec` element cannot be mutated in place. But the output is *just bytes*, so the port
    is a pure `save_manifest(text, model) -> [u8]` working on **offsets** into the original text and
    appending to one `Vec<u8>`. The host keeps file I/O and the TOML rendering; `encodeSaveModel`
    ships already-rendered values, so quoting rules are not reimplemented in a second language. Three
    line walks replace the C++'s single mutate-then-emit (insertion happens mid-file and must know the
    final written-set before the first byte is emitted), and `KV::written` becomes an 11-bit mask in
    an i64. ~533 lines, 21 private helpers.
    **`encodeSaveModel` deliberately sits OUTSIDE the `SENTINELIDE_SENTINEL` guard** so the xcheck
    builds the model with the exact bytes the host does — two encoders that must agree is a
    silent-divergence trap.
    **One deliberate divergence from the oracle, and it is the better behaviour.** `save_manifest` is
    byte-transparent; the C++ round-trips through UTF-16 via `readUtf8`, and `MultiByteToWideChar`
    without `MB_ERR_INVALID_CHARS` rewrites every invalid UTF-8 byte to U+FFFD. So a Latin-1 comment
    or an unmodeled value survives the Sentinel path and is silently corrupted by the C++ one. Kept
    rather than bug-compatibly broken, documented in `parsers.sentinel`'s header, and the host path
    therefore uses **raw byte I/O, not `readUtf8`/`writeUtf8`**.
    **A review found a process-fatal bug in the first cut.** `rd_le8`'s guard was written
    `off + 8 <= len`, and that addition **wraps**: an `off` in `[0x7FFFFFFFFFFFFFF0,
    0x7FFFFFFFFFFFFFF7]` makes `off + 8` negative, so the bound passes while `off >= 0` also passes,
    and the index runs at `i64::MAX`. Reproduced, aborting with `0xC0000409` — and a bounds violation
    inside a Sentinel export **terminates the process** with no recovery (R2). Unreachable from a
    manifest (every prefix is a host-computed byte count), but the comment above it asserted an
    overflow guarantee the code did not have. Now `off <= len - 8`, with the comment corrected to say
    what is actually true.
    **Verified:** `tests/saveproject_xcheck.cpp` — 27 cases byte-identical to the C++ oracle, covering
    the case asymmetry, duplicate keys and sections, alignment, target blocks in and out of order, a
    header with no closing bracket, LF-only input and the trailing-blank collapse. `ctest` is now
    **7 tests**. Live: Project Settings ▸ Save over `examples/sentinel.toml` kept all 16 comments and
    lost no line, growing exactly 45 bytes — the 45 LF→CRLF conversions the writer has always done.

45. **Post-build signing verifies before it claims success (RD-06).** On a successful build with
    `signing.sign = true`, the IDE ran `snc sign` and printed a green `[signed · <name>.sig]` **off
    the signer's exit code alone** — `verifyFile` was never called on that path. So any way for `snc
    sign` to exit 0 without leaving a usable signature produced a green line asserting the one thing
    the feature exists to assert. The UX spec had forbidden exactly this, in as many words
    ("never on the signer's exit code alone"), and the code did it anyway; the mismatch only surfaced
    when phase 43 reconciled the spines against the shipped product.
    Now a successful `snc sign` is followed by `verifyFile` — **the same function the trust chip
    uses**, so the Output line and the chip can no longer disagree about what "signed" means. Verified
    it prints `[signed · <name>.sig · verified]`; not verified prints a red *"sign reported success
    but the signature does NOT verify — treat as UNSIGNED"* and logs at Error. A non-zero `snc sign`
    still reports `[sign failed · exit N]` as before.
    **Verified live** on a project with `sign = true` and a real `snc keygen` key: the build produced
    `signdemo.exe.sig` and the Output pane showed the green `· verified` form, with the log line
    changing from the old "(exit 0)" to "signature verified". The red branch's predicate was checked
    separately — `snc verify` against a one-byte-flipped `.sig` exits non-zero, which is what makes
    `verifyFile` return `Invalid`.

46. **Direct2D editor — COMPLETE. Slices 1-8 landed; there is now only one editor.** (This
    heading has tracked the work slice by slice; the numbered sub-sections below are in the order
    they landed, so the deletion is the last of them. **Read the slice NUMBERS below with care:
    they collide.** The plan called the deletion "slice 7"; the session that shipped *text
    drag-and-drop* got there first and stamped **7** on that instead — in the code
    (`D2DEditor.cpp`'s drop target, `CMakeLists.txt`'s `d2d_dialect` note, `d2d_dialect_test`) and
    in the sub-section headed "SLICE 7 LANDED — TEXT DRAG AND DROP" below. The repo's own numbering
    wins, so **the deletion is slice 8** and the code says so; a few older paragraphs above still
    say "slice 7 deletes…", written before that collision existed, and they mean slice 8.) The
    editor replacement is the largest remaining item and is genuinely multi-session, so it was
    designed before any code moved: three strategies proposed independently (parallel control,
    model-first, render-first), judged on regression risk / effort / how early something ships, then
    synthesized.
    **The chosen spine: a `SentinelD2DEditor` that speaks RichEdit's own message dialect** — the ~18
    `EM_*` messages `MainWindow` actually sends, plus *synchronous* `EN_CHANGE`/`EN_SELCHANGE`/
    `EN_VSCROLL` — selected by one setting at the single `CreateWindowExW` in `createControls`. The
    host then needs almost no feature branches. Two neat consequences: `EM_GETTEXTEX` honouring
    `GT_USECRLF` means `saveFile` and its phase-17 `n*2+16` sizing stay untouched, and
    `EM_GETOLEINTERFACE` returning nullptr makes `editorDoc()` return nullptr, so phase 18's
    `suspendUndo`/`resumeUndo` become no-ops with **no** host edit. Grafted from the other two
    proposals: vendor SQLTerminal's pure `EditorModel` with unit tests, lift the tokenizer into a
    pure `computeSpans()`, and make error tints painted decoration rather than character formatting —
    which buys out the one place a pure dialect shim collapses (`EM_SETCHARFORMAT` with
    `SCF_SELECTION`, ~500 calls per keystroke).
    **Landed now — slice 1(a), the safety net, on RichEdit, before the new control exists.**
    `confirmSaveIfDirty` re-derives `g.dirty` at the moment of asking, as a **one-way OR — never a
    plain assignment**. That polarity is the whole point: it can only ever *add* a prompt, so even a
    wrong comparison cannot wave someone past unsaved work. Since phase 44 `g.dirty` is a pure
    function of `(editorText(), g.savedText)` and the notification is only the trigger to recompute,
    so on RichEdit this is provably a no-op — verified: the detector did not fire in live testing.
    It is bought now because the D2D control **replaces the notification path**, and a missed change
    notification there means silently discarding a buffer. If it ever fires, the log says so at
    Error. (The design suggested a debug `assert`; that would be useless here — builds are Release
    with `NDEBUG` since phase 44, so it compiles to nothing. A log line is the detector that fires.)
    **Slice 1(b–d) also landed.** `src/editor/EditorModel.{h,cpp}` is **vendored** from
    SQLTerminal-Win32 (same author, same GPL-3.0; recorded in `THIRD-PARTY-NOTICES.txt`) with
    exactly two changes, both marked at their site: the word-class predicate is Sentinel's
    `iswalnum(c) || c == '_'` rather than SQLTerminal's ASCII-only `[A-Za-z0-9_]`, so word
    navigation agrees with what `highlight()` calls an identifier — Sentinel sources really do carry
    non-ASCII identifiers; and **`textCrlf()`** was added, the on-disk CRLF form that
    `EM_GETTEXTEX`/`GT_USECRLF` will be built on so `saveFile` keeps working untouched against
    either editor. `tests/editor_model_test.cpp` pins it with **39 assertions**, and
    **`ctest` is now 8 tests**. The load-bearing one is case 10: it reads the real
    `examples/crypto.sentinel` (426 bytes, 12 CRLF), `setText` → `textCrlf()`, and asserts the bytes
    come back **identical** — because that file is a committed *signed* demo, opens by default, and
    Build auto-saves it, so a CRLF slip would invalidate its `.sig` without anyone pressing Ctrl+S.
    None of it is linked into the exe yet.
    **Slice 2 LANDED** — `src/host/win32/D2DEditor.{h,cpp}` (1,435 lines) + vendored
    `D2DSupport.h`, `tests/d2d_editor_demo.cpp` (a standalone host) and `tests/d2d_render_test.cpp`.
    No-wrap from the start: `DWRITE_WORD_WRAPPING_NO_WRAP`, one cached `IDWriteTextLayout` **per
    line** created only for the visible range (a 20k-line file lays out ~40 lines, not 20,000),
    `trimCache` bounding live layouts to visible + 2*64, a `lineStarts` index, `scrollX`/`scrollY`
    with both bars via `SetScrollInfo`. Still **not linked into the exe** — that's slice 3.
    Six defects found by review and fixed before it landed, two of them data-loss class:
    **AltGr arrives as Ctrl+Alt**, so on a Polish/German/Czech layout typing an accented letter ran
    `selectAll` and — because `TranslateMessage` has *already queued* the `WM_CHAR` — the next
    insert **replaced the whole document with one character** (RichEdit gets this right today, so it
    would have been a regression at slice 6); and **cut deleted even when the copy failed**
    (`OpenClipboard` fails transiently whenever a clipboard manager holds it). Also: device loss
    never rescheduled its paint; the cache trim sat inside the device-good branch, so with the
    device lost, navigation could build one layout per line with nothing reclaiming them;
    autoscroll ran from timers queued before the drag ended; and `ensureCaretVisible` mixed content
    and scroll coordinates so Ctrl+Home never reached 0.
    **SLICE 3 LANDED — the control is in the shipping exe, DEFAULT OFF.** `D2DEditor.cpp` +
    `EditorModel.cpp` now compile into the `SentinelIDE` target (Release `/MT`, `/W4`, zero
    warnings) and `createControls` picks the class at ONE `CreateWindowExW`: `[editor] d2d=1` in
    `settings.ini`, or `--d2d-editor` / `--richedit` to force one run. The flag is scanned in
    `wantD2DEditor()` and **not** in `runApp`'s argv loop, because that loop runs *after*
    `WM_CREATE` has already built the control; it still gets a consuming arm there so the
    catch-all `else openArg = a` cannot mistake the flag for a path. Deliberately **not** in the
    Settings dialog until slice 5 — with no colouring yet a checkbox would advertise something
    visibly worse (`lineNumbers` is the precedent for an `[editor]` key the dialog does not show).
    **The dialect is ~20 messages**, each with its trap written at the site. Load-bearing ones:
    `EM_GETTEXTEX` forks on `GT_USECRLF` → `textCrlf()` vs `text()` (`cb` is BYTES in, a CHARACTER
    count out — reading it as characters would overrun `editorText()`'s `n+1` buffer on every
    keystroke); `EM_GETTEXTLENGTHEX` is deliberately LENIENT about unknown flags because a 0 there
    makes `saveFile` write an **empty file** and a negative one throws inside `s.resize`;
    `EM_EXSETSEL` clamps, treats `cpMax == -1` as "to end", and **must not scroll** (that is
    `EM_SCROLLCARET`, which is why `gotoLineCol` sends it next); `EM_LINEINDEX` returns **-1** past
    the last line, which three call sites test for; `EM_POSFROMCHAR` returns exactly
    `drawContent`'s origin so the gutter needs no line-height arithmetic; `EM_SETCHARFORMAT` is a
    no-op for `SCF_SELECTION` (slice 4) but **not** for `SCF_ALL`, which is the *only* channel
    `Settings → editor font` has (`g.hEdit` never gets a `WM_SETFONT`); and `EM_GETOLEINTERFACE`
    returns 0 so `editorDoc()` is null and `suspendUndo`/`resumeUndo` become no-ops with no host
    edit — correct, not merely tolerated, since `EditorModel`'s undo stack snapshots text +
    selection only and a format cannot enter it.
    **The funnel is one function called from one place**, the tail of `afterEdit()`, which every
    text mutation already passed through — coverage by construction, not by inspection.
    `SendMessageW`, never `Post`. `WM_COMMAND` carries `EN_CHANGE`/`EN_VSCROLL`/`EN_HSCROLL` and
    `WM_NOTIFY` carries `EN_SELCHANGE`, which is **RichEdit's own split**, so the host runs the
    *same* branch for both editors and any difference is the control, not the dialect. `lParam`
    must be the HWND (`MainWindow.cpp:1931` tests `lParam != 0`). Scroll and selection are reported
    by DIFF rather than per-site, because they are written in a dozen places each; `EN_CHANGE` is
    not — it is explicit and gated on nothing. `EN_VSCROLL` is a deliberate SUPERSET of RichEdit
    (any scroll change, not just scrollbar clicks), which also fixes the stale gutter on
    keyboard-only scrolling. **`EM_UNDO`/`EM_REDO` funnel too, and that is not optional**: the
    accelerator table claims Ctrl+Z/Ctrl+Y before the control ever sees them, so `ID_UNDO`/`ID_REDO`
    is the *only* undo path in the exe and `D2DEditor`'s own `'Z'`/`'Y'` arms are live only in the
    demo host. (Ctrl+Shift+Z does reach them — no `FSHIFT` entry — so it redoes, where RichEdit does
    nothing.) `EN_SELCHANGE` is gated on `WM_SETREDRAW`, which is the host's own *exact* marker for
    programmatic bookkeeping: every selection storm sits in a redraw-off window and `gotoLineCol`
    does not.
    **Host branches: six, plus the switch.** `applyTheme` and `WM_DPICHANGED` call
    `d2dEditorApplyTheme` / `d2dEditorUpdateDpi` (child windows never receive `WM_DPICHANGED`, so
    the control cannot learn it alone); the argv arm; and **three gates that must move together —
    `highlight()`, `clearErrorMarks()` and `markErrorLines()`.** **The gate is about UNDO, not
    cost** — `applyColor`'s `EM_EXSETSEL` reaches `EditorModel::setSelection`, which clears
    `typingRun_`, so running it on every `EN_CHANGE` would make every keystroke its own undo step
    and collapse the history to the last 200 characters (`kMaxUndo`). Slice 3 first shipped with
    only `highlight()` gated, which was an inconsistency with a real cost: `applyBackColor` has the
    *identical* `EM_EXGETSEL`/`EM_EXSETSEL`/`EM_SETCHARFORMAT` shape, so undo granularity quietly
    degraded around **every build** — the moment someone is most likely to keep typing. Both error
    paths are now gated the same way; their colouring was a visual no-op on that control anyway.
    The state bookkeeping deliberately stays OUTSIDE the gate (`g.errorMarks := false`, and the
    gutter invalidate), or `clearErrorMarks` would be re-entered on every keystroke forever.
    Slice 4 deletes all three lines, and inherits the rule: **colour by drawing ranges, never by
    moving the selection.**
    **Two things found by putting it on screen.** (1) The line-number gutter painted the top
    partial line's number OVER the file-tab strip — `DrawTextW` into the whole-window back buffer
    with no clip, and the loop only ever guarded its BOTTOM edge, which was safe while RichEdit
    scrolled by whole lines and stopped being safe the moment an editor scrolled by pixels. Fixed
    unconditionally with an `IntersectClipRect` on `g.rGutter` (a no-op on RichEdit; the cached
    `g.memDC` is reset afterwards). (2) `saveFile` now refuses to write when a non-empty buffer
    fetches as zero characters — same one-way polarity as 1(a), it can only ever REFUSE.
    **Found by review, after the fact, and fixed:** (a) **`EM_EXSETSEL` did not clear the sticky
    vertical column `desiredX`** — every other caret-moving path does. Double-click a build
    diagnostic (which sends `EM_EXSETSEL` + `EM_SCROLLCARET`) and press Down and the caret landed
    in the column you were in *before* the jump; A/B'd against RichEdit as `Ln 2 Col 3` vs
    `Ln 2 Col 31`. `onAutoScroll` had the same omission (it hand-rolls the tail of `caretMoved`
    because it owns the scroll) and is fixed with it; `EM_SCROLLCARET` correctly does *not* clear
    it, because it moves the view and never the caret. Pinned by `d2d_dialect` case 7.
    (b) **`WM_SIZE`, `setFontInternal` and `d2dEditorUpdateDpi` changed the view through
    `clampScroll` without reporting it**, contradicting the funnel's own "any change of
    scrollY/scrollX reports here" — measured, the first visible line moved 381 → 331 with an
    `EN_VSCROLL` delta of 0. Harmless only because every host caller repaints anyway, which is a
    property of the callers, not the control; they all flush now. (c) `EM_POSFROMCHAR` derived its
    line pitch from `st->lineH` but called only `ensureLineIndex`, so it was correct purely by the
    accident that `WM_SETTEXT` happens to run `ensureFormat` first; `ensureFormat` is now called
    at the three sites that consume `lineH` (`EM_POSFROMCHAR`, `visibleRange`, `contentHeight`).
    (d) Two comments were wrong and are rewritten: the `WM_SETREDRAW` counter buys correct
    NESTING and immunity to an extra TRUE, **not** protection from an unbalanced FALSE (which
    parks it at 1 and mutes `EN_SELCHANGE` permanently, where a bool would self-heal); and
    `afterEdit` listed "drop" among the mutations it covers, which is a path that **does not
    exist** — the control registers no drop target and handles no `WM_DROPFILES`.
    **Known difference, accepted at the time — CLOSED IN SLICE 7:** dragging TEXT within the
    editor did nothing (RichEdit supports it). Dropping a FILE fell through to the main window's
    handler, guarded by `confirmSaveIfDirty` — and keeping that true after the control grew a drop
    target of its own is the hard half of slice 7. See the slice-7 section below.
    **SLICE 4 LANDED — syntax colouring and the error tints, both PAINTED.** The rules moved out
    of `MainWindow.cpp::highlight()` into **`src/editor/SyntaxLexer.{h,cpp}`** — pure, no Windows,
    no `COLORREF` — exposing `computeSpans(text, len, LexState&, out)` with an explicit
    line-start STATE, plus `advanceState` (the same scan with the spans thrown away) and
    `isKeyword`. **Both editors now run it**: `highlight()` still paints
    `applyColor(0, -1, textPrimary)` first and then one `applyColor` per span, so its call
    sequence is unchanged, and slice 7 deletes only the consumer. Pinned by
    **`ctest -R syntax_lexer` (89 assertions)**; **ctest is now 11 tests** (the brief said 12; 10
    + 1 is 11).
    **The extraction is provably behaviour-preserving on the RichEdit path.** HEAD's inline loop
    was pasted into a throwaway harness with `applyColor(...)` replaced by `emit(...)`, and its
    `(start, end, class)` sequence compared against `computeSpans` over every `.sentinel` file in
    the repo *in the lone-CR form `editorText()` actually returns* plus 29 adversarial literals:
    **33 cases, 0 mismatches**, including `parsers.sentinel` at **2,600 identical `applyColor`
    calls**. That is a stronger claim than "it looks the same", and it is the only reason to
    believe a refactor of a 45-phase-old hand lexer.
    **HOW THE COLOUR IS APPLIED, and the two rules it must not break.** (1) **Never
    `SetDrawingEffect`** — a drawing effect is a BRUSH, i.e. device-bound, and the per-line layout
    cache is shared with the offscreen WIC target and survives device loss precisely because it
    holds none. `drawContent` instead draws **the SAME one layout once per coloured run inside a
    `PushAxisAlignedClip`** over that run's x-range, `D2D1_ANTIALIAS_MODE_ALIASED` so adjacent
    runs share a boundary x, round to the same pixel and tile exactly. Same layout every time
    means identical shaping, so painting and hit-testing cannot drift; the OUTER edges of a line
    open out to the client edge so glyph ink that overhangs its advance width is not shaved. A
    line with no spans takes the old single unclipped `DrawTextLayout`. (2) **Never colour by
    moving the selection** — `applyColor`/`applyBackColor`'s `EM_EXSETSEL` reaches
    `EditorModel::setSelection`, clears `typingRun_` and makes every keystroke its own undo step.
    So **`highlight()`'s `g.d2dEditor` early-out is PERMANENT**, and this handover's own
    prediction that slice 4 would delete all three gate lines was **wrong**: `markErrorLines`'
    branch was *replaced* (as written), `clearErrorMarks`' became a one-line
    `d2dEditorSetErrorLines(g.hEdit, {})`, and `highlight()`'s stays until slice 7 takes the
    whole function.
    **Two defects review caught in slice 4's own first cut, both fixed before it landed, both
    worth knowing because they are the shape of mistake this migration keeps making.**
    (a) `d2dEditorRenderToPng` parked THREE brushes; slice 4 had added five more to
    `createBrushes` and `releaseBrushes` and missed the park. An offscreen render therefore
    destroyed the window's five new brushes while the window still owned them, after which
    `brushForClass` returned null, `drawRun` fell back to `brText`, and the live editor painted
    **monochrome for the rest of its life** — no crash, no leak, nothing to see. Three places
    had to agree about one set and one of them was updated by hand. All eight brushes now live
    in an `EditorState::Brushes` struct, so park/restore is a struct copy that cannot omit a
    member. **Do not flatten it back into loose fields.**
    (b) `errorLines` holds ABSOLUTE line numbers and `setTextInternal` did not clear them, so
    opening a clean file after a failed build painted red bands at the old file's line numbers
    — and `loadFileIntoEditor` sets `g.errorMarks = false` without calling `clearErrorMarks`,
    so no later keystroke could clear them either; only the next build. RichEdit cannot have
    this bug (its tint is a `CFM_BACKCOLOR` character format that dies with the text). Painted
    decoration has to be told: `setTextInternal` now clears them, which covers both
    `loadFileIntoEditor` and `closeProject` since both go through `WM_SETTEXT`.
    Also fixed: the per-run clip bounded **y** to the line box as well as x, so on lines that
    happened to contain a span, glyph ink overhanging the box (accented and CJK identifiers —
    which the lexer's own comment says Sentinel sources really carry) was shaved. Only x needs
    bounding. And `syntax_lexer_test` only ever resumed a string with `"`, so a lexer that
    hardcoded the double quote on resume passed 89/89; a mirror case resuming inside `'…'`
    over a line containing `"` now catches it (verified against that exact injected fault).
    **The tints are decoration**: `d2dEditorSetErrorLines(HWND, const std::vector<int>&)`
    (0-based, empty clears), painted as a full-client-width band behind the text in
    `blendColor(windowBg, diagError, 24)` — the *same* colour, now computed from **one copy of
    `blendColor`, moved to `Theme.h`** so the two editors cannot tint differently. `g.errorMarks`
    is honest again (`= !lines.empty()`). The band is deliberately full-width where RichEdit's
    `CFM_BACKCOLOR` stops at the last character.
    **NOT re-lexing per keystroke.** `EditorState` gained a per-line **lexer start-state prefix
    cache** (`lineLexState` + `lexStateKnown`) beside `lineStarts`, and `rebuildLineIndex` — the
    one place the text is known to have changed — only ever *lowers* the watermark, to the line
    containing the first character that actually differs. It finds that line by `memcmp` against
    `prevText`, a kept copy of the buffer. **The obvious cheap alternative, "invalidate from the
    caret's line", is WRONG** for a paste over a multi-line selection (the caret lands below the
    first changed line) and for undo (which can change anything), and a stale colouring cache is
    a bug you only see on someone else's file. The painter also writes each line's out-state
    forward, so a visible line is lexed once per paint, not twice.
    **Measured**, driving the real control through the real dialect on a **20,041-line / 1.7 MB**
    buffer, 100 keystrokes each with a repaint, against the slice-3 binary on the same harness:
    typing on **line 1** 0.91 → **1.44 ms/keystroke**; on **line 19,000** 0.87 → **1.75**; the
    one-off cold jump to line 19,000 (which lexes lines 0..19,000) 0.74 → **3.17 ms**. So the
    cache is worth **~2.4 ms per keystroke** at line 19,000 and the `prevText` `memcmp` + copy it
    rides on costs **~0.38 ms** there and **~0.07 ms** at line 1. Note which half is which:
    the `memcmp` stops at the first difference, so it really is ~0 when you type at the top,
    but the `prevText = text` copy is UNCONDITIONAL and O(document) wherever you type — an
    earlier note here said "~0 at line 1" and was measuring only the compare. It also doubles
    the resident size of the document, which is small beside EditorModel's 200 whole-document
    undo snapshots but is not nothing.
    O(document) per edit: the `'\n'` scan (pre-existing), the `memcmp`, the copy. O(visible):
    everything else — layouts, `computeSpans`, the draws.
    **`ctest -R d2d_render` now asserts on COLOUR (38 assertions, was 16).** All five Theme text
    colours must be present with >= 25 attributed pixels each, and on the right LINES — bands
    asked of the control through `EM_LINEINDEX` + `EM_POSFROMCHAR`, never hard-coded. A pixel is
    attributed to the nearest of the five within 12 units; one honest limitation is written at
    the site: in the dark theme `synComment` is very nearly a 70%-covered `synNumber` over the
    background, so a partly-covered digit can be counted as a comment pixel — never the reverse,
    which is why the only absence assertions sit on lines with no numbers. Case 7 sets an error
    line, asserts the band is exactly the tint, that the syntax colours still show through it,
    and that clearing returns the image **byte-identically** to the untinted render.
    **Verified by negative control**: brush selection forced to `brText`, rebuilt, `d2d_render`
    → **12 failures, exit 1** (`ctest` exit 8) — and note that **case 4 still PASSED** at 23,603
    inked pixels, which is exactly the split that was wanted: a monochrome editor satisfies "text
    was drawn" and fails "the editor is not monochrome". Reverted; `git diff` clean.
    **Undo granularity is pinned, not asserted** — `d2d_dialect` case 10 (the file is now 101
    assertions): 65 characters typed **with a repaint after each** and an error tint set and
    cleared partway = **1 undo step**; the same 60 characters with an `EM_EXSETSEL` between them
    = **60 undo steps**. The second half is what makes the first half mean something.
    **SLICE 5 PREPARED, NOT PUBLISHED** (uncommitted at the time of writing → see the working
    tree). It makes the D2D editor *discoverable* so it gets real exposure before slice 6 flips
    the default. Three things:
    (a) **A Settings checkbox** — "Use the Direct2D editor  (preview)", `BS_AUTOCHECKBOX` (not
    owner-draw, so UIA/Narrator see a real checkbox with a real checked state), written to
    `[editor] d2d`. It was held back until slice 4 on purpose: a checkbox offering a *visibly
    worse* editor is worse than no checkbox. The setting is READ EXACTLY ONCE, by
    `wantD2DEditor()` at `WM_CREATE`, so it needs a restart — the hint under it says so, because
    a checkbox that appears to do nothing is the worst of the three options. Nothing tries to
    swap the live control: the class is chosen at the single `CreateWindowExW` that built it, and
    re-creating it mid-session would mean re-homing the text, the undo stack, the dirty state and
    every host handle pointing at it.
    (b) **A REAL SYSTEM CARET** — this was banked below as slice 6's problem and is bought here
    instead, because slice 5 is the release that actually offers this to users. The control paints
    its own caret; `GetGUIThreadInfo` and the `OBJID_CARET` accessible object read the *system*
    caret, so with none created there was nothing for Narrator, Magnifier's "follow the text
    cursor", or an IME placing its candidate list to track — a genuine accessibility regression
    against RichEdit, which creates one. Now created on focus, moved with `SetCaretPos`, destroyed
    on kill-focus (a caret is a per-THREAD object) — and **deliberately never `ShowCaret`'d**: a
    shown caret is a SECOND caret, a blinking GDI bar beside the painted one. Created-and-
    positioned is all the reporting APIs read. `caretClientPos` is the ONE copy of that arithmetic,
    shared by the painted caret, the system caret and the IME composition window, in
    `drawContent`'s exact float form — `(LONG)(n + f)` and `n + (LONG)f` disagree by a pixel when
    `n + f` is negative, which put the IME window off the glyph on a scrolled-away caret.
    `d2d_dialect` case 11 pins it: the system caret sits exactly on `EM_POSFROMCHAR` at three
    scattered offsets, follows a pure scroll, is one line tall, and dies with focus.
    (c) **Version 0.1.7 → 0.1.8** in `scripts/build.bat` (`MKT`/`MKTRC`; CRLF verified intact
    afterwards — 91 CRLF, 0 bare LF). Nothing else release-related was touched: no tag, no
    appcast, no signing. Publishing is a separate, deliberate act — see *Releases*.
    **Found by screenshotting the dialog, not by anything that compiles:** the checkbox's hint
    static was sized `S(34)`, one line plus a sliver, so its second line was clipped mid-word into
    the Theme row below. Now `S(46)`. Look at dialogs you change.

    **SLICE 6 DONE — the Direct2D control is the DEFAULT.** `Settings::d2dEditor` is now
    `true`, so a machine with no `[editor] d2d` key gets it. Three escape hatches survive and
    all three were verified live off the log's `Editor control:` line: no flag and no key →
    `Direct2D (default)`; `--richedit` → `RichEdit (opted out)`; `--d2d-editor` → Direct2D.
    Unticking the box writes `d2d=0` and is permanent, and `createControls` still falls back to
    RichEdit on its own if the window class fails to register. **Anyone who ran 0.1.8 and left
    the box unticked keeps RichEdit** — `saveSettings` wrote `d2d=0`, and an explicit opt-out
    must outrank a changed default.
    The Settings label dropped "(preview)" and the hint flipped with it: a ticked-by-default
    checkbox must not describe itself as something you opt into, and the useful sentence is no
    longer what you gain by ticking but **what you lose by unticking** — at slice 6 that was
    dragging TEXT within a file. **Slice 7 closed that gap, so the clause is gone**; the hint now
    says only that the box needs a restart and that unticking returns you to RichEdit. (Shortening that hint was not
    cosmetic: the longer wording ran to three lines in a two-line static and clipped, the same
    way slice 5's did. Screenshot the dialog after changing its text.)
    **What slice 6 deliberately does NOT do:** remove the choice. That is slice 7, and it should
    not run until this default has had real use. The two editors are not feature-identical yet.
    **VERIFIED BY HAND on 2026-09-04, on the released 0.1.9.185 build** (the per-machine install at `C:\Program Files\Sentinel-IDE`, reached by a real in-app update from 0.1.4.151 — five releases in 22 seconds, which also closed the "nobody has ever confirmed a client INSTALLS this" gap that had stood since v0.1.0). A human pressed the keys and moved the mouse:
    - **Ctrl+Z**, **Ctrl+Y**, **Ctrl+S** — real keypresses, the hardware -> Windows -> `TranslateAcceleratorW` link that `d2d_dialect` case 13 explicitly cannot reach.
    - **Dragging a text selection** — the drag threshold, `DoDragDrop`'s modal loop, `QueryContinueDrag`/`GiveFeedback` and the drop caret, none of which a harness can drive.
    - **Dropping a FILE on the editor with a dirty buffer** — it opened AND raised the Save prompt, which is the regression the text drop target could most easily have caused.
    - Ctrl+S on the signed `examples/crypto.sentinel` left it byte-identical at 426 bytes (`git status examples/` clean), independently re-checked.
    **Remaining slices:** 7 was *text drag-and-drop* (below), which is the stated precondition;
    what is left is deleting the RichEdit path (the *editor* path only — `msftedit.dll`,
    `MSFTEDIT_CLASS` for `g.hOut` and the `EN_LINK` → `parseDiag` → `gotoLineCol` chain all
    survive).
    **Slice 6's acceptance list, banked now:** dropping a file onto the editor AREA (RichEdit
    registers its own OLE drop target today; the D2D control registers none, so the drop falls to
    the main window — verify, do not assume); multi-monitor `WM_DPICHANGED`; and **the physical
    keystrokes — Ctrl+Z / Ctrl+Y / Ctrl+S with `d2d=1`**, which NOTHING has yet tested with real
    keys. Every undo/redo/save check in slices 3-5 posted the `WM_COMMAND` that
    `TranslateAcceleratorW` produces, which is the code those slices changed but is not the
    keyboard. An automated session cannot inject them (it cannot foreground the window); a human
    can do it in a minute. Do it before the default flips.
    *(The system-caret item that used to be banked here is DONE — see slice 5(b) above.)*
    **`ctest -R d2d_dialect` LANDED (`tests/d2d_dialect_test.cpp`, 97 assertions).** It was going
    to be slice 4's; it is here now because the dialect shipped with *zero* automated coverage and
    `editor_model_test` only pins the pure model — the bytes that reach disk go through the
    HANDLERS' arithmetic, and nothing tested any of it. It builds a real parent window and a real
    `WS_CHILD` control and drives them with the message shapes `MainWindow` sends. What it pins:
    the exact `saveFile` sequence (`EM_GETTEXTLENGTHEX{GTL_NUMCHARS}` → `resize(2n+16)` →
    `EM_GETTEXTEX{cb = bytes, GT_USECRLF}`) round-trips the real `examples/crypto.sentinel`
    **byte-identically** with its 12 CRs intact; the `GT_DEFAULT` form is self-consistent and has
    no CR; **`cb` is honoured as BYTES**, proven with a canary past the declared capacity so
    reading it as characters *or* dropping the `- 1` is a failure rather than silent heap
    corruption; `EM_GETTEXTLENGTHEX` is never negative and never 0 for a non-empty document under
    nine flag combinations including all-ones; `EM_LINEINDEX`/`EM_EXLINEFROMCHAR` are inverses at
    every line *and* every character offset, with -1 past the end; `EM_EXSETSEL` treats
    `cpMax == -1` as "to end" and clamps `gotoLineCol`'s unbounded column; and **`EN_CHANGE` is
    SYNCHRONOUS** — the parent's counter has moved before `SendMessageW` returns and nothing is
    left in the queue, which is the check that catches a regression to `PostMessage`.
    **Verified by negative control, four ways** (each mutation applied, rebuilt, run, reverted):
    `cb` read as a character count → 8 failures incl. an explicit overrun report; the `- 1`
    dropped → 8 failures; `SendMessageW` → `PostMessageW` in the funnel → 9 failures; the
    `GT_USECRLF` fork ignored (the LF-only save that destroys the `.sig`) → 5 failures including
    byte-identity. Exit 1 every time. Everything it asserts is a count, a range or an invariant —
    no pixel, glyph or font metric — so a font, DPI or theme change cannot break it.
    **`ctest -R d2d_render` is the verification spine for slices 4-7.** It renders the control
    offscreen through WIC — the *same* `drawContent` the window uses — decodes the PNG back and
    asserts on real pixels: the background is Theme `windowBg`, and **>=500 pixels differ from it**
    so a blank render FAILS. On `examples/crypto.sentinel` it measures **23,603 of 788,316 pixels
    inked (2.99%)**. Verified by negative control: suppressing the draw gives `0 of 788316` and the
    test fails, exit 1. It exists because rendering had NO automated coverage: a blank editor
    would compile, run, open a window and pass every other test in the repo. `capture.ps1` can
    show you the control — it works fine on it, *while the window is foreground* — but seeing is
    not asserting, an automated session cannot foreground anything, and slice 4's colouring needs
    a check that runs headless in CI and fails by itself.
    One constraint it imposes: the per-line layouts must stay **device-independent** (no
    `SetDrawingEffect`), which is what lets the window target and the WIC target share one cache.
    Slice 4 must colour by drawing ranges, not by attaching effects to layouts, or the offscreen
    path and the device-loss recovery path break together.

    **The two ways this can lose work, and how the plan stops them:** a missed `EN_CHANGE` leaves
    `g.dirty` false and the buffer is discarded with no prompt — stopped by 1(a) above plus a single
    `SendMessageW` notification funnel (never `Post`: `loadFileIntoEditor` and `closeProject` clear
    `g.loadingFile` immediately, so a posted notification arrives too late). And `saveFile` writing
    LF instead of CRLF would rewrite `examples/crypto.sentinel` and invalidate its signature —
    **and Build auto-saves the open file, so that fires without anyone pressing Ctrl+S, on the file
    that opens by default.** Stopped by the byte-identity golden test and a manual `git status
    examples/` gate on every slice from 3 onward.
    **Both were exercised live at slice 3, with the switch ON.** Typing raised the dot and the
    coral Save; `Close Project` and an externally-delivered second file both raised the
    Save/Don't Save/Cancel prompt on an unsaved buffer; `ID_UNDO`/`ID_REDO` moved the dot off and
    back on and tracked `Ln/Col`; double-clicking a Problem jumped the caret to `Ln 202, Col 20`
    and scrolled the line into view. Ctrl+S and the **Build auto-save** each wrote a
    `crypto.sentinel` through the Direct2D path that came back at **426 bytes, 12 CRLF, 12 CR,
    12 LF** — every break CRLF, no strays — with `snc verify --sig` reporting **signature OK**.
    The 1(a) detector (`Dirty flag missed a change`) did not fire once, on either path.

    > **HOW TO RUN THAT CHECK — THE SAFE FORM, AND IT IS THE PROCEDURE FOR SLICES 4-7.**
    > The slice-3 session ran it by pointing the IDE at `examples/` and saving
    > **`examples/crypto.sentinel` itself**. The end state was clean and the signature verified,
    > so nothing was lost — but that was the *outcome*, not the *design*. The whole reason the
    > test exists is that a slip in the new `EM_GETTEXTEX` handler writes the file short or
    > LF-only; running it on the committed original means the first symptom of the bug you are
    > hunting is the destruction of the evidence, plus a committed `.sig` that no longer matches
    > and a dirty tree to explain. The house rule — *never write to `examples/`* — is not a
    > formality here, it is the containment.
    > **Do this instead**, and never the other thing:
    > 1. `robocopy examples %TEMP%\snt-crlf crypto.sentinel crypto.sentinel.sig sentinel-trust.toml`
    >    (or a `.sntproject` copy of the whole folder) — a SCRATCH COPY outside the repo.
    > 2. Open the scratch folder in the IDE (`--d2d-editor`), edit, Ctrl+S, and run a Build so the
    >    **auto-save** path is exercised too — that is the one that fires with nobody pressing
    >    Ctrl+S, and it is the one that reaches the signed file in real use.
    > 3. Compare against the pristine original, which is still untouched in `examples/`:
    >    `fc /b examples\crypto.sentinel %TEMP%\snt-crlf\crypto.sentinel` for byte identity, and
    >    `snc verify --sig` in the scratch folder for the signature.
    > 4. `git status --porcelain examples/` must print **nothing**, before and after. If it ever
    >    prints a line, `git checkout -- examples/` and find out which write got out.
    >
    > `ctest -R d2d_dialect` now covers the same byte-identity property with no file write at
    > all (it reads `examples/crypto.sentinel` `GENERIC_READ` and compares in memory), so the
    > live run is confirmation that the *host* wiring is right, not the primary evidence.


    **SLICE 7 LANDED — TEXT DRAG AND DROP, and with it the last behavioural difference against
    RichEdit.** Drag a selection to move it, Ctrl-drag to copy, accept `CF_UNICODETEXT` dropped
    from other applications, with the caret tracking the drop point. The two editors are now
    feature-identical, which was the stated precondition for deleting the RichEdit path.

    **`OleInitialize` replaced `CoInitializeEx` in `runApp` (MainWindow.cpp).** `RegisterDragDrop`
    fails with `CO_E_NOTINITIALIZED` on a thread that has only a plain COM apartment. It is a
    strict upgrade — `OleInitialize` calls `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`
    itself — so the shell dialogs, WIC and the shell-item APIs are untouched. **Pairing checked,
    not assumed:** that call and the `CoUninitialize` (now `OleUninitialize`) after the message
    loop are the *only* COM init/teardown on the UI thread, and the five `std::thread`s this app
    starts (signing, build, run, and two in `Updater.cpp`) initialise no apartment and use no COM.
    One init, one uninit, same thread.

    **THE TRAP, and it is bigger than the feature: REGISTERING A DROP TARGET STEALS FILE DROPS.**
    An OLE drop target on a CHILD wins outright over the parent's `DragAcceptFiles`, so from the
    moment `RegisterDragDrop` succeeds the shell stops sending `WM_DROPFILES` to the main window
    for anything dropped on the editor's rectangle. Left unhandled, dropping a `.sentinel` on the
    text area does nothing — or, if `CF_HDROP` fell through to the text branch, pastes its PATH.
    Either is a worse regression than the gap being closed. So `CF_HDROP` is classified FIRST and
    handed straight back to the host: `EditorDropTarget::forwardFilesToHost` copies the shell's
    `HGLOBAL` byte-for-byte and `SendMessage`s it to the parent as `WM_DROPFILES`, which lands in
    MainWindow's existing handler → `requestOpenPath` → the `confirmSaveIfDirty`-guarded
    `WM_APP_OPEN_PATH`. **No second opening path was written**, deliberately: that guard is the
    only thing standing between a dropped file and someone's unsaved buffer. The block is COPIED
    rather than forwarded because the host finishes with `DragFinish` (a `GlobalFree`) while the
    `STGMEDIUM` belongs to the data object and is returned with `ReleaseStgMedium` — handing one
    handle to both is a double free.

    **A move is ONE undo step.** `EditorModel::moveSelectionTo(dest)` does the delete and the
    insert under a single `recordPreEdit(false)`. Spelled as `deleteSelection() + insertText()` it
    would be two snapshots and the first Ctrl+Z would show the text deleted and not re-inserted —
    a half-move. A `dest` inside `[min, max]` (inclusive at both ends) is a no-op that pushes no
    undo step at all, so dropping a selection onto itself changes nothing rather than costing a
    Ctrl+Z to get past.

    **Every drop ends in `afterEdit`.** `applyDrop` is the only mutating function in the
    drag-and-drop section and its last statement is the funnel, i.e. a SYNCHRONOUS `EN_CHANGE`. A
    drop that mutated the buffer and returned to OLE without notifying would leave the buffer
    discardable with no prompt — this project's defect #1, arriving through a path nobody types
    into.

    **`DoDragDrop` is a MODAL LOOP inside the window proc**, and every line of `beginTextDrag` is
    placed for that: the dragged run is COPIED out of the model before the call (a `const wchar_t*`
    into `model.text()` would dangle across a reallocation); the local move is performed by the
    DROP against the model's state at that instant, never against offsets captured at drag start;
    the delete that ends an EXTERNAL move re-checks that the range still holds the exact text that
    was handed over, and skips it otherwise (leaving a copy is recoverable, deleting the wrong run
    is not); `state(hwnd) != st` is re-tested after the call in case the window went away with it;
    the blink timer is stopped for the duration and `st->selecting` is cleared, which is also what
    makes `onAutoScroll` a no-op if a `WM_TIMER` queued before the drag arrives during it.
    A drag is recognised as LOCAL by a private clipboard format carrying the source `HWND`
    (`SentinelD2DEditorDrag`), not by "is `DoDragDrop` on our stack" — the latter cannot be tested
    without a mouse and would call a second editor's drag local.

    **A press that does not travel is still a plain click.** A left-press inside the selection
    does NOT move the caret; it arms a drag and waits. `SM_CXDRAG`/`SM_CYDRAG` of travel starts the
    drag, `WM_LBUTTONUP` without it collapses the selection exactly as a press outside one would.
    (The capture release in `WM_LBUTTONUP` had to become unconditional: an armed drag holds the
    capture without setting `selecting`.)

    **`ctest -R d2d_dialect` case 14 (175 assertions in the file now).** It drives the control's
    REAL `IDropTarget` — fetched through a window property the control publishes for exactly this,
    because `RegisterDragDrop` has no getter and the alternative was a second, test-only drop path
    that could pass while the shipping one is broken — with its own `IDataObject`. It pins: an
    external `CF_UNICODETEXT` drop inserts at the drop point and raises a **synchronous**
    `EN_CHANGE` (counter moved before `Drop` returned, queue empty); a local MOVE is ONE undo step
    that restores the text exactly and leaves **nothing behind it**; a drop inside the source
    selection is refused at BOTH edges for MOVE and for Ctrl-COPY, edits nothing and pushes no undo
    step; Ctrl maps to COPY; **`CF_HDROP` reaches the parent as `WM_DROPFILES` with a block
    `DragQueryFileW` still parses**, with the buffer untouched — and `CF_HDROP` beats
    `CF_UNICODETEXT` when a drag carries both. Every drop point is far left or far right of a line,
    so not one assertion is a font metric. The test host now calls `OleInitialize`, and its first
    check — "the control has a live drop target" — is what says out loud that the app needs it.

    **MEASURED LIVE, with the switch ON, by real OLE drops** (`scripts` has nothing for this; a
    throwaway harness under `%TEMP%` fabricated the data objects and called `DoDragDrop` with an
    `IDropSource` that drops immediately, and read the live control's text and selection back with
    `EM_EXGETSEL`/`EM_GETTEXTEX` through `ReadProcessMemory`, since those take pointers):
    * **A dropped FILE still opens.** `DoDragDrop` returned `DRAGDROP_S_DROP`/`DROPEFFECT_COPY` and
      the log printed `Opening externally-delivered path:` then `Opened file:` — i.e. it went
      through `requestOpenPath` and the guard, not through some new path. **Also re-measured on
      `--richedit`**, where it still falls through to the main window: both editors open a dropped
      file, which is the property that had to survive.
    * **A drag-MOVE.** `fn main() {…}` with `n main() {` selected, dropped past the end →
      `DROPEFFECT_MOVE`, text `f\n    let x = 1\n}\nn main() {`, moved run selected,
      **`EM_CANUNDO` = 1**. One `ID_UNDO` restored the original text byte-for-byte and left
      `EM_CANUNDO` = 0 — one step, no half-move behind it.
    * **The prompt still appears.** After a drag-move, `WM_CLOSE` raised the `SentinelSaveDlg`
      Save/Don't Save/Cancel dialog — and the log did **not** contain
      `Dirty flag missed a change…`, which `confirmSaveIfDirty` writes whenever it has to
      re-derive `g.dirty` itself. Absent = `EN_CHANGE` had already fired and `onEditChanged` had
      already set it. `ID_SAVE` (gated on `g.dirty`) then wrote the moved text to disk, which is
      the same fact from the other side.
    * **Dropping inside the source selection changed nothing**: `DROPEFFECT_NONE`, identical text,
      identical selection, `EM_CANUNDO` still 0 **and `EM_CANREDO` still 1** — the redo stack was
      not even cleared, so `recordPreEdit` never ran.
    * Ctrl-drag reported `DROPEFFECT_COPY` where the same drag without it reported `MOVE`.
    All of it on a **scratch copy** under `%TEMP%`; `git status --porcelain examples/` empty
    throughout.

    **Was not covered by any harness; NOW COVERED BY HAND.** The drag threshold,
    `DoDragDrop`'s loop as a user drives it, `QueryContinueDrag`/`GiveFeedback` and the drop
    caret all need a real mouse on a foreground window, which an automated session cannot
    arrange. A human dragged a selection on the released 0.1.9.185 build on 2026-09-04 and it
    behaved. Still genuinely untested: the EDGE AUTOSCROLL during a drag (dragging past the
    window edge), which nobody has exercised either way.

    **THE RICHEDIT EDITOR IS DELETED — the last slice, and phase 46 is now closed.** What went:
    `highlight()` and the three functions that existed only to feed it (`colorForSpan`,
    `applyColor`, `applyBackColor`); the RichEdit halves of `clearErrorMarks` and
    `markErrorLines`; `editorDoc()` / `suspendUndo()` / `resumeUndo()` and the `ITextDocument`
    they cached, with `<richole.h>` and `<tom.h>`; `g.d2dEditor`, `g.highlighting` and
    `g.textDoc`; `Settings::d2dEditor` and the `[editor] d2d` read; `wantD2DEditor()` and the
    `--richedit` / `--d2d-editor` flags; and the Settings checkbox with its hint.
    **Net −154 lines across `MainWindow.cpp`, `SettingsDialog.cpp` and `Settings.h`**
    (−101 / −37 / −16, i.e. 2152→2051, 240→203, 110→94); −81 across all five source files, once
    the D2D addition below is counted. `src/editor/SyntaxLexer.{h,cpp}` did not change by a
    character — slice 4's whole point — and is now consumed only by the control's `drawContent`;
    `MainWindow.cpp` no longer includes it.

    **WHAT DID NOT GO, and misreading this breaks the app: `msftedit.dll`, `MSFTEDIT_CLASS` and
    `styleEditor`.** RichEdit was doing two jobs and only one of them was the editor. The
    **Output pane (`g.hOut`) is still a real `RICHEDIT50W`**, still needs the unconditional
    `LoadLibraryW(L"Msftedit.dll")` at the top of `createControls`, and still carries the
    `EN_LINK` → `parseDiag` → `gotoLineCol` chain that makes `file:line:col` in build output
    clickable. `styleEditor` now serves both panes with no undo guard — on the Output pane it is
    the dark theme, on the editor its `SCF_ALL` arm is the ONLY channel by which Settings ▸ editor
    font reaches the control (`g.hEdit` is sent no `WM_SETFONT` anywhere in the program).

    **THE DECISION THE SLICE FORCED: what happens when the editor cannot be built.** Until now
    `createControls` fell back to RichEdit when `registerD2DEditorClass` failed. With the fallback
    deleted the only alternative to failing is a window that opens with a dead editor, so the
    answer is split by *which* failure, because the two have opposite characters:
    * **Class or window creation fails → FATAL, loudly.** `createControls` returns `false`,
      `WM_CREATE` returns `-1`, `CreateWindowExW` fails and `runApp` exits non-zero — after an
      Error log line carrying `GetLastError` and a `MB_ICONERROR` box naming the failure. Nothing
      is ever shown. An IDE whose editor does not exist has no degraded mode worth offering, and
      the failure would otherwise be discovered by typing into a void. The box takes **no owner**
      and `MB_TASKMODAL`: it runs inside the main window's `WM_CREATE`, so owning it to that
      half-built, about-to-be-destroyed window would pump messages into a `WndProc` whose controls
      do not all exist yet.
      **MEASURED, by negative control** — `if (true || !registerD2DEditorClass(...))` forced into
      `createControls`, rebuilt, launched detached: the log carried
      `FATAL: the editor window class could not be registered (GetLastError=0)`, the message box
      came up with exactly the intended text, `IsWindowVisible` on the main window was **False**
      (it never appeared), and after clicking OK `GetExitCodeProcess` returned **1**. Reverted;
      `grep` for the control is clean and the tree rebuilt `BUILD_OK`.
    * **The D2D *device* fails → explain in place, keep retrying.** This one is lazy (first paint),
      routinely transient (GPU reset, an RDP session change) and already self-heals on the
      control's 250 ms retry timer, so killing the process would be wrong. But nothing else paints
      this control, so a *persistent* failure was exactly the silent blank pane the brief forbids.
      `D2DEditor.cpp::paint` now tracks how long the failure has run and, past
      `kDeviceFailQuietMs` (1 s — long enough that a blink never flashes a notice), draws a **GDI**
      explanation over the client area saying the text is not lost and the file on disk is intact,
      and logs once per episode at Error. That, plus repairing the comments in that file which
      named functions this slice deleted, is the **+72** net in `D2DEditor.cpp`.

    **A stale `[editor] d2d=0` cannot strand anyone**, which is the migration hazard here: 0.1.8
    and 0.1.9 both wrote that key, and this machine had one. Nothing reads it any more — that
    alone is what makes it inert — and `saveSettings` additionally **deletes** it
    (`WritePrivateProfileStringW(..., nullptr, ...)`) so the file stops carrying a setting the
    program no longer has. Verified by planting `d2d=0` by hand and launching: the log said
    `Editor: Direct2D control created`, `GetDlgItem(IDC_EDIT)` reported class `SentinelD2DEditor`,
    it held 9 lines of the opened file, and typing armed `EM_CANUNDO`. The key was gone from
    `settings.ini` afterwards.

    **WHAT WAS KEPT ON PURPOSE, so the next session does not "finish the job" wrongly.** The
    control still answers `WM_SETREDRAW`, `EM_EXGETSEL` and `EM_GETOLEINTERFACE` even though the
    host now sends none of them (every sender was in the deleted colouring path). They are part of
    the RichEdit dialect this window class advertises and `d2d_dialect` pins them; dropping
    `EM_GETOLEINTERFACE` in particular would leave a future caller's out-param uninitialised.
    `st->redrawOff` therefore sits at 0 for the life of the window — that is expected, not dead
    code left by accident.

    **Verified.** `BUILD_OK`, zero warnings, `ctest` **11/11** with the `EXCLUDE_FROM_ALL` targets
    rebuilt first (they are not in `all`; run them stale and you test the old binary). Then, live,
    driven through Win32 messages from a background session:
    * **The Output pane still works, EN_LINK included.** A scratch project under `%TEMP%` whose
      entry fails to compile; `notes.sentinel` open in the editor so the diagnostic named a
      *different* file. Build → `broken.sentinel:4:18` in the Output pane. Then the pane was
      **actually clicked** — `WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/`WM_LBUTTONUP` on a grid, so RichEdit
      itself did the `CFE_LINK` hit-test — and on the click at client (52,98) focus moved from the
      Output pane to the editor and the log printed
      `Opened file: …\broken.sentinel`. That is the whole chain, end to end, not a simulated
      notification. **The diagnostic line is scrolled out of view by default** (`outAppend` ends
      with `WM_VSCROLL SB_BOTTOM`), so a naive click sweep finds nothing — scroll the pane up
      first if you repeat this.
    * **The unsaved-changes prompt still appears.** One `WM_CHAR` into the editor, then `WM_CLOSE`
      → `SentinelSaveDlg` with "Save changes to “crypto.sentinel”?" and Save / Don't Save / Cancel.
    * **`saveFile` still writes CRLF and round-trips byte-identically.** On a **scratch copy** of
      `examples/crypto.sentinel` under `%TEMP%` (426 bytes, 12 CRLF, 0 bare LF): Ctrl+S with
      nothing changed wrote nothing at all (`ID_SAVE` is gated on `g.dirty`, and no `Saved:` line
      appeared); typing `X` and saving gave 427 bytes, **still 12 CRLF and 0 bare LF**; one
      Backspace and a second save returned the file to the original **SHA-256**.
      `git status --porcelain examples/` empty throughout.
    * **The Settings dialog was screenshotted and read**, not just rebuilt — this exact defect
      (a clipped or gapped row) shipped twice, in slices 5 and 6. Editor font → Theme → Log level
      → Log file → BUILD TOOLCHAIN → snc → MSVC env → Cancel/OK, evenly spaced at 48 px, nothing
      clipped, no gap where the checkbox was, and the dialog is correspondingly shorter (client
      456×332). It closes on its own because every row is placed from a running `yy` and the
      window is sized from it — `powershell -File scripts\capture.ps1 -Class SentinelSettingsDlg`
      works from a background session, unlike a capture of the D2D editor.

    **VERIFIED BY HAND on 2026-09-04, on the released 0.1.9.185 build** (the per-machine install at `C:\Program Files\Sentinel-IDE`, reached by a real in-app update from 0.1.4.151 — five releases in 22 seconds, which also closed the "nobody has ever confirmed a client INSTALLS this" gap that had stood since v0.1.0). A human pressed the keys and moved the mouse:
    - **Ctrl+Z**, **Ctrl+Y**, **Ctrl+S** — real keypresses, the hardware -> Windows -> `TranslateAcceleratorW` link that `d2d_dialect` case 13 explicitly cannot reach.
    - **Dragging a text selection** — the drag threshold, `DoDragDrop`'s modal loop, `QueryContinueDrag`/`GiveFeedback` and the drop caret, none of which a harness can drive.
    - **Dropping a FILE on the editor with a dirty buffer** — it opened AND raised the Save prompt, which is the regression the text drop target could most easily have caused.
    - Ctrl+S on the signed `examples/crypto.sentinel` left it byte-identical at 426 bytes (`git status examples/` clean), independently re-checked.

    Phase 46 therefore has **no untested edges left**.

47. **The appcast reader moves to Sentinel — the last parser out of C++, and the only one fed by
    the NETWORK.** `parse_appcast(body, mine)` in `src/sentinel/parsers.sentinel` replaces
    `Updater.cpp`'s `parseVersion` + `versionIsNewer` + `appcastVersion` in ONE crossing (the check
    runs hourly and on a menu click, so a single FFI call is free and it moves all three). The
    WinINet `fetchAppcast` **stays in C++** — porting a thin Win32 wrapper would move the FFI
    boundary, not any logic; native fetch, Sentinel parse, the same split the other four ports use.
    `readAppcast` returns `{found, valid, newer, version}` and the flags nest, so a caller that only
    consults `newer` is already fail-closed.
    **This is not a transcription — the C++ it replaces had two real defects**, and moving the code
    is what made someone read it. (a) `out[i] = out[i] * 10 + (*v - '0')` accumulated into an `int`
    over however many digits the feed supplied. UB, and in practice a wrap: measured, a feed saying
    `sparkle:version="0.1.2147483648.0"` makes the shipped reader compute component **-2147483648**
    and rank a hugely higher version BELOW `0.1.12.196`, silently suppressing it; a feed saying
    `"99999999999999"` computes **276447231** and is OFFERED as newer. Components are now capped at
    9 digits (≤ 999,999,999) and an over-long one **rejects the whole version** rather than wrapping
    or truncating — inventing a number the feed never stated is what decides an update the wrong way.
    (b) `appcastVersion` returned whatever sat between the quotes, unchecked, up to the ~256 KB fetch
    cap — and it was then shown in the offer dialog, logged, and on **Skip this version** written
    into `settings.ini`. Measured: `sparkle:version="99 red balloons"` was OFFERED as an update
    (`99 > 0`) and that string was the one persisted. The version is now validated — 1–4 components,
    1–9 digits each, single dots, nothing else — and the bytes are returned **only when valid**, so
    an unvalidated feed string cannot reach a dialog, a log line or the ini file at all.
    **FIRST match, not highest**, deliberately: our feed carries one `<item>` (make-appcast.ps1
    emits one and enforces `^\d+\.\d+\.\d+\.\d+$`), first-match is what shipped, and it bounds
    what a 256 KB body can steer us to — where "highest" would let any entry anywhere in it decide.
    Nothing here is the security gate anyway: this only decides whether to OFFER; WinSparkle re-reads
    the feed, downloads, and refuses anything that does not verify against the compiled-in key.
    `tests/appcast_xcheck.cpp` (25 cases, `ctest -R appcast_parity`) runs THREE implementations on
    every case — the verbatim shipped C++ as oracle, the verbatim `#else` fallback, and Sentinel —
    and each case declares Parity or **Diverges** with a reason. A Diverges case FAILS if the two
    ever agree again, so the old behaviour cannot be quietly restored and stay green. Also pinned:
    `0.1.9` vs `0.1.10` (the trap the v0.1.10 release row calls out), `0.1.6 == 0.1.6.0`, a GitHub
    404 page as the body, and the real `appcast.xml` still parsing as a well-formed version
    (read out of the feed, not pinned — see phase 48).
    **Verified on the live feed**, not just in the test: a manual ≡ ▸ Check for Updates… on build
    200 fetched `raw.githubusercontent.com` over real HTTPS and reported *"Sentinel-IDE 0.1.12.200
    is up to date"* — the path that silently broke for four releases still works.

48. **The sealed-container FRAMING moves to Sentinel — attacker-chosen bytes, parsed before
    anything is authenticated.** `parse_seal_header(file)` and `parse_seal_archive(archive)` in
    `src/sentinel/parsers.sentinel` (exports 7 and 8, the first BINARY readers of the eight)
    replace the parsing halves of `Seal.h::unsealProject` and `Seal.h::sealExtractArchive`, plus
    `sealUnsafeRelPath`. **TWO exports because they run at different times**: the header is read
    BEFORE decryption (it is what says where the salt and the wrapped DEK are); the archive index
    only exists AFTER the DEK is unwrapped and the payload decrypted. They cannot be one call.
    **THE CRYPTO DID NOT MOVE, and that is the design, not a compromise.** No password, salt, KEK
    or DEK crosses the FFI boundary; every CNG call and every `SecureZeroMemory` is untouched.
    Sentinel gets the bytes that contain no secret and hands back **byte offsets** — to the salt,
    the wrapped DEK, the nonces, the tags and each file's data — and the host reads from those
    offsets exactly as it did. The crypto core stays blocked on **R1 (no secure-zero for
    `[secret u8]`)**, where porting it would be a net security *regression*; the framing has no such
    problem, which is precisely why it was separable. If a future change finds key material moving
    into Sentinel, that is a misread of this split.
    **Why this code and not another reader:** it was the last place in the IDE where
    attacker-chosen bytes met hand-rolled pointer arithmetic **before anything was authenticated**.
    A `.sealed` file is fully attacker-controlled — the feature exists so someone can SEND you a
    sealed project — and GCM proves only that the bytes did not change in transit, not that the
    sealer was benign. Phase 31 found five real defects here; the wrapping ones carried hand-written
    mitigations in comments, and those properties are now structural.
    **Two defects close by construction, and both are asserted as divergences, not parity:**
    (a) **THERE IS NO `u64` IN SENTINEL** — verified against `crates/sentinel-types` (`I64 | I32 |
    U8 | U128 | F64 | Bool`) and against the whole of `sentinel_library`, which contains the token
    `u64` **zero** times. Every length in this container is a u64 on disk, so a hostile
    `archive_size`, `payload_len` or `data_len` of 2^63 or more reads back NEGATIVE. `rd_u64` tests
    the top byte BEFORE accumulating, so it cannot overflow for any input at all, and returns −1
    rather than a wrapped value; callers refuse, never clamp. For `archive_size` that is a
    **behaviour change**: the shipped reader accepted `0xFFFFFFFFFFFFFFFF` and handed it to
    `sealDecompress` as the output-buffer size — measured end-to-end, the ported build now answers
    *"Not a sealed project (bad header)"* instead. In a v2 file the AAD would have caught it; a v1
    file has no AAD at all, and v1 files are still read.
    (b) **NOTHING IS WRITTEN UNTIL EVERYTHING IS CHECKED.** `sealExtractArchive` parsed and wrote in
    one loop, so an archive whose last entry was `..\evil` had already dropped every earlier file
    into the destination before it refused — and `openSealedProject`'s failure cleanup is a
    `RemoveDirectoryW`, which only removes an EMPTY directory, so they stayed. The parse now
    validates the whole index — every bound, every path — and the host writes only after it returns
    clean. Measured: the shipped extractor left 3 files behind on that archive; the port leaves 0.
    (c) **THE COLON IS REFUSED ANYWHERE**, not only at position 1 as the pre-port guard had it.
    Measured: `ab:c` passed that guard, and on NTFS it writes an ALTERNATE DATA STREAM `c` on a
    zero-length file `ab` — content a tree walk of the unsealed project never shows. It cannot cost
    a real file (':' is illegal in a Windows filename, and every archived path came from
    `FindFirstFileW` walking a Windows directory), and it is also the one rule where "second byte"
    and "second character" are different questions, so asking it this way is what makes the UTF-8
    guard exactly as strong as the UTF-16 one instead of nearly so.
    **The `..` test is PER COMPONENT**, and stays that way: `notes..txt`, `v1..2.md` and
    `sub\ok..name.txt` all still unseal (the phase-31 regression, re-pinned in both tests), while
    `..`, `../evil.txt`, `sub/../../x`, rooted and drive-qualified paths are refused.
    **One documented divergence at the site:** the C++ guard runs on the UTF-16 string after folding
    `/` to `\`; Sentinel guards the UTF-8 bytes. For the separator and `..` rules they cannot
    disagree — every UTF-8 continuation byte is ≥ 0x80, so `\`, `/`, `:` and `.` can only appear as
    themselves, and `MultiByteToWideChar` without `MB_ERR_INVALID_CHARS` maps an invalid sequence to
    U+FFFD, never to a separator. The colon was the one rule where a byte-indexed test would NOT have
    matched (a multi-byte first character puts a continuation byte at index 1), which is why (c)
    above asks it differently rather than transcribing it. Argued in full above `seal_unsafe_rel`,
    following `save_manifest`'s precedent.
    **Failure is a reason CODE, not a string**: `sealHeaderMessage` maps each to a message
    `unsealProject` already had, so the port added **no new user-facing text**. Payload-framing
    failures are deliberately held back until after the unlock attempt, so a file that is both
    truncated and given the wrong password still says "wrong password" — the order the messages came
    out in before.
    `tests/seal_xcheck.cpp` (**53 cases**, `ctest -R seal_parity`) reuses `seal_test.cpp`'s corpus
    directly — it seals a real three-file tree with the real `sealProject`, then patches bytes at the
    same named offsets — and runs THREE implementations on every case: the shipped C++ at 72a6b82 as
    oracle, `Seal.h`'s own `#else` fallback, and Sentinel. Each case declares Parity or **Diverges**
    with a reason, and a Diverges case FAILS if the two ever agree again. The archive cases compare
    the **directory tree each side leaves behind**, not just the verdict, because (b) differs only in
    what got written before the refusal. `tests/seal_test.cpp` (25 assertions) is unchanged and still
    green against the restructured C++ fallback. **Verified end-to-end on the shipping
    configuration** (`SENTINELIDE_SENTINEL` + `parsers.lib`), not only in the parity test: an
    11-file project sealed and unsealed with every path and every SHA-256 identical; a hostile
    `data_len` of 2^64−1 inside a container that AUTHENTICATES correctly, refused after decryption
    and before any write; an unknown v2 slot type 99 stepped over with the password slot behind it
    still unlocking.
    **Also fixed here, and unrelated to the port:** `tests/appcast_xcheck.cpp` cases 23–25 pinned the
    published feed as the literal `0.1.12.196`, so the `0.1.13.202` release commit turned
    `appcast_parity` red without touching anything the test is about — it was already failing at
    HEAD. Those cases now read the version out of the feed (via the oracle's own extractor, not the
    code under test) and assert the RELATION: well-formed, same is not an update, one build older is,
    one build newer is not. A release bump cannot break it again.

49. **FIND AND REPLACE — the gap that had stood for fifteen releases.** There was no Ctrl+F
    anywhere in this product; `D2DEditor.cpp`'s dialect comment said so outright ("there is no
    Find/Replace and no printing in this product"). There is now: **Ctrl+F**, **Ctrl+H**,
    **F3 / Shift+F3**, wrap-around, a live match count, an explicit *No results* state, Match
    case and Whole word, Replace and Replace All, and Escape back to the editor. Four files:
    `src/editor/TextSearch.{h,cpp}` (the matcher), `src/host/win32/FindBar.{h,cpp}` (the bar),
    plus `EditorModel::replaceRanges`, four calls on `D2DEditor.h` and the wiring in
    `MainWindow.cpp`.

    **THE SENTINEL-VS-C++ DECISION, WITH THE NUMBER. The matcher is C++, and the reason is not
    the FFI boundary.** A complete Sentinel `find_matches` was written and compiled (`snc build
    --lib`, first try) before anything was decided, and benchmarked against the C++ core on the
    same buffers — full parity on every case, 20 cases × 4 corpus sizes, identical ranges.
    On a **1 MB buffer with the default case-insensitive search — one find-as-you-type
    keystroke — C++ takes ~2 ms and Sentinel ~14 ms**; at 5 MB it is ~11 ms against ~50 ms.
    A second Sentinel version given three optimisations (the match-case branch hoisted out of
    the scan, the fold inlined, an ASCII fast path that tests one byte per position) got to
    ~10 ms at 1 MB — still 5x, and still most of a 16.7 ms frame spent before the editor has
    laid out or painted a single line. So: **C++**.

    **The brief's hypothesis was that the crossing would be the cost, and that is FALSE —
    measured.** An export that scans nothing, handed the same 5 MB buffer, costs **0.00014 ms
    per call** (140 ns): the `&[u8]` parameter is a pointer and a length, so there is no copy
    in, and the owned `-> [u8]` return is no more expensive at 49,696 matches (a 795 KB record)
    than at zero — `let` over 5 MB and an absent needle over 5 MB time the same. The whole gap
    is the SCAN: snc's generated code is ~5-10x slower than MSVC /O2 on a tight indexed loop.
    That is a useful, specific finding about the toolchain and not a reason to avoid the FFI.
    **The port would also have cost a behavioural divergence**, which is worth stating because
    it would have been the argument even at parity speed: whole-word search uses
    `EditorModel`'s own `iswalnum(c) || c == '_'`, a CRT locale table with no Sentinel
    equivalent, so a Sentinel matcher has to approximate it (">= 128 is a word character") and
    Ctrl+Left would then disagree with Whole word about where a word ends.
    **The buffer is passed as UTF-16-AS-BYTES, not converted**, and that decision is what made
    the crossing free — a UTF-8 conversion would have been an O(document) pass per keystroke
    each way, plus a second index space in which a surrogate pair can be cut. Worth reusing if
    a future port needs the editor's buffer.

    **A modeless BAND, not a modal dialog, and there are four independent reasons** (written
    out in `FindBar.h`): a modal disables the main window, i.e. the editor the feature is about;
    `uiIsBusy()` would then report busy for as long as the user is working, re-arming the
    4-second deferral on the open-path and update-offer paths over and over; a modal's private
    pump never calls `TranslateAcceleratorW`, so Ctrl+S, Ctrl+Z and F5 would all be dead inside
    it; and its null-filter pump would dispatch the synchronous `EN_CHANGE` a Replace All
    raises. The cost is that `runApp` now calls `IsDialogMessageW` for the bar — **gated on the
    focus being inside it**, because called unconditionally it would swallow Tab and Escape
    aimed at the editor, and Tab is a character in a code editor.

    **Replace All is ONE undo step**, via `EditorModel::replaceRanges` — one `recordPreEdit`,
    one buffer rebuild, one `Ctrl+Z`. Spelled as a loop in the control it would have been one
    step per match. It refuses malformed input (descending, overlapping, out of bounds) rather
    than half-rewriting a file, and a rewrite whose result is IDENTICAL pushes no undo step and
    raises no notification — checked against the built result, not guessed from the arguments,
    so a needle that merely folded to the replacement is caught too.

    **TWO DEFECTS FOUND BY THE TESTS, both silent, both fixed:**
    (a) **The bar's child-control ids started at 1, colliding with `IDOK` (1) and `IDCANCEL`
    (2).** An EDIT sends `EN_UPDATE` with the same id and a different notification code, so
    every character typed into the find field fell through the `WM_COMMAND` switch into
    `case IDOK:` and ran Find Next — find-as-you-type walking forward through the file by one
    match per keystroke. No crash, no warning, and plausible on a small file. Ids now start at
    101; Windows reserves 1-11 for the standard dialog buttons and nothing sharing a
    `WM_COMMAND` switch with `IsDialogMessageW` may use them.
    (b) **`IsWindowVisible` was the wrong question for "is the bar open".** It is false whenever
    any ANCESTOR is hidden, so it reported closed during the main window's `WM_CREATE` (where
    `layout()` runs, and would have given the band no height) and for the whole of
    `d2d_dialect_test`, whose host window is deliberately never shown — which silently turned
    `hideFindBar` into a no-op there. Open/closed is now the bar's own flag.

    **A comment was measured wrong and corrected.** The accelerator table lists
    `{FVIRTKEY|FSHIFT, VK_F3}` and `{FVIRTKEY, VK_F3}`, and the first version of the comment
    beside them claimed the order was load-bearing — that an unshifted row swallows the shifted
    chord. It does not: `d2d_dialect_test` case 13 now builds the two rows in BOTH orders and
    presses Shift+F3 against each, and it resolves to Find Previous either way. **ACCEL
    modifiers are matched exactly.** Both rows are still needed; the order is readability.

    **CASE-INSENSITIVITY IS ORDINAL AND PARTIAL, stated rather than implied.** The fold is
    ASCII `A-Z` plus Latin-1 `U+00C0-U+00DE` (minus `U+00D7`, the multiplication sign — folding
    it would make × match ÷). So `CAFÉ` finds `café`, which is the case
    `EditorModel.cpp`'s own comment says Sentinel sources really hit. **What is NOT
    implemented, deliberately:** Latin Extended-A and beyond (Ł/ł, Ş/ş), Greek and Cyrillic
    (Σ/σ, Д/д), full case folding (ß vs SS), `ÿ`/`Ÿ` (the one Latin-1 letter whose uppercase
    escapes the block), the Turkish dotless-i pair, and **any normalisation** — a precomposed
    `é` does not match a decomposed `e`+`U+0301` in either case mode. The alternatives were a
    Unicode case-folding table this project has no other use for, or `CompareStringOrdinal`,
    which would drag the OS into a file whose whole value is having no OS in it. Every one of
    these non-folds is a `check()` in `text_search_test` case 3, so widening the rule means
    coming to that test and saying so.

    **Surrogate pairs.** A match whose start or end would land between a high and a low
    surrogate is DISCARDED, never trimmed — exactly `EditorModel::snap`'s condition, so "a
    match is never split" and "the caret is never snapped" are one rule rather than two that
    happen to agree. That matters because a find field really can hold half a pair (paste half
    an emoji, or an IME): the answer is *no match*, not half a codepoint selected and replaced.

    **Verified.** `BUILD_OK`, zero warnings, **ctest 14/14** with every `EXCLUDE_FROM_ALL`
    target rebuilt first. New: `text_search_test` (**78 assertions**, `ctest -R text_search`) —
    the matcher and `replaceRanges`, no window; `d2d_dialect_test` grew case 15 and the
    accelerator rows (**245 assertions**, up from 218), driving the REAL find bar with WM_CHAR
    into its real EDIT and BM_CLICK on its real BUTTONs.
    Then, **live, against the shipping `Sentinel-IDE.exe`**, driven cross-process from a
    background session (`VirtualAllocEx` + `ReadProcessMemory` for the pointer-taking messages;
    a 24 KB, 722-line scratch file under `%TEMP%`, `git status --porcelain examples/` empty
    throughout) — **31/31**:
    * Ctrl+F raised a real `SentinelFindBar` whose bottom edge is exactly the editor's top edge
      — it takes a strip, it does not overlap.
    * Typing `let` reported **1 of 241**, selected the match (3 chars), and three Nexts moved
      both the selection and the counter. `ZEBRAMARKER` near the end of the file selected at
      24617..24628 and took the **first visible line from 0 to 695** — scrolled into view
      through `ensureCaretVisible`, not a parallel mechanism.
    * **Wrapping**: walked to `241 of 241`, one more Next gave `1 of 241`; Previous from there
      gave `241 of 241`.
    * **No results** on a needle that is not in the file, and nine more keystrokes from that
      state moved nothing. (Note what is NOT claimed: reaching a no-match does not leave the
      caret where it started, because a bare `q` DOES occur in the file and find-as-you-type
      legitimately visits its matches on the way. Every editor behaves this way.)
    * **F3 as a posted WM_KEYDOWN**, stepping the search through `TranslateAcceleratorW` in the
      live process — the accelerator link case 13 can only reach by faking the key state.
    * **Surrogates**: a lone low surrogate in the find field → *No results*; the whole
      character → `1 of 2`, selected 6..8, i.e. both code units on the pair's own boundary; and
      Replace All over both left `alpha @ beta @ gamma`.
    * **Replace All of 241 occurrences → `Replaced 241`**, buffer 24,635 → 25,840 chars, ONE
      undo restored it to **24,635 chars identical to the original** with nothing behind it.
    * **THE DATA-LOSS PATH, measured rather than assumed**: after a Replace All, `WM_CLOSE`
      raised the **`SentinelSaveDlg`** Save / Don't Save / Cancel prompt. `EN_CHANGE` really
      does reach `onEditChanged` and `g.dirty` really is recomputed, so the buffer is not
      discardable in silence.

    **NOT COVERED, said plainly.** The bar's PIXELS — the band, the dark field, the red
    *No results* — are not asserted anywhere: `d2d_render_test` renders the editor, not this
    bar, and `capture.ps1` needs a foreground window an automated session cannot arrange. Nor
    is a real mouse click on a button (`BM_CLICK` sends the same `WM_COMMAND` a click would,
    but not the hit-test or the pressed state), the Tab cycle between the fields and the
    buttons (`IsDialogMessageW` is exercised for Escape only), or the hardware-to-Windows link
    for Ctrl+F and Ctrl+H. **A human should press Ctrl+F, Ctrl+H, Tab and Shift+F3 once on a
    real build and look at the band** — that is the same short list phase 46 closed by hand.

See `docs/prototype.md` and `docs/sentinel-project.md` for detail; `docs/RELEASING.md` for the
release + update-signing procedure.

**Screenshot coverage is partial, despite "screenshot-verified" above:** `docs/screenshots/` holds
14 PNGs covering phases 1–11, 13, 15 and 39. Phases 12, 14 and 16–38 were verified live during their
sessions but no image was committed — treat their screenshots as absent, not lost.

50. **The trust chip stops overstating itself.** The status-bar chip read **✓ Signed** while you
    edited a signed file. `signState` comes from an async `snc verify` of the FILE ON DISK
    (`refreshSignState`), so the instant the buffer went dirty the green tick was vouching for text
    that nothing had checked — the exact defect shape this project keeps finding, in the one
    indicator whose entire job is trust. It had sat open in *What's next* for sessions because the
    obvious fix does not work: `refreshSignState` spawns a process, so calling it from
    `onEditChanged` would be one `snc verify` **per keystroke**. Nothing needs re-verifying — the
    answer is known for free the moment the buffer differs from what was signed. The chip now reads
    **✎ Edited since signing** (amber) while `g.dirty && signState == Signed`, and saving re-runs
    the real verify. **Only the Signed case is overridden:** an INVALID signature does not become
    more invalid because you typed, and Unsigned stays unsigned — in both the chip is still true of
    the file on disk, so changing them would trade one inaccuracy for another. `rStatus` joined the
    invalidation list in `onEditChanged`; without it the chip kept its old text until something
    unrelated repainted the status bar, and an indicator that is only right after an unrelated
    repaint is not an indicator.

51. **THE FILE CHANGED UNDERNEATH THE EDITOR AND NOBODY SAID SO.** The last gap in the
    unsaved-changes family, open since phase 39 shipped the guard. Nothing in the IDE ever asked
    whether the open file was still the file it had loaded, so a `git checkout`, a formatter, or a
    second editor could rewrite it and the next Ctrl+S would put the stale buffer straight over the
    top — silently, which is the shape of all three defects v0.1.0–v0.1.4 shipped. Four files:
    `src/core/FileStamp.h` (the detector), `src/host/win32/ReloadDialog.{h,cpp}` (the prompt), plus
    the stamp points and the `WM_ACTIVATEAPP` trigger in `MainWindow.cpp`.

    **THE STAMP CARRIES A DIGEST, NOT JUST (mtime, size), AND THAT IS THE DESIGN.** A stat pair
    answers "was this file written", not "is its content different", and in a source tree the two
    diverge constantly: `git checkout` of the branch you are already on, a formatter that rewrites
    identical bytes, a backup tool touching timestamps. Every one of those raises a prompt about a
    file nobody changed — and a prompt that is usually wrong is one users learn to dismiss unread,
    which would destroy the one case that is a real either/or. So the stat pair is the CHEAP FILTER
    (two syscalls, no open handle, and the answer on the overwhelmingly common path) and an FNV-1a
    digest is the ANSWER, computed only when the timestamp or length has already moved. **FNV-1a is
    not a security claim** and the header says so out loud, because this repo ships signature
    verification and someone will ask: a collision means one edit goes unnoticed, i.e. exactly
    today's behaviour with no detection at all, so it fails no worse than the status quo.

    **THE DIGEST IS OVER DISK BYTES ON BOTH SIDES, and that is not an implementation detail.**
    `editorText()` returns the buffer with lone `\r` while the file holds CRLF — `MainWindow.cpp`'s
    own comment says the two "are never compared against each other" — so the obvious comparison
    (buffer vs file) would report *every* file as changed, on the first activation, forever.
    Stamping both sides off disk sidesteps it: nothing in `FileStamp.h` knows how the editor spells
    a newline. This was nearly written the wrong way round; the existing comment is what caught it.

    **A CLEAN BUFFER RELOADS WITHOUT ASKING; ONLY A DIRTY ONE PROMPTS.** The asymmetry is
    deliberate. With nothing unsaved the buffer holds exactly what the file held, so a reload
    cannot lose anything a user typed — the only thing at stake is the view, and
    `reloadCurrentFile` puts the caret back on its line (and its column, but only while that column
    still lands on the same line — a shortened line would otherwise carry the caret into the next
    one, which reads as the reload having moved it). Prompting there would be a question with one
    sane answer asked over and over.

    **KEEP IS THE DEFAULT, AND ENTER OBEYS FOCUS — v0.1.12's defect, transplanted rather than
    rediscovered.** Reload throws away typing that exists nowhere else and that no undo can recover
    (a reload resets the undo buffer); Keep only risks a later, separately-initiated save. So
    `DM_GETDEFID` answers `IDCANCEL` (Keep) — without it `IsDialogMessageW`'s `VK_RETURN` fallback
    sends `IDOK`, and an unattended Enter would discard someone's work — *and* the loop clicks the
    focused button itself, or a keyboard user who Tabbed to Reload and pressed Enter would get
    Keep. Both halves, because v0.1.11 shipped the first and v0.1.12 had to ship the second.

    **Deletion is reported, not prompted about**, and `g.dirty` is deliberately NOT forced to make
    the buffer un-closable: it is a pure function of `(editorText(), savedText)` that
    `onEditChanged` REASSIGNS on the next keystroke, so a forced flag would survive only until the
    user typed — an indicator that stops being true when touched. Save recreates the file, which
    loses nothing. **An unreadable file (locked mid-write) stores no stamp at all**: recording a
    file caught half-written as its own final state would make the real change invisible forever.

    **Verified, and every claim below was run.** `BUILD_OK`, zero warnings, **ctest 15/15** with
    every `EXCLUDE_FROM_ALL` target rebuilt first. New: `file_stamp_test` (**48 assertions**,
    `ctest -R file_stamp`) — real files on disk, no window. Its load-bearing cases are 3 (a
    same-length edit under an IDENTICAL pinned mtime is still Modified), 4 (a rewrite with identical
    bytes under a NEWER mtime is None — the false positive the digest exists to kill), 6 (a file
    held open exclusively classifies Unreadable, not None) and 7 (the digest is stable across the
    64 KiB chunk boundary, and a bit flipped at offset 70,000 is still caught).
    **The tests were checked for teeth, not assumed to have them.** Replacing `changedFrom`'s
    digest comparison with the naive `(mtime, size)` design failed exactly cases 3 and 4; resetting
    the FNV seed per chunk failed case 7 — *including* its "still reported Modified" assertion,
    because a per-chunk reset makes a change outside the final chunk vanish silently. Both reverted
    and re-run green.
    Then, **live against the shipping `Sentinel-IDE.exe`**, driven cross-process from a background
    session (the phase 49 precedent; a scratch file under `%TEMP%`, `git status --porcelain
    examples/` empty throughout) — **25/25, twice**: a clean buffer reloaded with no prompt and an
    edit+save then wrote the EXTERNAL text back (a stale buffer would have written the original over
    it); a dirty buffer raised a real `SentinelReloadDlg`, modal, with both answers found by the
    caption a user reads rather than by control id; Keep kept the edit, saved it over the newer
    file, and **did not ask again on the next activation**; Reload discarded both the kept and the
    live edit and took the file; an identical rewrite under a new mtime raised nothing *with the
    buffer dirty*; and a deleted file raised no prompt, stayed deleted, and came back on Save with
    the live edits intact. Disabling the one `WM_ACTIVATEAPP` line failed 9 of those 25, so the
    driver fails when the mechanism is gone.

    **NOT COVERED, said plainly.** The dialog's PIXELS — the band, the button treatment, the dark
    ground — are asserted nowhere; `capture.ps1` needs a foreground window an automated session
    cannot arrange. Nor is a real mouse click (`BM_CLICK` sends the `WM_COMMAND` a click would, but
    not the hit-test or the pressed state), the Tab cycle, or a real alt-tab producing
    `WM_ACTIVATEAPP` from hardware rather than a posted message. **A human should alt-tab away,
    change the open file in another editor, and alt-tab back once** — the same short list phases 46
    and 49 closed by hand.

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
| `v0.1.8` | 180 | `Sentinel-IDE-0.1.8.180-setup.exe` | Minor. **The Direct2D editor, as an opt-in preview** (phase 46 slices 1-5) — Settings ▸ *Use the Direct2D editor (preview)*, restart required; RichEdit stays the DEFAULT, so the release is behaviourally a no-op for anyone who does not tick the box, which is the entire point of baking it before slice 6 flips it. Carries no-wrap with real horizontal scroll, syntax colouring from a lexer now SHARED with the RichEdit path (so the two cannot drift), painted error-line tints, and a **real system caret** — the painted-only caret was invisible to Narrator, Magnifier's follow-the-cursor and IME candidate lists, a genuine accessibility regression against the control it replaces. Only visible lines are laid out and lexed. Known gap at the time of that release, stated in its release notes and **closed since, in slice 7**: dragging TEXT within the editor was not supported (dropping a FILE always opened it, and still does). |
| `v0.1.9` | 185 | `Sentinel-IDE-0.1.9.185-setup.exe` | Minor. **The Direct2D editor becomes the DEFAULT** (phase 46 slice 6), and **text drag-and-drop** closes the last regression against RichEdit — the two editors are feature-equivalent for the first time, which is the stated precondition for slice 7. Escape hatches intact: `--richedit`, the Settings checkbox, and a `d2d=0` written by 0.1.8 all outrank the new default. **All three are gone at HEAD** — phase 46's last slice deleted the RichEdit editor — so this row is the last release in which unticking was possible. Dropping a FILE on the editor still opens it guarded — the new `IDropTarget` classifies `CF_HDROP` first and forwards it to the host's existing `WM_DROPFILES` handler, because registering a text drop target would otherwise have swallowed file drops silently. |
| `v0.1.10` | 189 | `Sentinel-IDE-0.1.10.189-setup.exe` | Minor. **The RichEdit editor path is deleted** (phase 46 complete) — the Settings checkbox, `--richedit`/`--d2d-editor` and `Settings::d2dEditor` are gone, a stale `[editor] d2d` key is ignored *and* deleted, and there is now exactly one editor. `msftedit.dll` and `MSFTEDIT_CLASS` stay: the OUTPUT pane is still RichEdit and its `EN_LINK` → `parseDiag` → `gotoLineCol` chain is what makes `file:line:col` clickable. Also carries a fix older than the flags it came from: an unrecognised switch used to fall into `else openArg = a` and be mistaken for the path, so `Sentinel-IDE.exe C:\proj --richedit` (which 0.1.9's own notes told people to use) opened an empty window with no message — as did any typo. Unknown switches are now ignored and logged at Warn. **0.1.9 → 0.1.10 is the version-comparison trap**; `versionIsNewer` parses components to ints, so it offers correctly. |
| `v0.1.11` | 193 | `Sentinel-IDE-0.1.11.193-setup.exe` | Patch. **Check for Updates… asks before it installs again.** The menu item had gone straight to download-install-restart since 0.1.5, because WinSparkle 0.9.3's prompt-then-install flow is broken (empty payload path, no error — the defect behind four dead releases). The manual check now runs our OWN appcast poll and raises the themed Install now / Later / Skip dialog, and — the half that matters — REPORTS all three outcomes: offered, already current, or the check failed. Silence was indistinguishable from a menu item that does nothing. |
| `v0.1.12` | 196 | `Sentinel-IDE-0.1.12.196-setup.exe` | Patch. **Enter obeys focus in the update prompt.** 0.1.11 answered `DM_GETDEFID` with IDCANCEL so a stray Enter could not accept an offer nobody was looking at (it had happened — an unattended Enter ran an installer). But `IsDialogMessageW` only routes Enter to the FOCUSED control when it reports `DLGC_DEFPUSHBUTTON`, so a keyboard user who Tabbed to *Install now* and pressed Enter got **Later** — an affirmative keystroke silently doing the negative thing. **Shipped by 0.1.11 because that release went out before its own review finished.** |
| `v0.1.13` | 202 | `Sentinel-IDE-0.1.13.202-setup.exe` | Patch. **The appcast reader moves to Sentinel** (phase 47), closing two real defects by construction: an unbounded `int` accumulate over feed-supplied digits (a crafted version could suppress a real update or fake a newer one) and NO validation of the extracted string, which reached the offer dialog, the log and `settings.ini`. **`appcast_parity` was red at HEAD when this shipped** — the appcast commit moves the feed the test reads, and nothing re-ran ctest after it. `RELEASING.md` now has that step. |
| `v0.1.14` | 207 | `Sentinel-IDE-0.1.14.207-setup.exe` | Minor. **The sealed-container framing moves to Sentinel** (phase 48) — the first port that parses genuinely hostile input rather than files the IDE wrote. Closes an alternate-data-stream hole (`ab:c` passed the traversal guard and wrote a hidden NTFS stream), partial extraction leaving files behind on a rejected archive, and two DoS bounds (a 206-byte container could commit 65,667 MB; 11,400 unlock slots ≈ 17.7 min of PBKDF2 on the UI thread). |
| `v0.1.15` | 213 | `Sentinel-IDE-0.1.15.213-setup.exe` | Minor. **Find and Replace** (phase 49), absent for fifteen releases. Ctrl+F / Ctrl+H, F3 / Shift+F3, wrap, live match count, *No results*, Match case, Whole word; Replace All is ONE undo step. Plus **the trust chip stops claiming the buffer is signed after you edit it** (phase 50). |

**0.1.10 IS PUBLISHED (2026-09-04) and PHASE 46 IS COMPLETE.** Tag `v0.1.10` points at **44322fe** (`rev-list` 89 + `BUILDBASE` 100 = build **189**). Same order of checks as 0.1.9 — no debug CRT, *Valid signature*, the **enclosure confirmed HTTP 200 at 3,619,304 bytes against both the appcast `length` and the local installer BEFORE the feed was pushed**, then the live feed. One extra check this release needed: **0.1.9 → 0.1.10 is the version-comparison trap** — a string compare makes `0.1.10` look OLDER than `0.1.9` and clients would never be offered it. `Updater.cpp::versionIsNewer` parses each component to an int, so `[0,1,10,189]` beats `[0,1,9,185]`; verified before publishing rather than discovered from silence afterwards.

**0.1.9 IS PUBLISHED (2026-09-04).** Tag `v0.1.9` points at **7f88a27**, the clean tree the binary
was built from (`rev-list --count` 85 + `BUILDBASE` 100 = build **185**). Checks run, in the order
that matters: `dumpbin /DEPENDENTS` showed no debug CRT; `sign-release.ps1` reported *Valid
signature* against the key compiled into `Updater.cpp`; the **enclosure was confirmed HTTP 200 at
3,618,031 bytes — matching the appcast `length` AND the local installer — BEFORE the feed was
pushed**; then the live feed itself, serving `sparkle:version="0.1.9.185"`. ctest 11/11.
Notes at `docs/release-notes-0.1.9.md`.
**Not verified, as with every release so far: that a real client installs it.** The steps above
verify the OFFER path only, which is exactly all anyone verified for v0.1.0-v0.1.4 — every one of
which offered updates that then installed nothing. The install check needs a throwaway client
stamped below the feed; see the box at the end of `docs/RELEASING.md`.

**A note on the environment, because it cost an hour and will recur.** The v0.1.9 build was blocked
by `build\Sentinel-IDE.exe` becoming a **0-byte stub** on the `G:` share after a failed link: it
could not be relinked, deleted, renamed or moved (`LNK1104`), with **no process holding it** —
verified by walking every process's `MainModule` against that path, not assumed. The code was never
at fault: the same objects linked fine to local disk, and `ctest` stayed 11/11 because the test
targets link `D2DEditor.cpp` themselves rather than the app exe. If `LNK1104` appears with no
obvious holder, suspect the share, not the build.

**Still owed before cutting it, and no harness here can do them:** drag text once with a real
mouse, and press **Ctrl+Z / Ctrl+Y / Ctrl+S** — every check across slices 3-6 posted the
`WM_COMMAND` that `TranslateAcceleratorW` produces, which is the code those slices changed but is
not the keyboard. Also uncovered: the drag threshold, `QueryContinueDrag`/`GiveFeedback`, and edge
autoscroll during a drag.

**0.1.8 IS PUBLISHED (2026-09-04).** Tag `v0.1.8` points at **55ae4bd**, the clean tree the
binary was built from (`rev-list --count` 80 + `BUILDBASE` 100 = build **180**), so it rebuilds
from its tag. Checks that were actually run, in this order, because the order is the point:
`dumpbin /DEPENDENTS` listed only Windows system DLLs plus `WinSparkle.dll` — no debug CRT, the
check four early releases failed; `sign-release.ps1` reported *Valid signature* against the key
compiled into `Updater.cpp`; the **enclosure was confirmed HTTP 200 at 3,614,330 bytes, matching
the appcast `length` and the local installer byte for byte, BEFORE the feed was pushed** — a 404
enclosure is the one failure that makes every client see an update it then cannot download; then
the feed itself, live at `raw.githubusercontent.com`, serving `sparkle:version="0.1.8.180"`.
**Not verified: that a real client installs it.** Steps like these verify the OFFER path only,
which is exactly all anyone verified for v0.1.0-v0.1.4 — every one of which offered updates that
then installed nothing. The install check needs a throwaway client stamped below the feed; see the
box at the end of `docs/RELEASING.md`.

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

## UX spines (BMad) — status: RECONCILED to v0.1.7 (phase 43)

`_bmad-output/planning-artifacts/`
- **`REALITY-DELTA.md`** — the verified record of how the artifacts diverged from the code:
  27 rows (`RD-01`…`RD-27`), each giving what the artifacts claimed, what the code does, and the
  **file+line that proves it**, classed `STALE` / `NOT BUILT` / `UNSPEC`. Every later edit cites a
  row id instead of re-deriving. **Start here** before touching any spine.
- **`GAP-REGISTER.md`** — the single register the owner's D1 requires: specified-but-not-built on
  one side, built-but-not-specified on the other.
- `ux-designs/…/DESIGN.md` + `EXPERIENCE.md` — reconciled. Signing is ADR-0061 throughout, the
  project/target/tier IA and the scheme selector now exist in the spec, and every
  designed-but-unbuilt item is **kept** but carries an in-place `[NOT BUILT — RD-nn]` marker.
- `prds/…/prd.md` — **re-opened and reconciled** (owner decision D2, reversing the earlier park).
  The timestamping HIGH is closed **dead** (RFC-3161 presupposes certificates; ADR-0061 has none);
  the "never report signed when it is not" HIGH is **re-filed against shipped code** — see below.
- **Owner decisions that governed this pass:** D1 spec+gap-register (not as-built), D2 reopen the
  PRD, D3 keep the `trust-verified` token name (no rename to `trust-signed`, in docs or code).
- **Still stale, deliberately out of scope:** the three `mockups/*.html` still render the
  Authenticode signing UI, and the brief/addendum's exposed-surface map.

⚠ **Two live code defects this reconciliation surfaced.** One is fixed, one is not:
- ~~**Post-build signing can report success when it failed.**~~ **FIXED in phase 45.** It printed
  `[signed · <name>.sig]` off `snc sign`'s **exit code alone**, with `verifyFile` never called —
  the exact thing the spec forbade ("never on the signer's exit code alone"). RD-06.
- **The trust chip — FIXED in phase 50, and the shape of the fix is the reusable part.** It used
  to keep showing `✓ Signed` while you edited a signed file. The reason it stayed open so long is
  that the obvious fix is wrong: `refreshSignState` spawns `snc verify`, so hooking it to
  `onEditChanged` is one process per keystroke. Nothing needed re-verifying — the answer is free
  the moment the buffer differs from what was signed, so only the RENDERING changed. Kept here
  because the misreading it caused is still a live trap: the `// edits invalidate any existing
  .sig` comment at the `saveFile` call site explains why SAVING re-verifies and must not be read
  as an edit hook — a reconciliation pass misread it exactly that way and asserted the wrong
  behaviour in four documents before the critic caught it.

## What's next (open options)

- ~~Undo / redo memory~~ (phase 18), ~~dark popup menus + right-click New File~~ (phase 19),
  ~~clickable `file:line` in Output~~ (phase 20), ~~LOC dogfood + About badges~~ (phase 21),
  ~~per-target editing~~ (phase 22), ~~sign the built artifact~~ (phase 23), ~~recents + close project~~
  (phase 24), ~~sealed projects (password)~~ (phase 25), ~~file associations~~ (phase 26),
  ~~unsaved-changes guard~~ (phase 39) — all **done**.
- ~~**Single-instance / IPC + drag-drop**~~ — **done, phase 42.** Still open from that bullet: a shell
  "New ▸ Sentinel Project" entry, and honouring flags (not just the path) on a second launch.
- **Unsaved-changes follow-ons** (guard shipped phase 39): ~~undo-to-original still leaves `●` set~~
  **fixed** — `g.dirty` is now a comparison against `g.savedText` (a snapshot taken at load and at
  save) rather than a one-way latch, so undoing back to the loaded *or* saved text clears the dot and
  the prompt. ~~Still open: there is no "reload from disk" prompt when a file changes underneath the
  editor.~~ **Done, phase 51.** Nothing is open in this family now. The follow-on that phase names
  for itself: a change arriving while Sentinel-IDE is *already* the foreground window is not noticed
  until focus leaves and returns, because the trigger is `WM_ACTIVATEAPP`.
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
  file reader/parser in the IDE is now Sentinel**. `saveProject` — the comment-preserving manifest
  writer, a surgical structure-preserving TOML rewrite — followed as the fifth (phase 44), the
  appcast reader as the sixth (phase 47), and the **sealed-container FRAMING** as the seventh and
  eighth (phase 48): `parse_seal_header` + `parse_seal_archive`, the first BINARY readers, and the
  last place in the IDE where attacker-chosen bytes met hand-rolled pointer arithmetic before
  anything was authenticated. The seal **crypto** core stays blocked on R1 (secure-zero) — and note
  that the framing port was possible precisely BECAUSE it touches no secret: nothing that must be
  wiped crosses the FFI boundary, Sentinel returns byte offsets and the host keeps doing CNG. Do not
  read phase 48 as the crypto port having started.
- **Signing follow-ons (remaining):** surface capability-bound verify failures as Problems; an
  editable trust manifest (policy/grants) beyond add+import; a Settings field for a default signing
  key (today post-build signing uses `sentinel.key` in the project dir).
- **Targets follow-ons (remaining):** per-target `lib_paths`; a definable output dir; add/remove
  `[[target]]` blocks from the form (today it edits existing blocks' name/entry/type in place).
- **Undo/redo follow-up:** track the saved point so undo-to-clean clears `●`; toolbar button hover states.
- **Find/Replace follow-ons** (phase 49 shipped the core): find in FILES across the project,
  reported into the Problems pane; regular expressions; a search-history dropdown on the field;
  and a "selection only" scope for Replace All. None are started. The one thing OWED is a human
  pressing Ctrl+F / Ctrl+H / Tab / Shift+F3 on a real build and LOOKING at the band — the bar's
  pixels have no automated coverage and cannot get any without a foreground window.
- **The Direct2D editor is DONE** — phase 46 is closed and the RichEdit *editor* is deleted, so
  there is one editor and no setting to choose another. **Unreleased**: 0.1.9 shipped the flip
  with all three escape hatches intact, and HEAD has none of them, so the next release note has to
  say that unticking is no longer possible. The human checks that were owed are **DONE** — keys,
  mouse drag and a guarded file drop, all on the released 0.1.9.185 build (2026-09-04).
  Then: dark **title-bar menu bar**; a project-templates picker
  (lib/exe/multi-target) for New Project.
- **Reconcile the spines/PRD** to ADR-0061 signing + the project/tier model (un-park PRD work).

---

## Seed prompt for a new session

_Keep this SHORT. It is the agent's first message, not a second copy of the handover — its only
jobs are to point at the reference, and to pre-empt the mistakes and wrong priors that would happen
in the first few tool calls, before the handover has been read. Everything look-up-able once you've
read the handover belongs in the handover body, not here._

> You're continuing **Sentinel-IDE** (`G:\SentinelIDE`) — a native Win32 IDE for the **Sentinel**
> language, whose thesis is a thin C++ host that shrinks as logic moves *into* Sentinel. It is a
> **released, public, auto-updating** product: **sixteen releases, latest v0.1.15 (build 213)**, and real
> Sentinel code runs in the shipped binary — every file reader, the network-fed update feed and the
> sealed-container framing (eight exports, ~1,960 lines). Treat `main` as shippable.
>
> **If no task follows this prompt, read the handover and then ASK before changing anything.** Do not
> pick something off "What's next" and start — this is live software with users on auto-update.
>
> **Read `docs/HANDOVER.md` first — it is the current state** (phase list 1–50, Environment gotchas,
> the Releases table, What's next, per-area detail; ~1,940 lines, so skim `grep -n '^## '` for the
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
> and prefer running the thing over reading that it works.
>
> That habit is not historical. In the session that produced v0.1.9–v0.1.15 it caught, among
> others: a claim that the screenshot tool "cannot capture Direct2D", generalised from ONE blank
> capture — false, and it led to accusing two reviewers of fabricating readings they had actually
> taken; a port claiming it had closed a multi-GB allocation defect when it had closed only part,
> leaving a 206-byte file able to commit 65,667 MB; a "7 objects" figure that was ninja's
> incremental step counter; and v0.1.13 shipping with a red test because nothing re-ran ctest after
> the release procedure's own last commit. **Every one was found by re-measuring a claim, not by
> reading code.** When you break something on purpose to prove a test catches it, revert and re-run
> — that loop is why those are on this list instead of in the product.
>
> That's the seed. The phase history, build/screenshot commands and window classes, the
> trust-manifest four-site lockstep rule, the release procedure and the full gotcha list are all in
> `docs/HANDOVER.md`. Read there; don't expect it duplicated here.
