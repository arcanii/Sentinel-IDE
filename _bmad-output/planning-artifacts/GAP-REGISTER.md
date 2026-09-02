---
title: "SentinelIDE — Gap Register (specified-but-not-built · built-but-not-specified)"
status: reference
created: 2026-09-02
applies_to: "shipped Sentinel-IDE v0.1.7 (build 164), phases 1–42"
artifacts_dated: 2026-06-27 / 2026-06-28
required_by: "decision D1 — the spines stay an intended-v1 SPEC; every designed-but-unbuilt item is marked in place AND listed here"
delta_record: "REALITY-DELTA.md"
sources:
  - "src/host/win32/MainWindow.cpp"          # editor, tab strip, gutter, Problems, accelerators, build, guard
  - "src/core/Toolchain.h"                   # what stands in for toolchain readiness
  - "src/core/Logger.h"                      # logging
  - "src/core/Seal.h"                        # sealed projects
  - "src/core/FileAssoc.h"                   # file associations
  - "src/core/Settings.h"                    # what persists
  - "src/host/win32/Updater.cpp"             # auto-update
  - "src/host/win32/SingleInstance.h"        # single instance + IPC
  - "src/host/win32/SaveChangesDialog.{h,cpp}"
  - "src/host/win32/WinMain.cpp"
  - "packaging/Sentinel-IDE.iss"             # installer
  - "tests/seal_test.cpp"
  - "docs/HANDOVER.md"                       # phase list 1–42, Releases
governs:
  - "planning-artifacts/prds/prd-SentinelIDE-2026-06-27/prd.md"
  - "planning-artifacts/ux-designs/ux-SentinelIDE-2026-06-27/EXPERIENCE.md"
  - "planning-artifacts/ux-designs/ux-SentinelIDE-2026-06-27/DESIGN.md"
  - "planning-artifacts/briefs/brief-SentinelIDE-2026-06-27/brief.md"
---

# Gap Register

Decision **D1** keeps the planning spines as an **intended-v1 SPEC**: behaviour that was designed
but never built is *kept, not deleted* — marked in place at the point of the claim, and listed here.
This file is that single list, in both directions.

**Part A — SPECIFIED BUT NOT BUILT.** Written into the artifacts, absent from the code. These carry
an in-place `[NOT BUILT in v0.1.7 — …]` marker in the artifact that claims them; this register is
where you find out *why* it is absent and what building it would actually cost.

**Part B — BUILT BUT NOT SPECIFIED.** Shipped subsystems with no requirement behind them. These need
*adding* to an artifact, not marking. Each entry names the owning document.

**What this register is not.** It does not cover the third staleness class — behaviour the artifacts
describe **wrongly**, where the product does something real but different (the Authenticode→ADR-0061
signing reversal, the folder→manifest project model, targets, tiers, the scheme selector, the derived
build command, the git-derived build number). Those are fixed by rewriting the claim, not by
registering a gap. They live in [`REALITY-DELTA.md`](REALITY-DELTA.md) rows `RD-01`…`RD-12`, `RD-15`.

Every entry below was verified against the named source file at the named line. Nothing is listed on
the strength of another document. See §3 for what was checked and what was deliberately left out.

---

## Part A — SPECIFIED BUT NOT BUILT

| ID | Item | Specified in | Delta row | Cost shape |
|---|---|---|---|---|
| **GAP-A1** | The UJ-2 secret-leak journey — diagnostic **severity**, **squiggles**, **gutter shield** | PRD UJ-2, FR-8, FR-9, FR-11; EXPERIENCE Flow 2, Component Patterns, State Patterns; DESIGN `squiggle-*`, `diagnostic-badge-security` | RD-13, RD-14 | Large — needs data the compiler does not emit **and** paint the editor control cannot do |
| **GAP-A2** | `Ctrl+P` fuzzy open-file finder | PRD FR-3; EXPERIENCE IA, Component Patterns, Interaction Primitives | RD-18 | Medium — a new surface, and a file list that does not yet exist |
| **GAP-A3** | Multi-tab editing | PRD FR-1, FR-3; EXPERIENCE IA, Component Patterns "Tabs"; DESIGN `tab-active`/`tab-inactive` | RD-17 | Large — the whole editor is written against one buffer |
| **GAP-A4** | Find / replace, incl. regex | PRD FR-1; EXPERIENCE Component Patterns "Editor", Interaction Primitives; DESIGN `dialog` | RD-18 | Medium — plain search is cheap, regex is not, and the two spines disagree on the shape |
| **GAP-A5** | Go-to-line | PRD FR-1; EXPERIENCE Interaction Primitives | RD-18 | **Small — the mechanism already ships; only the prompt is missing** |
| **GAP-A6** | Multi-cursor | PRD FR-1; EXPERIENCE Component Patterns "Editor", Interaction Primitives `[ASSUMPTION]` | RD-18 | **Largest in the register — not addable to the current editor control at all** |
| **GAP-A7** | `F8` / `Shift+F8` problem navigation | EXPERIENCE Interaction Primitives (no PRD FR) | RD-18 | Small |
| **GAP-A8** | Toolchain-readiness surface + dialog (UJ-3) | PRD UJ-3, FR-12, §12.1, §14; EXPERIENCE IA, Component Patterns, State Patterns, Flow 3; `mockups/key-toolchain-readiness.html` | RD-19 | Medium — blocked on an upstream answer (OQ-7) before the copy can be written |

---

### GAP-A1 — The UJ-2 secret-leak journey: severity, squiggles, gutter shield

The flagship moment. The PRD calls UJ-2 "the language's headline guarantee showing up *in the
editor*"; EXPERIENCE Flow 2 is starred `★ flagship`. **None of its three rendering elements exist.**

**Specified in.** PRD §2.3 UJ-2; FR-8 *Inline squiggles* (both testable consequences — "a squiggle
spanning exactly that range" and "styling reflects severity"); FR-9 *Problems list* ("file, line,
message, **severity**"); FR-11 *Sentinel-specific diagnostics*; §12.1 MVP in-scope. EXPERIENCE Flow 2
steps 2–3, the Component Patterns **Editor** / **Problems list** / **Diagnostic triad** rows, the
State Patterns **Sentinel safety finding** row. DESIGN `{components.squiggle-error}` /
`{-warning}` / `{-security}`, `{components.diagnostic-badge-security}`, and the `{components.problems-list}`
error/warning/security row colours. Mockup `mockups/key-secret-leak-editor.html`.

**What exists today.**

- `struct Diag { std::wstring file; int line = 1, col = 1; std::wstring msg; };`
  — `MainWindow.cpp:76`. **No severity field. No end column. No diagnostic code.**
- Severity is *derived and then discarded*. `WM_APP_LINE` (`MainWindow.cpp:1657–1659`) picks the
  Output-pane line colour by substring — `×` or `error` → `diagError`, `warning` → `diagWarning` —
  and then `addProblem` (`MainWindow.cpp:991`) stores none of it. The classification is thrown away
  one line after it is made.
- The Problems list has **three** columns: `Message · File · Line`
  (`ListView_InsertColumn` ×3, `MainWindow.cpp:216–218`). It is a plain `SysListView32` with one
  `ListView_SetTextColor` for the whole control (`:212`) and **no `NM_CUSTOMDRAW` handler**, so every
  row is the same colour. No shield glyph, no severity column, no security class.
- **No squiggles anywhere in the codebase.** In their place, `markErrorLines`
  (`MainWindow.cpp:528`) tints the **whole line** background with
  `blendColor(windowBg, diagError, 24)` for every diagnostic in the open file — the same colour for
  all of them — and `clearErrorMarks` wipes it on the next edit.
- The gutter paints **line numbers only** (`MainWindow.cpp:334–352`), and only when `g.lineNumbers`
  is on, which is `Ctrl+L`, **off by default** (`Settings::lineNumbers = false`, `Settings.h:24`).

**Why it is not there.** Three independent blockers, in ascending order of cost.

1. **The severity does not arrive.** `parseDiag` (`MainWindow.cpp:946` Sentinel branch / `:972` C++
   fallback) reads `snc`'s *rendered miette text* and extracts a file, a line and a column. That is
   all the text reliably carries. The `tokStart`/`tokEnd` out-params are offsets **within the Output
   pane's line**, used by `outLinkify` to make `file:line:col` clickable — they are **not** source
   spans, and it is easy to misread them as such.
2. **The span does not arrive either.** FR-8's testable consequence asks for a squiggle spanning
   "cols C1–C2". `snc` gives a start column. Without an end column, only FR-8's fallback clause —
   "a zero-width range squiggles the word at that position" — is implementable.
3. **The editor cannot draw it.** The editor is `MSFTEDIT_CLASS` (RichEdit 4.1,
   `MainWindow.cpp:190–191`). RichEdit does have a wavy underline (`CFM_UNDERLINETYPE` +
   `CFU_UNDERLINEWAVE`), but its underline **colour is a 16-entry palette index** in the high nibble
   of `bUnderlineType`, not an RGB — so DESIGN's three distinct squiggle colours, and
   `{colors.diag-security}` coral in particular, cannot be expressed through the control's own API.

**What building it would require.**

- **Severity + span at the source.** Either extend `parse_diag` in `src/sentinel/parsers.sentinel` to
  return a severity and an end column — which means extending the C-ABI record, the C++ fallback, and
  `tests/diag_xcheck.cpp` that holds the two in lockstep (11 cases today) — or get a machine-readable
  diagnostic stream (miette JSON) out of `snc` upstream. The second is the honest fix; the first
  encodes a guess about rendered text into a security-boundary parser.
- **Owning the squiggle paint.** Subclass the RichEdit, and after each `WM_PAINT` overdraw each
  span's wave using `EM_POSFROMCHAR` per span — re-run on every scroll, resize, edit and theme
  change. The alternative is the Direct2D editor already recorded as future work in HANDOVER
  phase 15. Neither is a small change.
- **The gutter shield.** Cheap once the paint exists — the gutter is a `RECT` on the main window
  (`g.rGutter`), parent-painted, not a child control. But it is **hidden by default**, so either the
  shield forces the gutter open or the flagship moment is invisible on a fresh install. That is a
  product decision, not an implementation detail.
- **Problems-list severity.** An `NM_CUSTOMDRAW` handler for per-row colour, plus a fourth column or
  a leading glyph. Contained: `addProblem` is the list's only writer.
- **Keeping FR-10 true.** "One diagnostic model" currently holds because the two surfaces that exist
  (Problems rows and Output links) both come from `Diag`. Squiggles must **replace** `markErrorLines`,
  not sit beside it, or the model silently forks into two renderings of the same finding.

---

### GAP-A2 — `Ctrl+P` fuzzy open-file finder

**Specified in.** PRD FR-3 ("plus a fuzzy open-file finder", with the testable consequence "matches
files by subsequence on path; selecting a result opens it in a tab"). EXPERIENCE IA row
*Fuzzy open-file finder · reached from `Ctrl+P`*, the Component Patterns **Fuzzy finder** row
(`Enter` opens the top result, `↑/↓` to choose, `Esc` cancels), and Interaction Primitives.

**What exists today.** Nothing. The complete accelerator table is nine entries
(`MainWindow.cpp:1917–1928`): `Ctrl+S` `Ctrl+Z` `Ctrl+Y` `Ctrl+N` `Ctrl+O` `Ctrl+Shift+N`
`Ctrl+Shift+B` `F5` `Ctrl+L` `Ctrl+,`. **`Ctrl+P` is not bound.** Files are opened from exactly four
places: a tree node, a Problems row, an Output `file:line` link, and a path handed in from outside
(`requestOpenPath`, `MainWindow.cpp:1179`).

**What building it would require.**

- A new overlay or modal in the existing themed-dialog family. EXPERIENCE's rule that dialogs stack
  **one level deep** applies, and `uiIsBusy(hwnd)` (`MainWindow.cpp:1165`) already answers "is a
  modal, a tracking popup, or a splitter drag holding the UI right now" — reuse it rather than
  inventing a second answer.
- **A file list, which does not exist.** `g.nodePaths` maps tree nodes to paths, but it holds only
  what the tree currently shows, and the Project view filters to sources. A finder over the whole
  project needs its own recursive walk plus an invalidation story — and there is **no file-system
  watcher anywhere in this codebase**, so "the tree reflects on-disk structure" is refresh-driven
  today. A stale finder index is worse than none.
- Subsequence match + ranking, `Ctrl+P` in the accelerator table, and routing the chosen path through
  `openFile()` (`MainWindow.cpp:671`) so it inherits the unsaved-changes guard for free. Bypassing
  `openFile` would reintroduce the phase-39 defect.

---

### GAP-A3 — Multi-tab editing

**Specified in.** PRD FR-1 ("edit multiple `.sentinel` files **with tabs**") and FR-3's consequence
("opening a node opens the file in a tab"). EXPERIENCE IA *Editor area + tab strip*, and the
Component Patterns **Tabs** row: open/close/reorder, `•` dirty glyph, closing a dirty tab prompts.
DESIGN `{components.tab-active}` / `{components.tab-inactive}` ("active tab: window-bg + a 2px coral
top-border").

**What exists today.** **One buffer.** The entire editor state is four globals —
`g.curFilePath`, `g.curFileName`, `g.dirty`, `g.savedText` — and one `MSFTEDIT` child window. The
"tab strip" is four lines of painting (`MainWindow.cpp:321–326`): a band showing that single file
name, `untitled` when none, a `●` prefix when dirty, and a coral underline whose width is estimated
as `tab.size() * sc(8)`. **There is no tab control, no per-tab hit-testing, no close box, and no
collection of open files.** Opening a second file replaces the first. (The coral underline does match
DESIGN's accent border — the *visual* claim survives; the behavioural one does not.)

**What building it would require.**

- Promote those four globals to a vector of buffers plus an active index. Every consumer written
  against "the one open file" follows: `saveFile`, `markErrorLines`, `refreshSignState` and the
  trust chip (which is bound to `g.curFilePath` — see RD-04), the tree's selection restore, and
  `loadFileIntoEditor`.
- **Decide the undo story first.** `EM_UNDO`/`EM_REDO` history lives inside the RichEdit control and
  cannot be serialised out. One control shared across tabs therefore **discards undo on every tab
  switch** — which is precisely the class of silent work loss phase 39 existed to end. One control
  per tab avoids it and costs a window, a font, a TOM `ITextDocument` and a highlight pass per open
  file.
- **`confirmSaveIfDirty` becomes per-tab.** It is currently the single choke point (`:659`) for ten
  call sites, every one of which assumes "the one open file". Each has to be re-decided: opening a
  file and switching a target mean *this tab*; Close Project, `WM_CLOSE` and Seal mean *all tabs*,
  which turns a single prompt into a loop with a Cancel that must unwind cleanly.
- Real hit-testing on the strip, close boxes, drag-to-reorder, and overflow behaviour — none of which
  the current painted band has any structure for.

---

### GAP-A4 — Find / replace, including regex

**Specified in.** PRD FR-1 ("find/replace (incl. in-file regex)"). EXPERIENCE Component Patterns
**Editor** row, Interaction Primitives (`Ctrl+F` / `Ctrl+H`, "regex toggle **in the bar**"), and the
IA note that dialogs — "Settings, About, toolchain remediation, **find/replace**, Import Signing Key"
— stack one level deep. DESIGN `{components.dialog}`: "Used for About, Settings, toolchain
remediation, **find/replace**."

**What exists today.** Nothing. No `EM_FINDTEXTEXW`, no `FINDREPLACE`/`FindTextW`, no `Ctrl+F` or
`Ctrl+H` in the accelerator table.

**A spec conflict to resolve before building.** EXPERIENCE describes a **bar** with a regex toggle;
DESIGN lists find/replace among the things `{components.dialog}` is used for. Both spines "win on
conflict" by their own preamble, so this one has to be settled explicitly rather than by whoever
implements first.

**What building it would require.**

- Plain search is small: `EM_FINDTEXTEXW` gives case and whole-word flags directly on the control.
- **Regex is not.** RichEdit has no regex. It means pulling the buffer out through `editorText()`
  (`MainWindow.cpp`, the shared accessor that returns the buffer with RichEdit's lone-`CR` line
  breaks intact), matching in C++ (`std::wregex`) or Sentinel, then mapping match offsets back to
  `EM_EXSETSEL` character positions. The lone-`CR` representation matters: `saveFile` deliberately
  re-fetches with `GT_USECRLF` for the on-disk form, and the two offset spaces are **not**
  interchangeable.
- Replace-all must wrap in `suspendUndo()` / `resumeUndo()` and mark dirty exactly once — otherwise
  it pollutes the native undo stack the same way the syntax highlighter did before phase 18, and
  `Ctrl+Z` starts undoing formatting instead of edits.

---

### GAP-A5 — Go-to-line

**Specified in.** PRD FR-1. EXPERIENCE Interaction Primitives: "`Ctrl+G` — go-to-line".

**What exists today.** The *mechanism ships*; only the user-facing command is missing.
`gotoLineCol(hwnd, file, line, col)` (`MainWindow.cpp:1150`) already opens the file if needed
(through `openFile`, so it is guarded), converts line/col to a character index via `EM_LINEINDEX`,
selects, scrolls with `EM_SCROLLCARET` and focuses the editor. It is reached today from a Problems
row and an Output `file:line:col` link. `Ctrl+G` is simply not bound.

**What building it would require.** The smallest item in Part A: a one-field themed modal following
the existing `PasswordDialog` / `SaveChangesDialog` pattern, a `Ctrl+G` accelerator entry, a clamp
against `EM_GETLINECOUNT`, and one call to `gotoLineCol(hwnd, L"", n, 1)`.

---

### GAP-A6 — Multi-cursor

**Specified in.** PRD FR-1 ("multi-cursor"). EXPERIENCE Component Patterns **Editor** row, and
Interaction Primitives: "Multi-cursor — `Ctrl+Click` adds a caret; column/box select via `Alt+drag`
`[ASSUMPTION]`".

**What exists today.** Nothing. One selection, one caret — whatever `MSFTEDIT_CLASS` provides.

**Why it is not there, and why it is the largest item here.** RichEdit has **exactly one selection**
(`EM_EXSETSEL` / `EM_EXGETSEL`, the pair this codebase uses throughout to save and restore selection
around highlighting) and **exactly one caret**. Multi-cursor is not a feature that can be layered on
top of that control; it requires owning the text buffer, the caret model, hit-testing and the paint —
i.e. **replacing the editor**, which is the Direct2D editor noted as unbuilt future work in HANDOVER
phase 15.

The `[ASSUMPTION]` tag EXPERIENCE put on the `Alt+drag` half was never tested against the chosen
control. Its honest cost is *"the editor rewrite"*, not *"a feature"* — and it should be scoped that
way in any plan that keeps it, rather than sitting in an FR-1 list beside undo/redo as if it were
peer-sized.

---

### GAP-A7 — `F8` / `Shift+F8` problem navigation

**Specified in.** EXPERIENCE Interaction Primitives: "`F8` / `Shift+F8` — next / previous problem
(jumps the triad)". **No PRD FR names it** — it exists only in the behaviour spine.

**What exists today.** Nothing. `F8` is not in the accelerator table. Diagnostics *are* an ordered
`std::vector<Diag> g.problems`, and `gotoLineCol` already does the jump, but there is no cursor into
the list and no keyboard route to it. Today the only way to a diagnostic is clicking a Problems row
or an Output link.

**What building it would require.** An index into `g.problems`, two accelerator entries, wrap-around,
and one decision: `g.problems` is rebuilt from scratch on each build (`outClear` + fresh
`addProblem` calls) and is **never invalidated by editing**, so after a few edits the stored line
numbers drift from the buffer. "Next problem" has to define what it means against a stale list — the
same question `markErrorLines` sidesteps by clearing its tints on the first edit.

---

### GAP-A8 — Toolchain-readiness surface and dialog (UJ-3)

**Specified in.** PRD §2.3 UJ-3, FR-12 *Detect & guide toolchain setup* with both testable
consequences (names the specific missing component; where the fix is upstream, says so and does not
imply a local fix), §12.1 MVP in-scope, §14 Risk. EXPERIENCE IA row *Toolchain-readiness surface*,
the Component Patterns **Toolchain readiness** row, the State Patterns **Toolchain not ready** row,
and Flow 3 end to end. Mockup `mockups/key-toolchain-readiness.html`.

**What exists today — a narrower, unattended substitute, not the specified surface.**

- `findVcvars()` (`src/core/Toolchain.h`) auto-detects `vcvars64.bat`, first through `vswhere` (any
  edition/version/preview), then through a matrix of well-known install paths.
- `captureMsvcEnv()` runs it and captures the resulting environment as a Unicode environment block,
  which the build child inherits — **this is the change that actually closed the Windows link gap**
  (HANDOVER phase 13), and it fixes the problem silently rather than guiding the user through it.
- `runBuild` prints exactly **one line** about it into the Output pane: the warning
  `(no MSVC environment found — link.exe won't be on PATH; set it in Settings → MSVC environment)`
  (`MainWindow.cpp:1130`) or, when found, the informational `(MSVC environment: <path> …)` (`:1132`).

There is **no readiness check** of `snc` beyond `findSnc` picking a binary, **no check of the runtime
archive at all**, **no dialog**, no component-by-component result, no copy-pasteable remediation, and
no statement that the fix is upstream. The IDE ships seven modal dialogs (Settings, Project Settings,
Signing & Trust, About, Password, Save Changes, Update); a toolchain one is not among them.

**What building it would require.**

- Three probes and a result model: is `snc` present *and which subcommands work* — `sncSigningCaps`
  (`src/core/Signing.h`) already models exactly this shape for `verify` vs `keygen`/`sign`, where a
  binary can advertise a capability in its help text and still fail at runtime, and is the pattern to
  copy; is the runtime archive present and matched to this linker; is a linker reachable.
- A themed modal in the existing dialog family, plus the remediation copy.
- **The copy is blocked upstream.** FR-12's own `[NOTE FOR PM]` and **OQ-7** both say the remediation
  text cannot be written until the exact Windows link-failure mode is confirmed with the language
  team. That is still true.
- One thing to be honest about when scoping it: the single component the IDE *can* fix locally is
  **already fixed automatically**. A readiness dialog built today would mostly be *reporting* a
  state the IDE has already repaired, plus naming a gap it cannot close. That is still worth
  building — Flow 3's value is that the failure becomes a guided step — but it is a smaller win than
  the flow implies, and the flow should say so.

---

## Part B — BUILT BUT NOT SPECIFIED

| ID | Subsystem | Lives in | Phase | Owning document |
|---|---|---|---|---|
| **GAP-B1** | **Logging** — *half-case: specified in EXPERIENCE, no PRD FR* | `src/core/Logger.h`, `Settings.h`, `SettingsDialog.cpp` | 5 | **PRD** (new FR). EXPERIENCE already correct |
| **GAP-B2** | **Auto-update** (WinSparkle + the phase-40/41 saga) | `Updater.{h,cpp}`, `UpdateDialog.{h,cpp}`, `scripts/make-appcast.ps1` | 32, 40, 41 | **PRD** §4 + §5, **EXPERIENCE** for the offer dialog |
| **GAP-B3** | **Inno Setup installer** | `packaging/Sentinel-IDE.iss`, `scripts/make-installer.bat` | 28, 33 | **PRD §7 Platform** |
| **GAP-B4** | **Sealed projects** | `src/core/Seal.h`, `PasswordDialog.{h,cpp}`, `tests/seal_test.cpp` | 25, 31 | **PRD** — new FR **and** a fourth §5 surface. **EXPERIENCE** for the flows |
| **GAP-B5** | **File associations** | `src/core/FileAssoc.h`, `openPathArg` | 26 | **PRD §7 Platform**, **EXPERIENCE** for the open rule |
| **GAP-B6** | **Unsaved-changes guard** — *partly specified, much narrower than reality* | `SaveChangesDialog.{h,cpp}`, `confirmSaveIfDirty` | 39 | **EXPERIENCE** (states), **PRD** to restate NFR-REL-1 |
| **GAP-B7** | **Single instance + drag-drop** | `SingleInstance.h`, `WinMain.cpp`, `requestOpenPath` | 42 | **EXPERIENCE** (IA + primitives), **PRD §7** |

---

### GAP-B1 — Logging

**A half-case, and the only one in Part B.** It *is* specified — in EXPERIENCE, twice, both rows
tagged `[new]`: the IA **Settings / Preferences** row ("**logging** (level + log-file location)") and
the Component Patterns **Settings** row ("level (Error / Warn / Info / Debug / Trace) + log-file
location, with a 'reveal in Explorer' affordance. Changes apply without restart"). **All of that
ships, as written.** What is missing is a **PRD requirement** — nothing in §4 asks for logging, so
the behaviour spine specifies a surface with no requirement behind it.

**Where it lives.** `src/core/Logger.h` — header-only, `std::mutex`-guarded, append-only, UTF-8,
level-gated, writing with `FILE_APPEND_DATA` + full share flags so an open log never blocks the app.
`Settings::logLevel` / `logFile` (`Settings.h:20–21`), default computed by `defaultLogFile()`
(`Settings.h:46`) as `%LOCALAPPDATA%\SentinelIDE\logs\sentinelide.log`, persisted as `[log] level` /
`[log] file`. In the Settings dialog: the level combo (`SettingsDialog.cpp:145`), the path field
(`:148`) and a **Reveal** button (`:149`) that runs `ShellExecuteW` on Explorer with `/select`
(`:66`). Reconfigured live at startup (`MainWindow.cpp:1495`) and immediately on Settings OK
(`:1817`) — hence "without restart". **56 `logMsg` call sites** across the codebase.

**Owner: the PRD.** One new FR in §4 — an operability/diagnostics requirement — plus the honest note
that the log is a plain local file with no redaction pass, which matters for a product whose §5
enumerates a secrets pillar. EXPERIENCE needs no change; it was right all along.

---

### GAP-B2 — Auto-update

**In no artifact.** The nearest thing is `SEC-4`, which makes *the IDE's own release provenance*
(Sigstore/SLSA/SBOM) an explicit roadmap item — a different subject: SEC-4 is about the trustworthiness
of what is published, not about a mechanism for delivering it to installed clients. Nothing in any
spine describes the IDE updating itself.

**Where it lives.** `src/host/win32/Updater.{h,cpp}` over vendored **WinSparkle 0.9.3**
(`third_party/winsparkle`; `CMakeLists.txt:36`).

- Feed: `https://raw.githubusercontent.com/arcanii/Sentinel-IDE/main/appcast.xml`
  (`Updater.cpp:25`), fetched over **unauthenticated HTTPS** from a public repo. The trust anchor is
  a **compiled-in Ed25519 public key**, not the transport: WinSparkle verifies the payload signature
  against it before anything runs.
- WinSparkle's own periodic check is **deliberately disabled**
  (`win_sparkle_set_automatic_check_for_updates(0)`), because its prompt leads to the install path
  that does nothing. In its place a detached thread polls the appcast itself — a **10-second startup
  settle then hourly** (`kStartupSettleMs` / `kCheckIntervalMs`, `Updater.cpp:148–149`) — and posts
  `WM_APP_UPDATE_AVAILABLE` once per run.
- The offer is a themed `UpdateDialog`: **Skip this version · Install now · Later**, with `Later` as
  the default button and `IDOK` ignored for 700 ms, and it refuses to open while another modal owns
  the window (parking in `g.pendingUpdate`, retried on a 4-second timer). All three are direct
  products of the phase-41 adversarial review.
- A skipped version persists as `[update] skip_version` (`Settings::updateSkipVersion`) and is never
  re-offered.
- `onUserRunInstaller` runs the verified payload **itself** with `/CURRENTUSER` or `/ALLUSERS` chosen
  by where the running exe lives, plus `SEE_MASK_NOASYNC` — WinSparkle's own execute step hands over
  an empty path in 0.9.3.
- `updaterAvailable()` (`Updater.cpp:253`) gates both the menu item (`MainWindow.cpp:1455`) and the
  About-box button (`AboutDialog.cpp:198`): with no key compiled in, the affordance is **hidden**,
  not greyed.
- Release side: `scripts/make-appcast.ps1`, `scripts/sign-release.ps1`, `docs/RELEASING.md`.

**The saga is the requirement.** Phase 32 added it; **v0.1.0–v0.1.4 offered, downloaded, verified and
then installed nothing, silently, for four releases** (phase 40); phase 41 found that the fix reached
only the *manual* check and replaced the background path too. The lesson that belongs in the spec is
the diagnostic one: an updater that fails silently is indistinguishable from one that works, so the
failure callbacks are load-bearing, not decoration.

**Owner.** **PRD** for the requirement (a delivery/updates FR) *and* for its honesty rule — what the
product may claim about an update it has offered versus one it has confirmed installed. **PRD §5**
for the security properties: the feed is unauthenticated transport, the signature is the trust
anchor, and a tampered feed can at worst provoke an offer WinSparkle then refuses to install.
**EXPERIENCE** for the offer dialog's states and its interruption rules — never over another modal,
`Later` defaulted, the `IDOK` guard, skip persistence. **DESIGN** needs nothing: the dialog is
`{components.dialog}` as already specified.

*(One documentation drift found while verifying: HANDOVER phase 41's prose still says the poll thread
sleeps 90 s and checks daily. The shipped constants are 10 s then hourly — changed by commit
`15439d6`. That is a `docs/` fix, out of this register's scope; noted here so it is not lost.)*

---

### GAP-B3 — Inno Setup installer

**In no artifact.** PRD §7 Platform and §8 Integration name Windows and `snc`, but no delivery
vehicle. Eight releases have shipped this way (HANDOVER **Releases** table).

**Where it lives.** `packaging/Sentinel-IDE.iss` (135 lines) + `scripts/make-installer.bat`.

- **Per-user by default, no admin**: `PrivilegesRequired=lowest` with
  `PrivilegesRequiredOverridesAllowed=dialog`.
- x64 only, both directives set: `ArchitecturesAllowed=x64compatible` *and*
  `ArchitecturesInstallIn64BitMode=x64compatible` — the second is what stops a 32-bit Setup resolving
  `{autopf}` through WOW64 into the x86 Program Files.
- Payload: the exe, `examples/*` (with `*.sig` **deliberately not excluded**, so the signed demo
  `examples/crypto.sentinel.sig` survives installation), README and LICENSE.
- `[Icons]`: Start-Menu entry, uninstall entry, optional desktop icon. `[Registry]`: the same two
  ProgIDs as GAP-B5 under `HKA`, with `uninsdeletekey`/`uninsdeletevalue` so uninstall reverses them.
- **The version is read from the built exe's `FileVersion` resource** (`GetVersionNumbersString`),
  never hand-typed, and the script hard-errors if the exe is not built. Specifically the *File*
  version, not Product — `SentinelIDE.rc` pins `PRODUCTVERSION 0,1,0,0`, so reading Product would
  silently stamp every installer `0.1.0.0`.

**Owner: PRD §7 Platform.** Delivery is a platform statement — what is shipped, how it installs, at
what privilege, and what it registers. The **version-provenance rule** belongs beside the versioning
statement in §0/§7: the exe, the installer filename and the appcast's `sparkle:version` all derive
from one source (the git-derived build number, RD-15), and `docs/RELEASING.md` requires them to
agree. No UX artifact needs to change.

---

### GAP-B4 — Sealed projects

**In no artifact.** And it is not a variant of code signing: sealing is **confidentiality of the
developer's own source at rest**, which none of §5's three pillars (untrusted input, secrets, supply
chain) covers.

**Where it lives.** `src/core/Seal.h` — 405 lines, native CNG (BCrypt) + the Windows Compression API,
no third-party crypto.

- Pipeline: archive the folder → **LZMS** compress → **AES-256-GCM** encrypt under a random master
  key (DEK). The DEK is wrapped per **unlock slot**, LUKS-style: v1 ships one password slot,
  PBKDF2-HMAC-SHA256 with a 16-byte salt and **600,000 iterations**
  (`kSealPbkdf2Iters`, `Seal.h:244`) → KEK → AES-256-GCM key-wrap. Further unlock methods become new
  slot types wrapping the *same* DEK — no re-encryption, several methods per project.
- Format **v2** (`SNTSEAL2`, `Seal.h:16–23`): slots carry a `slot_len` so an unknown slot type is
  **skippable** (v1 had none, so a reader hitting one had to abort — flatly contradicting the
  extensibility the slot design exists for), and the 24-byte header prefix is bound as AEAD **AAD**
  (in v1 the unauthenticated `archive_size` was fed straight to the decompressor as an output-buffer
  size, so flipping those 8 bytes steered a large allocation in a victim's process). v1 files still
  read.
- Hardening: extraction rejects `..` and absolute paths; archiving skips `target`, `build`, `.git`,
  `node_modules` and any `.sealed`.
- UI: `≡ ▸ Seal Project…` — greyed unless a project is loaded (`MainWindow.cpp:1431`) — routes
  through `confirmSaveIfDirty` (the seal archives what is on **disk**, so an unsaved buffer would be
  sealed stale), then a double-entry themed `PasswordDialog`, writing `<parent>\<name>.sealed`
  **non-destructively**, with the password buffers `SecureZeroMemory`'d after use.
  `≡ ▸ Open Sealed Project…` → picker → password → decrypts to a sibling `<name>-unsealed\`.
- `tests/seal_test.cpp` — **the repo's first test, 25 assertions**, including a hand-built v1 file
  that must still unseal byte-identically and still reject a wrong password.

**Owner: the PRD, in two places.** A new FR for the feature — and **a fourth entry in §5**, because
sealing is the one shipped capability that makes a **cryptographic promise the PRD has never
stated**. It also belongs in §5's surface enumeration as an explicitly **native, not-yet-hardened**
surface: `Seal.h`'s own header calls its AEAD+KDF core "the headline Sentinel-rewrite target"
(it maps onto `std/security`'s machine-verified constant-time ChaCha20-Poly1305 + SHA-256), which
makes it an FR-15 hardened-surface-coverage candidate that is currently uncounted because it is
unenumerated. **EXPERIENCE** owns the two menu flows and the password-dialog states.

---

### GAP-B5 — File associations

**In no artifact.**

**Where it lives.** `src/core/FileAssoc.h` (56 lines). Per-user `HKCU\Software\Classes` — no admin —
made effective immediately by `SHChangeNotify(SHCNE_ASSOCCHANGED)`. Two ProgIDs:
`SentinelIDE.Project` for `.sntproject` and `SentinelIDE.Source` for `.sentinel`, each with
`DefaultIcon = "<exe>",-100` / `-101` (negative = embedded resource id: the app icon and the
`.sentinel` page-plus-padlock icon) and `shell\open\command = "<exe>" "%1"`, plus `OpenWithProgids`
entries. Registered from `≡ ▸ Register File Associations…` (`MainWindow.cpp:1783`) and mirrored by
the installer's `[Registry]` section (GAP-B3).

**The user-visible behaviour that is written down nowhere:** a double-clicked file does **not** open
in isolation — `openPathArg` (`MainWindow.cpp:1190`) walks up at most 16 levels (`:1195`) to the
first folder `hasProject()` accepts and opens **that project**, falling back to the file's own folder.
A manifest opens the project landing in its active target's entry source.

The header also records the honest limit: if the user has an explicit per-extension shell
`UserChoice`, Windows honours that over our Classes default — by design.

**Owner: PRD §7 Platform**, alongside the installer — both are "how the product meets the OS".
**EXPERIENCE** should own the one menu item and, more importantly, the nearest-enclosing-project open
rule, which is a real interaction and currently exists only as a code comment.

---

### GAP-B6 — Unsaved-changes guard

**Partly specified, and much narrower than what shipped.** Two fragments exist: PRD **NFR-REL-1**
("Unsaved edits are never lost across build, run, or focus change") and EXPERIENCE's Component
Patterns **Tabs** row ("Closing a dirty tab prompts to save"). Neither describes a three-answer
prompt, a choke point, or the ten paths it guards — and NFR-REL-1's three named events are not the
three that matter.

**Where it lives.** `src/host/win32/SaveChangesDialog.{h,cpp}` — a themed modal returning
`SaveChoice{Save, Discard, Cancel}`, where Esc and the close box both mean **Cancel** — behind one
function: `confirmSaveIfDirty(hwnd, action)` (`MainWindow.cpp:659`). It returns true only when it is
safe to proceed, and a **failed write does not discard the buffer** (`SaveChoice::Save` returns
`saveFile`'s result, not `true`).

**Ten call sites, every discarding path:** opening another file (`:671`, which is itself how the
tree, a Problems row, an Output link and `gotoLineCol` all reach it), opening another project
(`:778`), New File (`:824`), New Project (`:882`), switching target (`:1246`), Project Settings over
a dirty manifest (`:1264`), Close Project (`:1299`), Seal Project (`:1337`), Open Sealed Project
(`:1383`), and `WM_CLOSE` (`:1851`) — which `≡ ▸ Exit` reaches by posting `WM_CLOSE` (`:1832`) rather
than duplicating the check.

**Two behaviours no artifact predicts.**

- **Build does not prompt — it auto-saves** (`runBuild`, `:1113`, logging "Auto-saved … before
  build"). That, not a prompt, is how NFR-REL-1's "never lost on build" is actually met.
- **An unattended update install saves without asking** (`:1849–1851`): when
  `updaterShutdownPending()` is true, WinSparkle has already requested shutdown and force-exits three
  seconds later, so a prompt would go unanswered and the watchdog would kill the process with the
  edits still only in the buffer.

**Owner.** **EXPERIENCE** — a State Patterns row for the three-answer prompt, and an Interaction
Primitives note that **Cancel aborts and changes nothing**; the Tabs row's one-liner is a subset and
should point at it. **PRD** should restate NFR-REL-1 to match what is enforced — auto-save on build,
a guard on every discarding path, and save-without-asking during an unattended update install —
instead of the narrower "build, run, or focus change". **DESIGN** needs nothing; it is
`{components.dialog}`.

---

### GAP-B7 — Single instance + drag-drop

**In no artifact.**

**Where it lives.** `src/host/win32/SingleInstance.h` (133 lines) + `WinMain.cpp`.

- `acquireSingleInstance()` (`SingleInstance.h:90`) takes a named mutex keyed on an FNV-1a hash of
  the **lowercased exe path**, so a dev build in `build\` and an installed copy are separate
  instances — without that, testing a local build would hand its argv to whatever release the user
  has installed.
- A second launch calls `handOffToRunningInstance(firstPathArg())` (`:105`), which finds the window
  by class name via **`EnumWindows`** (not `FindWindowW` — measured returning NULL for the same
  window, and enumeration is needed anyway to pick the right one among several copies), compares exe
  identity by **volume serial + file index** rather than path spelling (a mapped network drive
  reports `G:\…` one way and a UNC path the other, for the same file), calls
  `AllowSetForegroundWindow`, and sends `WM_COPYDATA` under a 5-second `SendMessageTimeoutW`.
  **Any** failure falls through to a normal launch: an extra window is a nuisance, a swallowed
  double-click is a bug.
- Drag-drop: `DragAcceptFiles(hwnd, TRUE)` (`MainWindow.cpp:1886`) → `WM_DROPFILES` (`:1550`), which
  takes only the **first** dropped path — deliberately, because each open replaces the single buffer,
  so five dropped files would mean four guard prompts and only the last survivor.
- Both converge on `requestOpenPath` (`:1179`) → a posted `WM_APP_OPEN_PATH`, which refuses to act
  while `uiIsBusy()` (a modal, a tracking popup menu, or a splitter drag) and retries on a 4-second
  timer, and drops the request entirely if an update install is pending. `WM_COPYDATA` is a *sent*
  message dispatched inside every modal's null-filter `GetMessageW`, which is exactly why the work is
  posted rather than done inline.

**A dependency worth recording.** The "first path only" rule is a **direct consequence of GAP-A3
being unbuilt**. If tabs ever ship, that rule should change with them — so whichever artifact owns
this must say *why* it is one path, not just that it is.

**Owner.** **EXPERIENCE** — the IA has no statement of how the app is entered from outside (argv, a
shell double-click, a drop), and Interaction Primitives has no "drop a file on the window". **PRD §7
Platform** for the single-instance requirement itself, next to the installer and the file
associations.

---

## 3. Verification

Every entry above was checked against the source file and line cited, in the working tree at
v0.1.7 / phase 42, not against another document. Specifically verified for this register:

- `struct Diag` has no severity field (`MainWindow.cpp:76`); the Problems list has three columns
  (`:216–218`); severity is computed for Output-pane colour and discarded (`:1657–1659`).
- No occurrence of squiggle, wavy-underline or shield rendering anywhere in `src/`; the gutter paints
  line numbers only (`:334–352`); `markErrorLines` (`:528`) tints whole lines in one colour.
- The complete accelerator table is nine entries (`:1917–1928`) — no `Ctrl+P`, `Ctrl+F`, `Ctrl+H`,
  `Ctrl+G`, `F8`.
- No `EM_FINDTEXTEXW`, `FINDREPLACE`, `FindText`, or multi-caret code in `src/`.
- The tab strip is four lines of painting over four single-file globals (`:321–326`).
- Seven modal dialogs exist; none is a toolchain-readiness dialog. `Toolchain.h` contains exactly
  `findVcvars` + `captureMsvcEnv`, and `runBuild` emits one MSVC line (`:1130` / `:1132`).
- `gotoLineCol` exists and works (`:1150`) — the go-to-line mechanism ships without the command.
- Part B: `Logger.h` and its Settings wiring, including the Reveal button
  (`SettingsDialog.cpp:66/145/148/149`) and live reconfigure (`MainWindow.cpp:1495`, `:1817`);
  `Updater.cpp` constants, feed URL, poll interval, gating and installer hand-off; the `.iss`
  `[Setup]`/`[Files]`/`[Icons]`/`[Registry]` sections and its version derivation; `Seal.h`'s v2
  layout, iteration count and menu wiring, plus the 25 assertions in `tests/seal_test.cpp`;
  `FileAssoc.h`'s ProgIDs and `openPathArg`'s 16-level walk-up (`:1190–1195`); all ten
  `confirmSaveIfDirty` call sites and the updater special case (`:1849–1851`); `SingleInstance.h` and
  `WinMain.cpp`'s hand-off, `WM_DROPFILES` (`:1550`) and `WM_COPYDATA` (`:1566`).

**Deliberately not listed.** Accuracy over completeness: anything not read in source is absent from
this register even where a document claims it. In particular —

- Items whose gap is **wrongness, not absence** (signing model, project/manifest model, targets,
  tiers, the scheme selector, the build command, versioning) are in `REALITY-DELTA.md`, not here.
- **Hardened-surface coverage in About** (RD-16) is a partial: the LOC mix ships, the coverage figure
  does not. It is a rewrite of one FR-17 clause rather than a missing feature, so it stays marked in
  place in the PRD and is not given a Part A entry.
- **Performance NFRs** (NFR-PERF-1..6 — keystroke latency, cold start, frame budget, the rope/B-tree
  buffer, the memory target) are unmeasured, not verifiably unbuilt. They need a measurement pass,
  not a register entry, and are excluded rather than guessed at.
- The **live signing defects** — the post-build path reporting `[signed · …]` off an exit code
  without re-verifying, and `SignState::Unknown` painting as `⊘ Unsigned` — are **defects in shipped
  behaviour**, not spec gaps. They belong in the PRD's re-filed adversarial finding and in RD-05 /
  RD-06, and are not restated here.
