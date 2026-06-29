---
title: "Adversarial Review — SentinelIDE PRD + Addendum"
status: review
created: 2026-06-28
reviewer: "Adversarial Reviewer (bmad-review-adversarial-general)"
targets:
  - prd.md
  - addendum.md
verdict: "CONDITIONAL — strong thesis, honest skeleton, but the central proof claim is smaller than the marketing implies and the v1 is gated on a toolchain the project does not control."
---

# Adversarial Review: SentinelIDE PRD + Addendum

**Role:** Cynical reviewer. Posture: this was handed over by someone who wants a yes, so the job is to find where the yes is unearned. I read the PRD, the addendum, the brief, the brief-addendum, the research digest, the memlog, and I checked the empirical claims against the actual `G:\Sentinel-lang` repo.

**One concession up front, so the rest lands as fair and not reflexive:** the §14 / FR-12 toolchain finding is *real*. I verified it. `snc.exe` and `sentinel_runtime.lib` (14 MB) exist; `libsentinel_runtime.a` does not; `crates/sentinel-driver/src/main.rs` L2011–2020 does branch `sentinel_runtime.lib` (MSVC) vs `libsentinel_runtime.a` (Unix); the README pins `aarch64-apple-darwin` + LLVM 18. This document does not invent its evidence. That is rarer than it should be and it buys the author credibility. It does **not** buy a pass on everything below.

---

## OVERALL VERDICT

**CONDITIONAL.** The document is intellectually honest in its bones — it tags assumptions, names its own blocking risk, and refuses a fake % native ceiling. But three things undermine it as a *decision-ready* PRD:

1. **The headline security claim is rhetorically inflated relative to what v1 actually ships.** "Every surface that touches untrusted bytes is Sentinel" (brief §The Solution, echoed in PRD §1) is not what v1 delivers. v1 delivers *one* surface — untrusted-input parsing — behind a C-ABI boundary, hosted by a native Win32/C++ process that is itself the largest attack surface in the binary and is exempted by definition. A skeptical CISO does not buy "the chrome is security-irrelevant" as an axiom; that is the very thing under review.

2. **The whole edit→build→run loop — the Tool pillar, SM-1, UJ-1 — is gated on a toolchain the project does not own and cannot currently make work end-to-end.** The PRD's own §14 confirms build→link is not turnkey. FR-12 is a deflection, not a fix: it converts "the IDE can't build your code" into "the IDE politely explains why it can't build your code." That is a real feature, but it is not the same as a working build loop, and the PRD's success metrics quietly assume the loop works.

3. **Most FR "consequences" are demonstrability statements, not pass/fail tests.** They tell you what a passing system looks like; they rarely tell you the input, the threshold, or the failure condition. A QA lead cannot write a failing test from several of these without inventing the missing half.

Ship-readiness of the *PRD*: not yet. It needs (a) the marketing language reconciled to the v1 surface, (b) the native-host attack surface explicitly named and risk-accepted rather than defined away, (c) the [ASSUMPTION] perf targets either confirmed by Bryan or demoted out of the success metrics, and (d) testable FRs that name thresholds.

---

## TOP FINDINGS (ranked by how much a sharp stakeholder would press)

### F1 — "Every surface that touches untrusted bytes is Sentinel" is overstated; the native host is the elephant. (§1, §5, §4.5 FR-14, SEC-1)

The thesis sentence — "the surfaces that touch untrusted bytes are written in a language that structurally forbids the bug" (§1) — is the product's reason to exist, and it leaks.

- **The native host processes untrusted-ish input that is not in scope.** §3 defines the native host as "window, message loop, and rendering," and §3/§5 classify all of that as "Chrome" → security-irrelevant → may stay native. But the Win32 message loop ingests OS-delivered input events, clipboard payloads, drag-and-drop data, IME composition strings, file-drop paths, and `WM_COPYDATA`. Several of those are classic memory-corruption and injection vectors and at least some are attacker-influenceable (a malicious file dropped onto the window; clipboard contents pasted from a hostile source; a crafted path). The PRD asserts by *definition* that this is security-irrelevant. That is assuming the conclusion. A CISO's first question — "what parses the bytes the OS hands your window proc?" — has no answer here except "we declared that out of scope."
- **The C-ABI boundary itself is an unhardened seam, and it is in the native side.** Per the addendum, the native host links a Sentinel-compiled C-ABI static library and calls across the C ABI. Marshalling untrusted bytes from C++ into the Sentinel parser — buffer lengths, pointer ownership, encoding, lifetime — happens in *native* code. A use-after-free or length-confusion bug on the C++ side of that call corrupts memory *before* the Sentinel guarantees ever apply. So the literal claim "the parsing of untrusted bytes is Sentinel code" (FR-13 consequence) is true only of the parse *interior*; the *ingestion and handoff* are native and unhardened. The PRD never acknowledges this seam as a surface.
- **SEC-1 is therefore not verifiable as written.** SEC-1: "zero non-Sentinel parsing of attacker-influenceable bytes." To verify, you must enumerate every native code path that touches attacker-influenceable bytes and prove none of them *parse*. But "parse" is undefined (does length-prefix decoding count? UTF-8 validation at the boundary? path canonicalization in the file-open dialog?), and the native host demonstrably touches such bytes. SEC-1 is currently an assertion of faith dressed as an NFR. **Fix:** define "parsing" precisely, enumerate the native input paths in FR-14's "documented boundary," and either bring the risky ones into Sentinel or explicitly risk-accept them with a named owner — do not define them away.

### F2 — The B-path/A-destination is credible as *direction* but the PRD hand-waves the hardest leg, and "no fixed v1 ceiling" removes the only thing that would have made A-destination falsifiable. (§7, §4.5 FR-15, §10, OQ-6, §11)

The feasibility question the brief itself raises — a GUI IDE in a language whose native-GUI/FFI is "in progress" — is answered with "B-path decouples shipping from language readiness" (§14). That is true and sensible *as far as it goes*. But:

- **The brief's own milestone ladder puts "native window rendering text" at Rung 1 and says it forces Sentinel to gain "FFI + native GUI bindings."** The PRD then says the native host stays native in v1 (§11) and the footprint "shrinks from v2." So in v1, essentially *all* GUI is native and the Sentinel share is one parser. That is a defensible v1 — but the document oscillates between "honest small beachhead" (good) and grand thesis language (§1, brief §What Makes This Different: "its exposed surfaces structurally cannot carry the bug classes that dominate real incidents"). Pick one. The grand version is not what v1 is.
- **OQ-6 "resolved: no fixed % native ceiling" is the wrong resolution for a proof.** A-destination is the headline ongoing metric ("native trends toward zero"). By refusing *any* numeric commitment — no v1 ceiling, no per-release floor of improvement, no target slope — the PRD makes A-destination **unfalsifiable**. "Trends down" with no rate is satisfied by shaving 0.1% a year forever. SM-C1 ("do not grow % native") only forbids regression; it does not compel progress. So the single metric that proves the "shrinking debt" story cannot fail. A skeptic reads "no fixed ceiling" as "no accountability." **Fix:** commit to at least a directional rate or a v2 target band, or admit A-destination is a narrative, not a metric.
- **FR-15's mix computation is undefined and gameable.** "Share of first-party IDE source that is Sentinel vs native" — measured how? Lines of code? Then a verbose native UI framework vs. a terse Sentinel parser produces a number that says nothing about *surface* coverage, which is what the security story actually depends on. 18%/82% by LOC could be 100% surface coverage or 5%. The number surfaced in the About dialog (FR-17) is thus security-theater unless "mix" is defined as *security-relevant surface* coverage, not raw source proportion. The PRD conflates "how much of the code is Sentinel" with "how much of the *danger* is Sentinel" — and only the latter matters to the thesis.

### F3 — The Tool pillar (SM-1, UJ-1, FR-4..FR-7) is built on a build loop the project cannot currently complete, and FR-12 launders that gap. (§14, FR-12, §4.2, SM-1, addendum "Windows toolchain finding")

This is the feasibility knife. The PRD's own §14 and the addendum confirm: on Windows today, `snc` **compiles but does not link to an executable** — it expects `libsentinel_runtime.a` and the MSVC build produced `sentinel_runtime.lib`. I confirmed this against the repo.

- **So "Build & run via local `snc.exe`" (§12.1, FR-4, FR-7) — the "single most important loop" (§4.2) — does not work end-to-end on the target platform as of this PRD.** FR-12 "detects & guides," but guidance to *what*? The remediation requires either the user to obtain a GNU/clang linker + a matching runtime archive that the project admits isn't being shipped, or the language team to fix gap #1. The PRD presents FR-12's happy path ("Once remediated, build→run succeeds") as if remediation is a known, documented, copy-pasteable procedure (UJ-3 climax: "without a web search"). **It isn't yet** — the addendum's own gap #1 asks the language team to *create* the turnkey path. You cannot ship a "copy-pasteable remediation" (UJ-3) for a fix that does not yet exist. UJ-3 is currently fiction dressed as a user journey.
- **SM-1 ("core Sentinel devs adopt it as daily driver within 4 weeks of v1") silently assumes the loop works.** A daily driver whose Build button frequently lands on a toolchain-readiness wall is not a daily driver. The 4-week window is also `[ASSUMPTION]` (§17) — so the primary Tool success metric is an unconfirmed number layered on an unproven capability. **Fix:** SM-1 must be made conditional on, or sequenced after, gap #1's resolution; and the PRD should state plainly that v1's build loop is *contingent on a language-team deliverable outside this project's control*, with a fallback if gap #1 slips.
- **Minor but telling:** the addendum says the shipped `snc.exe` "is resolving the GNU/clang name." The actual source (L2011–2020) shows a *correct* `cfg`-style MSVC-vs-Unix branch. So the real failure is more likely a build-configuration / target-triple mismatch (the shipped binary was built for a Unix-style target, or the runtime wasn't staged under the expected name), **not** that the driver "resolves the wrong name." This matters because FR-12's detection logic depends on correctly diagnosing *why* the link fails — and the addendum's stated cause may be wrong. Get the diagnosis right before building "guided remediation" on top of it.

### F4 — The [ASSUMPTION] perf targets are load-bearing for two success metrics and a counter-metric, yet none are confirmed and several are aspirational for a v1 on an immature toolchain. (§6, §15 SM-4, SM-C2, §17)

§17 is admirably honest that NFR-PERF-1/2/4/5/6 are "research-grounded inferences, not Bryan-supplied." But honesty about a gap does not close it:

- **SM-4 (a success metric) and SM-C2 (a counter-metric) both depend on numbers nobody has committed to.** "Keystroke latency and cold-start meet NFR-PERF-1/2" — but NFR-PERF-1 (<10ms, target ~2ms) and NFR-PERF-2 (<1s cold start) are borrowed from Zed (mature Rust + custom GPU stack, years of work) and VS Code. SentinelIDE is a *first* native host calling across a C-ABI into a Sentinel parser, on a toolchain whose Windows build doesn't link yet. Inheriting Zed's ~2ms aspiration is borrowing the destination's numbers for the starting line. **At minimum these must be re-baselined for "v1 on this stack," not "best-in-class native editor."**
- **NFR-PERF-3 is correctly marked a hard requirement** (off-thread builds) — good, that one's an architecture property, not a number. But the surrounding soft targets being entangled into *measured success/counter metrics* means the project can "fail" SM-4 for missing a number it never agreed to. Either Bryan confirms the numbers (converting them from [ASSUMPTION] to spec) or SM-4/SM-C2 drop out of the metrics and become non-binding aspirations. You cannot grade against a placeholder.
- **NFR-PERF-6 (<300 MB) and NFR-PERF-4 (120 FPS / ≥60 sustained)** are stated with false precision for a product with zero implementation. They read as borrowed marketing, not derived budgets.

### F5 — Most FR "Consequences (testable)" are not testable; they're demonstrations. (§4.1–§4.7 throughout)

The label "(testable)" appears repeatedly but the content frequently isn't a pass/fail condition:

- **FR-1:** "Edits to a 10,000-line file remain responsive (see NFR perf)." "Responsive" = which metric, what threshold, measured how? Without binding to a *number* this is unfalsifiable. (And it points at NFR-PERF, which is itself [ASSUMPTION] — F4.)
- **FR-1:** "Unsaved edits are never lost on build or focus change." *Never* across what event set? Crash? Power loss? OS-forced close? "Never" is untestable without an enumerated event list; NFR-REL-1 repeats the same unbounded "never."
- **FR-2:** "Highlighting updates incrementally ... without a full-file re-parse stall `[ASSUMPTION: incremental/Tree-sitter-style]`." The *consequence* is gated on an assumption about the *mechanism* — if the mechanism assumption is wrong, is the FR failed or just re-mechanized? Conflates requirement with implementation.
- **FR-11:** "At minimum, compile errors are surfaced; the `secret`/borrow/effect set is the v1 target." A requirement with two different bars ("minimum" vs "target") is a requirement you can always claim to have met. Which is the v1 *acceptance* bar? If a secret-leak diagnostic does **not** surface in v1, is FR-11 met or not? UJ-2 (the entire "headline guarantee shows up in the editor" moment, realizing FR-8..FR-11) *requires* the secret-leak diagnostic — yet FR-11 lets v1 ship with only generic compile errors. The flagship journey and the FR's floor contradict each other.
- **FR-14:** "review confirms no Surface crosses into native code." "Review confirms" is a human eyeballing, not a test. And per F1, the native host *does* touch attacker-influenceable bytes — so either the review will fail, or "Surface" has been defined narrowly enough to pass by construction. **Fix:** convert these to: input + action + observable threshold + failure condition.

---

## SECONDARY FINDINGS (still real, lower blast radius)

### F6 — Reference integrity is broken in §0. (§0)
§0 says it builds on three inputs "in `../briefs/brief-SentinelIDE-2026-06-27/`": the product brief, the brief addendum, and "this run's `research-ide-landscape.md`." I checked: the brief and brief-addendum *are* in that briefs folder, but **`research-ide-landscape.md` is not** — it lives in the *PRD* folder (`prds/prd-SentinelIDE-2026-06-27/`). The sentence files all three under the briefs path. A downstream architect following the citation to the briefs folder will not find the research file. Small, but it's exactly the kind of sloppiness that erodes trust in a document whose whole pitch is rigor.

### F7 — "Working title — confirm" and "[NOTE FOR PM]" left in a document headed downstream. (§0 title line, §11, §12.2)
The product name is still unconfirmed ("SentinelIDE — Working title — confirm"), and §11/§12.2 carry live `[NOTE FOR PM]` editorial markers about debugging being "emotionally load-bearing." These are author-to-author notes. Shipping them into the UX/architecture/epics consumers means either (a) the name churns after architecture starts, or (b) the notes get treated as requirements. Resolve or strip before handoff.

### F8 — The "supply-chain apex" threat is asserted, then almost entirely deferred. (§5 threat #3, SEC-3, §11)
§5 names supply-chain integrity as one of the three exposures that *justify the entire product* ("compromise here propagates downstream"). Then SEC-3 (signing, provenance, SLSA/Sigstore) is `[ASSUMPTION] (roadmap)`, the plugin sandbox is deferred (§11), and the IDE's own build/update channel — explicitly listed as a surface in the brief-addendum's exposed-surface map — appears nowhere in v1 scope. So two of the three pillars that motivate the product (secrets, supply-chain) are *out of v1*, and the threat model is doing rhetorical work ("we sit on the scariest threat model") that the v1 scope does not cash. The honest framing: v1 addresses **one-third of the stated threat model** (untrusted input). The PRD should say that in §5, not bury it in scope sections.

### F9 — The forcing-function deliverable (FR-16) is a list of one. (FR-16, SM-3, §10)
The Language-gap list is a headline pillar (Forcing function, SM-3) and a whole user journey (UJ-4, "the IDE has paid for itself as a roadmap input"). Its sole concrete content is gap #1, which — by the project's own account (memlog, addendum) — was found *before the IDE was built*, by manually running `snc` at a shell. So the "forcing function" has, to date, produced zero gaps *via the act of building the IDE in the IDE*; it produced one gap via ordinary command-line poking. SM-3's target ("maintained from v1, seeded with gap #1") is satisfied by a one-item list that predates the product. That is not yet evidence the forcing-function loop *works*; it's a seed and a promise. A skeptic notes the proof of pillar #3 is currently circular.

### F10 — "Not Electron" is positioning, not a requirement, and the perf justification leans on unconfirmed numbers. (§7)
"§7: Not Electron — the native-perf and security story depends on bypassing a browser engine." The *security* half of that claim is unsupported in the doc (an Electron app can also sandbox; the argument elsewhere is about Sentinel surfaces, not the absence of Chromium). The *perf* half rests on the [ASSUMPTION] NFRs (F4). So a foundational platform decision is justified by (a) a security claim the document doesn't substantiate and (b) numbers nobody confirmed. The decision may well be right — but the *justification* as written wouldn't survive a determined "why not Tauri/WebView2?" follow-up.

### F11 — SEC-2 measures the wrong thing to prove the security claim. (§5 SEC-2)
SEC-2: "the v1 surface compiles clean under `snc`'s safety checks." Compiling clean proves the Sentinel code passes Sentinel's checks — fine. It does **not** prove the surface is *complete* (that all untrusted-input handling actually routes through this Sentinel code rather than around it via the native host — see F1). You can have a Sentinel parser that compiles perfectly clean and a native code path that parses untrusted bytes beside it. SEC-1 is supposed to cover completeness, but SEC-1 is itself unverifiable as written (F1). So the two security NFRs together prove "the Sentinel part is safe" and assert-without-proving "there is no non-Sentinel part." The gap between those is the entire skeptic's case.

### F12 — Diagnostics reconciliation (FR-10) is a known-hard concurrency problem hand-waved into one bullet. (FR-10, addendum "Diagnostics reconciliation")
FR-10 requires fast on-keystroke diagnostics and authoritative on-build diagnostics to share one model and "reconcile so a build does not duplicate live squiggles." The addendum gives this one sentence ("build supersedes keystroke for overlapping ranges"). Range-overlap supersession across two asynchronous producers, with edits invalidating ranges mid-flight, is a genuinely fiddly state-machine problem (stale diagnostics after edits, races between a slow build and fast keystrokes, range remapping after text changes). It's fine for the PRD not to solve it — but flagging it as a one-line "define precedence" undersells a likely source of v1 bugs and user-visible flicker. The research digest even calls the triad-sharing-one-model the "common half-done state." The risk deserves a line in §14.

---

## WHAT'S ACTUALLY GOOD (so the verdict isn't dismissed as reflexive negativity)

- The §14 / FR-12 finding is empirically verified against the real repo. The evidence trail is honest.
- Assumptions are tagged inline and indexed (§17). The author is not hiding the soft spots — they're labeled. That is the right instinct and it's why this review can be precise instead of speculative.
- Counter-metrics (SM-C1/SM-C2) exist at all — most PRDs never name what *not* to optimize.
- The non-goals (§11) are unusually disciplined ("a boundary, not a backlog").
- The narrow honest claim *as stated in §1's second paragraph* ("v1 makes the narrow, honest claim true on day one") is the right altitude. The problem is the rest of the document — and the brief it inherits — keeps inflating past it.

---

## REQUIRED CHANGES BEFORE THIS PRD IS DECISION-READY

1. **Reconcile the marketing claim to the v1 surface.** Either soften §1/§5 and the inherited brief language to "v1 hardens the untrusted-input surface; secrets and supply-chain are roadmap," or expand v1 scope to match the grand claim. Stop letting "every surface" stand next to "v1 = one surface." (F1, F2, F8)
2. **Name the native host as an attack surface and risk-accept it explicitly.** Enumerate the native input paths (message loop, clipboard, drag-drop, file paths, C-ABI marshalling). Define "parsing" precisely so SEC-1 is verifiable. Do not classify untrusted-byte ingestion as "security-irrelevant" by fiat. (F1, F11)
3. **Make the build loop's external dependency explicit and add a fallback.** State that v1 build→run is contingent on gap #1 (language-team deliverable). Correct the addendum's mis-diagnosis of *why* the link fails before building FR-12's detection on it. Sequence SM-1 after the loop actually works. (F3)
4. **Confirm or demote the [ASSUMPTION] perf targets.** Get Bryan's numbers, or remove NFR-PERF-1/2/4/5/6 from SM-4/SM-C2 so success isn't graded against placeholders. (F4)
5. **Define "Sentinel/native mix" as security-surface coverage, not raw LOC** — and commit A-destination to at least a directional rate, or admit it's narrative. (F2)
6. **Convert FR consequences to real pass/fail** (input + threshold + failure condition). Resolve the FR-11-vs-UJ-2 contradiction: is the secret-leak diagnostic a v1 acceptance gate or not? (F5)
7. **Fix the §0 research-file path; strip `[NOTE FOR PM]` and "working title — confirm" before handoff.** (F6, F7)

**Bottom line:** the thesis — "the IDE is the proof" — *survives* contact with "it's mostly a native C++ host in v1," but only in its narrow form: *v1 proves Sentinel can own one dangerous surface (parsing) behind a C-ABI boundary.* It does **not** survive in its marketed form ("every surface that touches your data is Sentinel"), because the native host touches untrusted bytes and is exempted by definition rather than by argument, and because two of the three threat-model pillars are out of v1. The document is honest enough to know this — §1 para 2 says exactly the narrow thing — but it hasn't yet made the rest of itself, or the brief above it, tell the same small true story instead of the large impressive one.
