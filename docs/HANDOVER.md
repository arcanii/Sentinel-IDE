# Sentinel-IDE — Handover

_Last updated: 2026-06-29._

> **Naming:** the product/exe is **`Sentinel-IDE`** (matching `Sentinel-lang` / `Sentinel-learning`);
> the build output is `build\Sentinel-IDE.exe`. **Internal identifiers stay `SentinelIDE`** by design —
> the window class `SentinelIDEMainWindow`, the `sentinelide` C++ namespace, the settings dir
> `%LOCALAPPDATA%\SentinelIDE`, the CMake target id, and the local folder `G:\SentinelIDE`. So scripts
> that match the **process name** use `Sentinel-IDE`, but `-Class SentinelIDEMainWindow` is unchanged.

**Sentinel-IDE** is a native, Windows-first IDE for the **Sentinel** language, intended to
eventually be built *in* Sentinel (thin native host shrinking over time). Two workstreams
exist so far:

1. **UX design spines** (BMad) — `DESIGN.md` + `EXPERIENCE.md`, status **draft**.
2. **A working Win32 C++ prototype** — phases 1–8 built and verified.

---

## TL;DR — current state

- The prototype **builds and runs**: `scripts\build.bat` → `build\Sentinel-IDE.exe`.
- It's a real dark/coral Win32 IDE: themed shell, dark TreeView + RichEdit editor with
  Sentinel syntax highlighting, `snc` build/run with live streamed output + a Problems
  triad, a configurable logfile + Settings dialog, a **Sentinel project model**
  (a `*.sntproject` file, or legacy `sentinel.toml`) with **multiple build targets**, an
  **Xcode-style target ▾ · tier ▾ scheme selector**, a **Project/Files explorer**, a
  **New Project** command, a **structured Project Settings editor**, and a **Signing & Trust
  panel** driving *real* ADR-0061 `snc keygen`/`sign`/`verify` with a live status-bar trust chip.
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
| `src/core/Seal.h` | **Project sealing** (ADR-style): archive → LZMS-compress → AES-256-GCM under a random DEK, wrapped per password slot (PBKDF2-HMAC-SHA256). LUKS-like extensible unlock slots. Native CNG; the AEAD+KDF core is a Sentinel-rewrite target. |
| `src/core/FileAssoc.h` | Per-user (`HKCU\Software\Classes`) file associations for `.sntproject`/`.sentinel` → open in this exe (`registerFileAssociations`; ≡ ▸ Register File Associations…). |
| `src/core/Proc.h` | `runCapture` (synchronous run-and-capture) + `stripAnsi` |
| `src/core/Signing.h` | Trust manifest + `.sig` parsers, `verifyFile`, `sncSupportsSigning` |
| `src/core/Toolchain.h` | `findVcvars` (auto-detect MSVC env) + `captureMsvcEnv` (vcvars → env block for builds) |
| `src/core/Logger.h` | Thread-safe append logfile (level + location) |
| `src/core/Settings.h` | `settings.ini` in `%LOCALAPPDATA%\SentinelIDE` — font/theme/log/toolchain + the **`[recents]`** project list (`addRecent`, capped `kMaxRecents`) |
| `src/core/Project.h` | manifest parsing (`findManifest`: prefers `*.sntproject`, else `sentinel.toml`) + tiers + **`[[target]]`** + `saveProject` (surgical writer → the loaded manifest; preserves comments + unmodeled keys incl. `[[target]]`) |
| `packaging/` | `app.manifest` (ComCtl v6, per-monitor-v2 DPI), `SentinelIDE.rc`, `app.ico` (res 100), `file.ico` (res 101 — `.sentinel` tree icon) |
| `scripts/build.bat` | Configure + build (needs VS 2026 — path is hard-coded inside). **Auto-increments the build number** (`packaging/build_number.txt`) and writes `build/generated/Version.h` (consumed by the C++ sources + the `.rc`). |
| `packaging/build_number.txt` | Persistent build-number counter (committed). `build.bat` reads → +1 → writes it back each build. Marketing version (`0.1.0`) stays fixed; only the build number climbs. |
| `scripts/launch.ps1` | Launch the exe **detached** (WMI) so it survives the shell |
| `scripts/capture.ps1` | Screenshot the window → `build\shot.png` (DPI-aware PrintWindow; `-Class` for dialogs) |
| `scripts/convert-icon.ps1` | PNG → multi-size letterboxed `.ico`. Defaults `art/S2_icon.png`→`packaging/app.ico`; `-Src/-Dst` for `file.ico`. (Letterboxes — S2 is 554×657, not square. After rerun, touch the `.rc` so ninja relinks; the per-size Win icon cache may need `ie4uinit -ClearIconCache` + reboot to refresh large/extra-large.) |
| `scripts/loc.ps1` | Counts the IDE's source by language → `build/generated/Loc.h` (About-box badges); builds the corpus and runs **`tools/loc.sentinel`** via `snc` for the Sentinel-verified total. Called by `build.bat`. |
| `packaging/Sentinel-IDE.iss` | **Inno Setup** installer script → per-user `setup.exe` (Start-Menu shortcut, file associations mirroring `FileAssoc.h`, uninstall). |
| `scripts/make-installer.bat` | Build the app, then compile the installer (needs Inno Setup 6: `winget install JRSoftware.InnoSetup`) → `build/installer/`. |
| `tools/loc.sentinel` | **The first part of SentinelIDE written *in* Sentinel** — a whole-file line counter (read_file/write_file). Counts toward the "Sentinel" LOC badge. |
| `examples/` | Sample project: `sentinel.toml` (+ `[[target]]`s), `sentinel-trust.toml`, `crypto.sentinel`(+`.sig`), `hello.sentinel` |
| `art/` | `S2_icon.png` (app icon — metallic shield), `A_simple_clean…827808.png` (the `.sentinel` file icon — page + blue S + padlock), `S_icon.png` (old), `guard_icon1/2.png` |
| `docs/` | `prototype.md`, `sentinel-project.md`, this file, `screenshots/phase1..12*.png` |
| `_bmad-output/planning-artifacts/` | UX spines, PRD, brief (BMad) |
| `build/` | CMake/Ninja output (`Sentinel-IDE.exe`) — regenerated (gitignored) |

## Reference repos (read-only, on the same machine)

- **`G:\SQLTerminal-Win32`** — the Win32 **visual/architecture reference** (same author/house
  style). Source of `Theme.h`, themed dialogs, tree + splitters, worker threads. **GPL-3.0.**
- **`G:\Sentinel-lang`** — the language + **`snc` compiler** + ADRs/docs. Two binaries:
  `target\release\snc.exe` (C1.0b, 2026-06-27 — **no signing subcommands**) and
  `target\debug\snc.exe` (2026-06-28 — **has** ADR-0061 `keygen`/`sign`/`verify` +
  `--require-signatures`/`--trust`). The IDE's `findSnc()` now **prefers a signing-capable
  binary**, so it picks the debug build (rebuild the release to get a signing-capable release).
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

- **Versioning:** marketing version is **fixed at `0.1.0`**; the **build number auto-increments on
  every `build.bat` run** (counter in `packaging/build_number.txt`, currently at 3 → next build is 4).
  `build.bat` composes `build/generated/Version.h` (`SENTINEL_VERSION_DISPLAY_W` = `L"0.1.0 (build N)"`,
  `SENTINEL_FILEVERSION` = `0,1,0,N`, etc.), included by `MainWindow.cpp` (status bar), `AboutDialog.cpp`,
  and `SentinelIDE.rc` (the exe's **FileVersion** = `0.1.0.N`; **ProductVersion** stays the marketing
  `0.1.0.0`). CMake writes a fallback `Version.h` (build 0) so a raw `cmake --build` without `build.bat`
  still compiles. Note: every build consumes a number (incl. failed builds, since it's stamped before
  compile), so the counter may skip — that's expected for a monotonic dev counter.
- **Build env:** Visual Studio 2026 Community (MSVC + bundled CMake/Ninja). The exact path is
  hard-coded in `scripts\build.bat` — change it if VS moves. Drive builds through `cmd /c
  scripts\build.bat` (it calls `vcvars64.bat`). Note: a sandbox guard rejects `Remove-Item`
  in the **same** PowerShell call as `cmd /c` — keep them in separate calls.
- **Screenshots:** the app isn't an installed app, so the screenshot MCP can't allowlist it.
  Use `scripts\capture.ps1` (WMI-detached launch + DPI-aware `PrintWindow`).

## Prototype status — phases 1–27 (all done, screenshot-verified)

1. **Themed shell** — DWM dark titlebar, `≡` popup menu, dark/coral identity, status bar.
2. **Real controls** — dark `WC_TREEVIEW` + RichEdit editor, draggable splitter, Open Project (`IFileOpenDialog`).
3. **Syntax highlighting** — Sentinel keywords (incl. `secret`/`declassify`), strings, numbers, comments per `Theme.h`.
4. **Build/Run loop** — `snc.exe` on a worker thread, live-streamed Output (ANSI-stripped) + exit code; miette `file:line:col` → clickable Problems list.
5. **Logging + Settings** — configurable logfile (`%LOCALAPPDATA%\SentinelIDE\logs`) + themed Settings dialog (font, theme, log level/location).
6. **Project model + icon** — `sentinel.toml`; `art/S_icon.png` → app icon.
7. **Tier scheme selector** — Xcode-style top selector: target · **tier** ▾ · output; tiers = Development/Experimental/Stable/Hardened (TIERED_RELEASES.md); output → `target/<tier>/`.
8. **Explorer views** — **Project** (Sentinel-icon root, `sentinel.toml`/`sentinel-trust.toml`/Sources) and **Files** (raw tree), via tabs.
9. **Project Settings editor** — a themed modal form over `sentinel.toml` (name/version/type/entry, src/lib_paths/links/tier, ADR-0061 signing require/trust/sign). Opened by clicking the **project root node** or **≡ ▸ Project Settings…**; Save persists via `saveProject` (preserves comments + unmodeled keys like `icon`/`authors` **and `[[target]]` blocks**), then reloads. Clicking the `sentinel.toml` child still opens the **raw** manifest; opening a project lands in the **active target's entry source**. `docs/screenshots/phase9-project-settings.png`.
10. **Signing & Trust (ADR-0061, real)** — a status-bar **trust chip** (✓ Signed / ⊘ Unsigned / ⚠ Signature invalid) computed by an async `snc verify` of the open file; clicking it (or **≡ ▸ Signing & Trust…**) opens a panel that runs *real* `snc keygen`/`sign` (with `--grant` capabilities)/`verify` and views the consumer trust manifest (dependency · key · policy · grants · forbids), plus "Import current key as trusted". `examples/crypto.sentinel.sig` is a committed signed demo. `docs/screenshots/phase10-signing.png`.
11. **Multiple targets** — `[[target]]` array-of-tables in the manifest (a project with none gets one synthesized target — backward compatible). The scheme selector is now **`target ▾ · tier ▾`**; the Project tree gains a **Targets** group; Build/Run/output follow the active target. Switch via the selector dropdown or a target tree node. `docs/screenshots/phase11-targets.png`.
12. **New Project + `.sntproject`** — the IDE recognizes a folder as a project by a **`*.sntproject`** file (preferred — the native IDE project file) **or** legacy `sentinel.toml` (`findManifest`; `loadProject`/`saveProject`/the tree all use the discovered manifest). **≡ ▸ New Project…** opens a Save dialog, then `createNewProject` makes the chosen dir **+ any missing parent dirs** (`SHCreateDirectoryExW`), a `src/` folder with a starter `main.sentinel`, and the `<name>.sntproject` manifest, then opens it. App icon swapped to the cleaner **`art/S2_icon.png`**.
13. **Build toolchain in Settings + working builds** — Settings → **BUILD TOOLCHAIN** adds **Sentinel (snc)** and **MSVC env** fields (Browse + auto-detect hints; blank = auto). The IDE auto-detects `vcvars64.bat` and **injects the MSVC environment** into builds so `snc`'s `link.exe` step works — closing the build→link gap. Verified: `examples` builds at exit 0 → a runnable `crypto.exe` (exit 42). `docs/screenshots/phase13-settings-build.png`.
14. **Open Project + New File** — **≡ ▸ Open Project…** is a manifest file picker (`*.sntproject` / `sentinel.toml`) that loads the containing folder (replacing the old folder picker). **≡ ▸ New File…** creates a new `.sentinel` source via a Save dialog (defaulting to the project's `src/`), then opens it + refreshes the tree.
15. **Polish — themed About + S2 icon + smooth splitter.** App icon/tree-node/About all use `art/S2_icon.png`. **≡ ▸ About** is now a dark/coral themed dialog (S2 shield via `DrawIconEx`), not a classic MessageBox (`docs/screenshots/phase15-about.png`). Splitter drag is smoothed to match SQLTerminal's GDI technique: `layout()` batches pane moves with `BeginDeferWindowPos`; the drag repaints synchronously via `RedrawWindow(RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN|RDW_NOERASE)` (full-window invalidate so the parent-painted bands — tab strip, dock header, gutter — don't trail; skips no-op moves); `onPaint` reuses a **cached back-buffer** (no per-frame bitmap alloc); and the editor is **no-wrap** (`EM_SETTARGETDEVICE`) so width changes don't reflow. (True GPU-smoothness still wants the planned Direct2D editor — SentinelIDE is GDI + RichEdit; SQLTerminal uses Direct2D.)

16. **`.sentinel` file icon + icon polish.** `.sentinel` nodes in the explorer tree now use a custom icon (a page + blue **S** + padlock — `packaging/file.ico`, resource 101, loaded into the tree image list for `IMG_FILE`; falls back to the shell icon). `convert-icon.ps1` is parameterized (`-Src/-Dst`) and **letterboxes** to preserve aspect (S2 is 554×657). The app `.ico` is verified-correct at all sizes (256/48/32/16 PNG frames); stale large/extra-large icons in Explorer are the **Windows per-size icon cache** (`ie4uinit -ClearIconCache` + reboot to refresh), not the art.
17. **Editor: edit/save + line numbers + error highlight.** Edits set a **dirty** flag (`●` in the tab + a **toolbar Save button** that lights coral when dirty); **Save** (`Ctrl+S` / `≡ ▸ Save` / toolbar) writes UTF-8+CRLF; **Build auto-saves** the open file first. (Save bug fixed: the read buffer was sized by `GTL_NUMCHARS` but `GT_USECRLF` expands each `\r`→`\r\n`, so output overflowed and truncated — now sized `n*2+16`.) A **line-number gutter** toggles via `≡ ▸ Line Numbers` / `Ctrl+L` (persisted in settings; painted from `EM_GETFIRSTVISIBLELINE`/`EM_POSFROMCHAR`, repainted on `ENM_SCROLL`). **Error lines** from a build are tinted (`CFM_BACKCOLOR`) in the open file and cleared on edit. A real **accelerator table** now backs the menu shortcuts (Ctrl+S/N/O/Shift+N/Shift+B, F5, Ctrl+L, Ctrl+,). Gotcha: `EM_SETCHARFORMAT` raises `EN_CHANGE`, so highlight/error-tint ops set `g.highlighting` to avoid a spurious dirty flag. Also fixed the Problems **File** column (it rendered garbage glyphs): `ListView_SetItemText` is a macro that assigns `pszText` then `SendMessage`s in *separate* statements, so a temporary `baseName(d.file).c_str()` dangled before the send — now held in a named local.

18. **Undo / redo memory.** RichEdit has native multi-level undo, but the syntax highlighter was **polluting** it — `highlight()`/`markErrorLines()`/`clearErrorMarks()`/the base `styleEditor()` reformat call `EM_SETCHARFORMAT`, and each format landed on the undo stack, so `Ctrl+Z` undid a *color change* not the edit. Fix: a cached `ITextDocument` (TOM) fetched lazily via `EM_GETOLEINTERFACE` → `QueryInterface(__uuidof(ITextDocument))` (`#include <tom.h>`/`<richole.h>`; released in `WM_DESTROY`); `suspendUndo()`/`resumeUndo()` wrap each programmatic-format site with `ITextDocument::Undo(tomSuspend/tomResume)` so only real text edits are recorded. **Ctrl+Z → `EM_UNDO`**, **Ctrl+Y → `EM_REDO`** are wired in the accelerator table + the `≡` menu (Undo/Redo items, grayed via `EM_CANUNDO`/`EM_CANREDO`). Undo/redo go through `WM_COMMAND ID_UNDO`/`ID_REDO` → editor, then `SetFocus`. `EM_UNDO`/`EM_REDO` raise `EN_CHANGE` → `onEditChanged` re-highlights (undo-suspended) and marks **dirty** — so any undo/redo leaves `●` set (RichEdit doesn't track the saved point; accepted as simplest). Verified by driving `WM_CHAR`+`WM_COMMAND` against the live window: type 3 chars (429), one undo reverts *text* (428, not a no-op color undo), redo restores (429). **Toolbar Undo/Redo buttons** (`↶`/`↷` glyphs, `rUndo`/`rRedo`, between Save and the scheme selector) mirror the menu/accelerator paths; they gray out when `EM_CANUNDO`/`EM_CANREDO` is false and repaint only when availability changes (`refreshUndoButtons` — any invalidate forces a full-window repaint, so it skips the steady-state no-op; called from `onEditChanged`, which all edits/undo/redo reach via `EN_CHANGE`). Screenshot-verified: grayed at open → `↶` lights on first edit → `↷` lights after an undo.

19. **Dark popup menus + tree context menu.** Popup/context menus were classic light Win32 (clashing with the dark shell). `Theme.h::applyMenuDarkMode()` calls the undocumented uxtheme ordinals (135 `SetPreferredAppMode` → ForceDark/ForceLight per `currentTheme().dark`, 136 `FlushMenuThemes`) — the same technique SQLTerminal uses; guarded, so menus just stay light if the exports are missing. Called at startup and in `applyTheme` (theme changes). A **right-click context menu** on the explorer tree (`WM_CONTEXTMENU` → `showTreeMenu`, also keyboard Shift+F10) offers **New File…** (the headline action) + New/Open Project, routing to the existing command IDs. Screenshot-verified dark.

20. **Clickable `file:line:col` in Output.** As build output streams, the `path.sentinel:line:col` token in each diagnostic is marked `CFE_LINK` (`outAppend` now returns the insertion char-pos — CRLF-safe via `EM_EXGETSEL` clamp-to-end — and `outLinkify` selects `[base+ts,base+te)` from `parseDiag`'s reported span). `ENM_LINK` is enabled on the Output pane; `EN_LINK` (WM_LBUTTONUP) extracts the link text via `EM_GETTEXTRANGE`, re-parses it, and calls the same `gotoLineCol` the Problems list uses. Screenshot-verified: link renders underlined; clicking jumped the editor to Ln 3.

21. **Lines-of-code dogfood — the first piece of SentinelIDE written *in* Sentinel.** `tools/loc.sentinel` reads `loc_corpus.txt` (whole-file `read_file`), counts newlines, and `write_file`s the total to `loc_total.txt` — Sentinel has no argv/stdin/dir-walk (ADR 0035), so the native host hands it a fixed corpus and reads back the result (the thin-host / Sentinel-core split). `scripts/loc.ps1` (run by `build.bat` under the vcvars env) enumerates the IDE's own source by language (C++ / Sentinel / Build), writes the corpus, compiles+runs `loc.sentinel` via `snc` for the **Sentinel-verified grand total** (falls back to its own count if snc is absent — never blocks a build), and emits `build/generated/Loc.h` (`SENTINEL_LOC_*`). The **About box** renders them as shields.io-style flat badge pills (`drawBadge`: rounded clip region, dark label half + colored value half) — **C++** (blue), **Sentinel** (coral — the new one), **Build** (gray), **Total** (green) — under the caption "Lines of code · total counted by loc.sentinel". Snapshot at build 8: C++ 3307 · Sentinel 67 · Build 354 · Total 3728. (Gotcha learned: Sentinel has **no `%` operator** — use `v - (v/10)*10`; and `if` is an *expression*, so a bare `if cond { stmt; }` is a parse error — use `x + (if cond {1} else {0})`.)

22. **Per-target editing in Project Settings.** A **TARGETS** section (shown only when the manifest declares real `[[target]]` blocks — `SentinelProject::explicitTargets`) adds a target selector combo + editable **Name / Entry / Type** for the selected target; switching targets (`CBN_SELCHANGE`) commits the current one and loads the next, and Save commits the visible one. `saveProject` gained a **non-destructive `[[target]]` writer**: it rewrites existing `name/entry/type/links` values in place (matched by block order; the `[[` header parses to section name `"[target"`), never inserting — so comments and unmodeled per-target keys survive. Round-trip verified: renaming target 1 to `cryptoZ` and saving changed only that block; target 2 (`hello`) and all comments were untouched.

23. **Signing follow-on — sign the built artifact.** On a **successful** project build, when `signing.sign = true` and a `sentinel.key` exists in the project, the IDE runs `snc sign <artifact> --key sentinel.key` (the same key convention as the Signing panel) and reports `[signed · <name>.sig]` in Output — making the project's "Sign the built artifact" checkbox real. (Build-gating on `require = warn/strict` was already wired in `composeBuild` via `--require-signatures`/`--trust`; the stale "snc C1.0b doesn't accept it" note in the Project Settings form is corrected.) Verified end-to-end: keygen → sign=true → build `examples` → `crypto.exe.sig` produced + `[signed]` in Output.

24. **Recent Projects + Close Project.** `Settings` gained a `recents` list (most-recent-first, capped at `kMaxRecents`=10, persisted under `[recents]` in `settings.ini`; `addRecent` de-dupes case-insensitively and promotes to front). `openFolderPath` records a recent whenever a real project loads (so New/Open/CLI/recent all feed it). The `≡` menu gains a **Recent Projects ▸** submenu (`buildRecentsMenu` — `name⟶parent` per entry, `&`-escaped, then **Clear Recent Projects**; items are `ID_RECENT_BASE+i`); a missing folder is dropped on click. **Close Project** (in the `≡` menu + tree context menu, grayed when nothing's open) returns to the welcome state: auto-saves a dirty file first (like Build), then clears the project/file/tree/problems/output, hides the tree+editor, and resets the title/chip. Verified: opening two projects ordered the persisted recents correctly; opening recent #1 reopened the right project + its entry file; Close reset to the welcome screen. (Also fixed a pre-existing cosmetic bug — "Signing & Trust…" rendered as "Signing _Trust…" because the literal `&` was a menu mnemonic; now `&&`.)

25. **Sealed projects — encrypt so only the developer can open.** `src/core/Seal.h` (header-only, native CNG + Windows Compression API) seals a project: **archive folder → LZMS-compress → AES-256-GCM encrypt under a random master key (DEK)**. The DEK is wrapped per **unlock slot** (LUKS-style) — v1 = one **password** slot: PBKDF2-HMAC-SHA256(password, 16-B salt, 600k iters) → KEK → AES-256-GCM key-wrap of the DEK. More unlock methods (key file, Ed25519/smartcard, TPM) become new slot types that wrap the *same* DEK — no re-encryption, and a project can carry several at once. The `.sealed` format (magic `SNTSEAL1`, version, AEAD-alg id, archive size, slots, payload nonce/ct/tag) records algorithm ids so a future **ChaCha20-Poly1305** slot/payload coexists with AES files. Extraction sanitizes paths (rejects `..`/absolute); GCM auth detects tampering; archiving skips `target`/`build`/`.git`/`node_modules` and any `.sealed`. UI: **≡ ▸ Seal Project…** (themed `PasswordDialog`, double-entry) → writes `<parent>\<name>.sealed` (non-destructive — the plaintext is left in place), reports in Output. **≡ ▸ Open Sealed Project…** → file picker → password → decrypts to a sibling `<name>-unsealed\` and opens it; wrong password / corruption → a clear message. **This is the headline Sentinel-rewrite target:** the AEAD + KDF core maps onto `std/security` (machine-verified constant-time ChaCha20-Poly1305 + SHA-256); the native host keeps archive/compress/dir-walk (Sentinel has no dir traversal). **Verified** end-to-end: a standalone harness round-trips (seal→unseal byte-identical, wrong-password rejected, 1-bit tamper caught by GCM); and the IDE's own seal of `examples` (7 files · 3906 B → 2134 compressed → 2286 sealed) unseals to byte-identical sources. (A test-harness gotcha, not in the engine: building a key from two separate `"literal"` expressions spans different string objects when pooling is off → use one named buffer.)

26. **File associations — double-click `.sntproject` / `.sentinel` → opens the IDE.** `src/core/FileAssoc.h` registers per-user associations under `HKCU\Software\Classes` (no admin, effective immediately via `SHChangeNotify`): ProgIDs `SentinelIDE.Project` / `SentinelIDE.Source` with `shell\open\command = "<thisexe>" "%1"` and `DefaultIcon = "<exe>",-100/-101` (the app + `.sentinel` file icons, by negative resource id). **≡ ▸ Register File Associations…** writes them and confirms (with a note that an existing per-extension *UserChoice* would still win — by design). The exe already accepts a path on argv; `openPathArg` was improved so a double-clicked file opens its **nearest enclosing project** (walks up to the first folder with a manifest), and a manifest opens the project landing in its entry source rather than the raw file. Verified: registry keys point at `build\Sentinel-IDE.exe`; a pure shell-association launch (`Start-Process crypto.sentinel`, no exe path) opened the IDE with the crypto-lib project + `crypto.sentinel`. (Follow-up: single-instance/IPC so a double-click reuses an open window instead of spawning a new one; today each opens its own.)

27. **Rename to "Sentinel-IDE" + git/repo prep.** Display name → **Sentinel-IDE** (titlebar/`kAppName`, About box + caption, `.rc` ProductName/FileDescription/InternalName/OriginalFilename, `app.manifest` identity) and the **exe → `Sentinel-IDE.exe`** via CMake `OUTPUT_NAME` (the CMake target id, window class `SentinelIDEMainWindow`, `sentinelide` namespace, `%LOCALAPPDATA%\SentinelIDE` settings dir, and the `G:\SentinelIDE` folder stay "SentinelIDE" — so saved settings + the `-Class` capture path keep working). `launch.ps1`/`capture.ps1` now match the **process name** `Sentinel-IDE`; the file associations were re-registered to the new exe. **Repo prepped (to stay private initially):** added `LICENSE` (verbatim GPL-3.0, from SQLTerminal-Win32), a public `README.md`, `.gitignore` (excludes `build/`, `target/`, `*.o`/`*.obj`/`*.exe`/`*.pdb`/`*.lib`, `*.sealed`/`*.key`, `.claude/`, `_bmad/`), and `.gitattributes` (`* text=auto eol=crlf` so the signed demo keeps CRLF; `*.sig`/`*.ico`/`*.png` `binary` so the committed signature isn't mangled). `git init -b main` → clean initial commit **`e5f8386`, 85 files** (source/docs/examples/packaging/scripts/art/tools + the `_bmad-output` design docs; **no build artifacts, secrets, or tooling**). Gotchas handled: the repo sits on a network share (git needed `safe.directory`), and a first attempt committed `examples/target/**` build artifacts — history was rebuilt clean before any push. **Not yet pushed** — `gh` isn't installed: create a **private** `Sentinel-IDE` repo on GitHub, then `git -C G:\SentinelIDE remote add origin <url> && git -C G:\SentinelIDE push -u origin main` (or, after installing+auth'ing gh: `gh repo create Sentinel-IDE --private --source=. --remote=origin --push`). The local folder stays `G:\SentinelIDE`; only the GitHub repo + product/exe are "Sentinel-IDE".

See `docs/prototype.md` and `docs/sentinel-project.md` for detail.

## Key decisions

- **Identity:** dark-primary Claude-desktop coral, OS light/dark follow, sourced verbatim from
  SQLTerminal `Theme.h`. `Theme.h` in this repo is that palette in code.
- **Editor:** RichEdit for now; the Direct2D/DirectWrite GPU editor (as in the reference) is
  the eventual native-perf target. Editor font = Cascadia Code, user-overridable in Settings.
- **Signing = Sentinel-native ADR-0061** (Ed25519 keys via `snc keygen`/`sign`/`verify`,
  `sentinel-trust.toml` with trusted key + policy + **capability grants/forbids**, gated by
  `--require-signatures`). This **supersedes the earlier Authenticode framing** for the project.
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
- **Signing — RESOLVED (use the signing-capable binary).** ADR-0061 is v1-implemented. The
  **release** `snc.exe` (C1.0b, 2026-06-27) is stale (no `keygen`/`sign`/`verify`, rejects
  `--require-signatures`/`--trust`); the **debug** `snc.exe` (2026-06-28) has them. The IDE
  auto-prefers the signing-capable binary, so the signing UI is **fully real**. (For a
  signing+link-capable *release* binary, rebuild `snc --release`; the release also needs
  `libsentinel_runtime.a` for its own link path.)
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
  (phase 24), ~~sealed projects (password)~~ (phase 25), ~~file associations~~ (phase 26) — all **done**.
- **Single-instance / IPC:** a double-click (or a 2nd launch) currently spawns a new window; route the
  path to an existing instance (named pipe / `WM_COPYDATA` to a `FindWindow` of the app class) so the
  open project gains a file/tab instead. Also: drag-drop files onto the window; a shell "New ▸ Sentinel
  Project" entry.
- **Windows installer (drafted, phase 28):** `packaging/Sentinel-IDE.iss` (Inno Setup) + `scripts/
  make-installer.bat`. Per-user `setup.exe`: exe + examples + docs, Start-Menu shortcut, the
  `.sntproject`/`.sentinel` associations (declarative, mirrors `FileAssoc.h`), uninstall. **Not yet
  compiled** — Inno Setup isn't installed (`winget install JRSoftware.InnoSetup`, then
  `scripts\make-installer.bat`). WiX/MSI or MSIX (Store) are the heavier alternatives if needed later.
- **Cross-platform = one repo, layered (decided direction).** Keep a single repo (the IDE is one
  product); split into a **portable core** (project model, manifest/format parsers, the `.sealed`
  format + crypto, signing/trust, the snc driver) and a **per-platform native host** (`src/ui` = the
  Win32 host today; macOS/Linux hosts would be Cocoa / GTK-or-Qt rewrites — *not* a shared GUI). The
  reuse layer is **Sentinel itself**: the core's logic (esp. the sealing AEAD+KDF, which `std/security`
  already provides cross-platform + constant-time) becomes a Sentinel C-ABI lib each host links — the
  project's own thesis. **Do NOT scaffold empty `macos/`/`linux/` trees yet** (dead weight); add a host
  when a port actually starts. Today the `src/core/*.h` "core" is still Win32-coupled (wchar_t, BCrypt,
  Compression API, profile API) — step 1 of any port is pulling that logic behind a portable seam.
- **Sealing follow-ons:** more **unlock slots** (key file, Ed25519/smartcard, TPM — each wraps the
  same DEK, no re-encryption); a "**remove plaintext after sealing**" option (with confirmation;
  today seal is non-destructive); **re-seal in place** / "lock" of an unsealed working copy; show
  the `.sealed` in the tree and unseal on click; upgrade the KDF to **Argon2id** (PBKDF2 today —
  weaker vs. GPU); open the sealed payload **into memory** rather than to a plaintext working dir.
- **Writing more of the IDE in Sentinel** (the project's destination). What `snc` can do **today**
  (verified): `fn main() -> i64` (return = exit code), **whole-file** `read_file`/`write_file`,
  `print`/`print_bytes`, TCP sockets, `[u8]`/`Vec<u8>`, `while`, structs/enums/traits, the `secret`
  + constant-time checker, algebraic effects. What it **can't**: argv, stdin, directory traversal,
  `stat`, streaming/seek, recoverable I/O errors, `for`, closures, tuples, **`%`**. So the dogfood
  pattern is *the native host hands a fixed file in, reads a fixed file out*. **Top candidate: the
  seal crypto core** — port `Seal.h`'s AEAD + KDF to a Sentinel C-ABI lib using `std/security`'s
  constant-time ChaCha20-Poly1305 + SHA-256 (the `.sealed` format already reserves algorithm ids
  for a ChaCha slot/payload). Also low-risk/file-driven: a **diagnostic `file:line:col` parser** or a
  **trust-manifest validator**. Avoid: anything interactive/stream/multi-process (LSP-over-stdio).
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

> You're continuing **SentinelIDE** in `G:\SentinelIDE` — a native Windows-first IDE for the
> Sentinel language. **Read `docs/HANDOVER.md` first** for full state; also `docs/prototype.md`
> and `docs/sentinel-project.md`.
>
> There's a working **Win32 C++ prototype** (phases 1–26: themed dark/coral shell, **dark popup +
> right-click menus**, dark TreeView + RichEdit editor with Sentinel syntax highlighting,
> **line-number gutter (Ctrl+L)**, **dirty `●`/Save** (Ctrl+S + toolbar; Build auto-saves),
> **error-line tints**, **undo/redo** (Ctrl+Z/Y → `EM_UNDO`/`EM_REDO` + toolbar `↶`/`↷`; the
> highlighter no longer pollutes the undo stack — TOM `ITextDocument` undo suspended around
> formatting); `snc` build/run with streamed Output (**clickable `file:line:col` links**) + Problems
> triad — **builds link + run** (the IDE injects the auto-detected MSVC env so `link.exe` works) and
> a successful project build **signs the artifact** when `signing.sign=true` + a `sentinel.key`;
> Settings (snc + MSVC-env fields); a project model — a `*.sntproject` or legacy `sentinel.toml` —
> with **multiple `[[target]]`s**; an Xcode-style **`target ▾ · tier ▾`** selector; a Project/Files
> explorer with a custom `.sentinel` icon; **New / Open / Close Project + Recent Projects + New File** + a real
> **accelerator table**; a structured **Project Settings** form with **per-target editing**; a themed
> **About** box with **lines-of-code shields.io badges** (C++ / Sentinel / Build / Total, the total
> **counted by `tools/loc.sentinel`** — the first part of the IDE written *in* Sentinel); and a
> **Signing & Trust** panel wired to *real* ADR-0061 `snc keygen`/`sign`/`verify` with a live trust
> chip; **Seal Project / Open Sealed Project** — password-encrypted (AES-256-GCM + PBKDF2,
> LZMS-compressed, LUKS-style unlock slots in `core/Seal.h`; the crypto core is a Sentinel-rewrite
> target); and **per-user file associations** (`≡ ▸ Register File Associations…`) so double-clicking a
> `.sntproject`/`.sentinel` opens the IDE. App/file icons from `art/S2_icon.png` + `packaging/file.ico`. Build number auto-increments
> (`packaging/build_number.txt`); marketing version fixed at 0.1.0).
>
> Build: `cmd /c scripts\build.bat` → `build\Sentinel-IDE.exe`. Run/launch detached:
> `powershell -File scripts\launch.ps1 "G:\SentinelIDE\examples" --build` (open path + flags are
> **separate** args). The app can't be screenshotted via the MCP (not installed) — use
> `powershell -File scripts\capture.ps1` → `build\shot.png`, then Read that PNG (add
> `-Class SentinelProjectDlg` / `-Class SentinelSettingsDlg` for a modal). (Keep `Remove-Item`
> out of any `cmd /c` PowerShell call — a sandbox guard rejects it.)
>
> Reference repos: `G:\SQLTerminal-Win32` (Win32 patterns + `Theme.h`, GPL-3.0) and
> `G:\Sentinel-lang` (the `snc` compiler + ADRs). **Two snc binaries:** `target\release\snc.exe`
> (C1.0b, stale — no signing) and `target\debug\snc.exe` (2026-06-28 — full ADR-0061 keygen/sign/
> verify + `--require-signatures`/`--trust`); the IDE auto-prefers the signing-capable one. The
> **build→link gap is resolved**: the IDE auto-detects `vcvars64.bat` and injects the MSVC env so
> the debug `snc`'s `link.exe` step works (Settings → MSVC env to override; `examples` builds at
> exit 0 → a runnable `crypto.exe`). Remaining gaps: `snc` has no tier/opt flag (tiers only set the
> output dir); a *release* snc would still need `libsentinel_runtime.a` on its own link path.
>
> Signing is **Sentinel-native ADR-0061** (Ed25519 + `sentinel-trust.toml` + capability grants),
> superseding Authenticode — **now implemented in both `snc` and the IDE** (phase 10). The UX
> spines under `_bmad-output/` are **draft** and still assume Authenticode + debug/release —
> reconciliation is parked.
>
> **A part of the IDE is now written in Sentinel** (phase 21): `tools/loc.sentinel` counts the
> source corpus the build hands it and the About box shows the lines-of-code badges, with the total
> computed by that Sentinel program. See "What's next" for what `snc` can/can't do today and the
> next dogfood candidates (a diagnostic parser, a trust-manifest validator, or a C-ABI crypto lib).
> **Pick a next task** from "What's next": write more in Sentinel, the remaining signing/targets
> follow-ons, the Direct2D editor, or PRD/spines reconciliation.
