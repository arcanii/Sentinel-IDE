---
title: "SentinelIDE Brief — Addendum"
status: final
created: 2026-06-27
updated: 2026-06-28
---

# SentinelIDE Brief — Addendum

Downstream detail captured during discovery. This feeds the **PRD** and **architecture** phases and is intentionally kept out of the 1–2 page brief.

## Platform / toolchain status — OPEN RISK (reconcile first)

The only time-sensitive, blocking item in this file — it gates whether v1 "build & run" is real on day one, so resolve it before committing a v1 timeline.

- `snc.exe` **verified present** (release + debug) at `G:\Sentinel-lang\target` — a Windows build exists; build/run is not blocked outright.
- **However**, the Sentinel repo `README.md` still states the toolchain is pinned to **macOS / Apple-Silicon + LLVM 18**. Per the user, Windows compilation is *in progress* within the language team.
- **To reconcile:** does `snc.exe` build *arbitrary* `.sentinel` projects on Windows today, or only a subset? This determines how much of v1's edit→build→run loop is real on day one.
- No standalone LSP **server** binary exists yet.

## Build posture mechanics

Defined in the brief (§ The Solution: **B-path + C-rule + A-destination**). Architecture-relevant specifics not in the brief:

- **C-rule boundary, precisely:** native code is permitted **only** for security-irrelevant chrome — window creation, the message loop, widget painting, layout. Everything else is Sentinel.
- **A-destination metric:** *"% native code remaining"* is a per-release number driven toward zero; the migration history is itself a deliverable (the case study).
- Full surface taxonomy is in § Exposed-surface map below.

## Candidate milestone ladder (co-evolution)

Each rung is simultaneously a **feature**, a **language forcing-function**, and a **proof**:

| Rung | IDE gains | Forces Sentinel to gain | Proves |
|---|---|---|---|
| 1 | Native window rendering text | FFI + native GUI bindings | Sentinel can draw a real app |
| 2 | Editor + Sentinel highlighting | fast text model (reuse self-host lexer) | real editing surface, all-Sentinel |
| 3 | Build & run `.sentinel` via `snc`, output captured | capability-bounded subprocess + untrusted build-output parsing | the untrusted-input surface (source + build output) is hardened ← **v1 core** |
| 4 | Inline `secret`/borrow/effect diagnostics | LSP / `snc` stage-dump wiring | the IDE speaks Sentinel's unique safety |
| 5 | Completion, navigation, then debugging | incremental analysis, debug info | "rich & helpful" |

## Intelligence seams (architecture decision deferred)

- **`sentinel-lsp` crate** — salsa-tracked incremental front-end; the intended editor-tooling seam; currently a scaffold/stub (ADR 0025 D10).
- **`snc` stage-dump subcommands** — `lex / ast / resolve / types / effects / borrow / mir / ctverify / llvm` — already exist; an interim intelligence source before the LSP matures.
- Open decision: drive editor intelligence via the LSP, via `snc` subprocess dumps, or both.

## Exposed-surface map (the C-rule targets)

Single source for the surface taxonomy.

- **Untrusted input** — source files, project/config files, build and compiler output, anything attacker-influenced. ← **v1 beachhead**
- **Secrets** — signing keys, registry/publish tokens, cloud credentials. ← **v1 ships developer code-signing UX (import / sign / status); the signing-key handling surface is hardened next** (was "next surface"; see PRD §5 SEC-5 and FR-19..21).
- **Supply-chain integrity** — the code-processing and artifact-generating pipeline; the IDE's own build and update channel.
- **Security-irrelevant (may stay native)** — window creation, message loop, widget painting, layout.

## Style reference

SQLTerminal-Win32 is a **visual/UX reference, not a C++ codebase to fork**: native Win32 + Common Controls v6; RichEdit editor with syntax highlighting; virtual `SysListView32` grid; `SysTreeView32` sidebar; themed native dialogs; status bar; worker-thread-per-window. Its `Theme.h` is a dark/coral "Claude-desktop" palette that follows the OS light/dark setting (window `#161618`, coral accent `#D97757`, defined syntax colors).

## Open sub-decision — RESOLVED (post-PRD/UX update)

- **First hardened surface** = untrusted-input parsing (confirmed; visible and lower-risk). **Secrets update:** the signing-key surface ships its **UX in v1** (developer code signing) with **hardening as the next increment** — see the updated Exposed-surface map above and PRD §5 SEC-5 / FR-19..21. (Debugging's deferral to fast-follow is settled in the brief's v1 scope.)
