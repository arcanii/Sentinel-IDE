---
title: "Product Brief: SentinelIDE"
status: final
created: 2026-06-27
updated: 2026-06-27
---

# Product Brief: SentinelIDE

## Executive Summary

SentinelIDE is a native, Windows-first IDE for **Sentinel**, the security-first systems language from Anie Ltd. — and it is itself being built **in Sentinel**. It exists to do four things at once: give Sentinel developers a genuinely rich environment (completion, error reporting, debugging); **prove** that real, security-critical software can be built in Sentinel; act as a **forcing function** that drives the language's roadmap (native GUI, FFI) from real needs; and produce a reusable **migration playbook** for hardening existing native codebases by porting their exposed surfaces into Sentinel.

An IDE is the ideal vehicle for that argument. It executes untrusted code constantly, it holds secrets, and it sits at the very **top of a software supply chain** — the three places where development tooling is most dangerous and most leveraged.

Built in the dark/coral native style of SQLTerminal-Win32, SentinelIDE is **the tool that builds Sentinel, written in Sentinel** — Anie's first serious dogfood and its sharpest credibility argument for the regulated-industry market it targets.

## The Problem

Two problems, one tool.

- **Sentinel has no developer environment.** The language has a world-class security thesis and a self-hosting compiler, but anyone writing Sentinel today works without completion, inline diagnostics, or debugging. Tooling quality gates adoption — a security language nobody can comfortably write doesn't get adopted.
- **Sentinel's market buys on proof, not promise.** Banks, governments, and regulated industries need to *see* security-critical software actually built in the language — and a credible path to hardening the legacy native code they already run. Absent that, Sentinel reads as a research project, not a substrate to bet on.

And the dangerous truth underneath both: **an IDE is the apex of the supply chain.** It runs untrusted code on every build, holds signing keys and tokens, and everything authored through it inherits its integrity. That apex is currently built in languages where memory corruption, secret side-channels, and supply-chain tampering are structurally possible. That is the gap SentinelIDE closes by example.

## The Solution

A native desktop IDE for Sentinel, built in Sentinel, that developers actually want to use. The *how* is deliberate — it's the heart of the design:

- **B-path** — ship a usable IDE now on a thin **native host** for the window chrome, instead of waiting for the language to be able to draw every pixel.
- **C-rule** — **every security-relevant surface is written in Sentinel from day one**: untrusted input, secret material, the build/run subprocess, output and artifact generation. Native code is permitted **only** for security-irrelevant chrome (window, message loop, painting).
- **A-destination** — the native footprint is a tracked, **shrinking debt**, driven toward zero. Each release reports how much native code remains, and the migration itself is a documented deliverable.

Because the security of a dev tool lives in its *surfaces*, not its chrome, this makes the day-one claim both honest and strong: *"every surface that touches your data, your secrets, or your build output is Sentinel — the rest is window chrome, and it's shrinking."*

## What Makes This Different

- **The app is the argument.** Most security tools *describe* safety; SentinelIDE *is* safe by construction — its exposed surfaces structurally cannot carry the bug classes that dominate real incidents.
- **It targets the apex.** Hardening a dev tool hardens everything built with it — the highest-leverage place to plant a security flag.
- **It carries a migration playbook.** The native-to-Sentinel journey is a reference implementation for the **brownfield** hardening every regulated org actually faces — not a greenfield fantasy.
- **It pulls the language forward.** The IDE's unmet needs become the language's prioritized roadmap, and that honesty about what's missing is the mechanism, not an embarrassment.

No moat is claimed beyond execution and the language itself. But for this audience, *"provably can't be turned against you"* is the differentiator.

## Who This Serves

- **Primary — Sentinel application developers.** People writing Sentinel who need a rich, helpful environment: highlighting, completion, inline `secret`/borrow/effect diagnostics, build/run, and soon debugging. Success looks like reaching for SentinelIDE over a bare editor.
- **Secondary — the Sentinel language team (Anie).** They consume the forcing-function feedback; the IDE's gaps become their backlog.
- **Audience of the proof — security buyers** in banks, governments, and regulated industries evaluating Sentinel. They don't write in it yet; they need the assurance the proof provides.

## Success Criteria

**v1 succeeds when all three fire:**

1. **Tool** — a Sentinel developer prefers SentinelIDE to a bare editor for the edit→build→run loop.
2. **Proof** — the v1 hardened surface (untrusted-input parsing) is **100% Sentinel** and passes Sentinel's own safety checks: a true, narrow, defensible security claim.
3. **Forcing function** — the build produces a concrete, prioritized list of language gaps delivered to the language team.

**Ongoing (the A-destination metric):** *% native code remaining*, trending down release over release.

## Scope

**v1 — IN**
- Native-hosted window in the SQLTerminal dark/coral style (chrome native, per the C-rule).
- Edit `.sentinel` files with Sentinel syntax highlighting.
- Build & run via the local `snc.exe`; output and errors captured in a results panel.
- First hardened surface in Sentinel: **untrusted-input handling** — tokenizing/parsing source and build output (reuses the self-hosted lexer). Secret-handling is the next surface, not v1.
- Sentinel diagnostics surfaced inline — compile errors at minimum, the `secret`/borrow/effect diagnostics ideally.

**v1 — OUT** (a boundary, not a backlog)
- Code completion, go-to-definition, refactoring → *next*.
- Debugging → *fast-follow* (the top post-v1 priority; named core to "rich & helpful").
- macOS / Linux → **Windows-first**; cross-platform is the vision.
- GUI-*in-Sentinel* → deferred by design; the native chrome begins shrinking from v2.
- Multi-project workspaces, plugins/extensions, REPL.

## Vision

In two to three years, SentinelIDE is the flagship proof that Sentinel builds real, security-critical software: a cross-platform (Windows, macOS, Linux) native IDE written substantially or entirely in Sentinel, with the native host driven to near-zero. Its development has pulled Sentinel to full native-GUI and FFI capability, and its migration history stands as the canonical playbook regulated organizations follow to harden their own native codebases, one surface at a time.

The tool that builds Sentinel, built in Sentinel — and the reason a CISO trusts the language with the code that matters.
