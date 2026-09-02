---
title: "SentinelIDE — Reality Delta (artifacts vs. shipped code)"
status: reference
created: 2026-09-02
applies_to: "shipped Sentinel-IDE v0.1.7 (build 164), phases 1–42"
artifacts_dated: 2026-06-27 / 2026-06-28
sources:
  - "docs/HANDOVER.md"                       # phase list 1–42, Releases table
  - "docs/sentinel-project.md"               # project model + ADR-0061 trust model
  - "docs/prototype.md"
  - "src/host/win32/MainWindow.cpp"          # the shell: toolbar, chip, build/run, diagnostics
  - "src/core/Signing.h"                     # SignState, trust manifest, .sig, snc caps
  - "src/core/Project.h"                     # manifest, [[target]], tiers
  - "src/core/Settings.h"                    # what is actually configurable
  - "src/host/win32/Theme.h"                 # the palette tokens DESIGN.md names
  - "scripts/build.bat" / "CMakeLists.txt"   # versioning
governs:
  - "planning-artifacts/prds/prd-SentinelIDE-2026-06-27/prd.md"
  - "planning-artifacts/ux-designs/ux-SentinelIDE-2026-06-27/DESIGN.md"
  - "planning-artifacts/ux-designs/ux-SentinelIDE-2026-06-27/EXPERIENCE.md"
  - "planning-artifacts/briefs/brief-SentinelIDE-2026-06-27/brief.md"
---

# Reality Delta

The BMad planning artifacts in this folder were written **2026-06-27/28**, before the Win32
prototype existed, from a pre-prototype understanding of the product. The shipped product is
**Sentinel-IDE v0.1.7 (build 164)**, phases 1–42. Nothing in the artifacts was updated after
the prototype diverged.

**This file is the single verified record of that divergence.** Every claim below was checked
against the named source file, not against another document. Later edits to the PRD, DESIGN,
EXPERIENCE and brief **cite a row ID from here** (`RD-01` …) instead of re-deriving the facts.

## 0. Standing decisions this file assumes

| # | Decision | Consequence for every downstream edit |
|---|---|---|
| **D1** | **SPEC + gap register.** The spines remain an *intended-v1 SPEC*. Designed-but-unbuilt behaviour is **kept, not deleted** — but every such item is explicitly marked not-yet-built and appears in §5. | A reader must never be able to mistake intent for reality. When you keep an unbuilt requirement, mark it `[NOT BUILT — RD-nn]` in place. |
| **D2** | **The PRD is re-opened** despite `status: final`. Its signing FRs (FR-19..21), UJ-5, §5 SEC-5, the glossary "Code signing" / "Signing key" entries and OQ-9 are rewritten to ADR-0061. | See §6 for the exact target list, including the two adversarial-review HIGHs. |
| **D3** | **The token name `trust-verified` stays.** No rename to `trust-signed`, in docs or code. | `DESIGN.md` colors, `{colors.trust-verified}` references throughout EXPERIENCE.md, and `Theme.h:36 trustVerified` are all correct as written. Do not touch them. |

## 1. The delta table

**Class** — `STALE`: the artifacts describe something the product never did. `NOT BUILT`: the
artifacts specify something real and desirable that does not exist in code. `UNSPEC`: the product
ships something no artifact mentions.

| ID | Topic | What the artifacts say | What the code actually does | Proof | Class |
|---|---|---|---|---|---|
| **RD-01** | **Signing model** | Windows **Authenticode** signing of the produced PE. Import a `.pfx`/`.p12`/PEM **key file + passphrase**; the dialog shows the **loaded key's identity** (subject/issuer, "ACME Code Signing") and a **certificate validity window**. (PRD §3 glossary "Code signing"/"Signing key", FR-19, FR-20, UJ-5; EXPERIENCE Component Patterns "Import-key / Signing dialog", Flow 5; DESIGN `import-key-dialog`.) | **ADR-0061 Sentinel-native signing.** Ed25519 via `snc keygen` / `snc sign … --key … --grant …` / `snc verify … --sig …`. **No certificates, no passphrase, no identity, no subject/issuer, no validity window** — a key is a bare 64-hex Ed25519 public key. Trust is the consumer's own `sentinel-trust.toml`. | `src/core/Signing.h` (whole file; `verifyFile` L225, `sncSigningCaps` L206); `src/host/win32/SigningDialog.cpp` `doGenKey` L126 / `doSign` L141 / `doImport` L153; `docs/sentinel-project.md` §"Signing confirmations" | STALE |
| **RD-02** | **Trust store** | Not modelled. The artifacts have a key *in the IDE's memory for the session*, and nothing on the consumer side. | A file **`sentinel-trust.toml`** of `[[keys]]` array-of-tables: required bare-hex `pubkey`, optional `name` (diagnostics only), optional `grants` array. `deny_unknown_fields` upstream — `[dependencies.<name>]`, `sig`, `policy`, `forbids` are **hard parse errors that abort the build**. An `ed25519:` prefix parses and silently never matches. | `src/core/Signing.h` L1–18 (schema contract) and `loadTrust`; `SigningDialog.cpp::doImport` L153 (the writer); `docs/sentinel-project.md` | STALE |
| **RD-03** | **Build-time signature gate** | Absent. The artifacts have signing as a post-build *action on an artifact*, never as a gate on the build's **inputs**. | `[signing] require = off \| warn \| strict` in the manifest emits `--require-signatures <mode> --trust <path>` on the `snc build` line. `strict` refuses to build unsigned/untrusted sources. Editable in the Project Settings form. | `MainWindow.cpp::composeBuild` L1099 (last line of the function); `ProjectSettingsDialog.cpp` L219–221; `Project.h::SentinelProject::signRequire` L48 | UNSPEC |
| **RD-04** | **Trust chip — binding** | The chip is bound to the **build artifact**: "the chip is bound to the **current** artifact and is **never stale**"; a new build invalidates a prior Signed. (EXPERIENCE *Signing-indicator state model*; PRD FR-20/FR-21.) | The chip is bound to the **open source file**. `refreshSignState` looks for `<curFilePath>.sig`, reads the carrier, then runs `snc verify` asynchronously. It returns `Unknown` when no `.sentinel` file is open, and is recomputed on file **open** and on **save** — **NOT on edit**: `onEditChanged` never calls it, so a signed file that is being edited keeps showing `✓ Signed` until it is saved. It is **never** updated by a build. | `MainWindow.cpp::refreshSignState` L612; the only call sites are L648 (`loadFileIntoEditor`), L693 (`saveFile` — its comment "edits invalidate any existing .sig" explains why SAVING re-verifies, and must not be read as an edit hook), L1290 (`openSigning`), L1820 (Settings OK) | STALE |
| **RD-05** | **Trust chip — state count** | **SIX** states by precedence: `Signing… · Sign failed · Signed · Unsigned · Key loaded · No signing key`, with a full transition model. (EXPERIENCE *Signing-indicator state model*; PRD FR-21; DESIGN `status-signing` with six sub-tokens.) | **FOUR** rendered states: `✓ Signed` (`trustVerified`) · `⚠ Signature invalid` (`diagError`) · `… verifying` (`textMuted`) · `⊘ Unsigned` (`textSecondary`, the `default:` branch). There is no "key loaded" and no "no key" state — the IDE holds no key. `SignState` has five members, but `Unknown` falls through the `default:` and paints as **Unsigned**. | `MainWindow.cpp` L414–430 (the `switch`, L423–426); `Signing.h` L30 `enum class SignState { Unknown, Unsigned, Checking, Signed, Invalid }`. The dialog's `stateLabel` does render `Unknown` distinctly ("— no file open"), `SigningDialog.cpp` L46 | STALE |
| **RD-06** | **Post-build artifact signing** | FR-20: "reported only **after the signing operation is verified to have succeeded** (not merely attempted)"; EXPERIENCE: "the chip flips to Signed *only after* the IDE re-reads the produced artifact and confirms a valid signature — **never on the signer's exit code alone**." | Exactly what the spec forbids. On a successful build with `signing.sign = true` and a `sentinel.key` present, the IDE runs `snc sign <artifact> --key …` and prints `[signed · <name>.sig]` **off `runCapture`'s exit code, with no re-verify** (`verifyFile` is not called on this path). It also never touches the chip — see RD-04. | `MainWindow.cpp` `WM_APP_DONE` handler, the post-build signing block | STALE **+ live defect — FIXED** (phase 45): the path now calls `verifyFile` after a successful `snc sign` and only prints the green `[signed · <name>.sig · verified]` when the signature actually checks out; a sign that exits 0 but does not verify prints a red *"reported success but the signature does NOT verify — treat as UNSIGNED"*. The spec's rule ("never on the signer's exit code alone") now holds. |
| **RD-07** | **Project model** | "**Open a folder** as a project" — the project *is* a folder; the tree lists its `.sentinel` files. One project = one build. (PRD FR-1, FR-3; EXPERIENCE IA "Project tree — reached from **Open Folder**", Flow 1 step 1.) | A folder is a project only if it carries a **manifest**: `*.sntproject` (preferred) else legacy `sentinel.toml`. `findManifest` decides; a folder with neither opens as a plain **Files** view with no build. Open Project is a **manifest file picker**, not a folder picker. | `Project.h::findManifest` L129, `hasProject` L134, `loadProject`; `MainWindow.cpp::loadFolderPath` L751–765 (`if (hasProject(folder))` … else `g.sidebarView = 1`), `openProject` L783 (a manifest **file** picker) | STALE |
| **RD-08** | **Targets** | Absent. One project → one build → one executable. | **N `[[target]]` blocks** per manifest, each with `name` / `entry` / `type` (executable \| library \| shared) / per-target `links`. A manifest with none gets one synthesized target (back-compat, `explicitTargets = false`). Build / Run / output path / the tree's **Targets** group all follow the active target. | `Project.h::Target` L32, `parseTargets` L100, `SentinelProject::targets/explicitTargets` L52–53; `MainWindow.cpp::activeTarget` L118, `artifactPath` L1093 | STALE (absent axis) |
| **RD-09** | **Release tiers** | Absent as an axis. The artifacts' only build-configuration vocabulary is a single user-typed build command. **The words "debug" and "release" barely appear** — the missing axis is **tiers**, not debug/release. | **Four tiers — Development / Experimental / Stable / Hardened** (upstream `TIERED_RELEASES.md`), persisted as `[build] default_tier`, selectable per session, and driving the output directory `target/<dev\|experimental\|stable\|hardened>/`. | `Project.h::tierName` L20, `tierDir` L21, `tierFromName` L22, `defaultTier` L43; `MainWindow.cpp::projectOutDir` L1087, tier menu L1223 | STALE (absent axis) |
| **RD-10** | **Scheme selector** | Absent. The IA has no toolbar control between the buttons and the status bar. | An **Xcode-style scheme selector** in the toolbar: `[● target ▾ │ tier ▾]` with a type-coloured dot, plus a live derived output path `→ target\<tier>\<name>.exe`. Both zones open dropdowns; a target tree node also switches. | `MainWindow.cpp` L383–405 (paint), tier/target menus L1223 / L1233, hit-testing via `rScheme`/`rSchemeTarget`/`rSchemeTier` L109 | UNSPEC (IA region) |
| **RD-11** | **Project Settings dialog** | Absent. The artifacts have exactly one settings dialog (app Settings) and dialogs for About / toolchain remediation / find-replace / import-key. | A second modal: a **structured Project Settings form** over the manifest — name/version/type/entry, src / lib_paths / links / default tier, and the ADR-0061 **signing block** (require radio off/warn/strict, trust path, "Sign the built artifact"), plus a **TARGETS** section with per-target Name/Entry/Type editing. Save goes through a *surgical* writer that preserves comments and unmodeled keys. | `src/host/win32/ProjectSettingsDialog.cpp` (whole file; signing block L219–221); `Project.h::saveProject` | UNSPEC (dialog) |
| **RD-12** | **Build command** | "A developer can build … with the exact command visible **and configurable**" (FR-4); EXPERIENCE Settings row lists a **"build-command field"** in the Settings dialog. | **Not configurable.** The command is *composed* from the manifest + active target + tier by `composeBuild` and only **echoed** to the Output pane as `> <cmd>`. The Settings dialog has no build-command field; what it does expose is a **`snc.exe` path override** and an **MSVC `vcvars64.bat` override** (both blank = auto-detect). | `MainWindow.cpp::composeBuild` L1099, `runBuild` L1112–1136 (`outAppend(L"> " + cmd, …)`); `Settings.h::Settings` L18–26 (`sncPath`, `vcvarsPath`; no build command); `SettingsDialog.cpp` field list | STALE |
| **RD-13** | **Diagnostics — severity** | Every diagnostic carries a **severity**; the Problems list shows "file, line, message, **severity**"; squiggle styling reflects severity; there is a distinct **security** severity for `secret`/borrow/effect findings. (PRD FR-8, FR-9, FR-11; DESIGN `problems-list` error/warning/security row colors, `diag-security`.) | `struct Diag { file; line; col; msg; }` — **there is no severity field.** The Problems list has three columns: **Message · File · Line**. Every row renders identically; no shield glyph, no per-severity colour, no security class. | `MainWindow.cpp` L76 (`struct Diag`), L216–218 (the three `ListView_InsertColumn` calls), `addProblem` L991 | NOT BUILT |
| **RD-14** | **Diagnostics — squiggles & gutter** | Wavy underlines at the diagnostic's exact source range, coloured by severity; a **gutter shield glyph** for Sentinel-safety findings (`{components.squiggle-security}`, `{components.diagnostic-badge-security}`). This is the UJ-2 flagship moment. | **No squiggles anywhere.** After a build, `markErrorLines` tints the *whole line* background with `blend(windowBg, diagError, 24)` for every diagnostic in the open file, cleared on edit. The gutter paints **line numbers only**. No shield, no lock, no per-span rendering. | `MainWindow.cpp::markErrorLines` L528–552; gutter paint L334–352 | NOT BUILT |
| **RD-15** | **Versioning** | "Versioned from **0.1.0 (build 1)**, incrementing"; About renders `0.1.0 (build N)`. (PRD §0, §7, FR-17; EXPERIENCE About row.) | Marketing version is a literal in the build script (**`MKT=0.1.7`**); the build number is **derived from git**: `git rev-list --count HEAD` **+ `BUILDBASE=100`**, written into `build/generated/Version.h`. It never "increments" — the same commit always stamps the same number, and a dirty tree prints `BUILD_DIRTY`. A raw `cmake --build` without the script falls back to a stale `0.1.3 (build 0)`. Shipped: **0.1.7 (build 164)**. | `scripts/build.bat` L22–32 and L44–54; `CMakeLists.txt` L66–79 (the fallback header); `MainWindow.cpp` L56 `kVersion`; `AboutDialog.cpp` L170 | STALE |
| **RD-16** | **About dialog content** | Version + build, the **Sentinel/native mix** ("Sentinel 18% / Native 82%") and hardened-surface coverage. (FR-17, FR-15.) | Ships, and is **more** than specified: shields.io-style badge pills for **C++ · Sentinel · Build · Total** lines of code plus a "built in Sentinel" progress bar, whose grand total is counted by `tools/loc.sentinel` (Sentinel code counting the IDE's own source). Hardened-surface **coverage** is not rendered — only the LOC mix. | `AboutDialog.cpp` L34, L80–105 (badge row + bar), `Loc.h` from `scripts/loc.ps1` | Partly STALE / partly UNSPEC |
| **RD-17** | **Multi-tab editing** | "edit multiple `.sentinel` files **with tabs**"; tabs open/close/reorder, `•` dirty glyph, closing a dirty tab prompts. (FR-1; EXPERIENCE Tabs row; DESIGN `tab-active`/`tab-inactive`.) | **One buffer.** The "tab strip" is a painted label showing the single open file (`untitled` when none), with the `●` dirty dot and a coral underline. Opening a second file replaces the first. | `MainWindow.cpp` L321–326 (the whole tab strip), `g.curFilePath` / `g.curFileName` L100 | NOT BUILT |
| **RD-18** | **Editor commands** | `Ctrl+P` fuzzy open-file finder; find/replace incl. regex (`Ctrl+F`/`Ctrl+H`); go-to-line (`Ctrl+G`); multi-cursor (`Ctrl+Click`, `Alt+drag`); `F8`/`Shift+F8` next/previous problem. (FR-1, FR-3; EXPERIENCE *Interaction Primitives*, Fuzzy finder row.) | **None of these exist.** The accelerator table is exactly: `Ctrl+S` Save · `Ctrl+Z`/`Ctrl+Y` Undo/Redo · `Ctrl+N` New Project · `Ctrl+O` Open Project · `Ctrl+Shift+N` New File · `Ctrl+Shift+B` Build · `F5` Run · `Ctrl+L` Line Numbers · `Ctrl+,` Settings. Navigation to a diagnostic is by clicking a Problems row or an Output `file:line:col` link. | `MainWindow.cpp` L1917–1928 (the complete `ACCEL accels[]`); `gotoLineCol` L1150 | NOT BUILT |
| **RD-19** | **Toolchain readiness (UJ-3 / FR-12)** | A first-class **readiness surface**: checks `snc` + runtime archive + linker before/around a build, names the missing component, shows copy-pasteable remediation, says plainly when the fix is upstream. Has its own IA row and its own mockup. | **No readiness surface and no dialog.** What ships: `findVcvars` auto-detects `vcvars64.bat` and `captureMsvcEnv` injects the MSVC environment into the build child (which is what actually *closed* the link gap in phase 13), plus **one warning line in the Output pane** when no MSVC env is found. No component-by-component check, no remediation text, no upstream note. | `src/core/Toolchain.h` (`findVcvars`, `captureMsvcEnv`); `MainWindow.cpp::runBuild` L1130 (the single warning line); mockup `mockups/key-toolchain-readiness.html` has no counterpart in code | NOT BUILT (partial substitute) |
| **RD-20** | **Logging** | Specified in **EXPERIENCE only** (IA Settings row + Component Patterns Settings row, both tagged `[new]`): level Error/Warn/Info/Debug/Trace, log-file location, "reveal in Explorer". **No PRD FR covers it.** | Ships and matches: thread-safe append-only logger, level + path persisted in `settings.ini`, default `%LOCALAPPDATA%\SentinelIDE\logs\sentinelide.log`, with a working **Reveal** button. | `src/core/Logger.h`; `Settings.h` L20–21; `SettingsDialog.cpp` L63–66 (Reveal → `ShellExecuteW`), L149 | Spec'd in spine, **no PRD FR** |
| **RD-21** | **Auto-update** | Nothing. `SEC-4` mentions the IDE's *own* release provenance as roadmap, but no update mechanism appears in any artifact. | **WinSparkle 0.9.3** + an **Ed25519-signed `appcast.xml`** fetched over unauthenticated HTTPS from the public repo; a themed **Skip / Install / Later** dialog; startup check then **hourly** on the IDE's own timer (WinSparkle's periodic check is deliberately disabled). Refuses to init while the public key is the placeholder, and then hides the menu item rather than greying it. | `src/host/win32/Updater.cpp` (poll comment L144, `checkForUpdates` L302), `UpdateDialog.{h,cpp}`, `scripts/make-appcast.ps1`, `docs/RELEASING.md`; HANDOVER phases 32/40/41 | UNSPEC |
| **RD-22** | **Windows installer** | Nothing. | **Inno Setup** per-user `setup.exe` (`packaging/Sentinel-IDE.iss`, `scripts/make-installer.bat`) shipping the exe + examples + README/LICENSE, a Start-Menu shortcut, the file associations, and a full uninstall. Version is read from the built exe's **FileVersion** resource. Eight releases shipped this way. | `packaging/Sentinel-IDE.iss`; HANDOVER phases 28/33 + **Releases** table | UNSPEC |
| **RD-23** | **Sealed projects** | Nothing. | **`≡ ▸ Seal Project…` / `Open Sealed Project…`** — archive → LZMS compress → **AES-256-GCM** under a random DEK, wrapped per LUKS-style unlock slot (PBKDF2-HMAC-SHA256, 600k iters). Format **v2** (`SNTSEAL2`) with skippable slots and the header prefix bound as AEAD AAD; reads v1. Themed double-entry `PasswordDialog`. Covered by the repo's first test (25 assertions). | `src/core/Seal.h`; `PasswordDialog.{h,cpp}`; `tests/seal_test.cpp`; HANDOVER phases 25/31 | UNSPEC |
| **RD-24** | **File associations** | Nothing. | Per-user `HKCU\Software\Classes` ProgIDs `SentinelIDE.Project` / `SentinelIDE.Source` for `.sntproject` / `.sentinel`, with icons by negative resource id, registered from **`≡ ▸ Register File Associations…`** and mirrored by the installer. A double-clicked file opens its **nearest enclosing project**. | `src/core/FileAssoc.h`; HANDOVER phase 26 | UNSPEC |
| **RD-25** | **Unsaved-changes guard** | Partially specified, and narrower than reality: NFR-REL-1 "unsaved edits are never lost across build, run, or focus change", and EXPERIENCE's Tabs row "closing a dirty tab prompts to save". | A themed **Save / Don't Save / Cancel** modal behind **one choke point**, `confirmSaveIfDirty(hwnd, action)`, that **every** discarding path routes through: opening another file (tree, Problems row, Output link), switching target, Close Project, `WM_CLOSE`, `≡ ▸ Exit`, New Project, Open Sealed, Project Settings-over-a-dirty-manifest, and an **unattended auto-update install** (which is special-cased — the updater arms a 3-second `ExitProcess` watchdog, so it must not prompt). Cancel aborts and changes nothing. | `src/host/win32/SaveChangesDialog.{h,cpp}`; `MainWindow.cpp::confirmSaveIfDirty`; `Updater::updaterShutdownPending()`; HANDOVER phase 39 | Mostly UNSPEC |
| **RD-26** | **Single instance / drag-drop** | Nothing. | A named mutex keyed on the lowercased exe path; a second launch hands its path to the running instance via `WM_COPYDATA` and exits. Dropped files open via `WM_DROPFILES`. Both converge on `requestOpenPath` → posted `WM_APP_OPEN_PATH`, deferred while `uiIsBusy()`. | `src/host/win32/SingleInstance.h`; `MainWindow.cpp::requestOpenPath` L1176, `uiIsBusy` L1163; HANDOVER phase 42 | UNSPEC |
| **RD-27** | **Sentinel-in-the-binary** | The C-rule as *requirement* (FR-13: the untrusted-input surface is Sentinel), with no statement of what actually landed. | Four parsers are **compiled from Sentinel** into the binary via `snc build --lib` (ADR 0059) and called across the C-ABI: `parse_diag`, `parse_trust`, `parse_sig`, `parse_manifest` — i.e. **every file reader/parser in the IDE**. Each has a byte-identical C++ fallback held in lockstep by an `*_xcheck` test (11 + 12 + 12 + 14 cases). The manifest **writer** (`saveProject`) is still C++. | `src/sentinel/parsers.sentinel`; the `SENTINELIDE_SENTINEL` branches in `Project.h`, `Signing.h`, `MainWindow.cpp` L940–990; `CMakeLists.txt` L100–115; `tests/*_xcheck.cpp` | FR-13 substantially **met** — record it |

## 2. Signing: the concept-by-concept map

RD-01 in detail. Use this table when rewriting the PRD glossary and FR-19..21 — every left-hand
term must disappear from the artifacts, and the right-hand term is what replaces it.

| Artifact concept (Authenticode) | ADR-0061 reality | Where |
|---|---|---|
| `.pfx` / `.p12` / PEM key file | `sentinel.key` produced by **`snc keygen -o <path>`** | `SigningDialog.cpp::doGenKey` L126 |
| Passphrase | **none** — no passphrase anywhere in the signing path | `doSign` L141 (the full command is `snc sign <file> --key <key> [--grant g]…`) |
| Key identity (subject / issuer, "ACME Code Signing") | **none** — a key is 64 hex chars; the UI shows a 16-char prefix + `…` | `Signing.h::shortKey`; `SigInfo{algorithm,key,grants}` |
| Certificate validity window / expiry / revocation | **do not exist** — there are no certificates | `Signing.h`, whole file |
| RFC-3161 timestamping (PRD OQ-9) | **meaningless here** — nothing expires, so nothing needs a timestamp | — |
| Signing the produced **PE** | Signing **any file's raw bytes** into a detached, human-readable **`<file>.sig`** carrier (`algorithm` / `key` / `grants`) | `Signing.h::readSig`; `examples/crypto.sentinel.sig` |
| "Sign on Build" toggle | `[signing] sign = true` in the manifest, checkbox "Sign the built artifact" in Project Settings | `Project.h::signOutput` L50; `ProjectSettingsDialog.cpp` L220 |
| "Sign now" | The Signing & Trust panel's **Sign** button, operating on the **open file** (not on a build artifact) | `SigningDialog.cpp::doSign` L141 |
| Trust = a CA chain | Trust = the consumer's own `sentinel-trust.toml` `[[keys]]` list, plus a per-key `grants` ceiling | RD-02 |
| (nothing) | **Capability grants** — `snc sign --grant <cap>`, intersected against the trusted key's ceiling | `doSign` L141; §4 |
| (nothing) | **A build gate** — `--require-signatures off\|warn\|strict --trust <manifest>` | RD-03 |

**Two capabilities, not one.** `snc verify` is implemented inside `snc`; `snc keygen` / `snc sign`
shell out to `keygen_core.exe` / `sign_core.exe`, which are separate Sentinel programs that must
sit beside the `snc` binary. A build can advertise `snc keygen` in its help text and still fail at
runtime. The IDE therefore models `SncSigningCaps{verify, sign}` and picks the most capable `snc`
on disk rather than the first one found — greying only the buttons that genuinely cannot work.
(`Signing.h` L190–219; `MainWindow.cpp::findSnc` L1010–1030.)

## 3. Project and build: the shape the artifacts never had

```
manifest  (*.sntproject, else legacy sentinel.toml)      ← RD-07
   ├── [project]  name · version · type · entry · icon
   ├── [build]    src · lib_paths · links · default_tier ← RD-09
   ├── [signing]  require(off|warn|strict) · trust · sign ← RD-03
   └── [[target]] × N   name · entry · type · links      ← RD-08
                   ↓
         active target  ×  active tier                   ← RD-10  (the scheme selector)
                   ↓
    snc build [--lib|--shared] <entry> -o target/<tier>/<name>.<ext>
              [--lib-path …] [--link …]
              [--require-signatures <mode> --trust <manifest>]
                   ↓
         target/<tier>/<name>.exe   (+ optional <name>.exe.sig)  ← RD-06
```

The artifacts model **none** of this: no manifest, no targets, no tiers, no gate. Adding it is
not a wording fix — it is a new IA region (the scheme selector, RD-10), a new dialog (Project
Settings, RD-11), a new file format, and a rewrite of Flow 1's opening beat (RD-07).

## 4. Two ceilings the docs must not overclaim past

Both are upstream in Sentinel-lang and **cannot be fixed in this repo**. Any artifact text that
implies otherwise is wrong, however carefully hedged.

| # | Limit | Consequence for copy |
|---|---|---|
| **L-1** | `snc` v1's capability extractor **only ever detects `ffi`** (from `extern` blocks). `grants` is parsed and intersected for real, but `secret` / `constant_time` / `alloc` in a key's ceiling are **recorded intent, not an enforced gate**. `forbids` is unimplemented. | Never say a grants ceiling "restricts" or "enforces" what signed code may do. Identity and byte-integrity *are* genuinely enforced; capabilities are not. |
| **L-2** | `snc build --lib` / `--shared` **never invoke the trust gate** — `run_trust_gate` is called only from the executable path, and the library path silently discards the flags. | **Library and Shared targets display "signing: strict" and enforce nothing.** The IDE emits the flags for every target type (`composeBuild` L1099 appends them unconditionally). Any text describing `strict` as a guarantee must scope itself to executable targets. |

A third, smaller one worth carrying: **`snc` has no tier flag.** Everything builds at `-O0`
regardless of tier; tiers currently only choose the output directory. The IDE says so in the
Output pane on every project build (`MainWindow.cpp` L1128) — the artifacts should not describe
tiers as affecting optimization or hardening today.

## 5. Gap register — designed but NOT BUILT

Per **D1**, these stay in the spines as intended-v1 SPEC. Each must be marked in place, at the
point of the claim, in a form a skimming reader cannot miss — e.g. `**[NOT BUILT in v0.1.7 — RD-13]**`
— not relegated to a footnote or a status column at the end of a table.

| Ref | Not-yet-built item | Artifact locations that must carry the marker |
|---|---|---|
| **RD-13** | Diagnostic **severity** — no severity on `Diag`, no severity column, no security class | PRD FR-8 (severity styling), FR-9 (severity column), FR-11; DESIGN `problems-list` error/warning/security rows, `diagnostic-badge-security`; EXPERIENCE Problems list row, "Sentinel safety finding" state row |
| **RD-14** | **Squiggles** and the **gutter shield** — the UJ-2 flagship rendering | PRD FR-8, UJ-2, §12.1; DESIGN `squiggle-error`/`-warning`/`-security`; EXPERIENCE Flow 2 steps 2–3, Editor + Diagnostic-triad rows, `mockups/key-secret-leak-editor.html` |
| **RD-17** | **Multi-tab editing** — one buffer only | PRD FR-1; DESIGN `tab-active`/`tab-inactive`; EXPERIENCE Tabs row, IA "Editor area + tab strip" |
| **RD-18** | **`Ctrl+P` fuzzy finder** | PRD FR-3; EXPERIENCE IA "Fuzzy open-file finder", Fuzzy finder component row, Interaction Primitives |
| **RD-18** | **Find / replace (incl. regex), go-to-line, multi-cursor** | PRD FR-1; EXPERIENCE Editor row, Interaction Primitives, DESIGN `dialog` ("used for … find/replace") |
| **RD-18** | **`F8` / `Shift+F8` problem navigation** | EXPERIENCE Interaction Primitives |
| **RD-19** | **Toolchain-readiness surface** (UJ-3 / FR-12) — no check, no dialog, no remediation text | PRD UJ-3, FR-12, §12.1, §14; EXPERIENCE IA "Toolchain-readiness surface", Toolchain readiness component row, "Toolchain not ready" state row, Flow 3, `mockups/key-toolchain-readiness.html` |
| **RD-04/06** | The **verify-before-Signed** rule and the **artifact-bound, never-stale** chip | PRD FR-20, FR-21; EXPERIENCE *Signing behavior (honesty-critical)* bullets 2–3, *Signing-indicator state model* transitions |
| **RD-12** | A **user-configurable build command** | PRD FR-4; EXPERIENCE Settings IA row and Settings component row |
| **RD-16** | **Hardened-surface coverage** rendered in About (the LOC mix ships; coverage does not) | PRD FR-15, FR-17; EXPERIENCE About row, Proof & Co-evolution Surfaces |

**Shipped but unspecified** (the mirror list — these need *adding* to the artifacts, not marking):
RD-03 build-time signature gate · RD-08 targets · RD-09 tiers · RD-10 scheme selector ·
RD-11 Project Settings dialog · RD-21 auto-update · RD-22 installer · RD-23 sealed projects ·
RD-24 file associations · RD-25 the phase-39 unsaved-changes guard · RD-26 single-instance +
drag-drop. RD-20 logging is a half-case: specified in EXPERIENCE, absent from the PRD.

## 6. PRD reconciliation targets (D2)

The PRD is `status: final` and is being re-opened anyway. The concrete edit list:

| Target | Action |
|---|---|
| §3 Glossary — **"Code signing"** | Rewrite to ADR-0061 (RD-01). Delete "Windows **Authenticode**", "signing a built artifact … the executable `snc` produces". |
| §3 Glossary — **"Signing key"** | Rewrite: an Ed25519 keypair from `snc keygen`; no key file import, no passphrase, no identity (RD-01). Add **trust manifest** and **capability grant** as new glossary entries (RD-02). |
| **FR-19** (import a key file) | Replace wholesale: generate/select an Ed25519 key; sign the open file with optional grants; import a signature's key into the project's `sentinel-trust.toml` (RD-01, RD-02). |
| **FR-20** (sign built artifacts) | Rewrite to the two real paths — the Signing panel signing the **open file**, and the manifest's `signing.sign` signing the **build artifact**. Keep the verify-before-"Signed" requirement, marked **`[NOT BUILT — RD-06]`**. |
| **FR-21** (status indicator) | Six states → **four** (RD-05), bound to the **open file** not the artifact (RD-04). Keep the six-state precedence model marked `[NOT BUILT]` only if it still makes sense once the binding changes; a key-loaded state has no referent in a keyless model. |
| **New FR** (RD-03) | The build-time signature gate: `require = off/warn/strict` + `--trust`. Note **L-2** in its consequences. |
| **UJ-5** | Rewrite end to end: no `.pfx`, no passphrase, no "ACME Code Signing", no cert. The climax becomes the trust-manifest verification at build time, or the chip flipping on the open file. |
| **§5 SEC-5** | The "signing key held in a not-yet-hardened native surface" framing is now **wrong in the IDE's favour**: the IDE never holds key material — `snc` does the signing in a child process, and `sentinel.key` lives on disk in the project. Restate the actual residual risk instead of inheriting the Authenticode one. Note that the trust-manifest parser is now **Sentinel** (RD-27) — the first security-boundary port. |
| **§9 Aesthetic** | The `trust-verified` sentence is correct and stays (**D3**). |
| **OQ-9** (timestamping) | **Close as DEAD.** RFC-3161 timestamping is meaningless without certificates, and ADR-0061 has none. Say so explicitly rather than silently deleting the question. |
| **§17 Assumptions Index** | The FR-19 key-source assumption (`.pfx`/`.p12`/PEM; cert store and HSM as backlog) is void — remove or restate. |

**The two adversarial-review HIGHs.**

- **`review-adversarial-signing.md` C1 — timestamping.** Close as **DEAD**, with the reason on the
  record: Authenticode's expiry problem does not exist in a certificate-less Ed25519 model, so
  timestamping has nothing to protect. This is a *dissolved* finding, not a fixed one.
- **`review-adversarial-signing.md` B1 — "the IDE never reports an artifact as signed when it is
  not" has no verification path.** **Re-file it against the shipped code**, where it is no longer
  hypothetical: the post-build path at `MainWindow.cpp` L1683 prints **`[signed · <name>.sig]`**
  purely from `snc sign`'s exit code, never re-verifying the artifact (RD-06). The finding was
  right; the product built exactly the thing it warned about. (A1 — the UJ-5 climax overclaim —
  was already addressed in EXPERIENCE Flow 5 and is not re-opened here, though that flow is being
  rewritten anyway.)

## 7. What must NOT change

- **`trust-verified`** — the DESIGN token name stays (**D3**). It is cited by `{colors.trust-verified}`
  from EXPERIENCE.md and implemented as `Theme.h:36 trustVerified`; a rename breaks both. The
  existing DESIGN parenthetical explaining why the name was kept is still accurate.
- **The DESIGN palette generally.** `Theme.h` matches DESIGN.md's colour values exactly —
  `accent` `#D97757`, `diagError` `#E06C75`, `diagWarning` `#E5C07B`, `diagInfo` `#6CA8C4`,
  `diagSecurity` = accent, `trustVerified` `#7FB37A`, and the light-mode variants. DESIGN is the
  one artifact whose *colour* claims are true; its **component behaviour** claims (squiggles,
  six-state `status-signing`, `import-key-dialog`) are the stale part.
- **The C-rule / hardened-surface argument.** FR-13 is substantially met (RD-27) — do not weaken
  it while fixing everything around it.

## 8. Corrections to the briefing that produced this file

Four points where the working summary was imprecise. The rows above carry the corrected version.

1. **Logging is not unspecified.** It is specified in **EXPERIENCE.md** — the IA Settings row and
   the Settings component row, both tagged `[new]`, including level, log-file location and the
   "reveal in Explorer" affordance, all of which ship. What is missing is a **PRD FR** for it. The
   other five shipped-but-unspecified subsystems (auto-update, installer, sealed projects, file
   associations, single-instance/drag-drop) appear in **no artifact at all**. See RD-20.
2. **The unsaved-changes guard is partly specified.** NFR-REL-1 and the EXPERIENCE Tabs row
   ("closing a dirty tab prompts to save") do cover a slice of it. What is unspecified is the
   three-answer Save / Don't Save / Cancel modal, the single `confirmSaveIfDirty` choke point,
   and its coverage of window close, Exit, project close, target switch, seal, New Project and the
   unattended update install. See RD-25.
3. **The four-state chip is four *rendered* states over a five-member enum.** `SignState` is
   `{Unknown, Unsigned, Checking, Signed, Invalid}`; the status-bar `switch` handles four and lets
   `Unknown` fall through the `default:` branch, so **"no file open" paints as "⊘ Unsigned"**. The
   Signing dialog *does* label `Unknown` separately ("— no file open"), so the chip and the dialog
   disagree about the same state. Four is right for the artifact rewrite; the discrepancy is worth
   recording as a small live defect. See RD-05.
4. **The post-build Output text is `[signed · <name>.sig]`** — a middle dot, not a hyphen. Cite it
   exactly when re-filing B1, since the string is the evidence. See RD-06.

Everything else in the briefing checked out against the code: the Authenticode→ADR-0061 reversal,
the absent project/build model, the missing tier axis (debug/release genuinely barely appears in
the artifacts), the git-derived build number, the derived-and-echoed build command, the absent
severity/squiggles/shield, and both upstream ceilings (L-1, L-2).
