# Research Digest — Native Desktop IDE v1 (Sentinel, Windows-first)

> Reference material gathered during PRD discovery (web research subagent, 2026-06-27).
> Feeds the Features (FRs) and Cross-Cutting NFRs. Sources cited inline.

## 1. Table-stakes for a credible v1 (minimum vs. nice-to-have)

**Genuinely minimum (without these it reads as a toy):**
- Multi-file editing with tabs, undo/redo, find/replace (incl. in-file regex), go-to-line, multi-cursor.
- Syntax highlighting, ideally incremental (Tree-sitter-style) so it survives edits/large files without full re-parse.
- File/project tree (open folder as project) + open-file fuzzy finder.
- Build & Run integration — invoke the Sentinel compiler from a button/command, with the exact command visible/configurable (JetBrains treats one-click Build/Run as core).
- Output/terminal pane capturing build stdout/stderr **live (streamed, not batched at exit)**.
- Diagnostics in **two places**: inline **squiggles** at the diagnostic char range **and** a **Problems list** that jumps to location (LSP 3.17: editor owns presentation).
- Clickable diagnostics/errors: compiler error lines in the output pane navigate to file:line.

**Nice-to-have for v1 (defer without losing credibility):** autocomplete, go-to-def/find-refs, hover docs, rename refactor, integrated debugger, source-control UI, format-on-save, minimap, split panes. LSP-driven; land incrementally once the editor↔server channel exists.

**PRD gap authors miss:** the *Problems list ↔ inline squiggle ↔ output-pane-click* triad must share one diagnostic model; shipping one without the others is the common half-done state.

## 2. Native / perf-focused editor architecture

- **Zed** — native (Rust), custom GPUI + GPU shaders (DirectX 11 + DirectWrite on Windows); rope buffer + Tree-sitter incremental parse; ~2ms input latency, 120 FPS target.
- **Lapce** — native (Rust), Floem + wgpu; rope buffer; **WASI-sandboxed plugins**; low memory, fast cold open.
- **CLion / RustRover** — IntelliJ (JVM); language analysis out-of-process; Build/Run spawns the toolchain and pipes into a console tool window with **clickable error links**.
- **VS Code** — Electron (Chromium+Node); LSP servers/extensions in separate processes; heaviest footprint; an Electron shell undercuts a "fast systems-lang IDE" story.

**The pattern to adopt:**
- **Editor ↔ language server via LSP** (separate process). Ship a Sentinel language server, even diagnostics-only at first. Seams available: `snc lex/ast/parse` dumps + the `sentinel-lsp` crate.
- **Build/run = spawn a child process**, stream stdout/stderr into the output pane line-by-line, parse for `file:line:col` → clickable + Problems list.
- **Diagnostics flow:** two sources are normal — *fast* on-keystroke (language server) and *authoritative* on-build (compiler). Reconcile so a build doesn't duplicate live squiggles.
- **Native vs Electron:** native editors win input latency + memory by bypassing the browser engine (no DOM reflow / GC frame drops) and using rope + incremental parse + GPU text.

## 3. NFRs / quality bars (concrete numbers)

- **Keystroke-to-screen latency:** target **<10ms**, ideally ~2ms (Zed ~2ms; VS Code ~12–25ms). Perception: 10ms OK, ~100ms terrible (Pavel Fatin). Verify with **Typometer**.
- **Frame budget:** 60–120 FPS → **8.33ms/frame**; consistent frames matter more than peak FPS.
- **Cold startup:** VS Code ~0.8–1.0s with V8 snapshots; native editors faster. Target **sub-second to interactive**.
- **Large files:** disable heavy per-line highlighting beyond **~20,000 chars/line**; large-file optimizations; rope/B-tree buffer → **O(log n) edits** independent of file size.
- **Memory:** VS Code ~1–1.5GB with extensions; native Rust editors materially lower. Set a v1 budget well under the Electron baseline.
- **Responsiveness while building:** builds run off the UI thread (child process); editor stays fully interactive with output streaming — never block the UI on compile.

## 4. Security-hardened / supply-chain-secure dev-tooling positioning

- **Build provenance & signing:** SLSA provenance (Build L3 on GH Actions/GitLab CI) + **Sigstore/Cosign** signing (keyless, OIDC) so users verify the IDE binary matches reviewed source.
- **Reproducible builds:** deterministic from source (SLSA higher bar) — strong "trust the binary" story.
- **SBOM** shipped with releases.
- **Sandboxed extensions/plugins:** Lapce runs plugins in a WASI sandbox — the differentiating native-IDE security move vs VS Code's unsandboxed Node extensions. **Decide the sandbox posture before the plugin API exists — retrofitting is very hard.**
- **Memory-safe implementation:** building the IDE in a memory-safe language is itself a defensible claim (Sentinel goes further — constant-time `secret`, capability-bounding).

**Cheapest credible v1 wins:** (a) signed + provenance-attested releases (CI-native), (b) a sandboxed/permissioned plugin model designed in from day one. Reproducible builds = roadmap differentiator, not v1 table-stakes.
