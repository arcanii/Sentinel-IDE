---
title: "Product Requirements Document: SentinelIDE"
status: final
created: 2026-06-27
updated: 2026-06-28
---

# PRD: SentinelIDE

## 0. Document Purpose

This PRD is for the downstream UX, architecture, and epics/stories workflows, and for the Sentinel language team who will consume its feedback. It builds on — and does not duplicate — two finalized inputs in `../briefs/brief-SentinelIDE-2026-06-27/`: the **product brief** (vision, posture, scope) and its **addendum** (posture mechanics, milestone ladder, exposed-surface map), plus this run's **`research-ide-landscape.md`** (in this PRD folder; IDE table-stakes, native-editor architecture, NFR numbers). Vocabulary is Glossary-anchored (§3); features are grouped with globally numbered FRs; inferred values are tagged `[ASSUMPTION]` inline and indexed in §17.

The build posture is **locked** (brief): **B-path** (ship on a thin native host now) · **C-rule** (every security-relevant surface is Sentinel from day one; native only for security-irrelevant chrome) · **A-destination** (native footprint is a tracked, shrinking debt). This PRD turns that posture into requirements. Delivery is **agile** and versioned from **0.1.0 (build 1)**; this PRD scopes the v1 trajectory, not a frozen spec.

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
  Devon, a systems engineer new to Sentinel, opens a `.sentinel` folder, edits a file with syntax highlighting, hits Build, watches output stream into the results pane, and runs the resulting executable — all without leaving the window. **Climax:** exit code and stdout appear in the pane. **Realizes FR-1, FR-2, FR-4, FR-5, FR-7.**

- **UJ-2. Devon catches a `secret` leak before it ships.**
  Devon writes a branch on a `secret` value. The IDE shows a red squiggle at the exact span and a Problems-list entry "secret reaches a branch condition"; clicking either jumps to the line. He fixes it to a branch-free form; the diagnostic clears. **Climax:** the language's headline guarantee shows up *in the editor*, not just at the CLI. **Realizes FR-8, FR-9, FR-10, FR-11.**

- **UJ-3. Devon's machine isn't build-ready, and the IDE walks him through it.**
  On first build, the toolchain is incomplete (the Sentinel runtime archive `snc` links against isn't staged for this linker). Instead of a cryptic link error, the IDE detects the gap and shows a guided, copy-pasteable remediation. **Climax:** Devon goes from "broken" to "first successful build" without a web search. **Realizes FR-12.** *(Grounded in a real finding this session — see §14 Risk.)*

- **UJ-4. Priya harvests the gap list the IDE produced.**
  Priya, the Sentinel language lead, opens the IDE's accumulated **language-gap list** — concrete, prioritized capability requests surfaced by building and running the IDE itself (gap #1: turnkey Windows MSVC build→link). **Climax:** the IDE has paid for itself as a roadmap input. **Realizes FR-16.**

- **UJ-5. Devon signs his build.**
  Devon opens the app menu (`≡`) › **Signing › Import Signing Key…**, picks his key file (`.pfx`) and enters the passphrase; the dialog shows the loaded identity ("ACME Code Signing") and an honest note — the key is held by the native host this session, and key-handling is **not yet a hardened Sentinel surface** (next increment). He toggles **Sign on Build** and builds; the status-bar indicator goes "Signing…" then **"Signed"**, and the Output pane records the signed artifact and identity. **Climax:** the produced executable is signed and the **status bar shows "Signed" at a glance** — a *first concrete step* on the supply-chain-apex story (§5, pillar 3): signing is now something Devon *does*, even though full provenance (SLSA/SBOM) remains roadmap (SEC-4). The honest caveat keeps the claim precise: the artifact is signed; the **key surface hardens next**. **Realizes FR-19, FR-20, FR-21.** *(Grounded in UX EXPERIENCE Flow 5.)*

## 3. Glossary

- **Sentinel** — the security-first systems programming language the IDE targets and is built in. Files use the `.sentinel` extension.
- **snc** — the Sentinel compiler driver (`snc.exe` on Windows). Provides `build` (compile + link to executable), `build --lib/--shared --emit-header` (C-ABI library + header), and `lex`/`ast`/`parse` stage dumps.
- **Native host** — the thin layer providing window, message loop, and rendering, written in non-Sentinel code (e.g., Win32/C++). The only place non-Sentinel code is permitted (see C-rule).
- **Chrome** — security-irrelevant UI plumbing: window creation, message loop, widget painting, layout. May live in the native host.
- **Surface** — a unit of behavior that *interprets* untrusted bytes, touches secret material, or generates output/artifacts. Distinguish **ingestion/transport** (receiving raw bytes from the OS — clipboard, drag-drop, file read, IME) from **interpretation** (deriving structure/meaning from them); the C-rule hardens interpretation (see §5).
- **Hardened surface** — a Surface whose interpretation logic is implemented in Sentinel and subject to Sentinel's safety checks.
- **Hardened-surface coverage** — the share of identified security-relevant Surfaces implemented in Sentinel; the primary, falsifiable A-destination metric (per-Surface binary).
- **C-rule** — the contract that every security-relevant Surface is Sentinel from day one; native code is permitted only for Chrome.
- **Sentinel/native mix** — the share of first-party IDE source that is Sentinel vs non-Sentinel (native) code, measured per build and surfaced in the About dialog (FR-17); the A-destination metric — Sentinel trends up, native toward zero, with **no fixed v1 ceiling**. ("% native" denotes the native portion of this mix.)
- **Untrusted-input surface** — the v1 Hardened surface: parsing of source files, project/config files, and build/compiler output.
- **Secret** — a Sentinel `secret`-typed value, subject to the compiler's constant-time / no-leak checks.
- **Code signing** — signing a built artifact (the produced executable) so its origin and integrity can be verified — Windows **Authenticode** signing of the executable `snc` produces (confirmed), **not** source/commit signing. The v1 feature (FR-19..21); distinct from the IDE's own release provenance (SEC-4).
- **Signing key** — the developer's private code-signing key, imported in v1 from a **key file** (`.pfx` / `.p12` / PEM). A secret held by the **native host** in a **not-yet-hardened** surface in v1; hardened next increment (SEC-5).
- **Diagnostic** — a single compiler/language-server finding with a source range, severity, and message (e.g., a `secret`-leak, borrow, or effect error).
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
A developer can open a folder as a project and edit multiple `.sentinel` files with tabs, undo/redo, find/replace (incl. in-file regex), go-to-line, and multi-cursor.
**Consequences (testable):**
- Opening a folder lists its `.sentinel` files in a project tree (FR-3).
- Edits to a 10,000-line file remain responsive (see NFR perf, §6).
- Unsaved edits are never lost on build or focus change.

#### FR-2: Sentinel syntax highlighting
A developer sees Sentinel syntax highlighted, including the security-relevant keywords (`secret`, effects, borrow-related).
**Consequences (testable):**
- Keywords, types, literals, and comments are visually distinguished using the §9 palette.
- Highlighting updates incrementally on edit without a full-file re-parse stall `[ASSUMPTION: incremental/Tree-sitter-style highlighting]`.
- Per-line heavy highlighting is disabled beyond ~20,000 chars/line (research §3).

#### FR-3: Project tree & file open
A developer can browse the project tree and open files, plus a fuzzy open-file finder.
**Consequences (testable):**
- The tree reflects on-disk structure; opening a node opens the file in a tab.
- The fuzzy finder matches files by subsequence on path; selecting a result opens it in a tab.

### 4.2 Build & Run

**Description:** Invoke `snc` from the IDE and surface its results. The single most important loop. Realizes UJ-1.

#### FR-4: Build via snc
A developer can build the current project/file via a button or command that invokes `snc build`, with the exact command visible and configurable.
**Consequences (testable):**
- The invoked command line is shown to the user.
- Build runs as a child process **off the UI thread**; the editor stays interactive during the build (§6).

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

#### FR-8: Inline squiggles
Diagnostics render as inline squiggles at the diagnostic's source range.
**Consequences (testable):**
- A Diagnostic with range (line L, cols C1–C2) renders a squiggle spanning exactly that range; a zero-width range squiggles the word at that position.
- Squiggle styling reflects severity (error vs warning).

#### FR-9: Problems list
All current Diagnostics appear in a Problems list; selecting one navigates to its source range.
**Consequences (testable):**
- Every current Diagnostic appears as exactly one Problems-list row (file, line, message, severity).
- Selecting a row moves the editor caret to the Diagnostic's source range.

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

### 4.4 Toolchain Readiness

**Description:** Make build/run robust against an incomplete Windows toolchain. Grounded in a real session finding (§14). Realizes UJ-3.

#### FR-12: Detect & guide toolchain setup
The IDE checks Toolchain readiness — whether `snc`, the runtime archive, and a compatible linker are present and matched — before/around a build, and surfaces the result clearly rather than failing with only a raw linker error.
**Consequences (testable):**
- On a readiness failure, the IDE names the specific missing/mismatched component (e.g., "runtime archive absent") and shows the best-known remediation; where the fix is upstream (gap #1, §14), it says so and does **not** imply a local fix that does not yet exist.
- When readiness passes, build→run succeeds end-to-end.

**Notes:** `[NOTE FOR PM]` v1 detection is only as good as the current diagnosis of the Windows link gap (OQ-7) — confirm the exact failure mode with the language team before hard-coding remediation text.

### 4.5 Hardened Surfaces & Sentinel/native mix

**Description:** The C-rule as product requirements. See §5 for the contract and threat model.

#### FR-13: v1 untrusted-input surface is Sentinel
The Untrusted-input surface (parsing of source, project/config, and build output) is implemented in Sentinel and passes Sentinel's own safety checks.
**Consequences (testable):**
- The parsing of untrusted bytes is Sentinel code embedded via the C-ABI boundary (addendum); the native host does not parse untrusted input.
- That Sentinel code compiles clean under `snc` (incl. its safety checks).

#### FR-14: Native code restricted to chrome
Non-Sentinel code exists only in the Native host for Chrome; no security-relevant Surface is implemented in native code.
**Consequences (testable):**
- A documented boundary lists what is native; review confirms no Surface crosses into native code.

#### FR-15: Hardened-surface coverage & Sentinel/native mix
The **primary** co-evolution metric is **Hardened-surface coverage** — the share of identified security-relevant Surfaces (§5) implemented in Sentinel (a falsifiable, per-Surface binary). The **Sentinel/native mix** (LOC share) is a secondary indicator, surfaced in the About dialog (FR-17). No fixed v1 ceiling; both are tracked as trends.
**Consequences (testable):**
- The set of identified security-relevant Surfaces is enumerated; each is marked Sentinel or native; coverage = Sentinel Surfaces ÷ total. v1 target: the Untrusted-input surface is covered (FR-13).
- A current Sentinel/native LOC mix (e.g., "Sentinel 18% / Native 82%") is computed per build and recorded per release so the trend is visible.
- The LOC mix is explicitly **not** a measure of how much *danger* sits in Sentinel — Hardened-surface coverage is. Both are reported; coverage leads.

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
The About dialog shows the marketing version and build number (starting **0.1.0, build 1**, incrementing) and the current **Sentinel/native mix** (FR-15).
**Consequences (testable):**
- Version/build render as `0.1.0 (build N)`.
- The Sentinel/native mix renders (e.g., "Sentinel 18% / Native 82%") and updates as the mix changes.

#### FR-18: Migration history / hardening playbook
The project maintains a **migration history** — each security-relevant Surface's move from native to Sentinel, with before/after notes — usable as a brownfield-hardening playbook (the A-destination case study; brief addendum calls it "itself a deliverable").
**Consequences (testable):**
- Each hardened Surface has a dated migration-history entry.
- The history is published as a reusable, human-readable playbook artifact.

### 4.8 Code Signing

**Description:** Developer code signing of built artifacts, with an always-on status signal. The **import / sign / status UX ships in v1**; the signing-**key** (secret) handling surface is **hardened in the next increment**, disclosed honestly in-product per §5 (SEC-5). Realizes UJ-5. *(Signing here means Windows **Authenticode** signing of the executable `snc` produces — confirmed, **not** source/commit signing; distinct from the IDE's own release provenance, SEC-4.)*

#### FR-19: Import a developer signing key
A developer can import a code-signing **key file** (`.pfx` / `.p12` / PEM) with its passphrase (App menu › Signing); the IDE shows the loaded key's identity and an in-product honesty note that key handling is not yet a hardened Sentinel surface.
**Consequences (testable):**
- The import dialog accepts `.pfx` / `.p12` / PEM + passphrase and, on success, displays the loaded key's identity (e.g., subject/issuer). *(DESIGN `import-key-dialog`.)*
- The dialog carries a visible honesty note: while loaded, the key is held by the **native host** in a **not-yet-hardened** surface, hardened next increment (§5 SEC-5); it never implies the key is Sentinel-protected today.
- **Backlog (not v1):** Windows certificate store and hardware token / HSM / smart-card key sources — key-file import only in v1.
- v1 holds the key **in memory for the session only** (no at-rest persistence); any future persistent key storage joins the not-yet-hardened surface (SEC-5), hardened next increment.

#### FR-20: Sign built artifacts
A developer can sign the built executable with the loaded key — automatically via a **Sign on Build** toggle, or on demand via **Sign now** — and the Output pane records the signed artifact and signing identity.
**Consequences (testable):**
- With a key loaded and **Sign on Build** on, a successful build produces a signed executable; **Sign now** signs the current artifact on demand.
- The Output pane records the signed artifact and the signing identity.
- The "Signed" result reflects the **current build's** artifact and is reported only **after the signing operation is verified to have succeeded** (not merely attempted).
- On signing failure (e.g., wrong passphrase, key/algorithm mismatch), the artifact **stays unsigned** and the cause + fix are surfaced — the IDE never reports an artifact as signed when it is not.
- `[NOTE FOR PM]` Signature **longevity** — RFC-3161 timestamping so signatures survive certificate expiry — is an open decision (OQ-9); without it a signed artifact stops verifying once the cert expires.

#### FR-21: Always-on signing-status indicator
An always-visible status-bar indicator shows the current signing state; clicking it opens the Signing dialog (key info, Sign-on-Build toggle, Sign now).
**Consequences (testable):**
- The indicator renders exactly **one** state at a time (by precedence) from: **no key · key loaded · signing · signed · unsigned · failed** (DESIGN `status-signing`; the full state model lives in the UX spec).
- **"Signed" asserts only** that the current artifact carries an Authenticode signature produced with the loaded key — **not** that the key, its certificate chain, or the publisher is trusted/verified by any authority. Copy says "Signed," never "Verified/Secure."
- "Signed" uses the distinct trust color — **not** coral, which stays reserved for the Sentinel-safety signal; "unsigned" is neutral (not alarmist); "failed" uses the error color.
- Clicking the indicator opens the Signing dialog. This is the **one always-on security signal in the chrome**; the proof *metric* (Sentinel/native mix) stays in About (FR-17), distinct from operational signing status.

## 5. Security Model & Hardened-Surface Contract

*The centerpiece. Full depth per the stakes (security buyers).*

**Threat model — the three exposures an IDE uniquely carries:**
1. **Executes untrusted code** — every build/run spawns the compiler and runs produced binaries; every analyzer pass reads untrusted source.
2. **Holds secrets** — signing keys, registry/publish tokens, credentials. **v1 makes this concrete:** developer code signing (FR-19..21) means the IDE now actively holds a signing key — a *named secret surface* that is **not yet hardened** (SEC-5).
3. **Supply-chain apex** — everything authored through the IDE inherits its integrity; compromise here propagates downstream.

**What v1 actually claims — and what it does not (be precise; this is the line a CISO will probe):**
Each untrusted-input path has two parts: **ingestion/transport** (receiving raw bytes from the OS — file read, clipboard, drag-drop, IME) and **interpretation** (deriving structure/meaning — parsing). v1 hardens the **interpretation**: the parsers for source, project/config, and build output are Sentinel (FR-13) and held to Sentinel's own checks. The thin **ingestion/transport** layer and the **C-ABI marshalling** that hands bytes to the Sentinel parsers remain native in v1 — so they are a **named, minimized, audited boundary**, not a hardened one. The honest claim is therefore *"the interpretation of untrusted bytes is Sentinel,"* not *"100% of the IDE is Sentinel."*

> `[NOTE FOR PM]` Buyer messaging must hold this line. **Pillar 2 (secrets):** v1 ships the developer **signing UX** — artifacts are *genuinely signed* (FR-19..21) — but the **signing-key handling surface is not yet hardened** (hardening is the next increment, SEC-5); say *"the artifact is signed; the key surface hardens next,"* never *"keys are protected by Sentinel."* **Pillar 3 (supply chain):** *partly* addressed now via the v1 developer artifact-signing feature (FR-19..21); full provenance — the IDE's own SLSA/SBOM/reproducible builds — is **roadmap** (SEC-4), not v1. Signing ≠ a hardened supply chain. Overselling "100% Sentinel / fully hardened" — or implying the **key** is hardened — is the fastest way to lose a skeptical CISO; lead with the narrow, true claim and the shrinking boundary.

**The contract (C-rule), as testable requirements:**
- Every security-relevant Surface's interpretation logic is Sentinel (FR-13 for v1's Untrusted-input surface). The **signing-key handling surface** ships its UX in v1 (FR-19..21) but is **not yet hardened**; moving key handling into Sentinel is the next increment (SEC-5).
- Native code is confined to Chrome **plus** the explicitly-enumerated ingestion/transport + C-ABI marshalling boundary **and** the v1 signing-key handling surface (FR-14, SEC-3, SEC-5).
- Hardened-surface coverage is tracked and rising (FR-15).

**Security NFRs (cross-cutting):**
- **SEC-1:** No interpretation of untrusted structure (parsing of source / config / build output) occurs in non-Sentinel code; the parser entry points are enumerated and all are Sentinel. *(Validates FR-13; verifiable by reviewing the enumerated parser set.)*
- **SEC-2:** The v1 Untrusted-input surface compiles clean under `snc`'s safety checks (`secret`/borrow/effect) — it is itself held to Sentinel's guarantees. *(Validates FR-13, FR-11.)*
- **SEC-3:** The residual native ingestion/transport + C-ABI marshalling boundary is **enumerated and security-reviewed** each release; it does no structural interpretation of untrusted input and is minimized over time. *(Validates FR-14; this is the honest counterpart to SEC-1.)*
- **SEC-4 `[ASSUMPTION]` (roadmap):** *The IDE's own* released binaries are signed with build provenance (Sigstore/Cosign + SLSA), ship an **SBOM**, and pursue **reproducible builds**; an extension model — when it exists — is sandboxed by design (decide before the API exists). *(This is the IDE's own supply-chain provenance — distinct from the v1 developer artifact-signing feature, FR-19..21 / SEC-5. Research §4; roadmap, not v1 table-stakes.)*
- **SEC-5 (v1 honesty boundary — secrets pillar):** The developer signing key imported in v1 (FR-19) is held by the **native host** — a **named secret surface that is not yet a Hardened surface**. v1 **discloses this in-product** (import-key dialog + status-indicator honesty note) and **enumerates the key-handling surface for security review** each release; hardening it (moving key handling into Sentinel, held to `secret`/no-leak checks) is the **next increment**. The honest claim: the **artifact is genuinely signed**; the **key-handling surface is not yet hardened**. *(Validates FR-19..21; the secrets-pillar counterpart to SEC-3.)*

The differentiator this protects: *for the surfaces it covers, SentinelIDE provably can't be turned against you* — structurally, not by assertion.

## 6. Cross-Cutting NFRs

*Targets are research-grounded `[ASSUMPTION]`s (research §3) for Bryan to confirm/replace.*

- **NFR-PERF-1 `[ASSUMPTION]`:** Keystroke-to-screen latency < 10 ms (target ~2 ms); verify with Typometer.
- **NFR-PERF-2 `[ASSUMPTION]`:** Cold start to interactive < 1 s.
- **NFR-PERF-3:** Builds run off the UI thread with streamed output; the editor stays fully interactive (type/scroll/navigate) during a build. *(Hard requirement, not assumption.)*
- **NFR-PERF-4 `[ASSUMPTION]`:** Frame budget 8.33 ms (120 FPS) / ≥ 60 FPS sustained; consistent frames prioritized over peak.
- **NFR-PERF-5 `[ASSUMPTION]`:** Large-file handling via rope/B-tree buffer (O(log n) edits); heavy highlighting disabled beyond ~20,000 chars/line.
- **NFR-PERF-6 `[ASSUMPTION]`:** Typical-session memory well under the Electron baseline (target < 300 MB).
- **NFR-REL-1:** Unsaved edits are never lost across build, run, or focus change.
- **Security NFRs:** SEC-1..SEC-5 (§5).

## 7. Platform

- Native desktop application; **Windows-first (x64)**.
- Architecture: a thin Native host (Chrome) embedding Sentinel-written Surfaces via the C-ABI boundary (`snc build --lib/--shared --emit-header`; addendum).
- **Not Electron** — the native-perf and security story depends on bypassing a browser engine (research §2).
- **macOS is the next platform target**, gated on a shipping Win32 product; Linux later. (Non-Goal for v1, §11.)
- **Versioning & delivery:** marketing version starts at **0.1.0 (build 1)** and increments; delivery is **agile/iterative** — scope and the Sentinel/native mix (FR-15, FR-17) evolve build over build, not against a fixed up-front ceiling.

## 8. Integration & Dependencies

- **snc** — `build` (compile+link), `lex`/`ast`/`parse` Stage dumps. The IDE shells out to `snc.exe`.
- **Sentinel runtime + linker** — required for build→executable; the v1 Toolchain-readiness gap lives here (§14).
- **C-ABI embedding boundary** — `snc build --lib/--shared --emit-header` (ADR 0059) compiles Sentinel Surfaces to a C-ABI library + header the Native host links. This is how the C-rule is realized.
- **Language server seam** — `sentinel-lsp` crate (salsa-tracked, currently a scaffold) and/or Stage dumps. Which to drive editor intelligence is an architecture decision (§16, OQ-3).

## 9. Aesthetic & Tone

- Native desktop look modeled on **SQLTerminal-Win32** (a visual/UX reference, **not** a codebase to fork): RichEdit-style editor, tree sidebar, virtual grid/list, themed dialogs, status bar.
- **Dark/coral "Claude-desktop" palette** following the OS light/dark setting — e.g., window `#161618`, coral accent `#D97757`, with distinct syntax colors (per `Theme.h`).
- The **"Signed"** signing state uses a **distinct trust color** (green family; `trust-verified` in the UX DESIGN palette); coral stays reserved for the Sentinel-safety signal and is **not** reused for signing (FR-21).
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
- Native-hosted window in the §9 style.
- Edit `.sentinel` with syntax highlighting (FR-1, FR-2, FR-3).
- Build & run via local `snc.exe` with captured, streamed output (FR-4..FR-7).
- v1 Hardened surface = untrusted-input parsing, in Sentinel (FR-13).
- Sentinel diagnostics inline + Problems list, one model (FR-8..FR-11) — compile errors **and the `secret`-leak diagnostic** required (UJ-2); borrow/effect ideal.
- Toolchain-readiness detection & guidance (FR-12).
- % native measured (FR-15); language-gap list maintained (FR-16).
- Developer code-signing UX: import key file, sign built artifacts, always-on signing-status indicator (FR-19..FR-21) — with the in-product honesty note that the signing-**key** surface is not yet hardened (SEC-5).

### 12.2 Out of Scope for MVP
- Completion, go-to-definition, refactoring — *next* (LSP-driven once the seam exists).
- Debugging — *fast-follow* `[NOTE FOR PM]`.
- macOS / Linux — Windows-first.
- GUI-in-Sentinel; multi-project workspaces; plugins; REPL.
- **Hardening** of the signing-key handling surface — next increment (the v1 signing **UX** ships, FR-19..21, but key handling is not yet a Hardened surface, SEC-5); other secrets (publish/registry tokens, credentials) follow.
- Signing-key sources beyond key files — Windows certificate store and hardware token / HSM / smart card are **backlog** (post-v1).

## 13. Why Now

Sentinel has reached self-hosting and a Windows `snc.exe` exists, so a real IDE built in Sentinel is newly feasible — and the language needs a credible, security-shaped proof to move its regulated-industry market from interest to adoption. The proof and the tool are the same artifact.

## 14. Risk & Mitigations

- **Windows build→link not turnkey (confirmed this session).** `snc` links against a GNU/clang runtime archive (`libsentinel_runtime.a`) while the MSVC build produces `sentinel_runtime.lib`. **Mitigation:** FR-12 detects & guides; gap #1 feeds the language team (§10). Severity: medium — gates v1 build/run UX.
- **Language-capability dependency (GUI/FFI in progress).** **Mitigation:** B-path decouples shipping from language readiness; migrate inward as capabilities land.
- **B→A migration stalls** (native footprint never shrinks). **Mitigation:** FR-15 makes % native visible per release; SM-C1 counter-metric guards it.
- **Perf regression** undercuts the native story. **Mitigation:** NFR-PERF gates; Typometer in CI `[ASSUMPTION]`.
- **Signing key held in a not-yet-hardened native surface (v1).** With a key loaded, a secret sits in the native host, outside Sentinel's guarantees, until the surface is hardened. **Mitigation:** scope is "v1 UX, harden next" — the surface is named, enumerated for security review (SEC-5), and disclosed in-product (FR-19 honesty note); hardening is the next increment. Severity: accepted for v1, honestly stated.

## 15. Success Metrics

**Primary**
- **SM-1 (Tool):** A Sentinel developer prefers SentinelIDE to a bare editor for the edit→build→run loop. Target `[ASSUMPTION]`: the core Sentinel devs adopt it as daily driver within 4 weeks of v1. Validates FR-1..FR-11.
- **SM-2 (Proof):** The v1 Untrusted-input surface's interpretation logic is 100% Sentinel and passes Sentinel's safety checks, and the residual native ingestion/transport + C-ABI marshalling boundary is enumerated and reviewed. Target: 100% (binary) + boundary documented. Validates FR-13, SEC-1, SEC-2, SEC-3.
- **SM-3 (Forcing function):** A prioritized Language-gap list is produced and delivered. Target: maintained from v1, seeded with gap #1. Validates FR-16.

**Secondary**
- **SM-4 `[ASSUMPTION]`:** Keystroke latency and cold-start meet NFR-PERF-1/2 on reference hardware. `[NOTE FOR PM]` ungradeable until those targets are committed (OQ-8). Validates NFR-PERF.
- **SM-5:** Hardened-surface coverage is reported per release and increases over time (the falsifiable A-destination metric); the Sentinel/native mix is surfaced in the About dialog. Validates FR-15, FR-17.
- **SM-6 (Signing works):** A developer can import a key file and produce a **signed** executable end-to-end, with the status indicator reflecting it. Target: binary — pass on a reference key + project. Validates FR-19..FR-21.

**Counter-metrics (do not optimize)**
- **SM-C1:** Do **not** grow % native to ship features faster — it directly contradicts SM-2/A-destination. Counterbalances SM-1.
- **SM-C2:** Do **not** trade keystroke latency for feature count — a laggy "rich" editor fails the native-perf premise. Counterbalances SM-1.
- **SM-C3:** Do **not** let the signing UX imply more than v1 delivers — the **artifact is signed**, but the signing-**key** surface is **not yet hardened** (SEC-5). The "Signed" signal must never read as "the key is Sentinel-protected/verified." *Checkable each release:* no shipped copy or status text claims the key is hardened, protected, or trust-verified. Counterbalances FR-19..21 / SM-2 (protects the precise proof claim).

## 16. Open Questions

- **OQ-1 — resolved:** Windows-first; **macOS is the next platform target, gated on a shipping Win32 product**; Linux later.
- **OQ-2:** Debugger approach for the fast-follow — `snc`/LLVM debug info maturity on Windows?
- **OQ-3:** Language-server seam — drive intelligence via `sentinel-lsp`, `snc` Stage dumps, or both? (Architecture.)
- **OQ-4:** Extension model & sandbox posture (SEC-3) — decide before any plugin API.
- **OQ-5 — updated:** The **first secret is the signing key**, and the IDE holds it in **v1** (developer code-signing UX, FR-19..21) — but in a **not-yet-hardened** surface; **hardening** that surface is the next increment (SEC-5). Still open: the precise hardening timeline and *which* further secrets (publish/registry tokens, credentials) follow.
- **OQ-6 — resolved:** **No fixed % native ceiling** — track Hardened-surface coverage (primary) and the Sentinel/native mix as trends, surfaced in the About dialog (FR-15, FR-17).
- **OQ-7:** Does `snc` build *arbitrary* `.sentinel` projects on Windows today, or only a subset (beyond the known link gap)? Confirm toolchain maturity and the exact link-failure root cause with the language team before committing the v1 build/run timeline (FR-12, §14).
- **OQ-8:** Commit real NFR-PERF targets (keystroke / cold-start / FPS / memory) on named reference hardware — until then SM-4 and SM-C2 cannot be graded.
- **OQ-9 — signing longevity & validity:** Does v1 Authenticode signing apply an **RFC-3161 timestamp** (so signatures survive certificate expiry), and how are cert **expiry/revocation** surfaced? Determines whether a signed artifact keeps verifying over time (FR-20, §3 "Code signing").

## 17. Assumptions Index

- §6 / NFR-PERF — metric targets (keystroke < 10 ms, cold start < 1 s, 120 FPS) are research-grounded inferences; **< 300 MB memory is PRD-invented (no research number)**. All are placeholders to confirm, not Bryan-supplied (OQ-8).
- §4.1 FR-2 — incremental (Tree-sitter-style) highlighting assumed as the mechanism.
- §5 SEC-4 — the IDE's **own** release provenance (Sigstore/SLSA), SBOM, reproducible builds, and plugin sandbox flagged as roadmap, not v1 — distinct from the v1 **developer** artifact-signing feature (FR-19..21).
- §4.8 FR-19 — v1 signing-key source is **key-file import** (`.pfx`/`.p12`/PEM); Windows certificate store and hardware token / HSM / smart card are **backlog**.
- §5 SEC-5 / §4.8 — the "v1 UX, harden next" split (artifact signed in v1; signing-**key** surface hardened next) and the in-product honesty disclosures derive from the UX session (EXPERIENCE Flow 5; DESIGN `status-signing` / `import-key-dialog`); the `trust-verified` "Signed" color is a derived UX `[ASSUMPTION]`.
- §14 — Typometer-in-CI assumed as the perf-gate mechanism.
- §15 SM-1/SM-4 — adoption window (4 weeks) and perf-on-reference-hardware are placeholder targets to confirm; SM-4/SM-C2 ungradeable until committed (OQ-8).
