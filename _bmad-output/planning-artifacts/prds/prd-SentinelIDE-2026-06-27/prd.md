---
title: "Product Requirements Document: SentinelIDE"
status: reopened-and-reconciled
prior_status: final                # final 2026-06-28; re-opened 2026-09-02 under decision D2
created: 2026-06-27
updated: 2026-09-02
reconciled_against: "shipped Sentinel-IDE v0.1.7 (build 164) — phases 1–42"
reconciliation_source: "../../REALITY-DELTA.md"
---

# PRD: SentinelIDE

> ## ⚠ Re-opened 2026-09-02 and reconciled against the shipped product
>
> This PRD was written **2026-06-27/28, before the Win32 prototype existed**, and was marked
> `final` on 2026-06-28. It was never revised as the product diverged. It has now been
> **re-opened** (decision **D2**) and reconciled against **Sentinel-IDE v0.1.7 (build 164),
> phases 1–42**, using the verified delta record at [`../../REALITY-DELTA.md`](../../REALITY-DELTA.md).
> Every factual correction below cites a row ID (`RD-01` … `RD-27`) from that file rather than
> re-deriving it from source.
>
> **This document is still an intended-v1 SPEC. It is not a description of the shipped build**
> (decision **D1**). Read it with these three markers:
>
> | Marker | Meaning |
> |---|---|
> | **`[NOT BUILT in v0.1.7 — RD-nn]`** | Designed, still wanted, **does not exist in code.** Kept deliberately. Full list: **§18.1**. |
> | **`[SHIPPED — SPEC ADDED 2026-09-02]`** | The product shipped this with no requirement behind it; the requirement is being written after the fact. Full list: **§18.2**. |
> | *(no marker)* | Specified and shipped. |
>
> The single largest correction is **signing**: this PRD originally specified Windows
> **Authenticode** signing of a produced PE, with key-file import, a passphrase, a certificate
> identity and a six-state chip. **None of that was ever built and none of it is the plan.**
> The product implements **ADR-0061 Sentinel-native signing** — Ed25519, no certificates, no
> passphrase, no identity. §3, §4.8, UJ-5, §5 SEC-5 and OQ-9 are rewritten accordingly (RD-01).
>
> The second largest is that an entire **project/build model** — a manifest, N targets, four
> release tiers, a scheme selector and a Project Settings dialog — shipped with **no requirement
> anywhere in this PRD**. It is added as §4.9 (RD-07 … RD-11).

## 0. Document Purpose

This PRD is for the downstream UX, architecture, and epics/stories workflows, and for the Sentinel language team who will consume its feedback. It builds on — and does not duplicate — two finalized inputs in `../briefs/brief-SentinelIDE-2026-06-27/`: the **product brief** (vision, posture, scope) and its **addendum** (posture mechanics, milestone ladder, exposed-surface map), plus this run's **`research-ide-landscape.md`** (in this PRD folder; IDE table-stakes, native-editor architecture, NFR numbers). Vocabulary is Glossary-anchored (§3); features are grouped with globally numbered FRs; inferred values are tagged `[ASSUMPTION]` inline and indexed in §17.

The build posture is **locked** (brief): **B-path** (ship on a thin native host now) · **C-rule** (every security-relevant surface is Sentinel from day one; native only for security-irrelevant chrome) · **A-destination** (native footprint is a tracked, shrinking debt). This PRD turns that posture into requirements. Delivery is **agile**; this PRD scopes the v1 trajectory, not a frozen spec.

*(Reconciliation, RD-15: the original text here said "versioned from **0.1.0 (build 1)**". That was never how the product versioned. The marketing version is a literal in `scripts/build.bat` and the build number is **derived from git** — `git rev-list --count HEAD` plus a fixed `BUILDBASE`. Nothing increments; the same commit always stamps the same number. Shipped: **0.1.7 (build 164)**. See §7.)*

## 1. Vision

SentinelIDE is a native, Windows-first IDE for **Sentinel** that is itself built in Sentinel. It serves four ends at once: a genuinely useful environment for people writing Sentinel; a **proof** that security-critical software can be built in the language; a **forcing function** that drives Sentinel's roadmap from real needs; and a **migration playbook** for hardening native codebases surface by surface.

It earns that role because an IDE sits on Sentinel's exact threat model — it **executes untrusted code** on every build, **holds secrets**, and is the **top of a software supply chain**, where everything authored through it inherits its integrity. v1 makes a narrow, honest claim true on day one: the code that **interprets** untrusted bytes is written in a language that structurally forbids the bug — **the app is the argument**, even while the surrounding native host is still being migrated inward. §5 states the claim precisely, including what is *not* yet covered.

## 2. Target User

### 2.1 Jobs To Be Done

- **Sentinel application developer (primary):** "Give me a fast edit→build→run loop with real diagnostics so I can write Sentinel without fighting my tools." Functional: build/run, see errors in place. Emotional: confidence the tool won't betray the code.
- **Sentinel language team (secondary):** "Show me, concretely and in priority order, what the language is missing so I can build the right things next."
- **Security buyer / proof audience (banks, gov, regulated):** "I need comfort that the tooling at the top of my supply chain can't be turned against me." They need to believe Sentinel can build real, security-critical software — including the dangerous parts — and that the code handling untrusted bytes is *provably* sound, not merely asserted.

### 2.2 Non-Users (v1)

- Developers of languages other than Sentinel — this is **not** a polyglot IDE.
- macOS / Linux users — **Windows-first**; other platforms are post-v1.
- Plugin/extension authors — there is no extension API in v1.
- End users who only want a text editor — the value is the Sentinel-aware loop.

### 2.3 Key User Journeys

- **UJ-1. Devon builds and runs his first Sentinel program.**
  Devon, a systems engineer new to Sentinel, opens a project, edits a file with syntax highlighting, hits Build, watches output stream into the results pane, and runs the resulting executable — all without leaving the window. **Climax:** exit code and stdout appear in the pane. **Realizes FR-1, FR-2, FR-4, FR-5, FR-7.** *(Corrected, RD-07: the original beat was "opens a `.sentinel` **folder**". He opens a **manifest** — `*.sntproject` or legacy `sentinel.toml`, FR-24 — and the toolbar's scheme selector then tells him which target and tier Build will act on, FR-27. Opening a bare folder with no manifest gives him an editor and no build.)*

- **UJ-2. Devon catches a `secret` leak before it ships.** **`[NOT BUILT in v0.1.7 — RD-13, RD-14]`**
  Devon writes a branch on a `secret` value. The IDE shows a red squiggle at the exact span and a Problems-list entry "secret reaches a branch condition"; clicking either jumps to the line. He fixes it to a branch-free form; the diagnostic clears. **Climax:** the language's headline guarantee shows up *in the editor*, not just at the CLI. **Realizes FR-8, FR-9, FR-10, FR-11.**
  **What actually ships in v0.1.7:** `struct Diag` has **no severity field** (RD-13), so no diagnostic is distinguishable as a security finding; there are **no squiggles at all** (RD-14) — after a build the whole *line* background is tinted, cleared on the next edit — and the gutter paints line numbers only, with no shield glyph. The Problems list has three columns (Message · File · Line), and clicking a row does navigate. So the **navigation** half of this journey works and the **flagship rendering** half does not. This is the single most visible spec/reality gap in the document.

- **UJ-3. Devon's machine isn't build-ready, and the IDE walks him through it.** **`[NOT BUILT in v0.1.7 — RD-19]`**
  On first build, the toolchain is incomplete (the Sentinel runtime archive `snc` links against isn't staged for this linker). Instead of a cryptic link error, the IDE detects the gap and shows a guided, copy-pasteable remediation. **Climax:** Devon goes from "broken" to "first successful build" without a web search. **Realizes FR-12.** *(Grounded in a real finding this session — see §14 Risk.)*
  **What actually ships in v0.1.7:** no readiness surface and no remediation dialog. What closed the real link gap (phase 13) was silent auto-repair, not guidance: `findVcvars` locates `vcvars64.bat` and `captureMsvcEnv` injects the MSVC environment into the build child process. When no MSVC environment is found the Output pane prints **one warning line** naming Settings → MSVC environment. There is no component-by-component check, no copy-pasteable remediation, and no "the fix is upstream" statement (RD-19).

- **UJ-4. Priya harvests the gap list the IDE produced.**
  Priya, the Sentinel language lead, opens the IDE's accumulated **language-gap list** — concrete, prioritized capability requests surfaced by building and running the IDE itself (gap #1: turnkey Windows MSVC build→link). **Climax:** the IDE has paid for itself as a roadmap input. **Realizes FR-16.**

- **UJ-5. Devon publishes a module his consumers can refuse to build against.** *(Rewritten 2026-09-02 — RD-01, RD-02, RD-03.)*
  Devon opens the app menu (`≡`) › **Signing & Trust…**. There is no key file to import and no passphrase to type: he clicks **Generate**, which runs `snc keygen -o sentinel.key` into his project, and the panel shows the resulting Ed25519 public key as a 16-character prefix — **that is the entire identity**. With `crypto.sentinel` open in the editor he types `secret` into the grants field and clicks **Sign**, which runs `snc sign crypto.sentinel --key sentinel.key --grant secret` and writes a detached, human-readable **`crypto.sentinel.sig`** beside the source. The status-bar chip re-verifies the *open file* through `snc verify` and settles on **✓ Signed**.
  Downstream, Priya consumes Devon's module. She opens the `.sig`-carrying file, clicks **Import** in the same panel, and the IDE appends a `[[keys]]` block — Devon's bare 64-hex `pubkey` and his `grants` — to her project's **`sentinel-trust.toml`**. In **Project Settings** she sets signature requirement to **strict**. Now her builds emit `snc build … --require-signatures strict --trust sentinel-trust.toml`, and an unsigned or untrusted source **refuses to compile**.
  **Climax:** not a green badge on Devon's exe — the *consumer's build failing closed*. Signing here is not a publisher assertion attached to an artifact; it is an input gate the consumer controls.
  **The honest limits, stated in the journey because they are load-bearing:**
  - Devon's chip proves **byte-integrity and key identity of the source file**. It says nothing about the produced executable — the chip is bound to the open source file and is **never touched by a build** (RD-04).
  - Priya's `grants = ["secret"]` ceiling is **recorded intent, not an enforced gate**: `snc` v1's capability extractor only ever detects `ffi`, so `secret` in a ceiling is intersected but never derived from the code (**L-1**, §18.4). Identity and integrity *are* genuinely enforced; capabilities are not.
  - `strict` bites on **executable targets only**. `snc build --lib` / `--shared` never invoke the trust gate, so a Library or Shared target displays "signing: strict" and enforces nothing (**L-2**, §18.4).
  **Realizes FR-19, FR-20, FR-21, FR-22, FR-23.** *(Matches UX EXPERIENCE Flow 5, reconciled to ADR-0061 in the same pass.)*

## 3. Glossary

- **Sentinel** — the security-first systems programming language the IDE targets and is built in. Files use the `.sentinel` extension.
- **snc** — the Sentinel compiler driver (`snc.exe` on Windows). Provides `build` (compile + link to executable), `build --lib/--shared --emit-header` (C-ABI library + header), `lex`/`ast`/`parse` stage dumps, and — per ADR-0061 — `keygen` / `sign` / `verify` plus the build flags `--require-signatures off|warn|strict --trust <manifest>`. **`verify` and `keygen`/`sign` are separate capabilities that fail independently**: `verify` is implemented inside `snc`, while `keygen`/`sign` shell out to `keygen_core.exe` / `sign_core.exe`, separate Sentinel programs that must sit beside the `snc` binary. A build can advertise `snc keygen` in its help text and still fail at runtime, so the IDE probes both and greys only the buttons that genuinely cannot work (FR-19).
- **Native host** — the thin layer providing window, message loop, and rendering, written in non-Sentinel code (e.g., Win32/C++). The only place non-Sentinel code is permitted (see C-rule).
- **Chrome** — security-irrelevant UI plumbing: window creation, message loop, widget painting, layout. May live in the native host.
- **Surface** — a unit of behavior that *interprets* untrusted bytes, touches secret material, or generates output/artifacts. Distinguish **ingestion/transport** (receiving raw bytes from the OS — clipboard, drag-drop, file read, IME) from **interpretation** (deriving structure/meaning from them); the C-rule hardens interpretation (see §5).
- **Hardened surface** — a Surface whose interpretation logic is implemented in Sentinel and subject to Sentinel's safety checks.
- **Hardened-surface coverage** — the share of identified security-relevant Surfaces implemented in Sentinel; the primary, falsifiable A-destination metric (per-Surface binary).
- **C-rule** — the contract that every security-relevant Surface is Sentinel from day one; native code is permitted only for Chrome.
- **Sentinel/native mix** — the share of first-party IDE source that is Sentinel vs non-Sentinel (native) code, measured per build and surfaced in the About dialog (FR-17); the A-destination metric — Sentinel trends up, native toward zero, with **no fixed v1 ceiling**. ("% native" denotes the native portion of this mix.)
- **Untrusted-input surface** — the v1 Hardened surface: parsing of source files, project/config files, and build/compiler output.
- **Secret** — a Sentinel `secret`-typed value, subject to the compiler's constant-time / no-leak checks.
- **Code signing** — *(rewritten 2026-09-02, RD-01. The prior definition said "Windows **Authenticode** signing of the executable `snc` produces". That is wrong end to end and was never built.)* Producing an **Ed25519 signature over a file's raw bytes** with `snc sign`, per **ADR-0061**, emitted as a **detached signature** beside the file. There are **no certificates**, no certificate authority, no chain, no expiry and no revocation anywhere in the model — so no publisher identity is asserted and nothing needs a timestamp (see OQ-9, closed **DEAD**). Signing applies to any file; in practice the two paths are the **open source file** (Signing & Trust panel) and the **build artifact** (manifest `signing.sign`). Still distinct from the IDE's own release provenance (SEC-4).
- **Signing key** — *(rewritten 2026-09-02, RD-01.)* An **Ed25519 keypair generated by `snc keygen -o <path>`**, conventionally `sentinel.key` in the project directory. There is **no key-file import, no passphrase, no subject/issuer, no validity window** — a key's public half is **64 hex characters** and that is its whole identity; the UI shows a 16-character prefix. The IDE never holds key material: `snc` signs in a child process and the key file lives on disk in the project (see SEC-5, restated).
- **Detached signature (`.sig`)** — the carrier `snc sign` writes beside a signed file (`<file>.sig`). Human-readable, holding `algorithm`, the signer's public `key`, and any `grants`. Its presence is what the status chip reads (FR-21); an edit to the source invalidates it.
- **Trust manifest (`sentinel-trust.toml`)** — the **consumer's own** list of keys it will build against: an array-of-tables of `[[keys]]` entries, each with a **required bare 64-hex `pubkey`**, an optional `name` (diagnostics only) and an optional `grants` capability ceiling. Trust is entirely local and entirely the consumer's decision — there is no CA and no shared registry. `snc`'s parser is `deny_unknown_fields`, so unmodelled keys are **hard parse errors that abort the build in both `warn` and `strict`**, and an `ed25519:` prefix on a pubkey parses but silently never matches. (FR-22.)
- **Capability grant** — a capability name attached to a signature (`snc sign --grant <cap>`) and intersected against the trusted key's `grants` ceiling in the trust manifest. **See the ceiling limit L-1 (§18.4): `snc` v1 only ever extracts `ffi` from code, so a ceiling naming `secret` / `constant_time` / `alloc` is recorded intent, not an enforced gate.** Never describe a grants ceiling as restricting what signed code may do.
- **Signature requirement mode** — the build-time gate: `off` · `warn` · `strict`, declared as `[signing] require` in the project manifest and emitted as `--require-signatures <mode> --trust <path>` on the `snc build` line. `strict` refuses to build unsigned or untrusted sources. **Scoped by L-2 (§18.4): the gate runs on executable targets only.** (FR-23.)
- **Project manifest** — the file that makes a folder a project: `*.sntproject` (preferred) or the legacy `sentinel.toml`. Carries `[project]`, `[build]`, `[signing]` and zero or more `[[target]]` blocks. A folder with no manifest is **not** a project (FR-24).
- **Target** — one `[[target]]` block: a named artifact with its own `entry`, `type` (`executable` | `library` | `shared`) and optional `links`. A project has one or more; a manifest declaring none gets a single synthesized target for backward compatibility (FR-25).
- **Release tier** — one of **Development · Experimental · Stable · Hardened** (upstream `TIERED_RELEASES.md`), selected per session and persisted as `[build] default_tier`. **Today a tier only chooses the output directory** — `snc` has no tier flag, so everything builds at `-O0` regardless (FR-26).
- **Scheme** — the active **target × tier** pair, chosen in the toolbar scheme selector and determining what Build and Run act on and where the artifact lands: `target\<tier>\<name>.<ext>` (FR-27).
- **Diagnostic** — a single compiler/language-server finding with a source range, severity, and message (e.g., a `secret`-leak, borrow, or effect error). **`[Severity and source *range* are NOT BUILT in v0.1.7 — RD-13]`:** the shipped `struct Diag` carries only `file`, `line`, `col` and `msg`.
- **Diagnostic model** — the one in-memory representation of Diagnostics shared by the inline squiggles, the Problems list, and clickable output (the "triad").
- **Problems list** — the panel listing all current Diagnostics, each navigable to its source range.
- **Stage dump** — `snc lex|ast|parse` output; an interim intelligence source for editor features.
- **Language server** — the process the editor queries over LSP for Diagnostics (and later completion/hover); seam options are the `sentinel-lsp` crate and/or Stage dumps.
- **Language-gap list** — the prioritized, accumulating record of Sentinel capabilities the IDE needs but the language lacks; the forcing-function deliverable.
- **Toolchain readiness** — whether `snc`, the runtime archive, and a compatible linker are present and matched so that build→run succeeds.

## 4. Features

### 4.1 Editor

**Description:** A native code editor for `.sentinel` files with the table-stakes set a credible IDE needs (research §1). Realizes UJ-1.

#### FR-1: Multi-file editing
A developer can open a project and edit multiple `.sentinel` files with tabs, undo/redo, find/replace (incl. in-file regex), go-to-line, and multi-cursor.
**Consequences (testable):**
- Opening a project lists its `.sentinel` files in a project tree (FR-3). *(Corrected, RD-07: a **folder** is not a project — see FR-24. A folder without a manifest opens as a plain Files view with no build.)*
- Edits to a 10,000-line file remain responsive (see NFR perf, §6).
- Unsaved edits are never lost on build or focus change. *(Now realized by FR-33.)*
- **`[NOT BUILT in v0.1.7 — RD-17]` Tabs.** The IDE holds **one buffer**. The "tab strip" is a painted label for the single open file, with a `●` dirty dot and a coral underline; opening a second file replaces the first. Multi-tab editing — open/close/reorder, per-tab dirty state, closing a dirty tab — does not exist.
- **`[NOT BUILT in v0.1.7 — RD-18]` Find/replace (incl. regex), go-to-line, multi-cursor.** None of these exist. The complete accelerator table is `Ctrl+S` · `Ctrl+Z`/`Ctrl+Y` · `Ctrl+N` · `Ctrl+O` · `Ctrl+Shift+N` · `Ctrl+Shift+B` · `F5` · `Ctrl+L` · `Ctrl+,`. Undo/redo **does** ship; the rest of this bullet does not.

#### FR-2: Sentinel syntax highlighting
A developer sees Sentinel syntax highlighted, including the security-relevant keywords (`secret`, effects, borrow-related).
**Consequences (testable):**
- Keywords, types, literals, and comments are visually distinguished using the §9 palette.
- Highlighting updates incrementally on edit without a full-file re-parse stall `[ASSUMPTION: incremental/Tree-sitter-style highlighting]`.
- Per-line heavy highlighting is disabled beyond ~20,000 chars/line (research §3).

#### FR-3: Project tree & file open
A developer can browse the project tree and open files, plus a fuzzy open-file finder.
**Consequences (testable):**
- The tree reflects on-disk structure; opening a node opens the file. *(Ships. The tree additionally carries a **Targets** group driven by the manifest — FR-25.)*
- **`[NOT BUILT in v0.1.7 — RD-18]` The `Ctrl+P` fuzzy open-file finder.** There is no fuzzy finder and no `Ctrl+P` binding. Files are reached from the tree, a Problems row, or an Output `file:line:col` link.

### 4.2 Build & Run

**Description:** Invoke `snc` from the IDE and surface its results. The single most important loop. Realizes UJ-1.

#### FR-4: Build via snc
A developer can build the active **scheme** (target × tier, FR-27) via a button or command that invokes `snc build`, with the exact command line visible.
**Consequences (testable):**
- The invoked command line is shown to the user, echoed to the Output pane as `> <cmd>`.
- Build runs as a child process **off the UI thread**; the editor stays interactive during the build (§6).
- The command is **composed, not configured** *(corrected, RD-12: the original FR said "visible **and configurable**", and the UX spec listed a build-command field in Settings. Neither exists and neither is the design.)* The line is derived from the manifest + active target + tier: entry, `-o target\<tier>\<name>.<ext>`, `--lib`/`--shared --emit-header` for non-executable targets, `--lib-path` / `--link`, and the signature gate flags (FR-23). What Settings **does** expose is an **`snc.exe` path override** and an **MSVC `vcvars64.bat` override**, both blank = auto-detect.
- A dirty buffer is **auto-saved before the build**, not discarded and not blocked on a prompt.
- With a project loaded, the Output pane states plainly that the tier chose only an output directory (see FR-26 and **L-3**, §18.4).

#### FR-5: Streamed output pane
A developer sees build stdout/stderr stream into an output pane live, line by line (not batched at exit).
**Consequences (testable):**
- Output lines appear during the build, not only on completion.
- The pane preserves order and distinguishes stderr.

#### FR-6: Clickable file:line errors
Compiler output lines containing `file:line[:col]` are navigable: clicking moves the editor to that location.
**Consequences (testable):**
- A diagnostic line in the output pane jumps the cursor to the referenced span.

#### FR-7: Run
A developer can run the built executable and see its stdout/stderr and exit code in the pane.
**Consequences (testable):**
- Exit code is displayed; the run does not block the UI.

### 4.3 Diagnostics

**Description:** Sentinel's safety findings surfaced where developers work. The triad must share one model (research §1). Realizes UJ-2.

#### FR-8: Inline squiggles **`[NOT BUILT in v0.1.7 — RD-14]`**
Diagnostics render as inline squiggles at the diagnostic's source range.
**Consequences (testable):**
- A Diagnostic with range (line L, cols C1–C2) renders a squiggle spanning exactly that range; a zero-width range squiggles the word at that position.
- Squiggle styling reflects severity (error vs warning), with a distinct security treatment and a **gutter shield glyph** for Sentinel-safety findings.
- **Reality in v0.1.7:** there are **no squiggles anywhere** and **no gutter glyph**. After a build, every diagnostic in the open file tints its **whole line background** with a blend of the window background and the error colour; the tint clears on the next edit. The gutter paints line numbers only. Nothing is rendered per-span, and nothing distinguishes a security finding — which is what makes UJ-2 unbuilt rather than merely unstyled.

#### FR-9: Problems list
All current Diagnostics appear in a Problems list; selecting one navigates to its source range.
**Consequences (testable):**
- Every current Diagnostic appears as exactly one Problems-list row. **`[Partially built — RD-13]`:** the shipped list has **three** columns — **Message · File · Line** — and **no severity column**, because `struct Diag` carries no severity. Every row renders identically: no per-severity colour, no shield glyph, no security class.
- Selecting a row moves the editor caret to the Diagnostic's line/column. *(Ships; navigation is by line/col, not by range — there is no range on `Diag`.)*
- Selecting a row routes through the unsaved-changes guard when it would open a different file (FR-33).

#### FR-10: One diagnostic model
The inline squiggles (FR-8), Problems list (FR-9), and clickable output (FR-6) are driven by a single Diagnostic model.
**Consequences (testable):**
- A diagnostic appears consistently in all three places; clearing it clears all three.
- Build-time (authoritative) and any on-keystroke (fast) Diagnostics are reconciled so a build does not duplicate live squiggles.

#### FR-11: Sentinel-specific diagnostics
The IDE surfaces Sentinel's distinguishing Diagnostics — `secret`-leak (constant-time), borrow-check, and effect errors — produced by `snc`.
**Consequences (testable):**
- A program where a `secret` reaches a branch/index/divisor shows the `secret`-leak diagnostic at the right span. Realizes UJ-2.
- **v1-required:** compile errors **and** the `secret`-leak diagnostic (UJ-2 is the flagship "guarantee in the editor" moment and depends on it). Borrow-check and effect diagnostics are the v1 target — surfaced if `snc` exposes them, else fast-follow.
- **`[Partially built — RD-13]`:** whatever `snc` emits is parsed and listed, so a `secret`-leak diagnostic does reach the Problems list and is clickable. What does **not** exist is any way to tell it apart from a syntax error — no severity, no security class, no distinct rendering. The **Sentinel-specific** half of this FR is therefore unmet even though the plumbing works.

### 4.4 Toolchain Readiness

**Description:** Make build/run robust against an incomplete Windows toolchain. Grounded in a real session finding (§14). Realizes UJ-3.

#### FR-12: Detect & guide toolchain setup **`[NOT BUILT in v0.1.7 — RD-19]`**
The IDE checks Toolchain readiness — whether `snc`, the runtime archive, and a compatible linker are present and matched — before/around a build, and surfaces the result clearly rather than failing with only a raw linker error.
**Consequences (testable):**
- On a readiness failure, the IDE names the specific missing/mismatched component (e.g., "runtime archive absent") and shows the best-known remediation; where the fix is upstream (gap #1, §14), it says so and does **not** imply a local fix that does not yet exist.
- When readiness passes, build→run succeeds end-to-end.

**Notes:** `[NOTE FOR PM]` v1 detection is only as good as the current diagnosis of the Windows link gap (OQ-7) — confirm the exact failure mode with the language team before hard-coding remediation text.

**Reality in v0.1.7 (RD-19):** no readiness check, no readiness surface, no remediation dialog. The link gap this FR exists to explain was instead **closed silently**: `findVcvars` auto-detects `vcvars64.bat` and `captureMsvcEnv` injects the MSVC environment into the build child process, with an override in Settings. The only user-facing readiness signal is **one Output-pane line per build** — either "no MSVC environment found" (warning colour, naming the Settings field) or the path that was found. `snc` presence is handled by picking the most capable `snc` on disk. So the *outcome* this journey wanted (Devon reaches a first successful build) is largely delivered by auto-repair, while the *requirement as written* — component-by-component check, named missing component, copy-pasteable remediation, explicit "the fix is upstream" — is unbuilt. **Do not close this FR on the strength of the auto-detect.**

### 4.5 Hardened Surfaces & Sentinel/native mix

**Description:** The C-rule as product requirements. See §5 for the contract and threat model.

#### FR-13: v1 untrusted-input surface is Sentinel
The Untrusted-input surface (parsing of source, project/config, and build output) is implemented in Sentinel and passes Sentinel's own safety checks.
**Consequences (testable):**
- The parsing of untrusted bytes is Sentinel code embedded via the C-ABI boundary (addendum); the native host does not parse untrusted input.
- That Sentinel code compiles clean under `snc` (incl. its safety checks).
- **Status in v0.1.7 — substantially met (RD-27).** Four parsers are compiled from `src/sentinel/parsers.sentinel` into the binary via `snc build --lib` (ADR 0059) and called across the C-ABI: **`parse_diag`, `parse_trust`, `parse_sig`, `parse_manifest`** — every file reader/parser in the IDE. Each has a byte-identical C++ fallback for an `snc`-less build, held in lockstep by an `*_xcheck` test. **`parse_trust` is the first port at a security boundary**: the code deciding which keys a build trusts is itself Sentinel. The manifest **writer** (`saveProject`) is still C++, which is a *generation* surface, not an interpretation one — SEC-1's scope. This is the one place the product is ahead of the spec, and it should not be weakened while the rest of the document is corrected.

#### FR-14: Native code restricted to chrome
Non-Sentinel code exists only in the Native host for Chrome; no security-relevant Surface is implemented in native code.
**Consequences (testable):**
- A documented boundary lists what is native; review confirms no Surface crosses into native code.

#### FR-15: Hardened-surface coverage & Sentinel/native mix
The **primary** co-evolution metric is **Hardened-surface coverage** — the share of identified security-relevant Surfaces (§5) implemented in Sentinel (a falsifiable, per-Surface binary). The **Sentinel/native mix** (LOC share) is a secondary indicator, surfaced in the About dialog (FR-17). No fixed v1 ceiling; both are tracked as trends.
**Consequences (testable):**
- The set of identified security-relevant Surfaces is enumerated; each is marked Sentinel or native; coverage = Sentinel Surfaces ÷ total. v1 target: the Untrusted-input surface is covered (FR-13).
- A current Sentinel/native LOC mix (e.g., "Sentinel 18% / Native 82%") is computed per build and recorded per release so the trend is visible. *(Ships — see FR-17.)*
- The LOC mix is explicitly **not** a measure of how much *danger* sits in Sentinel — Hardened-surface coverage is. Both are reported; coverage leads.
- **`[NOT BUILT in v0.1.7 — RD-16]` Hardened-surface coverage is not rendered anywhere.** The About dialog computes and shows the **LOC mix only**. The primary, falsifiable metric — the one this FR says leads — has no surface in the product. Given the previous bullet, shipping the secondary indicator alone is the exact inversion this FR was written to prevent.

### 4.6 Co-Evolution / Language Feedback

**Description:** The forcing function as a first-class output. Realizes UJ-4. See §10.

#### FR-16: Maintain the language-gap list
The project produces and maintains a prioritized Language-gap list of Sentinel capabilities the IDE needs but the language lacks.
**Consequences (testable):**
- The list exists, is prioritized, and is delivered to the language team.
- It is seeded with gap #1 (turnkey Windows MSVC build→link).

### 4.7 About, Versioning & Migration History

**Description:** Product identity, the co-evolution metric, and the migration record — surfaced to the user and to the proof audience.

#### FR-17: About dialog
The About dialog shows the marketing version and build number and the current **Sentinel/native mix** (FR-15).
**Consequences (testable):**
- Version/build render as `<marketing> (build N)`. *(Corrected, RD-15: the original text said "starting **0.1.0, build 1**, incrementing" and "render as `0.1.0 (build N)`". The build number is **derived from git**, not incremented — see §7. Shipped: `0.1.7 (build 164)`.)*
- The Sentinel/native mix renders and updates as the mix changes. **Ships, and larger than specified (RD-16):** the dialog shows shields.io-style badge pills for **C++ · Sentinel · Build · Total** lines of code plus a "built in Sentinel" progress bar, whose grand total is counted by `tools/loc.sentinel` — Sentinel code counting the IDE's own source, which is itself part of the proof.
- **`[NOT BUILT in v0.1.7 — RD-16]`** Hardened-surface coverage is **not** rendered in About (FR-15).

#### FR-18: Migration history / hardening playbook
The project maintains a **migration history** — each security-relevant Surface's move from native to Sentinel, with before/after notes — usable as a brownfield-hardening playbook (the A-destination case study; brief addendum calls it "itself a deliverable").
**Consequences (testable):**
- Each hardened Surface has a dated migration-history entry.
- The history is published as a reusable, human-readable playbook artifact.

### 4.8 Code Signing & Trust (ADR-0061)

> **Rewritten 2026-09-02 (RD-01 … RD-06).** The original §4.8 specified Windows **Authenticode**
> signing: import a `.pfx`/`.p12`/PEM key with a passphrase, display a certificate subject/issuer
> and validity window, sign the produced PE, show a six-state chip, and decide RFC-3161
> timestamping. **None of that was built, and none of it is the plan.** The product implements
> **ADR-0061 Sentinel-native signing** — Ed25519, no certificates, no CA, no passphrase, no
> identity beyond a 64-hex public key, and no expiry. The concept-by-concept map is REALITY-DELTA §2.

**Description:** Ed25519 signing and consumer-controlled trust, per ADR-0061. The unit of value is
**not** a badge on a publisher's artifact but a **gate on a consumer's build**: a developer signs
files with a key they generate, a consumer records the keys they trust in their own manifest, and
the consumer's build refuses inputs that fail that check. Realizes UJ-5. Distinct from the IDE's own
release provenance (SEC-4) and from the IDE's own update signing (FR-30).

#### FR-19: Generate and use a Sentinel signing key
A developer can generate an Ed25519 keypair and sign the **open source file** with it from the app
menu (`≡` › **Signing & Trust…**), or from the status-bar chip.
**Consequences (testable):**
- **Generate** runs `snc keygen -o <path>` (conventionally `sentinel.key` in the project) and, on success, fills the key field with the produced path. **Browse** selects an existing key file. *(There is **no key import**, no `.pfx`/`.p12`/PEM, and **no passphrase** — those belong to the deleted Authenticode design.)*
- **Sign** runs `snc sign <open file> --key <key> [--grant <cap>]…`, writing a detached `<file>.sig` beside the source. On a non-zero exit the panel surfaces `snc`'s own output verbatim and no `.sig` is written.
- The panel displays a key as a **16-character prefix of its 64-hex public key**. There is no subject, no issuer, no organisation, no algorithm choice and no validity window to display, because none exist.
- The panel greys **only** the buttons the active `snc` genuinely cannot perform: `verify` and `keygen`/`sign` are **independent capabilities** (§3, "snc"), so the IDE probes both and selects the most capable `snc` on disk rather than the first one found. A build that advertises `snc keygen` in its help text may still fail at runtime; probing, not help text, decides.
- **Superseded:** the FR-19 honesty note about a key "held by the native host this session." The IDE **never holds key material** — see SEC-5 (restated).

#### FR-20: Sign the build artifact
A project can opt into signing its build artifact, and the Output pane records the result.
**Consequences (testable):**
- With `[signing] sign = true` in the manifest (checkbox "Sign the built artifact" in Project Settings, FR-28), a **successful** build runs `snc sign <artifact> --key <project>\sentinel.key` and reports the outcome in the Output pane.
- When `sign = true` but no `sentinel.key` is present, or the artifact is missing, the IDE says so in the Output pane in the warning colour and does **not** claim a signature.
- Signing the **open source file** on demand is FR-19's Sign button. *(The old "Sign on Build" toggle and "Sign now" pair map onto these two paths; the toggle now lives in the manifest, and "Sign now" acts on the open file, not on a build artifact.)*
- **`[NOT BUILT in v0.1.7 — RD-06]` — and this is a live defect, not merely an absence.** The original FR-20 required that a signature be "reported only **after the signing operation is verified to have succeeded** (not merely attempted)" and that "the IDE never reports an artifact as signed when it is not." The shipped path does the opposite: it prints **`[signed · <name>.sig]`** in the trust colour purely from `snc sign`'s **process exit code**, and never re-reads or verifies the artifact. `verifyFile` is not called on this path. **Re-filed as DEF-1 in §18.3, with the concrete failure and the closing condition.**
- **Removed:** the `[NOTE FOR PM]` on RFC-3161 timestamping and certificate expiry. Timestamping exists to make a signature outlive the certificate that produced it. **ADR-0061 has no certificates and nothing expires**, so there is nothing for a timestamp to protect. See **OQ-9, closed DEAD**.

#### FR-21: Always-on signing-status indicator
An always-visible status-bar chip shows the signing state **of the open source file**; clicking it
opens the Signing & Trust panel.
**Consequences (testable):**
- The chip is bound to the **open `.sentinel` file**, not to a build artifact *(corrected, RD-04: the original FR and the UX spec both said the chip was bound to "the current artifact" and was "never stale," with a new build invalidating a prior Signed. It is not, and a build never touches it.)* It is recomputed when a file is opened and **when it is saved** — **not on edit**, so the chip can read `✓ Signed` for a buffer that has already diverged from its signature — by reading `<file>.sig` and then running `snc verify` on a worker thread so the UI never blocks.
- The chip renders exactly **one of four** states *(corrected, RD-05: the original enumerated **six** — no key · key loaded · signing · signed · unsigned · failed. Two of those, "no key" and "key loaded", have **no referent** in a keyless model where the IDE holds no key, so they are not kept as gaps; they were requirements for a design that no longer exists.)*

  | Chip | When | Colour token |
  |---|---|---|
  | **✓ Signed** | `snc verify` passed, or a `.sig` exists and this `snc` cannot verify | `trust-verified` |
  | **⚠ Signature invalid** | `snc verify` failed | `diag-error` |
  | **…  verifying** | verification in flight | `text-muted` |
  | **⊘ Unsigned** | no `.sig` beside the file | `text-secondary` |

- **"Signed" asserts only** that the open file's bytes carry a valid Ed25519 signature by *some* key. It asserts **nothing** about who that key belongs to — there is no certificate, no chain and no authority to appeal to — and nothing about whether the reader trusts it. Trust is a separate, consumer-side act (FR-22). Copy says "Signed," never "Verified" or "Secure."
- "Signed" uses the distinct trust colour, **`trust-verified`** — the token name is settled and stays (**D3**); coral remains reserved for the Sentinel-safety signal. "Unsigned" is neutral, not alarmist; "invalid" uses the error colour.
- Clicking the chip opens Signing & Trust. This is the **one always-on security signal in the chrome**; the proof *metric* stays in About (FR-17).
- **Known defect (minor, RD-05):** with no file open the internal state is `Unknown`, which falls through the chip's `default:` branch and paints as **⊘ Unsigned** — while the Signing & Trust panel labels the same state "— no file open". The chip and the panel disagree about the same state. Logged in §18.3 as DEF-2.

#### FR-22: Consumer trust manifest **`[SHIPPED — SPEC ADDED 2026-09-02]`** *(RD-02)*
A project records the keys it trusts in its own **`sentinel-trust.toml`**, and the IDE can add a
signature's key to it in one action.
**Consequences (testable):**
- With a signed file open, **Import** reads `<file>.sig` and appends a `[[keys]]` block carrying a `name` (the file stem, diagnostics only), the signer's **bare 64-hex `pubkey`**, and its `grants` if any. A key already present is reported and not duplicated.
- The writer must emit `snc`'s schema exactly. `snc`'s parser is **`deny_unknown_fields`**, so any unmodelled key — `[dependencies.<name>]`, `sig`, `policy`, `forbids` — is a **hard parse error that aborts the build in both `warn` and `strict`**. A `pubkey` written with an `ed25519:` prefix parses successfully and then **silently never matches**, yielding UNTRUSTED with no error. Both are trap conditions the writer exists to avoid.
- Trust is **local and consumer-owned**. There is no registry, no CA, no shared root, and no way for a publisher to make a consumer trust them.
- The trust-manifest **parser is Sentinel** (`parse_trust`) — the first security-boundary port (FR-13, RD-27).

#### FR-23: Build-time signature requirement gate **`[SHIPPED — SPEC ADDED 2026-09-02]`** *(RD-03)*
A project declares whether its build requires its inputs to be signed by a trusted key.
**Consequences (testable):**
- `[signing] require = off | warn | strict` in the manifest (radio group in Project Settings, FR-28) appends **`--require-signatures <mode> --trust <project>\<trust file>`** to the `snc build` line whenever the mode is not `off`. `strict` refuses to build unsigned or untrusted sources.
- This is a gate on the build's **inputs** — the opposite direction from FR-20, which is an assertion about its **output**. The original PRD modelled only the latter; the gate is the part that actually protects a consumer.
- **Scope limit — state it wherever `strict` is described (L-2, §18.4):** `snc build --lib` / `--shared` **never invoke the trust gate**; the trust check runs only on the executable path and the library path silently discards the flags. The IDE emits the flags for every target type, so a **Library or Shared target displays "signing: strict" and enforces nothing.** This is upstream in Sentinel-lang and cannot be fixed here. Any copy describing `strict` as a guarantee must scope itself to executable targets.
- **Scope limit — grants (L-1, §18.4):** a key's `grants` ceiling is intersected for real, but `snc` v1's capability extractor only ever detects `ffi`, so a ceiling naming `secret` / `constant_time` / `alloc` is **recorded intent, not an enforced gate**, and `forbids` is unimplemented. Identity and byte-integrity *are* genuinely enforced. Never write that a grants ceiling "restricts" what signed code may do.

### 4.9 Project Model, Targets & Release Tiers **`[SHIPPED — SPEC ADDED 2026-09-02]`**

> **New section (RD-07 … RD-11).** The original PRD assumed a project *was* a folder and a build was
> one configurable command. The product shipped a **manifest-declared project with N targets across
> four release tiers**, a toolbar scheme selector and a Project Settings dialog. That is a file
> format, an IA region and a modal dialog with no requirement behind any of them. The shape:
>
> ```
> manifest (*.sntproject, else legacy sentinel.toml)              FR-24
>    ├── [project]  name · version · type · entry · icon
>    ├── [build]    src · lib_paths · links · default_tier        FR-26
>    ├── [signing]  require(off|warn|strict) · trust · sign       FR-23, FR-20
>    └── [[target]] × N   name · entry · type · links             FR-25
>                    ↓
>          active target × active tier  (the scheme)              FR-27
>                    ↓
>     snc build [--lib|--shared] <entry> -o target/<tier>/<name>.<ext>
>               [--lib-path …] [--link …]
>               [--require-signatures <mode> --trust <manifest>]
>                    ↓
>          target/<tier>/<name>.exe   (+ optional <name>.exe.sig)
> ```

#### FR-24: A project is declared by a manifest, not by a folder *(RD-07)*
**Consequences (testable):**
- A folder is a project only if it carries a manifest: **`*.sntproject`** (preferred) or the legacy **`sentinel.toml`**. **Open Project is a manifest *file* picker, not a folder picker.**
- A folder with **neither** opens as a plain **Files** view: the tree lists what is there, the editor works, and **there is no project build** — the single-file build path applies instead.
- Loading a manifest is what populates targets (FR-25), the default tier (FR-26), the scheme selector (FR-27) and the signing block (FR-20, FR-23).
- The manifest **parser is Sentinel** (`parse_manifest`, FR-13/RD-27); the **writer** is C++ and performs a *surgical* edit that preserves comments and unmodelled keys rather than re-serialising the file (FR-28).

#### FR-25: Multiple build targets per project *(RD-08)*
**Consequences (testable):**
- A manifest may declare N **`[[target]]`** blocks, each with `name`, `entry`, `type` (`executable` | `library` | `shared`) and optional per-target `links` that override `[build] links`.
- A manifest declaring **no** target gets exactly one synthesized from its `[project]`/`[build]` fields, and is flagged as such, so older manifests keep working unchanged.
- Build, Run, the artifact path, and the project tree's **Targets** group all follow the **active** target. Selecting a target node in the tree switches the active target.
- **Run applies to executable targets only**; with a Library or Shared target active the IDE says so rather than attempting to launch a `.a`/`.dll`.
- Switching target routes through the unsaved-changes guard (FR-33).

#### FR-26: Four release tiers *(RD-09)*
**Consequences (testable):**
- The tiers are **Development · Experimental · Stable · Hardened** (upstream `TIERED_RELEASES.md`). The project's `[build] default_tier` sets the initial tier; the selection is per session.
- The tier determines the **output directory**: `target\dev|experimental|stable|hardened\`.
- **Say only what is true (L-3, §18.4):** `snc` **has no tier flag**. Everything compiles at `-O0` regardless of tier, so a tier today chooses a directory and nothing else. The IDE states this in the Output pane on every project build; **no artifact may describe tiers as affecting optimization or hardening today.**
- *(Note for readers of the original PRD: the missing axis was always **tiers**. The words "debug" and "release" barely appear in this document, and the product has no debug/release switch.)*

#### FR-27: Scheme selector *(RD-10)*
**Consequences (testable):**
- The toolbar carries an **Xcode-style scheme selector** — `[● target ▾ │ tier ▾]` with a type-coloured dot — between the action buttons and the status bar. This is an **IA region the UX spine does not contain**.
- Each zone opens its own dropdown; changing either changes what Build and Run act on.
- The selector shows the **live derived output path**, `→ target\<tier>\<name>.<ext>`, so the consequence of the scheme is visible without building.

#### FR-28: Project Settings dialog *(RD-11)*
**Consequences (testable):**
- A modal **Project Settings** form edits the manifest structurally rather than as text: name / version / type / entry, `src` / `lib_paths` / `links` / default tier, a **TARGETS** section with per-target Name / Entry / Type, and the **signing block** — `require` radio (off | warn | strict), trust-manifest path, and "Sign the built artifact".
- Save goes through the **surgical writer** (FR-24): comments and keys the form does not model survive a round trip.
- Opening the dialog over a dirty buffer routes through the unsaved-changes guard (FR-33).

### 4.10 Application Shell & Platform Integration **`[SHIPPED — SPEC ADDED 2026-09-02]`**

> **New section (RD-20 … RD-26).** Six subsystems shipped with no requirement home in this PRD.
> Five appear in **no planning artifact at all**; logging is a half-case — it is specified in the UX
> EXPERIENCE spine (level, log-file location, reveal-in-Explorer, all of which ship) but has never
> had a PRD FR. These requirements are written **after** the code, and are here so the PRD stops
> under-describing the product it governs.

#### FR-29: Diagnostic logging *(RD-20 — specified in EXPERIENCE, no PRD FR until now)*
**Consequences (testable):**
- A thread-safe, append-only file logger records build invocations, file opens, signing operations, update checks and errors, each stamped with local time to the millisecond.
- **Level** (Error / Warn / Info / Debug / Trace) and **log-file path** are configurable in Settings and persisted; the default path is `%LOCALAPPDATA%\SentinelIDE\logs\sentinelide.log`.
- Settings offers **Reveal** to open the log's folder in Explorer.
- Messages at or below the configured level are written; an empty path disables logging entirely.

#### FR-30: In-product updates *(RD-21)*
**Consequences (testable):**
- The IDE checks a signed **appcast** for a newer version, offers **Skip / Install / Later** in a themed dialog, and can install without the user leaving the app. `≡` › **Check for Updates…** triggers a check on demand.
- The appcast is **Ed25519-signed** and the signature is verified before any update is offered; the feed is fetched over HTTPS from the project's public repository.
- Checking happens **shortly after startup and then hourly** on the IDE's own timer.
- If the embedded public key is still the build-time placeholder, the updater **refuses to initialise** and the menu item is **hidden rather than greyed** — an update path that cannot verify must not appear to exist.
- An **unattended** update install must not be blocked by a modal: the updater signals shutdown-pending so the unsaved-changes guard (FR-33) skips its prompt (the updater arms a short hard-exit watchdog, so a prompt there would be a lost-work hazard, not a safety net).
- `[NOTE FOR PM]` This is the IDE signing **its own releases** — adjacent to SEC-4 and unrelated to FR-19..23, which are the *developer's* signing of *their* code. Do not let the two collapse in messaging.

#### FR-31: Windows installer *(RD-22)*
**Consequences (testable):**
- The product ships as a **per-user `setup.exe`** requiring no administrator rights, carrying the executable, examples and README/LICENSE, creating a Start-Menu shortcut, registering the file associations (FR-32), and uninstalling cleanly.
- The installer's version is read from the built executable's **FileVersion** resource, so the package and the binary cannot disagree.
- *(Eight releases have shipped this way; the installer is the product's actual distribution channel and had no requirement behind it.)*

#### FR-32: File associations and external open requests *(RD-24, RD-26)*
**Consequences (testable):**
- `≡` › **Register File Associations…** writes per-user ProgIDs under `HKCU\Software\Classes` — `SentinelIDE.Project` for `.sntproject` and `SentinelIDE.Source` for `.sentinel` — with distinct icons, requiring no administrator rights and taking effect immediately. The installer mirrors the same registration.
- Double-clicking an associated file opens it in the IDE **within its nearest enclosing project**, not as an orphan file.
- The IDE is **single-instance**: a second launch hands its path to the running instance and exits, rather than opening a competing window.
- Files **dropped onto the window** open the same way.
- Every one of these paths — association, second launch, drop — converges on **one** open-request entry point, which routes through the unsaved-changes guard (FR-33) and defers while the UI is busy (a build, a modal) rather than interrupting it.

#### FR-33: Unsaved-changes guard *(RD-25 — NFR-REL-1's realization)*
**Consequences (testable):**
- Any action that would discard unsaved edits first asks, with **three** answers — **Save · Don't Save · Cancel**. A two-answer prompt is insufficient: it forces a user who simply mis-clicked to choose between two losses.
- **Cancel aborts the triggering action entirely and changes nothing.** Escape and the close box both mean Cancel.
- Every discarding path routes through **one choke point**, and the list is the requirement: opening another file (tree, Problems row, Output link), switching target, editing Project Settings, closing the project, sealing the project, opening a sealed project, New Project, New File, `≡` › Exit, and window close.
- The **one** deliberate exception is an unattended auto-update install (FR-30), which must not prompt.
- This FR is what makes **NFR-REL-1** true; NFR-REL-1 previously had no requirement realizing it.

#### FR-34: Sealed projects *(RD-23)*
**Consequences (testable):**
- `≡` › **Seal Project…** produces a single encrypted `.sealed` file from a project folder: archive → LZMS compress → **AES-256-GCM** under a randomly generated data key, with that key wrapped per unlock **slot** (LUKS-style) — v1 shipping one password slot, PBKDF2-HMAC-SHA256 at 600,000 iterations. `≡` › **Open Sealed Project…** reverses it.
- Password entry uses a themed **double-entry** dialog on seal and single-entry on open.
- The on-disk format is **v2** (`SNTSEAL2`) and reads v1. Two properties are requirements, not implementation detail: **slots carry a length so an unknown slot type can be skipped** rather than aborting the read (without which the extensibility the slot design exists for does not work), and **the fixed header prefix is bound as AEAD additional authenticated data** (without which the unauthenticated `archive_size` field steers a decompression allocation in the victim's process from a file the attacker cannot decrypt).
- Crypto is native CNG plus the Windows Compression API, with algorithm ids recorded on disk so a future Sentinel ChaCha20-Poly1305 slot or payload can coexist with existing AES files. **The AEAD + KDF core is a named Sentinel rewrite target** — it belongs in the §5 surface enumeration and in the FR-18 migration history, and it is currently **native**.
- Sealing routes through the unsaved-changes guard (FR-33).
- *(This is the repo's first test-covered subsystem.)*

## 5. Security Model & Hardened-Surface Contract

*The centerpiece. Full depth per the stakes (security buyers).*

**Threat model — the three exposures an IDE uniquely carries:**
1. **Executes untrusted code** — every build/run spawns the compiler and runs produced binaries; every analyzer pass reads untrusted source.
2. **Holds secrets** — signing keys, registry/publish tokens, credentials. *(Restated 2026-09-02, RD-01: the original text said "v1 makes this concrete: … the IDE now actively holds a signing key." **It does not.** Under ADR-0061 the IDE never holds key material — `snc` signs in a child process and `sentinel.key` sits on disk in the project. The real v1 secret surface is the **sealed-project password and derived key material** in `PasswordDialog` and the AES/PBKDF2 core, FR-34 — which is native. See SEC-5.)*
3. **Supply-chain apex** — everything authored through the IDE inherits its integrity; compromise here propagates downstream.

**What v1 actually claims — and what it does not (be precise; this is the line a CISO will probe):**
Each untrusted-input path has two parts: **ingestion/transport** (receiving raw bytes from the OS — file read, clipboard, drag-drop, IME) and **interpretation** (deriving structure/meaning — parsing). v1 hardens the **interpretation**: the parsers for source, project/config, and build output are Sentinel (FR-13) and held to Sentinel's own checks. The thin **ingestion/transport** layer and the **C-ABI marshalling** that hands bytes to the Sentinel parsers remain native in v1 — so they are a **named, minimized, audited boundary**, not a hardened one. The honest claim is therefore *"the interpretation of untrusted bytes is Sentinel,"* not *"100% of the IDE is Sentinel."*

> `[NOTE FOR PM]` Buyer messaging must hold this line. *(Rewritten 2026-09-02.)* **Pillar 2 (secrets):** the honest v1 statement is now *"the IDE does not hold your signing key"* — key generation and signing are `snc` child processes and the key file is the developer's, on disk (FR-19). The secret surface that **is** real is **sealed-project password handling and the AES-GCM/PBKDF2 core** (FR-34), which is **native and not yet hardened** (SEC-5). Say that; do not inherit the deleted Authenticode framing. **Pillar 3 (supply chain):** the v1 signing feature addresses it in the **consumer's** direction — a build that refuses untrusted inputs (FR-23) — not by decorating a publisher's artifact. Scope every claim about `strict` to **executable targets** (L-2) and never describe a `grants` ceiling as enforcing capabilities (L-1). The IDE's own provenance — SLSA/SBOM/reproducible builds — remains **roadmap** (SEC-4); the IDE's own **update signing** ships (FR-30) and is a different thing again. Overselling "100% Sentinel / fully hardened," or letting the developer's signing and the IDE's own release provenance collapse into one story, is the fastest way to lose a skeptical CISO.

**The contract (C-rule), as testable requirements:**
- Every security-relevant Surface's interpretation logic is Sentinel (FR-13 for v1's Untrusted-input surface). **Met in v0.1.7 and slightly exceeded** — all four parsers, including the trust-manifest parser at a genuine security boundary, are Sentinel (RD-27).
- Native code is confined to Chrome **plus** the explicitly-enumerated ingestion/transport + C-ABI marshalling boundary **and** the **sealed-project cryptographic core** (FR-34) — a named Sentinel rewrite target, still native (FR-14, SEC-3, SEC-5). *(The "v1 signing-key handling surface" this bullet used to name **does not exist**: the IDE holds no key material.)*
- The manifest **writer** (`saveProject`, FR-24/FR-28) and the trust-manifest **writer** (FR-22) are native. They **generate** output rather than interpret untrusted input, so they sit outside SEC-1's scope — but a writer that emits a malformed trust manifest breaks a consumer's build in both `warn` and `strict` (FR-22), so they belong in the SEC-3 enumeration.
- Hardened-surface coverage is tracked and rising (FR-15).

**Security NFRs (cross-cutting):**
- **SEC-1:** No interpretation of untrusted structure (parsing of source / config / build output) occurs in non-Sentinel code; the parser entry points are enumerated and all are Sentinel. *(Validates FR-13; verifiable by reviewing the enumerated parser set.)*
- **SEC-2:** The v1 Untrusted-input surface compiles clean under `snc`'s safety checks (`secret`/borrow/effect) — it is itself held to Sentinel's guarantees. *(Validates FR-13, FR-11.)*
- **SEC-3:** The residual native ingestion/transport + C-ABI marshalling boundary is **enumerated and security-reviewed** each release; it does no structural interpretation of untrusted input and is minimized over time. *(Validates FR-14; this is the honest counterpart to SEC-1.)*
- **SEC-4 `[ASSUMPTION]` (roadmap):** *The IDE's own* released binaries are signed with build provenance (Sigstore/Cosign + SLSA), ship an **SBOM**, and pursue **reproducible builds**; an extension model — when it exists — is sandboxed by design (decide before the API exists). *(This is the IDE's own supply-chain provenance — distinct from the developer signing feature, FR-19..FR-23, and distinct again from the IDE's own **update** signing, which ships: FR-30 verifies an **Ed25519-signed appcast** before offering any update, which is a real, shipped piece of the IDE's own release integrity even though SLSA/SBOM/reproducible builds remain roadmap. Research §4.)*
- **SEC-5 (v1 honesty boundary — secrets pillar) `[RESTATED 2026-09-02 — RD-01, RD-23]`:**
  *The prior SEC-5 said the developer's signing key "is held by the **native host** — a named secret surface that is not yet a Hardened surface," with in-product disclosure in the import-key dialog. **That statement is wrong, and wrong in the IDE's favour**, which is the dangerous direction: there is no import-key dialog, no passphrase, and the IDE never holds key material at all. Key generation and signing are `snc` child processes; `sentinel.key` is a file the developer owns, in their project directory. Deleting the claim silently would leave a security boundary undocumented, so it is replaced rather than removed.*
  **The actual residual secret surface in v0.1.7 is sealed projects (FR-34):** a user-entered password passes through the native `PasswordDialog`, and the native CNG-based **PBKDF2-HMAC-SHA256 → AES-256-GCM key-wrap and payload AEAD** derive, hold and use key material in the native host, outside Sentinel's `secret` / constant-time guarantees. That surface is **named, enumerated for security review each release, and a declared Sentinel rewrite target** — `std/security` already carries a machine-verified constant-time ChaCha20-Poly1305 and SHA-256, and the on-disk format records algorithm ids precisely so a Sentinel slot and payload can be introduced without breaking existing files.
  **A second, smaller residual:** `sentinel.key` sits **unencrypted on disk** in the project directory by convention. That is `snc`'s model, not the IDE's choice, but the IDE should not imply otherwise.
  **What genuinely improved:** the **trust-manifest parser is Sentinel** (`parse_trust`) — the code deciding which keys a build trusts is the first security-boundary port (FR-13, RD-27).
  The honest claim: **the IDE does not hold your signing key; it does hold your sealing password, in a surface that is not yet hardened.** *(Validates FR-19..23, FR-34; the secrets-pillar counterpart to SEC-3.)*

The differentiator this protects: *for the surfaces it covers, SentinelIDE provably can't be turned against you* — structurally, not by assertion.

## 6. Cross-Cutting NFRs

*Targets are research-grounded `[ASSUMPTION]`s (research §3) for Bryan to confirm/replace.*

- **NFR-PERF-1 `[ASSUMPTION]`:** Keystroke-to-screen latency < 10 ms (target ~2 ms); verify with Typometer.
- **NFR-PERF-2 `[ASSUMPTION]`:** Cold start to interactive < 1 s.
- **NFR-PERF-3:** Builds run off the UI thread with streamed output; the editor stays fully interactive (type/scroll/navigate) during a build. *(Hard requirement, not assumption.)*
- **NFR-PERF-4 `[ASSUMPTION]`:** Frame budget 8.33 ms (120 FPS) / ≥ 60 FPS sustained; consistent frames prioritized over peak.
- **NFR-PERF-5 `[ASSUMPTION]`:** Large-file handling via rope/B-tree buffer (O(log n) edits); heavy highlighting disabled beyond ~20,000 chars/line.
- **NFR-PERF-6 `[ASSUMPTION]`:** Typical-session memory well under the Electron baseline (target < 300 MB).
- **NFR-REL-1:** Unsaved edits are never lost across build, run, focus change, **or any action that discards the buffer** — realized by **FR-33**, and by the build path auto-saving a dirty buffer before invoking `snc` (FR-4). *(Widened 2026-09-02, RD-25: the shipped guard covers far more than build/run/focus — window close, Exit, project close, target switch, seal, New Project, Project Settings — and the original NFR was the only place in the PRD that gestured at it.)*
- **Security NFRs:** SEC-1..SEC-5 (§5).

## 7. Platform

- Native desktop application; **Windows-first (x64)**.
- Architecture: a thin Native host (Chrome) embedding Sentinel-written Surfaces via the C-ABI boundary (`snc build --lib/--shared --emit-header`; addendum).
- **Not Electron** — the native-perf and security story depends on bypassing a browser engine (research §2).
- **macOS is the next platform target**, gated on a shipping Win32 product; Linux later. (Non-Goal for v1, §11.)
- **Versioning & delivery `[CORRECTED 2026-09-02 — RD-15]`:** the original text — "marketing version starts at **0.1.0 (build 1)** and increments" — describes a scheme the product never used. Actual scheme: the **marketing version is a literal** in the build script (currently `0.1.7`), and the **build number is derived from git** as `git rev-list --count HEAD` plus a fixed base offset, written into a generated `Version.h`. It therefore does **not** increment: the same commit always stamps the same number, a dirty working tree stamps a dirty marker, and a raw `cmake --build` that bypasses the build script falls back to a stale hard-coded version. **Shipped: `0.1.7 (build 164)`.** Delivery is **agile/iterative** — scope and the Sentinel/native mix (FR-15, FR-17) evolve build over build, not against a fixed up-front ceiling.
- **Distribution:** a per-user Inno Setup installer (FR-31) plus in-product updates from a signed appcast (FR-30). Eight releases have shipped through that channel.

## 8. Integration & Dependencies

- **snc** — `build` (compile+link), `lex`/`ast`/`parse` Stage dumps, and **`keygen` / `sign` / `verify` + `--require-signatures … --trust …`** (ADR-0061, FR-19..23). The IDE shells out to `snc.exe` and **probes its capabilities** rather than assuming them: `verify` and `keygen`/`sign` are independently satisfiable, and the IDE selects the most capable `snc` on disk (§3, "snc").
- **`keygen_core.exe` / `sign_core.exe`** — separate Sentinel programs `snc` shells out to for `keygen`/`sign`. They must sit beside the `snc` binary; their absence is a runtime failure that `snc --help` does not predict.
- **WinSparkle + a signed appcast** — the update channel (FR-30). **Inno Setup** — the installer (FR-31).
- **Sentinel runtime + linker** — required for build→executable; the v1 Toolchain-readiness gap lives here (§14).
- **C-ABI embedding boundary** — `snc build --lib/--shared --emit-header` (ADR 0059) compiles Sentinel Surfaces to a C-ABI library + header the Native host links. This is how the C-rule is realized.
- **Language server seam** — `sentinel-lsp` crate (salsa-tracked, currently a scaffold) and/or Stage dumps. Which to drive editor intelligence is an architecture decision (§16, OQ-3).

## 9. Aesthetic & Tone

- Native desktop look modeled on **SQLTerminal-Win32** (a visual/UX reference, **not** a codebase to fork): RichEdit-style editor, tree sidebar, virtual grid/list, themed dialogs, status bar.
- **Dark/coral "Claude-desktop" palette** following the OS light/dark setting — e.g., window `#161618`, coral accent `#D97757`, with distinct syntax colors (per `Theme.h`).
- The **"Signed"** signing state uses a **distinct trust color** (green family; **`trust-verified`** in the UX DESIGN palette); coral stays reserved for the Sentinel-safety signal and is **not** reused for signing (FR-21). *(Unchanged and deliberately so: this is one of the few claims in the document that survived contact with the code intact — the DESIGN palette matches the shipped theme exactly. Per **decision D3**, the token name **`trust-verified` stays** and is **not** renamed to `trust-signed` anywhere, in docs or code; it is cited by name from the UX EXPERIENCE spine and implemented under that name in the theme, so a rename breaks both.)*
- Tone for product-generated text (diagnostics, guidance, remediation in FR-12): precise, calm, non-alarmist — assurance, not noise.

## 10. Co-Evolution / Language Feedback

The IDE is a forcing function for Sentinel. Building and running it surfaces concrete capability gaps, which become a prioritized **Language-gap list** (FR-16) fed to the language team, while **Hardened-surface coverage** (FR-15, the falsifiable primary) and the **Sentinel/native mix** (the About-dialog color, FR-17) track the A-destination — no fixed ceiling, coverage leads. The migration is captured as a reusable playbook (FR-18). Gap #1 already exists: turnkey Windows MSVC build→link (§14). This loop is a product output, not a side effect.

## 11. Non-Goals (Explicit)

- Not a polyglot or general-purpose IDE — Sentinel only.
- Not cross-platform in v1 — Windows-first (**macOS is the next target after a shipping Win32 build**; Linux later).
- Not a debugger in v1 — `[NOTE FOR PM]` debugging is the #1 post-v1 fast-follow and is emotionally load-bearing ("rich & helpful"); revisit if timeline permits.
- Not GUI-in-Sentinel in v1 — the Native host stays native; the footprint shrinks from v2.
- Not an extension marketplace / plugin API in v1 — though the sandbox posture is decided early (SEC-3).
- Not multi-project workspaces, and no REPL, in v1.

## 12. MVP Scope

### 12.1 In Scope
*(Annotated 2026-09-02. **✓** = in v0.1.7. **✗** = still unbuilt, kept as spec, see §18.1. **+** = shipped and added to scope after the fact, §18.2.)*
- **✓** Native-hosted window in the §9 style.
- **✓/✗** Edit `.sentinel` with syntax highlighting (FR-1, FR-2, FR-3) — single-buffer editing ships; **✗** tabs, find/replace, go-to-line, multi-cursor, `Ctrl+P` (RD-17, RD-18).
- **✓** Build & run via local `snc.exe` with captured, streamed output (FR-4..FR-7).
- **✓** v1 Hardened surface = untrusted-input parsing, in Sentinel (FR-13) — four parsers, including the trust-manifest parser (RD-27).
- **✓/✗** Sentinel diagnostics + Problems list, one model (FR-8..FR-11) — the list and click-to-navigate ship; **✗ no severity, no squiggles, no security class** (RD-13, RD-14), which is the flagship UJ-2 rendering.
- **✗** Toolchain-readiness detection & guidance (FR-12) — replaced in practice by silent MSVC auto-detect (RD-19).
- **✓/✗** Sentinel/native LOC mix measured (FR-15, FR-17); **✗** Hardened-surface coverage is not rendered (RD-16). Language-gap list maintained (FR-16).
- **✓** ADR-0061 signing & trust: generate a key, sign the open file, sign the build artifact, four-state chip, trust manifest, build-time requirement gate (FR-19..FR-23) — with the FR-23 scope limits (L-1, L-2) stated wherever `strict` is described. *(Replaces the deleted Authenticode line: "import key file, sign built artifacts … the signing-**key** surface is not yet hardened.")*
- **+** Manifest-declared projects, targets, tiers, scheme selector, Project Settings (FR-24..FR-28).
- **+** Logging, in-product updates, installer, file associations & external open, unsaved-changes guard, sealed projects (FR-29..FR-34).

### 12.2 Out of Scope for MVP
- Completion, go-to-definition, refactoring — *next* (LSP-driven once the seam exists).
- Debugging — *fast-follow* `[NOTE FOR PM]`.
- macOS / Linux — Windows-first.
- GUI-in-Sentinel; multi-project workspaces; plugins; REPL.
- **Hardening of the sealed-project cryptographic core** (PBKDF2 / AES-GCM / password handling, FR-34) — a named Sentinel rewrite target, still native (SEC-5); other secrets (publish/registry tokens, credentials) follow. *(Replaces "hardening of the signing-key handling surface", which was scoped against a design that does not exist — the IDE holds no key material.)*
- **Certificates, in any form.** No certificate store, no HSM / smart card / hardware token, no CA, no chain, no expiry, no revocation, and **no RFC-3161 timestamping** — ADR-0061 is certificate-free by construction, not by deferral. These are **not backlog**; they are outside the model. *(Replaces the former "signing-key sources beyond key files … backlog" line. See OQ-9, closed DEAD.)*
- **Enforced capability grants** and a **trust gate on library/shared targets** — both blocked upstream in Sentinel-lang and not fixable in this repo (L-1, L-2, §18.4).

## 13. Why Now

Sentinel has reached self-hosting and a Windows `snc.exe` exists, so a real IDE built in Sentinel is newly feasible — and the language needs a credible, security-shaped proof to move its regulated-industry market from interest to adoption. The proof and the tool are the same artifact.

## 14. Risk & Mitigations

- **Windows build→link not turnkey (confirmed this session).** `snc` links against a GNU/clang runtime archive (`libsentinel_runtime.a`) while the MSVC build produces `sentinel_runtime.lib`. **Mitigation:** FR-12 detects & guides; gap #1 feeds the language team (§10). Severity: medium — gates v1 build/run UX.
- **Language-capability dependency (GUI/FFI in progress).** **Mitigation:** B-path decouples shipping from language readiness; migrate inward as capabilities land.
- **B→A migration stalls** (native footprint never shrinks). **Mitigation:** FR-15 makes % native visible per release; SM-C1 counter-metric guards it.
- **Perf regression** undercuts the native story. **Mitigation:** NFR-PERF gates; Typometer in CI `[ASSUMPTION]`.
- **`[REPLACED 2026-09-02]` ~~Signing key held in a not-yet-hardened native surface~~.** The risk as written does not exist: the IDE never loads key material (RD-01). It is replaced by three real ones:
- **The IDE can report a build artifact as signed when it is not (live defect, RD-06).** The post-build path prints a signed marker off `snc sign`'s exit code without re-verifying the artifact — the exact failure the original FR-20 forbade and the adversarial review predicted. **Mitigation:** none in v0.1.7. Tracked as **DEF-1** (§18.3) with its closing condition. Severity: **high** — it is a false assurance in a product whose thesis is honest assurance.
- **`strict` enforces nothing on library and shared targets (L-2, §18.4).** A consumer can set `require = strict`, see it in Project Settings and in the build line, and get no gate at all. **Mitigation:** upstream fix required; until then every artifact and every piece of in-product copy describing `strict` must scope itself to executable targets. Severity: **high**, unfixable here — which makes the disclosure obligation the mitigation.
- **Sealed-project key material is handled natively (SEC-5, FR-34).** Password → PBKDF2 → AES-GCM runs in the native host, outside Sentinel's `secret`/constant-time guarantees. **Mitigation:** the surface is named and enumerated for review, the on-disk format records algorithm ids so a Sentinel core can be introduced without breaking existing files, and `std/security` already has the verified primitives. Severity: accepted, honestly stated.

## 15. Success Metrics

**Primary**
- **SM-1 (Tool):** A Sentinel developer prefers SentinelIDE to a bare editor for the edit→build→run loop. Target `[ASSUMPTION]`: the core Sentinel devs adopt it as daily driver within 4 weeks of v1. Validates FR-1..FR-11.
- **SM-2 (Proof):** The v1 Untrusted-input surface's interpretation logic is 100% Sentinel and passes Sentinel's safety checks, and the residual native ingestion/transport + C-ABI marshalling boundary is enumerated and reviewed. Target: 100% (binary) + boundary documented. Validates FR-13, SEC-1, SEC-2, SEC-3.
- **SM-3 (Forcing function):** A prioritized Language-gap list is produced and delivered. Target: maintained from v1, seeded with gap #1. Validates FR-16.

**Secondary**
- **SM-4 `[ASSUMPTION]`:** Keystroke latency and cold-start meet NFR-PERF-1/2 on reference hardware. `[NOTE FOR PM]` ungradeable until those targets are committed (OQ-8). Validates NFR-PERF.
- **SM-5:** Hardened-surface coverage is reported per release and increases over time (the falsifiable A-destination metric); the Sentinel/native mix is surfaced in the About dialog. Validates FR-15, FR-17.
- **SM-6 (Signing works) `[REWRITTEN 2026-09-02 — RD-01, RD-03]`:** *(was: "import a key file and produce a signed executable … with the status indicator reflecting it" — untestable, since nothing in the product imports a key file.)* Two binary passes on a reference project: **(a)** generate a key with `snc keygen`, sign an open `.sentinel` file with a grant, and see the chip settle on **✓ Signed** after a real `snc verify`; **(b)** import that signature's key into a second project's `sentinel-trust.toml`, set `require = strict`, and confirm the second project's **executable** build **fails closed** on an unsigned source and succeeds on the signed one. **(b) is the load-bearing half** — (a) alone only proves bytes were signed, not that anything refuses them. Validates FR-19..FR-23.
- **SM-7 (Signing is not over-reported) `[NEW 2026-09-02]`:** No shipped path reports a signature it has not verified. Target: binary — **currently FAILING** (DEF-1, §18.3). Graded by re-running the post-build sign path with a `snc sign` that exits 0 without writing a `.sig`. Validates FR-20.

**Counter-metrics (do not optimize)**
- **SM-C1:** Do **not** grow % native to ship features faster — it directly contradicts SM-2/A-destination. Counterbalances SM-1.
- **SM-C2:** Do **not** trade keystroke latency for feature count — a laggy "rich" editor fails the native-perf premise. Counterbalances SM-1.
- **SM-C3 `[REWRITTEN 2026-09-02]`:** Do **not** let the signing UX imply more than ADR-0061 delivers. *(The old wording policed a claim about key hardening that no longer has a referent, and — as the adversarial review noted — was a vibe rather than a metric: "must never read as" has no instrument. It is replaced with a four-item copy audit that can actually be run.)* **Checkable each release, as a pass/fail checklist over all shipped copy, status text and artifacts:**
  1. Nothing describes the signing model in certificate vocabulary — no *certificate*, *chain*, *authority*, *publisher*, *expiry*, *revocation*, *timestamp*.
  2. Nothing describes a `grants` ceiling as restricting or enforcing what signed code may do (**L-1**).
  3. Every description of `strict` as a guarantee is scoped to **executable targets** (**L-2**).
  4. The chip's "Signed" is never rendered or described as "Verified", "Secure", or "trusted" — it asserts a valid signature by *some* key, and trust is the consumer's separate act (FR-22).

  Counterbalances FR-19..FR-23 / SM-2 (protects the precise proof claim).

## 16. Open Questions

- **OQ-1 — resolved:** Windows-first; **macOS is the next platform target, gated on a shipping Win32 product**; Linux later.
- **OQ-2:** Debugger approach for the fast-follow — `snc`/LLVM debug info maturity on Windows?
- **OQ-3:** Language-server seam — drive intelligence via `sentinel-lsp`, `snc` Stage dumps, or both? (Architecture.)
- **OQ-4:** Extension model & sandbox posture (SEC-3) — decide before any plugin API.
- **OQ-5 — restated 2026-09-02 (RD-01, RD-23):** The premise was wrong: the IDE's first secret is **not** the signing key, because the IDE never holds one. The first secret it actually handles is the **sealed-project password and the key material derived from it** (FR-34), in a native surface (SEC-5). Still open: the timeline for porting the PBKDF2/AEAD core to Sentinel, and *which* further secrets (publish/registry tokens, credentials) follow.
- **OQ-6 — resolved:** **No fixed % native ceiling** — track Hardened-surface coverage (primary) and the Sentinel/native mix as trends, surfaced in the About dialog (FR-15, FR-17).
- **OQ-7 — partly answered 2026-09-02 (RD-19):** the link half is settled — the failure was a missing MSVC environment, and injecting `vcvars64.bat` into the build child closed it, which is why the product builds and runs today. What remains open is the original question's other half: does `snc` build *arbitrary* `.sentinel` projects on Windows, or only a subset? FR-12's readiness surface is still unbuilt (§18.1), so the IDE currently has no way to answer this for a user.
- **OQ-10 `[NEW 2026-09-02]` — the two upstream ceilings.** L-1 (capability extraction only detects `ffi`) and L-2 (`--lib`/`--shared` skip the trust gate) are both in Sentinel-lang and both silently weaken a feature the IDE presents as working. What is the upstream timeline, and until it lands, should the IDE **refuse to display** `require = strict` on a library/shared target rather than displaying a setting it knows is inert? *(Displaying an inert guarantee is the same class of error as DEF-1.)*
- **OQ-11 `[NEW 2026-09-02]` — release tiers have no compiler meaning.** `snc` has no tier flag, so all four tiers compile identically and differ only in output directory (L-3). Is the tier axis worth carrying in the UI before it does anything, and what does "Hardened" have to mean upstream for it to be honest?
- **OQ-8:** Commit real NFR-PERF targets (keystroke / cold-start / FPS / memory) on named reference hardware — until then SM-4 and SM-C2 cannot be graded.
- **OQ-9 — signing longevity & validity — CLOSED **DEAD** 2026-09-02.** *(Original question: does v1 Authenticode signing apply an **RFC-3161 timestamp** so signatures survive certificate expiry, and how are cert expiry/revocation surfaced?)*
  **The question is dissolved, not answered — and it is recorded here rather than deleted so nobody re-asks it.** RFC-3161 timestamping exists for exactly one purpose: to prove a signature was made while the signing **certificate** was still valid, so the signature outlives the certificate's expiry. **ADR-0061 has no certificates.** There is no CA, no chain, no validity window, no expiry and no revocation anywhere in the model — a key is a bare 64-hex Ed25519 public key and nothing about it lapses. With nothing that expires, there is nothing for a timestamp to protect and no expiry or revocation state to surface. A timestamp here would attest only *when* a file was signed, which is a provenance feature nobody asked for and not what the question was about.
  This also closes the adversarial finding it came from — **`review-adversarial-signing.md` C1 (HIGH)** — as **DEAD**. C1 was correct against the design it reviewed: Authenticode without a timestamp really does produce signatures with a fuse on them. The design changed out from under it. **C1 was dissolved, not fixed**, and nothing was done to address it; if the product ever reintroduces certificates, C1 comes back with it. See §18.3.
  *(What replaced this concern: a signature is invalidated by **editing the file**, not by time. The chip re-verifies on every open and every edit — FR-21.)*

## 17. Assumptions Index

- §6 / NFR-PERF — metric targets (keystroke < 10 ms, cold start < 1 s, 120 FPS) are research-grounded inferences; **< 300 MB memory is PRD-invented (no research number)**. All are placeholders to confirm, not Bryan-supplied (OQ-8).
- §4.1 FR-2 — incremental (Tree-sitter-style) highlighting assumed as the mechanism.
- §5 SEC-4 — the IDE's **own** release provenance (Sigstore/SLSA), SBOM, reproducible builds, and plugin sandbox flagged as roadmap, not v1 — distinct from the **developer** signing feature (FR-19..FR-23) *and* from the IDE's own **update** signing, which ships (FR-30).
- ~~§4.8 FR-19 — v1 signing-key source is key-file import (`.pfx`/`.p12`/PEM); Windows certificate store and hardware token / HSM / smart card are backlog.~~ **`[VOID 2026-09-02 — RD-01]`** The assumption is not merely unconfirmed, it is **false**: there is no key-file import and no certificate concept, so nothing named here is a key *source* under ADR-0061. Keys come from `snc keygen`. Certificate store / HSM / smart card are **outside the model, not backlog** (§12.2). *Struck rather than deleted so the void assumption is not silently re-inherited by downstream artifacts that still cite it.*
- ~~§5 SEC-5 / §4.8 — the "v1 UX, harden next" split (artifact signed in v1; signing-**key** surface hardened next) and the in-product honesty disclosures derive from the UX session (EXPERIENCE Flow 5; DESIGN `import-key-dialog`).~~ **`[VOID 2026-09-02 — RD-01]`** The split assumed a key-handling surface the IDE does not have. SEC-5 is restated against the real one (sealed projects, FR-34). **The `trust-verified` colour token is *not* void** — it matches the shipped theme exactly, and per **D3** the name stays (§9).
- §4.9 / §4.10 (FR-24..FR-34) — **these requirements were written after the code**, from the shipped behaviour, and are therefore descriptions of what exists rather than independently-derived intent. Treat each as needing product sign-off on whether the shipped behaviour is the *wanted* behaviour, particularly FR-26 (tiers that do nothing) and FR-23's scope limits.
- §18.4 L-1 / L-2 / L-3 — the three upstream ceilings are stated as facts about `snc` v1 as of 2026-09-02. They are **not** assumptions, but they **are** time-bound: re-check them against the toolchain before quoting them in anything customer-facing.
- §14 — Typometer-in-CI assumed as the perf-gate mechanism.
- §15 SM-1/SM-4 — adoption window (4 weeks) and perf-on-reference-hardware are placeholder targets to confirm; SM-4/SM-C2 ungradeable until committed (OQ-8).

## 18. Reconciliation Record (2026-09-02)

Added when this PRD was re-opened under **D2** and reconciled against **v0.1.7 (build 164)**. Every
row cites a `RD-nn` from [`../../REALITY-DELTA.md`](../../REALITY-DELTA.md), where the underlying
code citation lives. This section is the index; the markers in the body are the record.

**The canonical, cross-artifact gap register is [`../../GAP-REGISTER.md`](../../GAP-REGISTER.md)**
(required by **D1**). §18.1 and §18.2 below are this PRD's **slice** of it, kept here so a reader of
the PRD alone cannot miss what is unbuilt. Where the two ever disagree, the canonical register and
REALITY-DELTA win.

### 18.1 Gap register — specified here, NOT BUILT in v0.1.7

These requirements are **kept deliberately** (decision **D1**). They describe behaviour that is
still wanted and that does not exist in code. **Nothing in this table ships.**

| Where | Not-yet-built | RD |
|---|---|---|
| **UJ-2**, FR-8, FR-9, FR-11, §12.1 | **Diagnostic severity** — `Diag` has no severity field, the Problems list has no severity column, and no diagnostic is distinguishable as a security finding | RD-13 |
| **UJ-2**, FR-8, §12.1 | **Inline squiggles and the gutter shield glyph** — the flagship UJ-2 rendering. Whole-line background tinting is what ships instead | RD-14 |
| FR-1, §12.1 | **Multi-tab editing** — the IDE holds one buffer | RD-17 |
| FR-1, §12.1 | **Find / replace (incl. regex), go-to-line, multi-cursor** | RD-18 |
| FR-3, §12.1 | **`Ctrl+P` fuzzy open-file finder** | RD-18 |
| *(UX spine only)* | **`F8` / `Shift+F8` problem navigation** — no PRD FR ever covered it | RD-18 |
| **UJ-3**, FR-12, §12.1, §14 | **Toolchain-readiness surface** — no check, no dialog, no remediation text. Silent MSVC auto-detect is the partial substitute | RD-19 |
| FR-20 | **Verify-before-reporting-signed.** Specified since 2026-06-27, never implemented, and the shipped path does the thing the requirement forbids → **DEF-1, §18.3** | RD-06 |
| FR-21 | **An artifact-bound, never-stale chip.** The chip is bound to the open source file and a build never touches it. The *requirement* is kept; the six-state model behind it is not, because two of its states have no referent | RD-04, RD-05 |
| FR-4 | **A user-configurable build command.** The command is composed from the manifest and only echoed | RD-12 |
| FR-15, FR-17 | **Hardened-surface coverage rendered in About** — the LOC mix ships; the primary metric does not | RD-16 |

### 18.2 Shipped with no requirement — requirement homes added

| Subsystem | New FR | Prior artifact coverage | RD |
|---|---|---|---|
| Build-time signature gate | **FR-23** | none | RD-03 |
| Consumer trust manifest | **FR-22** | none | RD-02 |
| Manifest-declared project | **FR-24** | none (the PRD said "open a folder") | RD-07 |
| Targets (`[[target]]` × N) | **FR-25** | none — an absent axis | RD-08 |
| Release tiers × 4 | **FR-26** | none — an absent axis | RD-09 |
| Scheme selector | **FR-27** | none — an unmodelled **IA region** | RD-10 |
| Project Settings dialog | **FR-28** | none — an unmodelled **modal** | RD-11 |
| Logging | **FR-29** | **UX EXPERIENCE only** (level, path, reveal) — never a PRD FR | RD-20 |
| Auto-update | **FR-30** | none | RD-21 |
| Windows installer | **FR-31** | none | RD-22 |
| File associations, single-instance, drag-drop | **FR-32** | none | RD-24, RD-26 |
| Unsaved-changes guard | **FR-33** | NFR-REL-1 gestured at a slice of it; the modal, the choke point and its coverage were unspecified | RD-25 |
| Sealed projects | **FR-34** | none | RD-23 |

**Also recorded, not a gap:** FR-13's C-rule claim is **substantially met and slightly exceeded** —
four parsers compiled from Sentinel, including the trust-manifest parser at a real security
boundary (RD-27). This is the one place the product is ahead of the spec.

### 18.3 Adversarial-review dispositions

From `review-adversarial-signing.md`, reviewed against the shipped code.

**C1 (HIGH) — "Timestamping is entirely absent" → CLOSED **DEAD**.**
C1 was **correct against the design it reviewed**: Authenticode without an RFC-3161 timestamp
produces signatures that expire with the certificate, and a PRD selling supply-chain honesty could
not stay silent about it. That design was never built. **ADR-0061 has no certificates**, so nothing
expires, so there is nothing for a timestamp to protect and no expiry/revocation state to surface.
The finding is **dissolved, not fixed** — no work was done and none is owed. It is closed here in
writing, and in **OQ-9**, rather than deleted, so that (a) nobody re-raises it, and (b) if
certificates are ever reintroduced, C1 returns with them. *Its sibling C2 (cert expiry/revocation
surfacing) dies for the same reason and by the same argument.*

**B1 (HIGH) — "'the IDE never reports an artifact as signed when it is not' has no verification
path" → RE-FILED as **DEF-1**, against shipped code.**
B1 was filed in June against a *specification gap*: FR-20 asserted an absolute integrity promise
without saying how the IDE would confirm a signature took, and the review named the exact hazard —
*"lies whenever the signer exits 0 but didn't sign."* **The product then built precisely that.**

> **DEF-1 — the post-build path reports a signed artifact off an exit code, without verifying it.**
> **Status:** open, present in v0.1.7. **Severity:** high. **Owner:** unassigned.
> **Where:** the build-completion handler in `src/host/win32/MainWindow.cpp` (the `WM_APP_DONE`
> case, at the `outAppend` on **line 1683**).
> **What it does:** on a successful build, when the project has `signing.sign = true`, a
> `sentinel.key` exists and the active `snc` advertises the sign capability, the IDE runs
> `snc sign <artifact> --key <key>` through `runCapture` and then prints
> **`[signed · <name>.sig]`** in the **`trust-verified`** colour **based solely on that child
> process's exit code.** `verifyFile` — which exists, and which the status chip uses on the
> open-file path (FR-21) — **is never called here.** Nothing re-reads the artifact, nothing
> confirms a `.sig` was written, and nothing checks that any `.sig` present corresponds to the
> bytes just produced.
> **Concrete failure:** any `snc sign` that exits 0 without writing a valid signature — a
> `sign_core.exe` that is present but broken or version-mismatched, a write that fails silently to
> a read-only or full output directory, a stale `.sig` left from an earlier build while this
> invocation wrote nothing, an `snc` whose sign path is a no-op stub — produces a green
> **`[signed · …]`** line for an **unsigned artifact**, in the same trust colour the product uses
> to mean "verified". The user's only evidence is the line that just lied.
> **Why it matters more here than elsewhere:** this is a product whose entire thesis is that
> assurances are structural rather than asserted. A false *positive* assurance in the signing
> path is the specific failure mode this PRD's honesty discipline exists to prevent, and the
> requirement forbidding it (FR-20) has been in the document since 2026-06-27.
> **What would close it:** after a zero exit from `snc sign`, **re-verify the artifact before
> printing anything** — confirm `<artifact>.sig` now exists, parse it, and run `snc verify` on the
> artifact through the same `verifyFile` path the chip uses. Print the signed line **only on a
> verified result**; print a distinct failure line when the signer exits 0 but verification does
> not confirm, and name that case explicitly rather than folding it into "sign failed". If the
> active `snc` cannot verify (the capabilities are independent, §3), the line must say the
> signature was **produced but not verified** — not "signed". Graded by **SM-7**.
> *(Secondary, same handler: the build does not touch the chip at all — RD-04 — so nothing in the
> UI contradicts the false line either.)*

**DEF-2 (minor, RD-05) — the chip and the Signing & Trust panel disagree about "no file open."**
The internal state has five members; the status-bar chip's `switch` handles four and lets `Unknown`
fall through its `default:` branch, so **"no file open" paints as "⊘ Unsigned"** — asserting an
absence of signature about a file that does not exist. The panel labels the same state "— no file
open". **Closing condition:** give `Unknown` its own neutral chip rendering, or hide the chip
entirely when no `.sentinel` file is open.

**A1 (HIGH) — UJ-5 climax overclaim.** Addressed by the UJ-5 rewrite: the climax is now the
consumer's build failing closed, not a badge, and the three honest limits are stated inside the
journey rather than deferred to §5. Not re-opened separately.
**B2, B3, B4** — all three were about the Authenticode dialog, the six-state chip and "the current
artifact." Their subject matter no longer exists; superseded by FR-19, FR-21 and FR-20 as rewritten.

### 18.4 Upstream ceilings — limits no work in this repo can lift

All three are in Sentinel-lang. **Any artifact text implying otherwise is wrong, however carefully
hedged.** They are stated once here and cross-referenced from every requirement they touch.

| # | Limit | The obligation it creates |
|---|---|---|
| **L-1** | `snc` v1's capability extractor **only ever detects `ffi`** (from `extern` blocks). A key's `grants` ceiling is parsed and intersected for real, but `secret` / `constant_time` / `alloc` in a ceiling are never derived from the code. `forbids` is unimplemented. | **Never** say a grants ceiling "restricts" or "enforces" what signed code may do. It is **recorded intent**. Identity and byte-integrity *are* genuinely enforced — say that instead. (FR-19, FR-22, FR-23, UJ-5, SM-C3 item 2.) |
| **L-2** | `snc build --lib` / `--shared` **never invoke the trust gate**; it runs only on the executable path, and the library path silently discards the flags. The IDE emits them for every target type. | **A Library or Shared target displays "signing: strict" and enforces nothing.** Every description of `strict` as a guarantee must scope itself to **executable targets**. Tracked as a §14 risk and as OQ-10. (FR-23, FR-25, UJ-5, SM-C3 item 3.) |
| **L-3** | `snc` has **no tier flag**. Everything compiles at `-O0` regardless of tier. | Tiers today choose an **output directory** and nothing else. No artifact may describe them as affecting optimization or hardening. The IDE says so in the Output pane on every project build. (FR-26, OQ-11.) |

### 18.5 Downstream artifacts this reconciliation invalidates

Not in this PRD's scope to fix, listed so the inconsistency is not mistaken for a disagreement:

- ~~**UX EXPERIENCE**~~ and ~~**UX DESIGN**~~ — **both reconciled in this same pass**, so they no
  longer carry the Authenticode journey, the six-state indicator, the artifact-bound chip, the
  `import-key-dialog`, or an unmarked toolchain-readiness flow. Everything designed-but-unbuilt is
  retained under D1 with an in-place `[NOT BUILT — RD-nn]` marker and a row in `GAP-REGISTER.md`.
  DESIGN's colour values were checked against `Theme.h` and are correct; the `trust-verified` token
  name stays (D3). **Do not read this section as evidence the spines are still stale — they are not.**
- **Brief / addendum** — the exposed-surface map, which enumerates a signing-key surface the
  product does not have and omits the sealed-project cryptographic core, which it does.
- **Mockups** — `key-signing.html` (certificate identity, validity window), and
  `key-toolchain-readiness.html` (a dialog with no counterpart in code).
