---
name: SentinelIDE
title: "SentinelIDE — EXPERIENCE"
status: draft
created: 2026-06-27
updated: 2026-06-28
sources:
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

## Foundation

**Form-factor:** native Windows desktop application, **single-window IDE**, Windows-first (x64). Not Electron, not a web app in a shell — the native-performance and security story depend on bypassing a browser engine (PRD §7, research §2).

**UI system:** native **Win32 + Common Controls v6** — a RichEdit-style code editor, a `SysTreeView32` project sidebar, virtual `SysListView32` lists (Problems), themed native dialogs, and a status bar. The IDE is a **thin native host** (the only place non-Sentinel "chrome" code is permitted) that embeds **Sentinel-written surfaces across a C-ABI boundary** — in v1, the untrusted-input parser (FR-13, addendum). [DESIGN.md](DESIGN.md) is the visual identity reference; this spine specifies only behavior.

**What the user runs against:** the IDE shells out to a local **`snc.exe`** for builds, runs, and stage dumps; diagnostics come from `snc` (build-authoritative) and optionally `snc parse` for fast feedback, migrating to the `sentinel-lsp` server as it matures (architecture decision, OQ-3). The toolchain may be incomplete on a given machine — readiness is a first-class concern (FR-12).

## Information Architecture

One window, three persistent regions (tree · editor+tabs · bottom dock) plus a status bar; dialogs overlay for discrete tasks.

| Surface | Reached from | Purpose |
|---|---|---|
| **Editor area + tab strip** | App open, tree, fuzzy finder | Edit `.sentinel` files; the priority surface, never collapses (FR-1, FR-2) |
| **Project tree** | Open Folder, sidebar toggle | Browse on-disk project structure; open files into tabs (FR-3) |
| **Fuzzy open-file finder** | `Ctrl+P` | Subsequence match on path; select → open in tab (FR-3) |
| **Output pane** (bottom dock) | Build/Run, dock tab | Live-streamed build/run stdout/stderr; clickable `file:line` (FR-5, FR-6, FR-7) |
| **Problems list** (bottom dock) | Diagnostics present, dock tab | One row per diagnostic; select → jump to span (FR-9) |
| **Toolchain-readiness surface** | First build / readiness failure | Names the missing/mismatched component + copy-pasteable remediation (FR-12) |
| **Settings / Preferences** (dialog) | Menu / `Ctrl+,` | Editor font override, theme (follow/light/dark), build command config (FR-4), **logging** (level + log-file location) `[new]` |
| **About** (dialog) | Help menu | Version `0.1.0 (build N)` + **Sentinel/native mix** + hardened-surface coverage (FR-17, FR-15) |
| **Build/Run controls** | Toolbar / menu / shortcut | Invoke `snc build` / run the executable; show the exact command line (FR-4) |
| **App menu** (`≡` popup) | Toolbar `≡` button / `Alt` | File · Edit · View · Build · Run · Signing · Settings · Help; items carry accelerators |
| **Import Signing Key** (dialog) | App menu › Signing | Load a key file (.pfx/.p12/PEM) + passphrase; shows key identity + the not-yet-hardened honesty note (FR-19, FR-20; §5 SEC-5) |
| **Signing status** (status bar) | Always visible; click → Signing dialog | Operational signing state (no key / signed / unsigned / signing / failed) (FR-21) |

The **bottom dock** hosts Output and Problems as switchable tabs (VS Code-style), collapsible to reclaim vertical space. Dialogs (Settings, About, toolchain remediation, find/replace, **Import Signing Key**) stack **one level deep** — never a dialog atop a dialog. The **app menu** is a native popup off the toolbar `≡` button (not a permanent menu bar — faithful to the reference).

→ Composition references: [`mockups/key-secret-leak-editor.html`](mockups/key-secret-leak-editor.html) — the UJ-2 secret-leak moment; [`mockups/key-toolchain-readiness.html`](mockups/key-toolchain-readiness.html) — the UJ-3 toolchain-readiness remediation; [`mockups/key-signing.html`](mockups/key-signing.html) — Flow 5 import-key & signed status. All apply DESIGN.md dark tokens and show the `≡` app-menu + signing-indicator chrome. Remaining surfaces are **spine-only**. **Spines win on conflict.**

## Voice and Tone

Microcopy for all product-generated text — diagnostics, build/run status, toolchain guidance, remediation (FR-12). Brand voice and aesthetic posture live in [DESIGN.md](DESIGN.md). The register is **precise, calm, non-alarmist — assurance, not noise** (PRD §9). For a security tool, panic is a failure mode: the UI states facts and the fix, never raises its voice.

| Do | Don't |
|---|---|
| "`secret` reaches a branch condition." | "⚠️ DANGER: secret leak detected!!!" |
| "Runtime archive not found — build can compile but not link." | "Build failed." (with no cause) |
| "This fix is upstream (Sentinel gap #1); no local workaround yet." | Imply a local fix that doesn't exist (FR-12) |
| "Exit code 0 · 1.4s" | "Process completed successfully ✓🎉" |
| "Highlighting reduced on very long lines." | "Performance degraded." |
| Name the component, the span, and the next step. | Hide the failure or dress it up. |
| "Signed · key: ACME Code Signing" / "Unsigned" | "🔒 SECURE!" / implying more safety than v1 hardens |
| "Key handling isn't a hardened Sentinel surface yet — next increment." | Claiming the key is protected by Sentinel today |

**Honesty rule (load-bearing):** never claim more safety than v1 delivers. The product line is *"the interpretation of untrusted bytes is Sentinel,"* not *"100% Sentinel / fully hardened"* (PRD §5). Copy in About, Settings, and any proof-facing text holds that line.

## Component Patterns

Behavioral rules; visual specs live in [DESIGN.md `Components`](DESIGN.md).

| Component | Behavioral rules |
|---|---|
| **Editor** | Multi-cursor, find/replace incl. in-file regex, go-to-line, undo/redo. Incremental highlighting on edit (no full-file re-parse stall, FR-2). Unsaved edits **never lost** on build/run/focus change (NFR-REL-1). Renders `{components.squiggle-*}` at diagnostic spans. |
| **Tabs** | Open/close/reorder; `•` dirty glyph when unsaved (`{components.tab-inactive}`). Closing a dirty tab prompts to save. Build/focus change never discards buffer state. |
| **Project tree** | Reflects on-disk structure; activating a node opens the file in a tab. Expand/collapse; reveals the active file. |
| **Fuzzy finder** | `Ctrl+P`; matches files by **subsequence on path**; `Enter` opens the top result, `↑/↓` to choose, `Esc` cancels. |
| **Output pane** | Streams build/run output **line-by-line, live — never batched at exit** (FR-5). Preserves order; **stderr visually distinguished** (`{components.output-pane}` stderr color). `file:line[:col]` is a click target → moves the editor caret there (FR-6). |
| **Problems list** | Exactly **one row per current diagnostic** (file · line · message · severity); select → caret to the span (FR-9). Sentinel-safety findings carry the `{components.diagnostic-badge-security}` shield. |
| **Diagnostic triad** | Squiggles (FR-8), Problems list (FR-9), and clickable output (FR-6) are driven by **one Diagnostic model** (FR-10): a finding appears consistently in all three; clearing it clears all three. |
| **Build / Run controls** | Build invokes `snc build`; the **exact command line is shown** and is configurable in Settings (FR-4). Build runs as a child process **off the UI thread** (FR-4, NFR-PERF-3). Run executes the built binary and reports stdout/stderr + **exit code** (FR-7). |
| **Toolchain readiness** | Checked before/around a build (FR-12). On failure, names the specific missing/mismatched component and shows the best-known, copy-pasteable remediation; where the fix is upstream, says so plainly. |
| **Settings** | Editor font picker (default Cascadia Code; any installed mono) with live preview; theme override (follow / light / dark); build-command field; **logging** — level (Error / Warn / Info / Debug / Trace) + log-file location, with a "reveal in Explorer" affordance. Changes apply without restart. `[new]` |
| **About** | Renders `0.1.0 (build N)` and the current **Sentinel/native mix** (e.g., "Sentinel 18% / Native 82%") + hardened-surface coverage; updates as the mix changes (FR-17, FR-15). |
| **Status bar** | Shows cursor position (Ln, Col), indentation / EOL mode, transient build/run status ("Building…", "Exit 0 · 1.4s"), and the **signing status indicator** (below). The proof *metric* (Sentinel/native mix) is **not** here — it stays in About; signing is operational status, distinct from the proof metric. |
| **App menu (`≡`)** | A `≡` button in the toolbar opens a native popup menu (File · Edit · View · Build · Run · Signing · Settings · Help). `Alt` opens it; arrow keys / accelerators navigate; `Esc` closes. A popup, not a permanent bar (reference-faithful). |
| **Signing status indicator** | Status-bar chip, **always visible**. States: no key · key loaded · signing · **signed** · unsigned · failed (precedence + transitions in *Signing-indicator state model*). Click → Signing dialog (key info, **Sign on Build** toggle, **Sign now**). The one always-on security signal in the chrome. (FR-21) |
| **Import-key / Signing dialog** | From App menu › Signing or the indicator. Imports a key file (.pfx/.p12/PEM) + passphrase; shows the loaded key's identity **and certificate validity window** (expiry/timestamping handling is open — OQ-9); toggles Sign-on-Build. Carries an **honesty note**: key handling is not a hardened Sentinel surface yet (next increment) — never overclaim. (FR-19, FR-20; §5 SEC-5) |

## State Patterns

| State | Surface | Treatment |
|---|---|---|
| **No folder open** | Editor area | Empty state: "Open a folder to start." Single primary action (Open Folder) + recent folders. Calm, no marketing. |
| **Empty project** | Project tree | "No `.sentinel` files here yet." Tree shows the folder; New File affordance. |
| **Building** | Output pane + controls | Build button → **Cancel**; output streams live; **editor stays fully interactive** — type/scroll/navigate never blocked (NFR-PERF-3). Subtle in-progress indicator, no modal. |
| **Build succeeded** | Output pane | Final line shows exit status + elapsed (e.g., "Build OK · 1.4s"). Run becomes available. |
| **Build failed (compile)** | Triad | Diagnostics populate squiggles + Problems + clickable output, one model. Ordinary errors use `{colors.diag-error}`. |
| **Sentinel safety finding** | Triad | `secret`-leak / borrow / effect render with the **distinct security signature** — `{components.squiggle-security}` coral squiggle + gutter shield, Problems row shield-marked. This is the UJ-2 climax made routine. |
| **Running** | Output pane | stdout/stderr stream; **exit code** shown on completion; UI never blocks (FR-7). |
| **Toolchain not ready** | Readiness surface | Names the gap (e.g., "runtime archive absent"); shows remediation; if upstream (gap #1), says no local fix yet — honest, not a dead-end (FR-12, UJ-3). |
| **Unsaved edits** | Tabs | `•` dirty glyph; never lost across build/run/focus (NFR-REL-1). Close-dirty → save prompt. |
| **Very long line / large file** | Editor | Heavy per-line highlighting disabled beyond ~20,000 chars/line; edits stay responsive via the rope buffer (FR-2, NFR-PERF-5). Quiet status note, not an error. |
| **Diagnostics reconciling** | Triad | Fast on-keystroke (parse/LSP) and authoritative on-build (`snc`) sources are deduped so a build does **not** double-render live squiggles; **build supersedes keystroke** for overlapping ranges (addendum). |
| **Cold start** | Whole app | Sub-second to interactive `[ASSUMPTION]` (NFR-PERF-2); no splash theatrics. |
| **No signing key** | Status bar | Muted "No signing key" indicator; Sign actions prompt to import first. Neutral default, not an error. |
| **Key loaded** | Status bar / dialog | Indicator shows the key identity ("Key: ACME Code Signing"). Honesty note: the key sits in the not-yet-hardened native host this session. |
| **Build signed** | Status bar + Output | `{colors.trust-verified}` "Signed" chip; Output records the signed artifact + identity. |
| **Build unsigned** | Status bar | Neutral `{colors.text-muted}` "Unsigned" chip — normal when no key / Sign-on-Build off. Never alarmist. |
| **Signing in progress** | Status bar | `{colors.accent}` "Signing…" transient state; never blocks the UI. |
| **Signing failed** | Status bar + dialog | `{colors.diag-error}` "Sign failed"; dialog names the cause (bad passphrase, key/algorithm mismatch) + the fix. Artifact stays unsigned. |

### Signing-indicator state model

*Backs PRD **FR-21** ("one state at a time, by precedence") — added from the PRD reviewer triage. The chip collapses two underlying dimensions (is a key present? · what is the **current** artifact's signing outcome?) into **one** state, chosen by precedence (top row wins):*

| # | State | Shown when | Token |
|---|---|---|---|
| 1 | **Signing…** | a sign operation is in progress | `{colors.accent}` |
| 2 | **Sign failed** | the last sign attempt for the current artifact failed | `{colors.diag-error}` |
| 3 | **Signed** | the current artifact carries a signature from the loaded key | `{colors.trust-verified}` |
| 4 | **Unsigned** | a build exists but isn't signed (no key, or Sign-on-Build off) | `{colors.text-muted}` |
| 5 | **Key loaded** | a key is imported, no signing outcome yet for the current artifact | `{colors.text-secondary}` |
| 6 | **No signing key** | default — no key imported | `{colors.text-muted}` |

All six open the Signing dialog on click (state 6 → straight to import). **Transitions:** `No key` —import→ `Key loaded`; `Key loaded` —build w/ Sign-on-Build *or* **Sign now**→ `Signing…`; `Key loaded` —build w/o signing→ `Unsigned`; `Signing…` —success→ `Signed`, —failure→ `Sign failed`; `Sign failed` —**Sign now** retry→ `Signing…`; any of signed/unsigned/failed —**new build**→ re-evaluates (→ `Signing…` or `Unsigned`); any —key removed→ `No key`. A **new build invalidates** a prior `Signed`/`Sign failed` until the new artifact is (re-)signed — the chip is bound to the **current** artifact and is **never stale** (PRD FR-20).

> `[OPEN — PRD OQ-9: signing longevity & validity]` Authenticode without an **RFC-3161 timestamp** stops verifying once the signing cert expires; cert **expiry / revocation** is *not* modelled above. Decision (architecture + signing impl): apply timestamping? surface a **"Signed (cert expired / near-expiry)"** variant or a dialog warning? Until decided, **"Signed" must never imply perpetual validity** (honesty rule; PRD SM-C3). If a new state is added, mint its token in [DESIGN.md](DESIGN.md) `{components.status-signing}`.

**Signing behavior (honesty-critical) — backs FR-19/FR-20/FR-21 + SM-C3; from adversarial-review triage (A2/A3/B1/B4/C4):**
- **What "Signed" asserts:** *only* that the current artifact carries an Authenticode signature from the loaded key (origin + integrity since signing). **Not** that the key, its certificate chain, or the publisher is vetted/trusted, and **not** that the IDE's own supply chain is hardened. UI copy says "Signed," **never** "Verified" or "Secure."
- **Verify before "Signed":** the chip flips to Signed *only after* the IDE re-reads the produced artifact and confirms a valid signature — never on the signer's exit code alone. A new build resets it to "Unsigned" until the new artifact is (re-)signed (bound to the current artifact; never stale).
- **"Sign now" operand:** signs the *current build's* artifact. With no build yet — or source changed since the last build — it prompts to (re)build first rather than signing a stale binary and calling it "Signed."
- **Key lifetime:** v1 holds the key in **session memory only** — no at-rest persistence; decrypted key material is zeroized on unload/exit; passphrase re-prompted next session (PRD FR-19). Any future persistent storage joins the not-yet-hardened surface (§5 SEC-5).
- **Remove key:** unloads the key, zeroizes in-memory material, returns the indicator to "No signing key."

## Interaction Primitives

**Keyboard-first** — an IDE's primary audience is developers; the mouse is the fallback. Bindings follow **Windows / common-editor conventions** `[ASSUMPTION: exact bindings to confirm]`:

- `Ctrl+P` — fuzzy open-file finder
- `Ctrl+Shift+B` — Build · `F5` — Run · `Ctrl+Shift+B` cancels a running build when active
- `Ctrl+S` — save · `Ctrl+S` is also the dirty-state resolver
- `Ctrl+F` / `Ctrl+H` — find / replace (regex toggle in the bar) · `Ctrl+G` — go-to-line
- `F8` / `Shift+F8` — next / previous problem (jumps the triad)
- `Ctrl+,` — Settings · `Esc` — close dialog / popup / finder, cancel inline edit
- `Alt` — open the app menu (`≡` popup); arrow keys navigate, `Esc` closes
- Multi-cursor — `Ctrl+Click` adds a caret; column/box select via `Alt+drag` `[ASSUMPTION]`

**Mouse:** click a squiggle, a Problems row, or an output `file:line` → caret jumps to the span (the triad is mutually navigable). Click `≡` for the app menu; click the signing indicator to open the Signing dialog. Drag splitters to resize regions; double-click a splitter to collapse/restore the bottom dock.

**Banned everywhere:** blocking the UI thread on a build or run (NFR-PERF-3); modal stacks deeper than one level; alarmist interrupts for diagnostics (they belong in the calm triad, not pop-ups); trading keystroke latency for features (SM-C2).

## Accessibility Floor

> **v1 scope decision:** explicit accessibility work is **backlogged**; v1 relies on the **native defaults**. Behavioral floor below; visual contrast lives in [DESIGN.md](DESIGN.md).

- v1 inherits the accessibility that **Win32 + Common Controls v6 provide for free**: standard keyboard navigation and focus, native UI Automation (UIA) exposure of standard controls, and the OS high-contrast/scaling and DWM dark-titlebar behaviors. DPI scaling is honored per-monitor (`MulDiv`).
- **No explicit v1 commitment** to WCAG 2.1 AA conformance, full screen-reader narration of custom surfaces (the RichEdit editor, custom-drawn squiggles/gutter), or audited contrast ratios. The dark palette's contrast is **not yet formally verified** to AA.
- `[NOTE FOR UX]` **Procurement risk:** the proof audience (governments, banks) is exactly where **Section 508 / EN 301 549** accessibility can gate purchase. Backlogged for v1, but this is a likely fast-follow before regulated sales — track it as a named debt, not an oversight.

## Proof & Co-evolution Surfaces

How SentinelIDE's *reason for being* surfaces in the experience — kept deliberately quiet per the **About-dialog-only** decision (no ambient chrome signal in v1).

- **About dialog** is the home of the security/**proof** story: marketing version + build, the **Sentinel/native mix**, and **hardened-surface coverage** (FR-15, FR-17). The proof *metric* stays here — there is **no Sentinel/native-mix badge in the chrome** and no always-on hardened-surfaces panel in v1. *(Operational **signing** status is the one security element that does live in the chrome — distinct from the proof metric; see below.)*
- **Signing** (FR-19..21, UJ-5) — a developer imports a key file and signs built **executables** — Windows **Authenticode** signing of the PE that `snc` produces (confirmed; **not** commit-signing); status shows in the status-bar indicator. **Honesty (per §5):** while a key is loaded, the *native host* holds a secret in a surface that is **not yet hardened** — v1 says so plainly, and hardening the signing-key surface is the next increment (realizes the PRD's "secrets" surface as v1 UX; hardening next — §5 SEC-5). *(PRD updated: FR-19..21, UJ-5, §5 SEC-5.)*
- **Language-gap list** (FR-16) and **migration history / hardening playbook** (FR-18) are **maintained artifacts delivered to the language team / published**, not primary in-IDE UI in v1 `[ASSUMPTION: surfaced as exported docs, not a panel]`. Seeded with gap #1 (turnkey Windows MSVC build→link).
- The everyday felt proof is the **distinct security diagnostic** itself: every time a `secret`-leak shows up coral-and-shielded in the editor, the guarantee is demonstrated, not asserted.

## Inspiration & Anti-patterns

- **Lifted from Zed / Lapce (native, Rust):** the native-performance discipline — rope buffer + incremental parse + GPU text; ~2 ms input latency, 120 FPS targets; low memory by bypassing the browser engine (research §2).
- **Lifted from CLion / RustRover:** Build/Run spawns the toolchain into a console pane with **clickable error links** — the model for our output pane + triad.
- **Lifted from VS Code:** the **diagnostic triad** (inline squiggle ↔ Problems list ↔ clickable output sharing one model) and `Ctrl+P` fuzzy open — *the patterns*, not the Electron shell.
- **Rejected — an Electron/web shell:** undercuts the native-perf and security argument; the whole product thesis is bypassing the browser engine (PRD §7).
- **Rejected — alarmist security UI:** no red banners, klaxons, or modal "VULNERABILITY!" interrupts. Security shows up as calm, precise diagnostics (PRD §9).
- **Rejected — feature bloat that grows `% native` (SM-C1) or trades keystroke latency for feature count (SM-C2):** both contradict the product's own success metrics.
- **Deferred (not rejected):** completion / go-to-def / hover (LSP-driven, once the seam matures), **debugging** (the #1 post-v1 fast-follow), source-control UI, minimap, split panes.

## Responsive & Platform

| Concern | Behavior |
|---|---|
| **Primary platform** | Windows-first, **x64** native desktop. |
| **DPI** | Per-monitor DPI scaling via `MulDiv` (Theme.h `dpiScale`); crisp on mixed-DPI multi-monitor setups. |
| **Light/dark** | Follows the Windows light/dark setting (Theme.h `themeOverride -1`); user override to force light/dark in Settings. |
| **Window** | Resizable single window; regions resize via splitters; the editor area is the priority surface and never collapses. Bottom dock and tree are collapsible. |
| **Next platforms** | **macOS is the next target** (post-v1, gated on a shipping Win32 product); Linux later (PRD §7, §11). Not in v1 scope. |

This is a **native desktop** experience, not responsive web — there is no mobile/touch surface in scope.

## Key Flows

Named-protagonist journeys, mirroring the PRD UJ names verbatim, each with a climax beat.

### Flow 1 — Devon builds and runs his first Sentinel program (UJ-1)

1. Devon, a systems engineer new to Sentinel, chooses **Open Folder** and points at a `.sentinel` project; the project tree fills (FR-3).
2. He opens a file from the tree; syntax highlighting renders — keywords orchid, strings warm-orange (`{colors.syn-keyword}`, `{colors.syn-string}`) (FR-2).
3. He edits, hits **Build** (`Ctrl+Shift+B`); the exact `snc build …` command is visible, and output **streams live** into the Output pane while the editor stays fully interactive (FR-4, FR-5, NFR-PERF-3).
4. Build succeeds; he hits **Run** (`F5`).
5. **Climax:** the program's **stdout and exit code appear in the pane** (FR-7) — the whole edit→build→run loop closed without leaving the window.

*Failure:* a compile error → the triad lights up; clicking the output `file:line` jumps him to the span (FR-6).

### Flow 2 — Devon catches a `secret` leak before it ships (UJ-2)  ★ flagship

> Visual reference: [`mockups/key-secret-leak-editor.html`](mockups/key-secret-leak-editor.html)

1. Devon writes a branch on a `secret`-typed value.
2. The editor renders a **coral squiggle** at the exact span with a **gutter shield** (`{components.squiggle-security}`) — visibly different from an ordinary red error.
3. The Problems list adds one shield-marked row: "`secret` reaches a branch condition" (`{components.diagnostic-badge-security}`); clicking either the squiggle or the row jumps to the line (FR-8, FR-9, FR-11).
4. He refactors to a branch-free form.
5. **Climax:** the diagnostic **clears from all three places at once** (FR-10) — the language's headline guarantee showed up *in the editor*, coral-and-shielded, then resolved. The safety claim is demonstrated, not asserted.

*Failure:* on a full build, the authoritative `snc` finding supersedes the live keystroke squiggle for the same span — no double-render (addendum).

### Flow 3 — Devon's machine isn't build-ready, and the IDE walks him through it (UJ-3)

> Visual reference: [`mockups/key-toolchain-readiness.html`](mockups/key-toolchain-readiness.html)

1. On first build, the toolchain is incomplete — the runtime archive `snc` links against isn't staged for this linker.
2. Instead of a raw linker error, the IDE's **toolchain-readiness** check names the gap: "Runtime archive not found — compile works, link can't complete" (FR-12).
3. It shows a **copy-pasteable remediation**, and — because this is upstream (Sentinel gap #1) — states plainly that there's no local workaround yet, rather than implying a fix that doesn't exist.
4. Devon follows the guidance (or learns the honest status) and, once the environment is ready, builds again.
5. **Climax:** he goes from "broken" to **first successful build without a web search** — the failure became a guided step, not a wall.

### Flow 4 — Priya harvests the gap list the IDE produced (UJ-4)

1. Priya, the Sentinel language lead, opens the **Language-gap list** the project maintains (FR-16).
2. She reviews concrete, prioritized capability requests surfaced by actually building and running the IDE — gap #1: turnkey Windows MSVC build→link.
3. She folds the top items into the language backlog.
4. **Climax:** the IDE has **paid for itself as a roadmap input** — its unmet needs are now the language's prioritized work, the forcing function working as designed.

### Flow 5 — Devon signs his build  (UJ-5)

> Visual reference: [`mockups/key-signing.html`](mockups/key-signing.html)

1. Devon opens the app menu (`≡`) › **Signing › Import Signing Key…**.
2. He picks his key file (`.pfx`) and enters the passphrase; the dialog shows the loaded identity ("ACME Code Signing") and an **honesty note** — the key is held by the native host this session, and key-handling isn't a hardened Sentinel surface yet (next increment).
3. He toggles **Sign on Build**, then Builds. The status-bar indicator goes `{colors.accent}` "Signing…", then `{colors.trust-verified}` **"Signed"**; the Output pane records the signed artifact + identity.
4. **Climax:** the produced executable is **Authenticode-signed** and the status bar shows **Signed** at a glance — one concrete step on the supply-chain pillar. The honest caveat keeps it precise: the artifact is signed (origin + integrity), but signing here does **not** by itself harden the apex — the IDE's *own* release provenance (SEC-4) and the key-handling surface (SEC-5) come next. *(Narrowed per adversarial review A1 — was overclaiming "the apex story becomes something Devon does.")*

*Failure:* wrong passphrase or key/algorithm mismatch → `{colors.diag-error}` "Sign failed"; the dialog names the cause and the fix, and the artifact stays unsigned (never silently "signed").
