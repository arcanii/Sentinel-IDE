---
title: "SentinelIDE PRD — Addendum"
status: final
created: 2026-06-27
updated: 2026-06-28
---

# SentinelIDE PRD — Addendum

Technical-how and mechanism detail kept out of the PRD body (which states capabilities, not implementation). For the **architecture** phase. Does not duplicate the brief addendum (posture mechanics, milestone ladder, exposed-surface map) or `research-ide-landscape.md` (IDE landscape, NFR numbers) — read those alongside.

## C-rule boundary mechanism (how Sentinel Surfaces embed in the native host)

- `snc build --lib <file> -o <lib.a> --emit-header <h.h>` → C-ABI **static** library + C header (ADR 0059).
- `snc build --shared <file> -o <lib.dylib> --emit-header <h.h>` → C-ABI **shared** library (dlopen/ctypes) + header.
- Implication: the Native host (Win32/C++) links a Sentinel-compiled C-ABI library and calls Hardened Surfaces (e.g., the v1 Untrusted-input parser) across the C-ABI. This is the concrete realization of FR-13/FR-14 and the C-rule. Architecture must define the host↔Sentinel C-ABI interface for the parsing surface.

## Windows toolchain finding (detail behind FR-12 / §14 risk)

- `snc.exe` (release, **C1.0b**) runs on Windows; `snc lex|ast|parse|build` present and the stage dumps work.
- **Observed:** `snc build <f>` compiles, then **link fails** looking for `libsentinel_runtime.a` at `target/release/` — which is absent; the MSVC staticlib `sentinel_runtime.lib` (14 MB) *is* present (`sentinel-runtime` is `crate-type=["lib","staticlib"]`).
- `crates/sentinel-driver/src/main.rs` (~L2011–2020) *does* branch on `libsentinel_runtime.a` (Unix) vs `sentinel_runtime.lib` (MSVC). So the precise root cause is **not yet pinned from the outside** — it is a target/toolchain/staging mismatch (the linker `snc` invokes in this environment wants the `.a`, which isn't produced/staged here) rather than a confirmed naming bug. **Diagnose with the language team (OQ-7) before hard-coding FR-12 remediation.**
- **Net:** Windows compile works; build→link is not turnkey. FR-12 detects/guides honestly; gap #1 asks the language team to make Windows build→link turnkey.

## Language-server seam options (inputs to OQ-3)

- **`sentinel-lsp` crate** — salsa-tracked incremental front-end; intended seam; currently a scaffold/stub (ADR 0025 D10). Best long-term for completion/hover/incremental diagnostics.
- **`snc` stage dumps** — `lex/ast/parse` (and the broader oracle set) already work; cheapest interim source for highlighting tokens and parse/compile diagnostics before the LSP matures.
- Likely path: drive v1 diagnostics from `snc` (build-authoritative) + optionally `parse` for fast feedback; migrate to LSP as `sentinel-lsp` matures. Decision owned by architecture.

## Diagnostics reconciliation (behind FR-10)

- Two Diagnostic sources are normal: *fast* on-keystroke (parse/LSP) and *authoritative* on-build (full `snc`). The single Diagnostic model must dedupe/supersede so a build does not double-render live squiggles. Define precedence (build supersedes keystroke for overlapping ranges).
