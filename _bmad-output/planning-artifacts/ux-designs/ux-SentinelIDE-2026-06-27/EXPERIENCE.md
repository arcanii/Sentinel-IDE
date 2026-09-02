---
name: SentinelIDE
title: "SentinelIDE — EXPERIENCE"
status: draft
created: 2026-06-27
updated: 2026-09-02
reconciled_against: "shipped Sentinel-IDE v0.1.7 (build 164), phases 1–42"
sources:
  - "{planning_artifacts}/REALITY-DELTA.md"                                  # RD-01..27 — the verified artifact↔code delta
  - "{planning_artifacts}/GAP-REGISTER.md"                                   # GAP-A*/GAP-B* — the single D1 gap register
  - "{planning_artifacts}/prds/prd-SentinelIDE-2026-06-27/prd.md"            # FR-1..21, UJ-1..5, §5 security, §6 NFRs
  - "{planning_artifacts}/prds/prd-SentinelIDE-2026-06-27/addendum.md"       # diagnostics reconciliation, seams
  - "{planning_artifacts}/briefs/brief-SentinelIDE-2026-06-27/brief.md"      # posture, scope
  - "{planning_artifacts}/prds/prd-SentinelIDE-2026-06-27/research-ide-landscape.md"  # table-stakes, native arch
design: "DESIGN.md"
---

# SentinelIDE — EXPERIENCE.md

> Behavior spine — information architecture, states, interactions, accessibility, flows.
> Owns *how it works*. Visual identity lives in [DESIGN.md](DESIGN.md); this spine references its
> tokens by `{path.to.token}`. **Both spines win on conflict** with any mock, wireframe, or import.
> Realizes PRD FR-1..21 and UJ-1..5.

## 0. How to read this document (SPEC + gap register)

This spine was written **2026-06-27/28**, before the Win32 prototype existed. The product shipped
as **v0.1.7 (build 164)**, phases 1–42, and diverged. On **2026-09-02** the spine was reconciled
against the code, row by row, using
[`REALITY-DELTA.md`](../../REALITY-DELTA.md) — the single verified record of the divergence.

**This document remains an intended-v1 SPEC, not a description of the product.** Designed
behaviour that was never built is **kept** here, because it is still what v1 should do — but it is
marked in place, at the point of the claim, so intent can never be mistaken for reality:

| Marker | Meaning |
|---|---|
| **`[NOT BUILT in v0.1.7 — RD-nn]`** | Specified here, **absent from the shipped product**. Do not demo it, do not cite it as a capability. Indexed in §12 and listed in [`GAP-REGISTER.md`](../../GAP-REGISTER.md). |
| **`[SHIPPED — RD-nn]`** | Real behaviour that appeared in **no artifact** before this reconciliation. Added here so the IA is complete. |
| **`[CORRECTED — RD-nn]`** | This spine previously described something **the product never did**. The old text is gone; the row says what replaced it. |

`RD-nn` cites a row of REALITY-DELTA's delta table. Every factual claim below traces to a source
file named there; nothing here was re-derived from another document.

**Three decisions govern this file and are not re-litigated.** (1) The spine stays a SPEC with an
explicit gap register (above). (2) The PRD is re-opened to match — its signing FRs, UJ-5 and the
Authenticode glossary are being rewritten to ADR-0061, and the timestamping open question is closed
as dead. (3) The DESIGN token **`{colors.trust-verified}` keeps its name** — it is cited throughout
this file and implemented as `Theme.h` `trustVerified`; it is **not** renamed to `trust-signed`.

**The two hard limits.** Both are upstream in Sentinel-lang and cannot be fixed in this repo. No
copy anywhere in the product may imply otherwise:

- **L-1 — capability grants are recorded intent, not an enforced gate.** `snc` v1's capability
  extractor only ever detects `ffi` (from `extern` blocks). A key's `grants` ceiling is parsed and
  intersected for real, but `secret` / `constant_time` / `alloc` are not enforced; `forbids` is
  unimplemented. Identity and byte-integrity **are** genuinely enforced. Never write that a grants
  ceiling "restricts" or "prevents" what signed code can do.
- **L-2 — `strict` is a guarantee for executable targets only.** `snc build --lib` / `--shared`
  never invoke the trust gate; the library path silently discards the flags. The IDE emits
  `--require-signatures` for every target type, so a **Library or Shared target displays
  "signing: strict" and enforces nothing.** Scope every claim about `strict` to executables.
- A third, smaller one: **`snc` has no tier flag.** Everything compiles at `-O0` regardless of
  tier; tiers currently choose only the output directory. Tiers must never be described as
  affecting optimization or hardening today.

## Foundation

**Form-factor:** native Windows desktop application, **single-window IDE**, Windows-first (x64). Not Electron, not a web app in a shell — the native-performance and security story depend on bypassing a browser engine (PRD §7, research §2).

**UI system:** native **Win32 + Common Controls v6** — a RichEdit-style code editor, a `SysTreeView32` project sidebar, virtual `SysListView32` lists (Problems), themed native dialogs, and a status bar. The IDE is a **thin native host** (the only place non-Sentinel "chrome" code is permitted) that embeds **Sentinel-written surfaces across a C-ABI boundary**.

> **[SHIPPED — RD-27]** The C-rule landed further than v1 planned for. **Four** parsers are compiled
> from Sentinel into the binary via `snc build --lib` and called across the C-ABI — `parse_diag`,
> `parse_trust`, `parse_sig`, `parse_manifest`, i.e. **every file reader in the IDE**, including the
> code that decides which keys a build trusts. Each has a byte-identical C++ fallback held in
> lockstep by a cross-check test. The manifest *writer* is still C++. FR-13 is substantially met.

**What the user runs against:** the IDE shells out to a local **`snc.exe`** for builds, runs, signing and verification. Diagnostics come from `snc` (build-authoritative) — parsed out of the build output as `path.sentinel:line:col`. Fast on-keystroke diagnostics (`snc parse` / `sentinel-lsp`) are **[NOT BUILT in v0.1.7 — RD-13]**; today a diagnostic exists only after a build.

**What a project is:** a folder is a project **only if it carries a manifest** — `*.sntproject`
(preferred) or the legacy `sentinel.toml`. The manifest declares `[project]`, `[build]`,
`[signing]`, and **N `[[target]]` blocks**; the active target crosses with one of **four release
tiers** to determine what is built and where it lands. **[CORRECTED — RD-07/08/09]** This spine
previously assumed "Open Folder" and one configurable build command; neither is the product. See
§*Project, targets and tiers* below.

`snc` is auto-detected (overridable in Settings), and the IDE picks the **most capable** `snc` on
disk rather than the first — because `verify` and `keygen`/`sign` are **separate capabilities that
fail independently** (`snc verify` is internal; `keygen`/`sign` shell out to `keygen_core.exe` /
`sign_core.exe`, which must sit beside the binary). A build can advertise `snc keygen` in its help
text and still fail at runtime.

The toolchain may be incomplete on a given machine. A first-class readiness surface (FR-12) is
**[NOT BUILT in v0.1.7 — RD-19]**; what ships is MSVC-environment auto-detection plus one warning
line in the Output pane.

## Project, targets and tiers  **[SHIPPED — RD-07/08/09/10]**

The axis this spine was missing entirely. It is not a wording fix — it is an IA region, a dialog,
and a file format.

```
manifest  (*.sntproject, else legacy sentinel.toml)
   ├── [project]  name · version · type · entry · icon
   ├── [build]    src · lib_paths · links · default_tier
   ├── [signing]  require(off|warn|strict) · trust · sign
   └── [[target]] × N   name · entry · type(executable|library|shared) · links
                   ↓
         active target  ×  active tier          ← the scheme selector
                   ↓
    snc build [--lib|--shared] <entry> -o target/<tier>/<name>.<ext>
              [--lib-path …] [--link …]
              [--require-signatures <mode> --trust <manifest>]
                   ↓
         target/<tier>/<name>.exe   (+ optional <name>.exe.sig)
```

- **Targets.** A manifest with no `[[target]]` block gets one synthesized from `[project]`/`[build]`
  (back-compat). Build, Run, the output path, the window title, the tree's **Targets** group and the
  scheme selector all follow the **active** target.
- **Tiers.** **Development · Experimental · Stable · Hardened.** `default_tier` persists in the
  manifest; the active tier is a **session** choice that resets to the default on project load.
  Today a tier chooses only `target/<dev|experimental|stable|hardened>/` — see L-3 above; the
  Output pane says so on every project build, and the product must keep saying so.
- **No project, still usable.** A folder without a manifest opens as a plain **Files** view with no
  build; a single `.sentinel` file can still be built ad hoc to a sibling `.exe`.

## Information Architecture

One window, three persistent regions (tree · editor · bottom dock) plus a toolbar and a status bar; dialogs overlay for discrete tasks.

| Surface | Reached from | Purpose |
|---|---|---|
| **Editor area** | App open, tree, Problems row, Output link | Edit `.sentinel` files; the priority surface, never collapses (FR-1, FR-2). **One buffer** — the tab strip is a single painted label with a `●` dirty dot. Multi-tab open/close/reorder is **[NOT BUILT in v0.1.7 — RD-17]** |
| **Project tree** | **Open Project** (a *manifest file* picker), New Project, Recent Projects, drag-drop, double-clicked file | Two switchable views **[SHIPPED — RD-07]**: **Project** (manifest · trust manifest · Targets · Sources) for a manifest-declared project, **Files** (plain `.sentinel` listing) otherwise. Activating a node opens the file; the project node opens Project Settings; a target node switches the active target |
| **Fuzzy open-file finder** | `Ctrl+P` | Subsequence match on path; select → open (FR-3). **[NOT BUILT in v0.1.7 — RD-18]** |
| **Scheme selector** (toolbar) | Always visible when a project is loaded | **[SHIPPED — RD-10]** Xcode-style `[● target ▾ │ tier ▾]` with a type-coloured dot, plus the live derived output path `→ target\<tier>\<name>.exe`. Each zone opens its own dropdown |
| **Output pane** (bottom dock) | Build/Run, dock tab | Live-streamed build/run stdout/stderr; the composed command echoed as `> …`; clickable `file:line[:col]` (FR-5, FR-6, FR-7) |
| **Problems list** (bottom dock) | Diagnostics present, dock tab | One row per diagnostic; select → jump to the line. Columns are **Message · File · Line** — a **severity** column is **[NOT BUILT in v0.1.7 — RD-13]** |
| **Toolchain-readiness surface** | First build / readiness failure | Names the missing component + copy-pasteable remediation (FR-12). **[NOT BUILT in v0.1.7 — RD-19]** — substituted by MSVC auto-detect + one Output warning line |
| **Project Settings** (dialog) | App menu › Project Settings…, or the project node in the tree | **[SHIPPED — RD-11]** Structured form over the manifest: PROJECT (name/version/type/entry) · BUILD (src, lib paths, links, default tier) · TARGETS (per-target name/entry/type) · **SIGNING** (require off/warn/strict, trust path, "Sign the built artifact"). Saves through a surgical writer that preserves comments and unmodeled keys |
| **Settings / Preferences** (dialog) | App menu / `Ctrl+,` | Editor font override, theme (follow/light/dark), **logging** (level + log-file path + Reveal), and **BUILD TOOLCHAIN**: `snc.exe` path override and MSVC `vcvars64.bat` override, both blank = auto-detect **[CORRECTED — RD-12]**. A **build-command field** is **[NOT BUILT in v0.1.7 — RD-12]** — the command is composed, not configured |
| **About** (dialog) | App menu | Version `0.1.7 (build 164)` **[CORRECTED — RD-15]** + the **Sentinel/native mix** rendered as badge pills (C++ · Sentinel · Build · Total LOC) and a "built in Sentinel" bar (FR-17, FR-15). **Hardened-surface coverage** is **[NOT BUILT in v0.1.7 — RD-16]** |
| **Build/Run controls** | Toolbar / menu / shortcut | Invoke `snc build` for the active target×tier / run the built executable; the exact command line is echoed into Output (FR-4) |
| **App menu** (`≡` popup) | Toolbar `≡` button | New/Open/Recent/Close Project · Seal / Open Sealed · New File · Save · Undo/Redo · Project Settings · Build · Run · Line Numbers · Signing & Trust · Register File Associations · Settings · Check for Updates · About · Exit |
| **Signing & Trust** (dialog) | App menu › Signing & Trust…, or the status chip | **[CORRECTED — RD-01]** ADR-0061 panel over the **open file**: state line, key path (Generate → `snc keygen` / Browse), a capability-grants field, **Sign · Verify · Import**, and the project's `sentinel-trust.toml` rendered as **Name · Key (16-hex prefix …) · Grants**. No key file import, no passphrase, no identity, no certificate |
| **Signing status chip** (status bar) | Always visible; click → Signing & Trust | The **open source file's** signature state, four rendered states (FR-21). **[CORRECTED — RD-04/05]** |
| **Save changes?** (dialog) | Any path that would discard an unsaved buffer | **[SHIPPED — RD-25]** Three answers — **Save · Don't Save · Cancel** — behind one choke point. Cancel aborts the command that raised it |
| **Password** (dialog) | Seal Project… / Open Sealed Project… | **[SHIPPED — RD-23]** Themed double-entry on seal, single-entry on unseal |
| **Update available** (dialog) | Startup + hourly check, or App menu › Check for Updates… | **[SHIPPED — RD-21]** New version + current version in one composed line, **Skip this version · Install now · Later** (Later is the default). **No release notes are rendered** — the dialog shows a single sentence, not the appcast's notes |
| **File associations** | App menu › Register File Associations… | **[SHIPPED — RD-24]** Per-user ProgIDs for `.sntproject` / `.sentinel`; a double-clicked file opens its **nearest enclosing project** |

The **bottom dock** hosts Problems and Output as switchable tabs, above a draggable splitter. Dialogs stack **one level deep** — never a dialog atop a dialog. The **app menu** is a native popup off the toolbar `≡` button (not a permanent menu bar — faithful to the reference).

→ Composition references: [`mockups/key-secret-leak-editor.html`](mockups/key-secret-leak-editor.html) — the UJ-2 secret-leak moment **(depicts [NOT BUILT — RD-14] rendering)**; [`mockups/key-toolchain-readiness.html`](mockups/key-toolchain-readiness.html) — the UJ-3 remediation dialog **(depicts [NOT BUILT — RD-19]; no counterpart in code)**; [`mockups/key-signing.html`](mockups/key-signing.html) — **stale: it shows the Authenticode import-key dialog this spine no longer specifies (RD-01)**. All three predate the prototype. Remaining surfaces are **spine-only**. **Spines win on conflict.**

## Voice and Tone

Microcopy for all product-generated text — diagnostics, build/run status, toolchain guidance, signing state. Brand voice and aesthetic posture live in [DESIGN.md](DESIGN.md). The register is **precise, calm, non-alarmist — assurance, not noise** (PRD §9). For a security tool, panic is a failure mode: the UI states facts and the fix, never raises its voice.

| Do | Don't |
|---|---|
| "`secret` reaches a branch condition." | "⚠️ DANGER: secret leak detected!!!" |
| "Runtime archive not found — build can compile but not link." | "Build failed." (with no cause) |
| "This fix is upstream (Sentinel gap #1); no local workaround yet." | Imply a local fix that doesn't exist (FR-12) |
| "[done · exit 0]" | "Process completed successfully ✓🎉" |
| "Highlighting reduced on very long lines." | "Performance degraded." |
| Name the component, the span, and the next step. | Hide the failure or dress it up. |
| "✓ Signed" · "⊘ Unsigned" · "Key: `a3f19c40b7e2…`" | "🔒 SECURE!" / "Verified" / any claim of vetted identity |
| "(tier Hardened: snc has no tier flag yet — built at -O0 → target\hardened)" | Let a tier name imply optimization or hardening it doesn't do |
| "warn/strict pass `--require-signatures --trust`." | "strict guarantees every dependency is trusted." (false for library/shared targets — **L-2**) |

**Honesty rule (load-bearing):** never claim more safety than the product delivers. The product line is *"the interpretation of untrusted bytes is Sentinel,"* not *"100% Sentinel / fully hardened"* (PRD §5). Copy in About, Settings, Project Settings and any proof-facing text holds that line — including the two upstream limits in §0, which the UI must not paper over.

**[CORRECTED — RD-01]** Rows about a loaded key's identity ("Signed · key: ACME Code Signing") and about the key living in a not-yet-hardened native surface are gone. There is no identity to show — a key is 64 hex characters — and **the IDE never holds key material**: `snc` signs in a child process and the key file lives on disk in the project.

## Component Patterns

Behavioral rules; visual specs live in [DESIGN.md `Components`](DESIGN.md).

| Component | Behavioral rules |
|---|---|
| **Editor** | Undo/redo (multi-level, RichEdit-native — the highlighter runs with undo suspended so it never pollutes the stack). Incremental highlighting on edit (no full-file re-parse stall, FR-2). Unsaved edits **never lost** on build/run/focus change (NFR-REL-1) — a build **auto-saves** the buffer first. Renders `{components.squiggle-*}` at diagnostic spans — **[NOT BUILT in v0.1.7 — RD-14]**; what ships is a whole-line background tint after a build, cleared on the next edit. Multi-cursor, find/replace incl. regex, and go-to-line are **[NOT BUILT in v0.1.7 — RD-18]** |
| **Tab strip** | Shows the **one** open file (`untitled` when none) with a leading `●` dirty glyph and a coral rule the width of the label (`{components.file-label}`). Opening another file **replaces** it, after the unsaved-changes guard. A real tab strip — open/close/reorder, `{components.tab-active}`/`{components.tab-inactive}` — is **[NOT BUILT in v0.1.7 — RD-17]** |
| **Project tree** | Two views, **Project** and **Files**, switched by a word pair at the top of the sidebar (`{components.tree-view-switcher}`). Project view is rooted at the project name (an S-shield node) with the manifest, the trust manifest (when present), a **Targets** group (only when the manifest declares more than one), and **Sources**. Activating a *file* node opens it (guarded); the *project* node opens Project Settings; a *target* node calls setActiveTarget (guarded). Files view mirrors on-disk `.sentinel` files. `{components.project-tree}` |
| **Scheme selector** | **[SHIPPED — RD-10]** Two hit zones in one bordered control. Left: a type dot (`{colors.accent}` executable · `{colors.diag-info}` library · `{colors.diag-warning}` shared) + the active target's name, with `▾` only when the project has more than one target. Right: a tier dot + the tier name, always with `▾`. To its right, the derived output path in `{typography.editor}` / `{colors.text-muted}`. Changing the target is a **guarded** action; changing the tier is not (it discards nothing). `{components.scheme-selector}` |
| **Output pane** | Streams build/run output **line-by-line, live — never batched at exit** (FR-5). Preserves order. **[CORRECTED]** Colour is keyed by **line content, not by stream** (`{components.output-pane}`): stdout and stderr are merged before colouring, and a line reads as an error because it contains `×`/`error`, not because of where it came from. A true stdout/stderr split is **[NOT BUILT in v0.1.7]**. The composed command is echoed first as `> <cmd>`, followed by the tier note and the MSVC-environment note. `file:line[:col]` is a click target → moves the caret there (FR-6, guarded). Terminal lines: `[done · exit N]`, and for a signing build `[signed · <name>.sig]` / `[sign failed · exit N]` |
| **Problems list** | Exactly **one row per current diagnostic**, columns **Message · File · Line**; select → caret to the line (FR-9, guarded). A **severity** field and the `{components.diagnostic-badge-security}` shield on Sentinel-safety findings are **[NOT BUILT in v0.1.7 — RD-13]**: the shipped `Diag` record carries file/line/col/message only, and every row renders identically |
| **Diagnostic triad** | Squiggles (FR-8), Problems list (FR-9), and clickable output (FR-6) are driven by **one Diagnostic model** (FR-10). Shipped: two of three legs — Problems and clickable Output share the model; the squiggle leg is **[NOT BUILT in v0.1.7 — RD-14]**, substituted by line tinting |
| **Build / Run controls** | Build composes `snc build` from **manifest + active target + tier**, echoes the exact command, and runs it as a child process **off the UI thread** with the MSVC environment injected (FR-4, NFR-PERF-3). Run executes the built binary for the active target and reports stdout/stderr + **exit code** (FR-7); it refuses on non-executable targets and when no artifact exists yet. **[CORRECTED — RD-12]** the command is *derived and shown*, not user-editable |
| **Toolchain readiness** | Checked before/around a build; names the specific missing component and shows copy-pasteable remediation; where the fix is upstream, says so plainly (FR-12). **[NOT BUILT in v0.1.7 — RD-19].** The shipped substitute: `vcvars64.bat` auto-detection, MSVC environment capture into the build child, and a single `{colors.diag-warning}` Output line when none is found |
| **Settings** | Editor font (default Cascadia Code; any installed mono); theme override (follow / light / dark); **logging** — level (Error/Warn/Info/Debug/Trace) + log-file path with a **Reveal** button that opens Explorer at the file; **build toolchain** — `snc` path and MSVC env path, each showing the auto-detected result when blank. Changes apply without restart |
| **Project Settings** | **[SHIPPED — RD-11]** A structured form over the manifest, not a text editor. Entry is a combo populated from the project's `.sentinel` files. The SIGNING block writes `[signing] require/trust/sign` and states its own consequence: *"warn/strict pass `--require-signatures --trust`; signing uses `sentinel.key` in the project."* Saving rewrites the manifest **preserving comments and unmodeled keys**, then reloads the project (which resets the active tier to the manifest default). Opening it over a **dirty manifest buffer** is guarded — the form would otherwise overwrite the raw edits. `{components.project-settings-form}` |
| **About** | Renders the marketing version + git-derived build number and the current **Sentinel/native mix** as badge pills plus a progress bar, counted by `tools/loc.sentinel` — *Sentinel code counting the IDE's own source*, which is itself the proof (FR-17, FR-15). Hardened-surface **coverage** is **[NOT BUILT in v0.1.7 — RD-16]** |
| **Status bar** | Left: caret position (`Ln N, Col N`). Middle: transient status ("Building…", "Build finished — exit 0 · 3 problem(s)", the open path). Right: the **signing chip** then the version. Indentation / EOL-mode indicators are **[NOT BUILT in v0.1.7]**. The proof *metric* is **not** here — it stays in About; signing is operational status, distinct from the proof metric |
| **App menu (`≡`)** | A `≡` button opens a native popup (`{components.menu-popup}`). Items grey out against real state (Seal needs a project; Save needs a dirty buffer; Undo/Redo track `EM_CANUNDO`/`EM_CANREDO`). **Check for Updates** is **hidden, not greyed**, when auto-update is unavailable — a permanently disabled item invites "why?" and the user can do nothing about it. A popup, not a permanent bar (reference-faithful) |
| **Signing status chip** | Status-bar chip, **always visible**, bound to the **open source file**. Four rendered states (below). Click → Signing & Trust. The one always-on security signal in the chrome (FR-21) |
| **Signing & Trust dialog** | **[CORRECTED — RD-01]** Operates on the **open `.sentinel` file**, never on a key store. **Generate** runs `snc keygen -o <path>`; **Browse** picks an existing key; **Sign** runs `snc sign <file> --key <key> [--grant <cap>]…`, producing a detached, human-readable **`<file>.sig`** carrier (`algorithm` / `key` / `grants`); **Verify** re-runs `snc verify`; **Import** appends the open file's signing key to the project's `sentinel-trust.toml` as a `[[keys]]` table. Buttons grey by **measured** `snc` capability — `verify` and `keygen`/`sign` are independent, and a disabled button here is honesty, not breakage. `{components.signing-panel}`; the old `{components.import-key-dialog}` is retired DEAD |
| **Trust manifest list** | The `sentinel-trust.toml` rendered as **Name · Key · Grants**, keys shown as a 16-char prefix + `…`. The manifest schema is `deny_unknown_fields` **upstream**: `[[keys]]` with a required **bare 64-hex** `pubkey`, optional `name` (diagnostics only) and optional `grants`. An `ed25519:` prefix parses and **silently never matches**; any other key (`sig`, `policy`, `forbids`, `[dependencies.…]`) is a hard parse error that **aborts the build** in both warn and strict. The IDE's writer stays in lockstep with that schema |
| **Save changes? dialog** | **[SHIPPED — RD-25]** Themed modal (`{components.save-changes-dialog}`), three answers. "Save changes to *file*?" / "Your changes will be lost if you continue *&lt;action&gt;* without saving." **Save** is the default; **Cancel** is also `Esc` and the close box. `Don't Save` deliberately leaves the buffer dirty — so a second guard on the same command would re-prompt after the work is already done. Guard **once**, as early as anything writes to disk |
| **Password dialog** | **[SHIPPED — RD-23]** Double entry when sealing, single when opening. `{components.password-dialog}`. Password bytes are zeroized after use |
| **Update dialog** | **[SHIPPED — RD-21]** New version + current version, one composed sentence — **not release notes**, which the dialog does not render (`{components.update-dialog}`); **Later** is the default and takes focus — the deliberate opposite of the save prompt, because an update is never urgent enough to interrupt |

## State Patterns

| State | Surface | Treatment |
|---|---|---|
| **No project open** | Editor area + tree | Empty state: the editor area reads "`≡  ▸  New Project…   or   Open Project…`"; the sidebar reads "(no folder open)". Calm, no marketing. Recent Projects live in the app menu |
| **Folder without a manifest** | Tree | **[SHIPPED — RD-07]** Opens in the **Files** view, listing `.sentinel` files. No project, no targets, no tiers; Build falls back to a single-file `snc build <file> -o <file>.exe` |
| **Project loaded** | Tree · toolbar · title bar | **[SHIPPED — RD-08/09]** Project view populates; the scheme selector appears; the active tier resets to the manifest default; the window title becomes `Sentinel-IDE — <project> › <target> (<type>) · <tier>`; the target's entry source opens automatically. The project is added to Recent Projects |
| **Empty project** | Project tree | "No `.sentinel` files here yet." New File writes into the project's `src/` when it exists |
| **Building** | Output pane + controls | Build button reads "Building…"; output streams live; **editor stays fully interactive** — type/scroll/navigate never blocked (NFR-PERF-3). No modal. A second Build is ignored while one runs |
| **Build succeeded** | Output pane | `[done · exit 0]` in `{colors.trust-verified}`. Run becomes available for executable targets |
| **Build failed (compile)** | Triad | Diagnostics populate the Problems list and clickable output from one model; the dock switches to Problems. Error lines in the open file are tinted with a blend of `{colors.window-bg}` and `{colors.diag-error}`, cleared on the next edit. **Squiggles are [NOT BUILT in v0.1.7 — RD-14]** |
| **Sentinel safety finding** | Triad | `secret`-leak / borrow / effect render with the **distinct security signature** — `{components.squiggle-security}` coral squiggle + gutter shield, Problems row shield-marked. This is the UJ-2 climax made routine. **[NOT BUILT in v0.1.7 — RD-13/RD-14]** — the shipped diagnostic model has no severity, so such a finding is currently indistinguishable from an ordinary compile error |
| **Running** | Output pane | stdout/stderr stream; **exit code** shown on completion; UI never blocks (FR-7). Run on a library/shared target is refused with a status-bar note rather than a dialog |
| **Toolchain not ready** | Readiness surface | Names the gap; shows remediation; if upstream, says there is no local fix yet (FR-12, UJ-3). **[NOT BUILT in v0.1.7 — RD-19]** — the shipped behaviour is one Output warning line naming Settings → MSVC environment |
| **Unsaved edits** | Tab strip · toolbar · every discarding path | `●` dirty glyph and a coral "● Save"; never lost across build/run/focus (NFR-REL-1); a build auto-saves first. Any path that would **discard** the buffer raises the Save/Don't Save/Cancel prompt — see *The unsaved-changes guard* |
| **Very long line / large file** | Editor | Heavy per-line highlighting reduced beyond very long lines; edits stay responsive (FR-2, NFR-PERF-5). Quiet status note, not an error |
| **Diagnostics reconciling** | Triad | Fast on-keystroke and authoritative on-build sources are deduped; **build supersedes keystroke** for overlapping ranges (addendum). **[NOT BUILT in v0.1.7 — RD-13]** — only the on-build source exists, so there is nothing to reconcile yet |
| **Cold start** | Whole app | Sub-second to interactive `[ASSUMPTION]` (NFR-PERF-2); no splash theatrics. A second launch does not start a second app — it hands its path to the running instance and exits **[SHIPPED — RD-26]** |
| **No file open** | Status chip | The chip renders `⊘ Unsigned`. **Known defect (RD-05):** the underlying state is `Unknown`, and the Signing dialog labels it correctly ("— no file open"), but the chip's `switch` lets `Unknown` fall through to the Unsigned branch. The chip and the dialog disagree about the same state; the chip should read "—" or be blank |
| **Open file unsigned** | Status chip | `⊘ Unsigned` in `{colors.text-secondary}` — no `.sig` beside the file. The neutral default: normal, never alarmist |
| **Verifying** | Status chip | `…  verifying` in `{colors.text-muted}` while `snc verify` runs on a worker thread; never blocks the UI. A result for a since-closed file is discarded |
| **Open file signed** | Status chip | `✓ Signed` in `{colors.trust-verified}` — `snc verify` confirmed the file's bytes match the `.sig`. The panel also shows `Key: <16-hex>…` and any grants |
| **Signature invalid** | Status chip | `⚠ Signature invalid` in `{colors.diag-error}` — a `.sig` exists but the bytes no longer match. The ordinary way to reach this is **editing a signed file and then saving it**; the chip re-computes on open and on save — **not on edit**, so it keeps reading `✓ Signed` while the edit is unsaved |
| **Signing failed** | Dialog | `snc`'s own stderr in a message box naming the cause; the file stays unsigned and the chip does not move. Causes are missing key, missing `sign_core.exe`, or an `snc` build without the sign capability — **never** a passphrase |
| **Signing unverifiable** | Status chip | **Honesty defect (see §*Signing behavior*).** When the resolved `snc` cannot `verify`, a `.sig` file existing beside the source is enough to paint `✓ Signed` with **no verification performed**. The dialog is honest about it ("Signature present — this snc build can't verify it"); the chip is not |
| **Build gate active** | Output pane | With `require = warn|strict`, the build line carries `--require-signatures <mode> --trust <path>`, and `snc` reports untrusted sources into the ordinary diagnostic stream. **strict enforces for executable targets only (L-2)** — a library or shared target shows the same flags and gates nothing |
| **Artifact signed after build** | Output pane | With `signing.sign = true` and a `sentinel.key` in the project, a successful build runs `snc sign` on the artifact and prints `[signed · <name>.sig]`. **This line is printed off the signer's exit code, with no re-verification — see the honesty rule below (RD-06). The status chip is not involved: it tracks the open source file, not the artifact** |
| **Sealing / unsealing** | Output pane + status | **[SHIPPED — RD-23]** `[sealed · <name>.sealed]` in `{colors.trust-verified}`, followed by the plain note that the plaintext project is unchanged. Unseal decrypts to a sibling `<name>-unsealed` folder (numbered if taken) and opens it; a wrong password fails cleanly and removes the empty destination |
| **Update available** | Dialog | **[SHIPPED — RD-21]** Offered at startup and hourly. **Later** is the default. Installing posts a close to the main window and the updater arms a 3-second exit watchdog — which is why the unsaved-changes guard **must not prompt** on that path (below) |

### Signing-indicator state model  **[CORRECTED — RD-04/RD-05]**

*Backs PRD **FR-21** ("one state at a time, by precedence").* The chip is bound to the **open source
file** and collapses one dimension — *does this file carry a signature whose bytes still verify?* —
into one state.

| # | State | Shown when | Token |
|---|---|---|---|
| 1 | **⚠ Signature invalid** | a `.sig` exists but `snc verify` rejects it (the usual cause: the file was edited) | `{colors.diag-error}` |
| 2 | **… verifying** | a verify is in flight on the worker thread | `{colors.text-muted}` |
| 3 | **✓ Signed** | `snc verify` confirmed the file's bytes against its `.sig` | `{colors.trust-verified}` |
| 4 | **⊘ Unsigned** | no `.sig` beside the open file — the neutral default | `{colors.text-secondary}` |

All four open the Signing & Trust panel on click. **Transitions:** opening a `.sentinel` file
recomputes from scratch (`Unsigned` if no `.sig`, else `verifying` → `Signed`/`Signature invalid`);
**saving** recomputes (the save is what invalidates the `.sig`; editing alone does **not** re-run the check); **Sign** in the panel
recomputes on close. A build **never** touches the chip.

**The two states this model used to have and no longer does.** *Key loaded* and *No signing key*
are **deleted as never-true, not deferred**: the IDE holds no key at any time, so neither state has
a referent. `SignState` has a fifth member, `Unknown` ("no file open"), which the chip currently
mis-paints as Unsigned — a defect, not a state (see State Patterns).

**Still specified, still absent:**

- **[NOT BUILT in v0.1.7 — RD-04]** A second binding: the chip (or a companion indicator) should
  also report the **active target's build artifact** — re-verified after each build, reset when the
  artifact is rebuilt, **never stale**. Today the artifact's signature is reported only as one line
  of Output text that scrolls away.

> **`[CLOSED — PRD OQ-9: signing longevity & validity] — DEAD.`** The question asked whether to
> apply **RFC-3161 timestamping** and surface a "Signed (cert expired)" variant. It is **dissolved,
> not deferred**: ADR-0061 has **no certificates**. Nothing expires, nothing is revoked, so there is
> nothing for a timestamp to protect. A signature is an Ed25519 signature over the file's bytes; it
> verifies for as long as the bytes and the key match. No new chip state is needed and none should
> be minted.

**Signing behavior (honesty-critical) — backs FR-19/FR-20/FR-21 + SM-C3:**

- **What "Signed" asserts:** *only* that this file's bytes match a signature made by the key named
  in its `.sig`. **Not** that the key is trusted (that is the consumer's `sentinel-trust.toml`, a
  separate decision), **not** that the signer's identity is known (there is none to know — a key is
  64 hex characters), and **not** that the IDE's own supply chain is hardened. UI copy says
  "Signed," **never** "Verified" or "Secure."
- **What a capability grant asserts:** that the signer *declared* those capabilities and the
  trusting key's ceiling permitted them. Per **L-1** this is **recorded intent, not enforcement**.
  Copy must never say a grant "restricts" what the code can do.
- **Verify before "Signed" — [NOT BUILT in v0.1.7 — RD-06], and violated twice:**
  1. The post-build path prints **`[signed · <name>.sig]`** purely from `snc sign`'s exit code and
     never re-reads the artifact. It can report signed when it is not.
  2. When the resolved `snc` lacks the `verify` capability, the chip paints **`✓ Signed`** on the
     mere existence of a `.sig` file, with no verification at all.
  The rule stands as a v1 requirement: **nothing may report "Signed" except a verification that
  actually ran and actually passed.** Where verification is impossible, the honest state is
  "Signature present, not verified" — a state DESIGN must mint.
- **"Sign" operand:** the **open source file**, not a build artifact. Signing source is the right
  default for ADR-0061 — the trust gate consumes signed *sources* — but it means the panel's Sign
  button and the manifest's `signing.sign` act on **different objects**, and the UI must not let
  that blur.
- **Key lifetime:** the IDE **never holds key material**. `snc` signs in a child process; the key
  file lives on disk in the project (conventionally `sentinel.key`). **[CORRECTED — RD-01]** The
  old "session memory only / zeroize on exit / re-prompt for the passphrase" model described a
  design that was never built and no longer applies. The residual risk moved: it is now **an
  unencrypted private key sitting in a project folder**, which the product should say plainly and
  which key-at-rest handling must eventually address.
- **Remove key:** there is nothing to remove. Deleting the key file is a filesystem action, not an
  IDE state change.

### The unsaved-changes guard  **[SHIPPED — RD-25]**

One rule, one implementation, no exceptions: **every path that can discard an unsaved buffer asks
first, with three answers, and a Cancel aborts that command completely.**

| Guarded path | What Cancel must leave unchanged |
|---|---|
| Opening another file — tree node, Problems row, Output `file:line` link | the current buffer, and any UI state the caller already moved (a tree selection) |
| Switching the **active target** | the scheme selector stays on the old target — ask *before* the assignment, or the selector points at a target whose source never opened |
| Close Project · window close · `≡ ▸ Exit` | the project stays open |
| New Project · New File · Open Sealed Project | **ask before writing to disk** — a cancel afterwards leaves a scaffolded project or a decrypted copy the IDE never opened |
| Seal Project | the seal archives what is on **disk**, so an unsaved buffer would be sealed stale |
| Project Settings over a dirty **manifest** buffer | the form's save rewrites that same file from the model, discarding the raw edits |

**Ask exactly once per command.** "Don't Save" deliberately leaves the buffer dirty — the text is
still unsaved until something replaces it — so a second guard later in the same command re-prompts
for a file after the disk work is already done, and a Cancel there aborts something that has
already half-happened.

**The one deliberate exception: an unattended update install.** The updater posts a window close
and force-exits after three seconds, so a prompt there would go unanswered and the watchdog would
kill the process with the edits still only in the buffer. On that path alone the IDE **saves
without asking** and logs that it did. This is not a hole in the rule — it is the rule (never lose
edits) applied where no human is present to answer.

## Interaction Primitives

**Keyboard-first** — an IDE's primary audience is developers; the mouse is the fallback.

**Bound in v0.1.7** (the complete accelerator table):

- `Ctrl+S` — Save · `Ctrl+Z` / `Ctrl+Y` — Undo / Redo
- `Ctrl+N` — New Project · `Ctrl+O` — Open Project · `Ctrl+Shift+N` — New File
- `Ctrl+Shift+B` — Build · `F5` — Run
- `Ctrl+L` — toggle line numbers · `Ctrl+,` — Settings
- `Esc` — close dialog / popup; in the Save prompt, `Esc` = **Cancel**

> The app menu advertises **`Ctrl+;`** beside *Project Settings…*, but no accelerator is registered
> for it — the shortcut does nothing. Either register it or drop the label; a menu that lies about
> a shortcut is worse than a menu with none.

**Specified, not yet bound — [NOT BUILT in v0.1.7 — RD-18]:**

- `Ctrl+P` — fuzzy open-file finder
- `Ctrl+F` / `Ctrl+H` — find / replace (regex toggle in the bar) · `Ctrl+G` — go-to-line
- `F8` / `Shift+F8` — next / previous problem (jumps the triad)
- `Ctrl+Shift+B` cancelling a running build when one is active (today a second Build is ignored)
- `Alt` opening the app menu (today the `≡` button is the only way in)
- Multi-cursor — `Ctrl+Click` adds a caret; column/box select via `Alt+drag`

**Mouse:** click a Problems row or an output `file:line` → the caret jumps to the line (both
guarded). Click the `≡` button for the app menu; click either zone of the scheme selector for its
dropdown; click the signing chip for the Signing & Trust panel. Right-click the tree for New File /
New Project / Open Project. Drag the splitters to resize regions. **Drag a file or folder onto the
window to open it** **[SHIPPED — RD-26]**; a dropped file opens its nearest enclosing project, and
the open is deferred while a build or a modal is busy. Clicking a squiggle is **[NOT BUILT — RD-14]**.

**Banned everywhere:** blocking the UI thread on a build, a run, or a verify (NFR-PERF-3); modal
stacks deeper than one level; alarmist interrupts for diagnostics (they belong in the calm triad,
not pop-ups); trading keystroke latency for features (SM-C2); **any state label that asserts a
check the product did not perform** (the honesty rule, above).

## Accessibility Floor

> **v1 scope decision:** explicit accessibility work is **backlogged**; v1 relies on the **native defaults**. Behavioral floor below; visual contrast lives in [DESIGN.md](DESIGN.md).

- v1 inherits the accessibility that **Win32 + Common Controls v6 provide for free**: standard keyboard navigation and focus, native UI Automation (UIA) exposure of standard controls, and the OS high-contrast/scaling and DWM dark-titlebar behaviors. DPI scaling is honored per-monitor (`MulDiv`).
- **No explicit v1 commitment** to WCAG 2.1 AA conformance, full screen-reader narration of custom surfaces, or audited contrast ratios. The dark palette's contrast is **not yet formally verified** to AA.
- The **owner-drawn** surfaces — the toolbar, the scheme selector, the sidebar and dock tab strips, and the status-bar signing chip — are painted, not native controls, so UIA sees nothing there. The signing chip is the one that matters: **the product's only always-on security signal is currently invisible to a screen reader.** Track it as a named debt.
- `[NOTE FOR UX]` **Procurement risk:** the proof audience (governments, banks) is exactly where **Section 508 / EN 301 549** accessibility can gate purchase. Backlogged for v1, but a likely fast-follow before regulated sales — a named debt, not an oversight.

## Proof & Co-evolution Surfaces

How SentinelIDE's *reason for being* surfaces in the experience — kept deliberately quiet per the **About-dialog-only** decision (no ambient chrome signal in v1).

- **About dialog** is the home of the security/**proof** story: marketing version + build, and the **Sentinel/native mix** as badge pills plus a "built in Sentinel" bar (FR-15, FR-17). The count itself is produced by `tools/loc.sentinel` — Sentinel code measuring the IDE's own source — which is the argument in miniature. The proof *metric* stays here: there is **no Sentinel/native-mix badge in the chrome** and no always-on hardened-surfaces panel. *(Operational **signing** status is the one security element that lives in the chrome — distinct from the proof metric.)* Hardened-surface **coverage** is **[NOT BUILT in v0.1.7 — RD-16]**.
- **The C-rule, actually landed [SHIPPED — RD-27].** Four parsers are compiled from Sentinel into the shipped binary and called across the C-ABI — the diagnostic parser, the **trust-manifest** parser, the `.sig` parser and the **manifest** parser. That is every file reader in the IDE, and `parse_trust` is the first port at a **security boundary**: the code that decides which keys a build trusts is now written in the language built to interpret untrusted bytes. Each is held byte-identical to a C++ fallback by a cross-check test. The manifest *writer* is still C++, and that is the next honest target.
- **Signing** (FR-19..21, UJ-5) — **[CORRECTED — RD-01]** a developer generates an Ed25519 keypair with `snc keygen`, signs **source files** with `snc sign` (optionally declaring capability grants), and a *consumer* decides what to trust in their own `sentinel-trust.toml`. The build gate (`--require-signatures warn|strict --trust`) is where that trust becomes enforcement. **Honesty (per §5):** the IDE holds no key material — but the private key sits unencrypted in the project folder, `strict` gates **executable targets only (L-2)**, and capability grants are **declared intent, not enforced (L-1)**. All three must be said plainly wherever signing is described. *(This replaces the previous Authenticode framing and the "native host holds a secret this session" caveat, neither of which was ever true of the product.)*
- **Language-gap list** (FR-16) and **migration history / hardening playbook** (FR-18) are **maintained artifacts delivered to the language team / published**, not in-IDE UI `[ASSUMPTION]`. Seeded with gap #1 (turnkey Windows MSVC build→link) — closed in the product by MSVC-environment capture, and the honest record of that closure belongs in the list. Two more are earned and unrecorded: **capability extraction that only detects `ffi` (L-1)** and **`--lib`/`--shared` bypassing the trust gate (L-2)**.
- The everyday felt proof was meant to be the **distinct security diagnostic** — a `secret`-leak showing up coral-and-shielded in the editor. **[NOT BUILT in v0.1.7 — RD-13/RD-14].** Until the diagnostic model carries a severity, the flagship proof moment does not exist in the product, and no artifact or demo may imply it does.

## Inspiration & Anti-patterns

- **Lifted from Zed / Lapce (native, Rust):** the native-performance discipline — incremental parse, low memory by bypassing the browser engine (research §2).
- **Lifted from CLion / RustRover:** Build/Run spawns the toolchain into a console pane with **clickable error links** — the model for our output pane. Shipped.
- **Lifted from Xcode:** the **scheme selector** — one control that names *what gets built* and *how*, with the resulting path shown beside it. Shipped **[RD-10]**; it is the single clearest expression of the target × tier model.
- **Lifted from VS Code:** the **diagnostic triad** and `Ctrl+P` fuzzy open — *the patterns*, not the Electron shell. Two-thirds of the triad shipped; the squiggle leg and `Ctrl+P` did not **[RD-14, RD-18]**.
- **Rejected — an Electron/web shell:** undercuts the native-perf and security argument (PRD §7).
- **Rejected — alarmist security UI:** no red banners, klaxons, or modal "VULNERABILITY!" interrupts. Security shows up as calm, precise diagnostics (PRD §9).
- **Rejected — a two-answer save prompt.** Save/Cancel forces a user who wants to discard to save first. Three answers, always **[RD-25]**.
- **Rejected — greying a permanently unavailable menu item.** Hide it (Check for Updates) — a greyed item the user cannot ever enable is a question with no answer.
- **Rejected — feature bloat that grows `% native` (SM-C1) or trades keystroke latency for feature count (SM-C2).**
- **Deferred (not rejected):** completion / go-to-def / hover (LSP-driven), **debugging** (the #1 post-v1 fast-follow), source-control UI, minimap, split panes.

## Responsive & Platform

| Concern | Behavior |
|---|---|
| **Primary platform** | Windows-first, **x64** native desktop. |
| **DPI** | Per-monitor DPI scaling via `MulDiv` (Theme.h `dpiScale`); crisp on mixed-DPI multi-monitor setups. |
| **Light/dark** | Follows the Windows light/dark setting (Theme.h `themeOverride -1`); user override to force light/dark in Settings. |
| **Window** | Resizable single window; regions resize via splitters; the editor area is the priority surface and never collapses. |
| **Instances** | **[SHIPPED — RD-26]** One instance per installed executable, enforced by a named mutex keyed on the exe path. A second launch hands its path to the running instance and exits. |
| **Entry points** | **[SHIPPED — RD-24/26]** Launch · command-line path · **drag-drop onto the window** · **double-clicked `.sntproject` / `.sentinel`** via per-user file associations, registered from the app menu and mirrored by the installer. A file opened from outside lands in its **nearest enclosing project**. |
| **Distribution** | **[SHIPPED — RD-21/22]** A per-user Inno Setup `setup.exe` (exe + examples + README/LICENSE, Start-Menu shortcut, file associations, full uninstall), and in-app auto-update against an **Ed25519-signed appcast** — checked at startup and hourly. The IDE refuses to initialize the updater while its public key is a placeholder. |
| **Next platforms** | **macOS is the next target** (post-v1, gated on a shipping Win32 product); Linux later (PRD §7, §11). Not in v1 scope. |

This is a **native desktop** experience, not responsive web — there is no mobile/touch surface in scope.

## Key Flows

Flows 1–5 mirror the PRD UJ names verbatim. Flows 6–10 are **shipped journeys that no PRD UJ
covers**; the PRD should adopt them.

### Flow 1 — Devon builds and runs his first Sentinel program (UJ-1)   **[REWRITTEN — RD-07/08/09/10]**

1. Devon, a systems engineer new to Sentinel, chooses **`≡ ▸ Open Project…`** and picks a
   **manifest** — `MyProject.sntproject` (or a legacy `sentinel.toml`). *He does not pick a folder;
   the manifest is what makes a folder a project.* The sidebar fills with the **Project** view:
   the manifest, the trust manifest, Targets, Sources (FR-3).
2. The active target's entry source opens by itself, and the toolbar shows the scheme —
   `[● app ▾ │ Experimental ▾]  → target\experimental\app.exe`. Syntax highlighting renders:
   keywords orchid, strings warm-orange (`{colors.syn-keyword}`, `{colors.syn-string}`) (FR-2).
3. He edits, hits **Build** (`Ctrl+Shift+B`). The buffer auto-saves, the composed
   `snc build … -o target\experimental\app.exe` is echoed as `> …`, and output **streams live**
   while the editor stays fully interactive (FR-4, FR-5, NFR-PERF-3). Two honest notes follow the
   command: that this tier does not yet change the compile, and which MSVC environment was found.
4. `[done · exit 0]`. He hits **Run** (`F5`), which launches the **active target's** artifact.
5. **Climax:** the program's **stdout and exit code appear in the pane** (FR-7) — the whole
   edit→build→run loop closed without leaving the window, against a project model the IDE
   understood, not a command he had to configure.

*Failure:* a compile error → the Problems list populates, the dock switches to it, error lines tint
in the editor, and clicking the output `file:line` jumps him to the line (FR-6).
*No manifest:* the folder opens in the **Files** view and Build falls back to the single open file.

### Flow 2 — Devon catches a `secret` leak before it ships (UJ-2)  ★ flagship

> ### ⛔ **[NOT BUILT in v0.1.7 — RD-13 / RD-14]**
> **This entire flow is unimplemented.** The shipped `Diag` record has no **severity** field, so a
> Sentinel safety finding is indistinguishable from an ordinary compile error: no coral squiggle, no
> gutter shield, no shield-marked Problems row, no `security` class anywhere. What a user actually
> sees is a Problems row and a tinted line, identical to a syntax error. This is the product's
> flagship journey and it does not exist yet — **it must never be demoed, screenshotted, or cited as
> a capability.** The mockup [`mockups/key-secret-leak-editor.html`](mockups/key-secret-leak-editor.html)
> depicts the intent, not the product.
>
> The flow is kept below **as the v1 requirement it still is.**

1. Devon writes a branch on a `secret`-typed value.
2. The editor renders a **coral squiggle** at the exact span with a **gutter shield** (`{components.squiggle-security}`) — visibly different from an ordinary red error.
3. The Problems list adds one shield-marked row: "`secret` reaches a branch condition" (`{components.diagnostic-badge-security}`); clicking either the squiggle or the row jumps to the span (FR-8, FR-9, FR-11).
4. He refactors to a branch-free form.
5. **Climax:** the diagnostic **clears from all three places at once** (FR-10) — the language's headline guarantee showed up *in the editor*, coral-and-shielded, then resolved. The safety claim is demonstrated, not asserted.

*Failure:* on a full build, the authoritative `snc` finding supersedes the live keystroke squiggle for the same span — no double-render (addendum).

**What it takes to build it:** a severity on the diagnostic model, a `security` class fed by `snc`'s
finding kind, a severity column and shield glyph in the Problems list, and per-span squiggle
rendering in the editor to replace whole-line tinting.

### Flow 3 — Devon's machine isn't build-ready, and the IDE walks him through it (UJ-3)

> ### ⛔ **[NOT BUILT in v0.1.7 — RD-19]**
> **There is no readiness check and no remediation dialog.** The mockup
> [`mockups/key-toolchain-readiness.html`](mockups/key-toolchain-readiness.html) has no counterpart
> in code. What ships is narrower and unnamed: `vcvars64.bat` auto-detection, MSVC-environment
> capture into the build child — which is what actually *closed* the gap this flow was written about —
> and **one warning line in the Output pane** when no MSVC environment is found. No
> component-by-component check, no copy-pasteable remediation, no "this fix is upstream" statement.
>
> The flow is kept below **as the v1 requirement it still is.**

1. On first build, the toolchain is incomplete — the runtime archive `snc` links against isn't staged for this linker.
2. Instead of a raw linker error, the IDE's **toolchain-readiness** check names the gap: "Runtime archive not found — compile works, link can't complete" (FR-12).
3. It shows a **copy-pasteable remediation**, and — because this is upstream (Sentinel gap #1) — states plainly that there's no local workaround yet, rather than implying a fix that doesn't exist.
4. Devon follows the guidance (or learns the honest status) and, once the environment is ready, builds again.
5. **Climax:** he goes from "broken" to **first successful build without a web search** — the failure became a guided step, not a wall.

### Flow 4 — Priya harvests the gap list the IDE produced (UJ-4)

1. Priya, the Sentinel language lead, opens the **Language-gap list** the project maintains (FR-16).
2. She reviews concrete, prioritized capability requests surfaced by actually building and running the IDE — gap #1: turnkey Windows MSVC build→link, now closed in the IDE by environment capture.
3. **[SHIPPED — RD §4]** Two further gaps are earned and belong on her list: **capability extraction only ever detects `ffi`**, so a signed key's `grants` ceiling is recorded intent rather than an enforced gate (**L-1**); and **`snc build --lib` / `--shared` never invoke the trust gate**, so a library or shared target displays `signing: strict` and enforces nothing (**L-2**). A third, smaller: `snc` has no tier flag, so all four release tiers compile identically.
4. She folds the top items into the language backlog.
5. **Climax:** the IDE has **paid for itself as a roadmap input** — including two limits it found by trying to ship a trust model on top of the language, which no amount of language-side testing would have surfaced.

### Flow 5 — Devon signs his build  (UJ-5)   **[REWRITTEN — RD-01/02/03]**

> The mockup [`mockups/key-signing.html`](mockups/key-signing.html) shows the **old Authenticode
> import-key dialog** and is stale for this flow.

1. Devon opens **`≡ ▸ Signing & Trust…`** with a `.sentinel` file open. The panel names the file's
   current state — `⊘ Unsigned`, "No detached signature (`crypto.sentinel.sig`) next to this file."
   Below it, the project's `sentinel-trust.toml` is listed as **Name · Key · Grants**.
2. He clicks **Generate** and saves `sentinel.key` into the project — `snc keygen -o …`, an
   **Ed25519** keypair. *There is no key file to import, no passphrase, no certificate, and no
   identity: a key is 64 hex characters.*
3. He types `ffi` into **Grants** and clicks **Sign**: `snc sign crypto.sentinel --key sentinel.key
   --grant ffi` writes a detached, human-readable **`crypto.sentinel.sig`** beside the source. He
   closes the panel; the chip runs `snc verify` off the UI thread and settles on
   `{colors.trust-verified}` **✓ Signed**.
4. On the consuming side, he opens the signed file and clicks **Import**: the signature's public key
   is appended to `sentinel-trust.toml` as a `[[keys]]` table — a bare 64-hex `pubkey`, the file's
   stem as a diagnostic-only `name`, and the grants it declared.
5. **Climax:** in **Project Settings ▸ SIGNING** he sets **Require: strict**. The next build's
   command line carries `--require-signatures strict --trust sentinel-trust.toml`, and a source
   whose key is not in that manifest **refuses to build**. The trust decision stopped being a badge
   on a finished binary and became a **gate on the build's inputs** — which is the whole point of
   ADR-0061.

*The honest caveats, which the UI must carry and no demo may drop:*
- **strict gates executable targets only** — `--lib` and `--shared` builds discard the flags
  silently while still displaying `signing: strict` (**L-2**).
- **the `ffi` grant is the only capability `snc` actually extracts** — every other grant in a
  ceiling is declared intent, not enforcement (**L-1**).
- **the private key sits unencrypted in the project folder.** The IDE never holds it, which is
  better than the old design; where it *does* live is now the residual risk.

*Failure:* editing a signed file invalidates its `.sig` → `⚠ Signature invalid` on the next
recompute. A failed `snc sign` surfaces snc's own stderr and leaves the file unsigned. A `.sig`
carrying an `ed25519:`-prefixed key **parses and silently never matches** — it reads as untrusted,
with no error; the IDE's own writer avoids the prefix, but a hand-edited manifest will not.

*Separately:* ticking **"Sign the built artifact"** in Project Settings makes a successful build run
`snc sign` on the produced binary and print `[signed · app.exe.sig]`. **That line is printed off the
signer's exit code without re-verifying the artifact [NOT BUILT — RD-06]**, and it does not move the
status chip, which tracks the open source file.

### Flow 6 — Devon switches target and tier   **[SHIPPED — RD-08/09/10 · no PRD UJ]**

1. Devon's project declares three `[[target]]` blocks — an executable `app`, a `library` `core`,
   and a `shared` `plugin`. The tree shows a **Targets** group; the toolbar shows the scheme.
2. He clicks the **target** zone of the scheme selector and picks `core (library)`. Because
   switching target opens that target's entry source, the **unsaved-changes guard fires first** —
   Cancel here leaves the selector where it was.
3. The dot turns `{colors.diag-info}` for a library, the window title becomes
   `… › core (library) · Experimental`, and the derived path updates to `→ target\experimental\core.a`.
4. He clicks the **tier** zone and picks **Hardened**. Nothing is discarded, so nothing is asked;
   the path becomes `→ target\hardened\core.a` and the title's tier segment changes.
5. **Climax:** Build produces exactly what the toolbar promised — `snc build --lib core.sentinel -o
   target\hardened\core.a --emit-header target\hardened\core.h` — and the Output pane states, before
   any of it runs, that **the tier chose the directory and nothing else** (`snc` has no tier flag).
   The selector never over-promises.

*Note:* the tier is a **session** choice. Reloading the project — including saving Project Settings —
resets it to the manifest's `default_tier`.

### Flow 7 — Devon edits the project itself   **[SHIPPED — RD-11/03 · no PRD UJ]**

1. Devon clicks the **project node** in the tree (or `≡ ▸ Project Settings…`) and gets a
   **structured form**, not a text editor: PROJECT · BUILD · TARGETS · SIGNING.
2. If the raw manifest is open **and dirty**, the guard fires here — because saving the form
   rewrites that same file from the model and would silently discard his hand edits.
3. He renames a target, points its Entry at another source, sets **Default tier: Stable**, and in
   SIGNING selects **Require: warn** with **Trust: `sentinel-trust.toml`**.
4. **Save.** The writer is **surgical**: comments and keys the IDE does not model survive untouched.
   The project reloads, the tree and title refresh, and the tier resets to the new default.
5. **Climax:** the next build carries `--require-signatures warn --trust …` — a build-time policy
   the developer set in a form, in the manifest, under version control, rather than in a settings
   blob on one machine. The form states its own consequence in place: *"warn/strict pass
   `--require-signatures --trust`; signing uses `sentinel.key` in the project."*

### Flow 8 — Devon almost loses an edit, and doesn't   **[SHIPPED — RD-25 · no PRD UJ]**

1. Devon has unsaved changes in `crypto.sentinel` — `●` on the tab, coral **● Save** in the toolbar.
2. He clicks another file in the tree. A themed modal asks **"Save changes to `crypto.sentinel`?"** —
   *"Your changes will be lost if you continue opening another file without saving."* Three answers:
   **Save · Don't Save · Cancel**. Save is the default; `Esc` is Cancel.
3. He picks **Cancel**. Nothing happens at all — the tree selection reverts, the buffer is untouched.
4. He tries again and picks **Don't Save**; the other file opens. Later he closes the window with
   edits pending and gets the same three answers, with the action line reading *"closing
   Sentinel-IDE."*
5. **Climax:** the same prompt, from one implementation, guarded every path that could have dropped
   the buffer — open, target switch, close project, exit, new project, new file, seal, unseal,
   project settings. There is no path in the product that discards an edit silently…
6. …with **one deliberate exception**: an unattended auto-update install, where the process is about
   to be killed by a watchdog and no one is present to answer. There the IDE **saves without asking**
   and logs it. The rule is "never lose edits," and that is the rule being applied, not broken.

### Flow 9 — Devon hands off a project without handing over its contents   **[SHIPPED — RD-23 · no PRD UJ]**

1. Devon chooses **`≡ ▸ Seal Project…`**. The seal archives what is on **disk**, so the guard fires
   first on any unsaved buffer.
2. The password dialog asks **twice** — a typo in a write-once password is unrecoverable.
3. The IDE archives the project, LZMS-compresses it, and encrypts under **AES-256-GCM** with a random
   data key, wrapped in a LUKS-style unlock slot (PBKDF2-HMAC-SHA256, 600 000 iterations). Output
   reports `[sealed · MyProject.sealed]` and then states plainly: **the plaintext project is
   unchanged — delete it yourself if you want only the sealed copy.** Sealing is non-destructive and
   says so.
4. On the far side, **`≡ ▸ Open Sealed Project…`** takes the `.sealed`, asks for the password once,
   decrypts to a sibling `<name>-unsealed` folder (numbered if that name is taken) and opens it.
   A wrong password fails cleanly and removes the empty destination.
5. **Climax:** a whole project moved as **one encrypted file** with no key infrastructure and no
   server — and the IDE never pretended the original had been protected. The password bytes are
   zeroized after use in both directions.

### Flow 10 — Devon takes an update   **[SHIPPED — RD-21 · no PRD UJ]**

1. Shortly after startup, and then hourly, the IDE checks a **Ed25519-signed appcast** for a newer
   release. Nothing interrupts him while it does.
2. When one exists, a themed dialog offers the version and its notes with **Skip this version ·
   Install now · Later** — and **Later is the default and takes focus**, the deliberate opposite of
   the save prompt. An update is never urgent enough to steal an Enter key.
3. **Install now** hands off to the signed installer and closes the IDE. Because the shutdown is
   unattended and watchdogged, the unsaved-changes guard **saves instead of asking** (Flow 8, step 6).
4. **Climax:** the IDE updated itself over a signature it verified, without ever having interrupted
   the work — and without a prompt going unanswered while a watchdog counted down.

*Honesty note:* the appcast is fetched over unauthenticated HTTPS from a public repository; the
Ed25519 signature on it, not the transport, is what is trusted. Where the IDE's own public key is
still a placeholder the updater refuses to initialize and the menu item is **hidden**, not greyed.

## 12. Gap register — index into GAP-REGISTER.md

**[`GAP-REGISTER.md`](../../GAP-REGISTER.md) is the single gap register**, and
[`REALITY-DELTA.md`](../../REALITY-DELTA.md) is the record of the facts behind it. This section is
an index for readers of *this* file alone: every row is behaviour **this spine specifies and v0.1.7
does not have**, and says where in this document the in-place marker sits.

| Register | Delta | Not built | Marked in this document at |
|---|---|---|---|
| **GAP-A1** | RD-13 | Diagnostic **severity** — no severity field, no severity column, no `security` class | IA Problems row · Component Patterns Problems list, Diagnostic triad · State Patterns *Sentinel safety finding*, *Diagnostics reconciling* · Flow 2 |
| **GAP-A1** | RD-14 | **Squiggles** and the **gutter shield** — per-span rendering (whole-line tinting ships instead) | Component Patterns Editor, Diagnostic triad · State Patterns *Build failed*, *Sentinel safety finding* · Interaction Primitives (mouse) · Flow 2 |
| **GAP-A3** | RD-17 | **Multi-tab editing** — one buffer only | IA Editor row · Component Patterns Tab strip |
| **GAP-A2 · A4 · A5 · A6 · A7** | RD-18 | **`Ctrl+P`** fuzzy finder · **find/replace** incl. regex · **go-to-line** · **multi-cursor** · **`F8`/`Shift+F8`** problem navigation. Plus two this spine adds: build-cancel on `Ctrl+Shift+B`, and `Alt` opening the app menu | IA Fuzzy finder row · Component Patterns Editor · Interaction Primitives *Specified, not yet bound* |
| **GAP-A8** | RD-19 | **Toolchain-readiness** surface — no check, no dialog, no remediation text | IA Toolchain row · Component Patterns Toolchain readiness · State Patterns *Toolchain not ready* · Flow 3 |
| *(no row)* | RD-04 | The chip (or a companion) also reporting the **build artifact**, re-verified and never stale | Signing-indicator state model, *Still specified, still absent* |
| *(no row)* | RD-06 | **Verify before "Signed"** — violated twice: the post-build `[signed · …]` line, and the no-verify-capability fallback | Signing behavior bullet 3 · State Patterns *Artifact signed after build*, *Signing unverifiable* · Flow 5 |
| *(no row)* | RD-12 | A **user-configurable build command** | IA Settings row · Component Patterns Build/Run controls, Settings |
| *(no row)* | RD-16 | **Hardened-surface coverage** in About (the LOC mix ships; coverage does not) | IA About row · Component Patterns About · Proof & Co-evolution |
| *(no row)* | — | Status-bar **indentation / EOL-mode** indicators; a true **stdout/stderr** split in the Output pane | Component Patterns Status bar, Output pane |

> **Four of these have no Part A row in GAP-REGISTER.md** (RD-04, RD-06, RD-12, RD-16, plus the two
> minor items on the last line). They are marked in place here as D1 requires, but the register
> should adopt them so the single list is actually single.

**Also recorded here as live defects, not gaps** — they are shipped behaviour that is wrong, and each
is small: the chip painting `Unknown` ("no file open") as **⊘ Unsigned** while the panel labels it
correctly (RD-05); the chip painting **✓ Signed** with no verification when the resolved `snc` lacks
the verify capability; the post-build **`[signed · …]`** line printed off an exit code (RD-06); and
the app menu advertising **`Ctrl+;`** for Project Settings with no accelerator registered.

**Shipped, previously unspecified, now in this spine:** the build-time signature gate (RD-03),
targets (RD-08), tiers (RD-09), the scheme selector (RD-10), Project Settings (RD-11), auto-update
(RD-21), the installer (RD-22), sealed projects (RD-23), file associations (RD-24), the
unsaved-changes guard (RD-25), single-instance + drag-drop (RD-26), and the four Sentinel parsers in
the binary (RD-27). **Logging** (RD-20) was already specified here and ships as specified — what is
missing is a **PRD FR** for it.

**Token alignment with DESIGN.md** (both spines were reconciled on 2026-09-02; verified against
DESIGN's token block, not from memory). Every `{path}` used above resolves. The surfaces this spine
added are minted there — `{components.scheme-selector}`, `{components.project-settings-form}`,
`{components.signing-panel}`, `{components.file-label}`, `{components.tree-view-switcher}`,
`{components.toolbar}` — as are the three decision modals, `{components.save-changes-dialog}`,
`{components.password-dialog}` and `{components.update-dialog}`, over a shared
`{components.message-dialog}` shape. `{components.import-key-dialog}` is retired **DEAD** there,
replaced by the signing panel.
**`{colors.trust-verified}` is not renamed.** One token is still missing and this spine needs it:
a **"signature present, not verified"** state on `{components.status-signing}`, for the case where
the resolved `snc` cannot verify — today that case is painted `✓ Signed`, which is the honesty rule
being broken by a fallback rather than by a design.
