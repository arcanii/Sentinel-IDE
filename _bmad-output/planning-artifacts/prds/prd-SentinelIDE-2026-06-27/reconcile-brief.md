---
title: "PRD ↔ Sources Reconciliation: SentinelIDE"
status: review
created: 2026-06-28
inputs:
  - briefs/brief-SentinelIDE-2026-06-27/brief.md
  - briefs/brief-SentinelIDE-2026-06-27/addendum.md
  - prds/prd-SentinelIDE-2026-06-27/research-ide-landscape.md
target:
  - prds/prd-SentinelIDE-2026-06-27/prd.md
  - prds/prd-SentinelIDE-2026-06-27/addendum.md
---

# PRD ↔ Sources Reconciliation — SentinelIDE

Purpose: catch material content or qualitative intent in the brief / brief-addendum / research that the PRD's FR structure silently dropped or weakened — and flag anything the PRD over-claims relative to its sources.

Method: full read of all five files; targeted cross-grep of load-bearing phrases. The PRD is structurally strong on the *functional* mechanics (the diagnostics triad, streamed off-thread builds, the C-rule, toolchain readiness all survive translation well — see "Correctly carried" below). The losses are concentrated in **qualitative/strategic framing and a few named research table-stakes**.

---

## GAPS — intent or content the PRD drops or weakens

### G1 — "The app is the argument" framing is gone (qualitative, high-value)
- **Source:** brief § What Makes This Different — *"The app is the argument. Most security tools describe safety; SentinelIDE is safe by construction…"* This is the brief's single sharpest strategic line: the product **is** the proof, not a vehicle that carries a proof.
- **PRD:** the phrase appears nowhere. The PRD reframes the four ends as a flat list ("a proof… a forcing function… a migration playbook") in §1 and a JTBD in §2.1, but the *argument-by-construction* thesis — that the artifact and the claim are one — is never stated as such. §13 "Why Now" comes closest ("The proof and the tool are the same artifact") but states it as timing, not as the core differentiator.
- **Why it matters:** this is the positioning spine for the security-buyer audience. Flattened into requirements, the "we don't describe safety, we are safe" punch is lost.
- **Fix:** restore an explicit framing sentence in §1 or §5, e.g. carry "safe by construction — the exposed surfaces structurally cannot carry the bug classes that dominate real incidents."

### G2 — The "give comfort to CISOs" tone / trust payoff is dropped (qualitative, high-value)
- **Source:** brief closes twice on this — *"the reason a CISO trusts the language with the code that matters"* (§ Executive Summary echo + § Vision final line), plus *"Anie's first serious dogfood and its sharpest credibility argument."* The emotional register is reassurance to a risk-averse buyer.
- **PRD:** the words **CISO, comfort, trust, dogfood, credibility** appear **nowhere**. The proof-audience JTBD (§2.1) is reduced to a clinical *"Convince me Sentinel can build real, security-critical software."* The differentiator phrase from the brief — *"provably can't be turned against you"* — is also absent; only a faint functional echo survives at §2.1 ("confidence the tool won't betray the code").
- **Why it matters:** the brief's whole go-to-market thesis is "this market buys on proof, not promise" and the proof must *feel* reassuring (assurance, not noise). The PRD keeps the tone instruction for *product-generated text* (§9: "precise, calm, non-alarmist") but loses the same tone as a **strategic stance toward the buyer**.
- **Fix:** add the buyer-trust framing to §2.1 (proof audience) and/or the §1 vision — name the CISO and the "comfort / trust" payoff.

### G3 — Migration-playbook / case-study angle is named but flattened to a clause (weakened)
- **Source:** the brief and addendum treat the native→Sentinel migration as a **headline deliverable**: *"It carries a migration playbook… a reference implementation for the brownfield hardening every regulated org actually faces — not a greenfield fantasy"* (brief § Different); *"the migration history is itself a deliverable (the case study)"* (addendum § Build posture mechanics); and the Vision's *"canonical playbook regulated organizations follow to harden their own native codebases."* Keywords: **brownfield, legacy, case study, canonical, reference implementation.**
- **PRD:** "migration playbook" survives as **one clause** in §1 and the FR-16/§10 co-evolution machinery, but: **brownfield, legacy, case study, reference implementation, canonical** appear **nowhere**, and there is **no FR or deliverable** for producing the migration history / case-study artifact. The A-destination is tracked only as the numeric "% native" trend (FR-15) — the *narrative playbook* (the thing regulated orgs would actually follow) has no home. The addendum explicitly says the migration history IS a deliverable; the PRD has no requirement that captures it.
- **Why it matters:** one of the four stated reasons-to-exist is reduced to a metric. The brownfield-vs-greenfield contrast (a deliberate market differentiator) is lost entirely.
- **Fix:** add an FR or §10 deliverable for the migration-history/case-study artifact (not just the % number); reintroduce "brownfield" / "reference implementation" language.

### G4 — SBOM dropped; reproducible builds dropped from the supply-chain story (research table-stakes, content)
- **Source:** research §4 lists four supply-chain moves explicitly: SLSA provenance, **Sigstore/Cosign signing**, **reproducible builds**, and **"SBOM shipped with releases."** Its "cheapest credible v1 wins" = signed+provenance releases and a designed-in plugin sandbox; reproducible builds flagged as roadmap.
- **PRD:** SEC-3 carries signing + provenance (Sigstore/Cosign + SLSA) and the sandbox posture — good. But **SBOM is absent entirely** (the word appears nowhere), and **reproducible builds** are absent (research called them out by name, even if only as a roadmap differentiator). For a product whose entire thesis is supply-chain-apex trust, silently dropping SBOM — a standard regulated-buyer ask — is a notable omission.
- **Why it matters:** the proof audience (banks/gov/regulated) routinely requires an SBOM; leaving it unmentioned weakens the very supply-chain credibility the product is built to claim.
- **Fix:** add SBOM (and a one-line reproducible-builds roadmap note) to SEC-3 / §5, even if flagged roadmap like the rest of SEC-3.

### G5 — Open sub-decisions from the addenda are silently resolved without flagging (minor, traceability)
- **Source — brief addendum § Open sub-decision:** "First hardened surface = untrusted-input parsing (default) **vs secret-handling** … *user may flip*." It is explicitly an open, flippable default.
- **PRD:** treats untrusted-input as **settled** (FR-13, SM-2 "100% binary", §12) with no note that the brief left it as a flippable default. Not wrong (the brief did default to it), but the PRD's Open-Questions list (§16) omits it — a reader can't tell it was ever a decision. Similarly, the **platform/toolchain OPEN RISK** the brief-addendum flagged as "reconcile first / gates the v1 timeline" ("does `snc.exe` build *arbitrary* `.sentinel` projects on Windows today, or only a subset?") is **never carried into §16 as an open question** — the PRD records the *linker* gap (§14, FR-12) but not the broader "is arbitrary-project compilation real on Windows yet?" question the addendum said must be resolved before committing a v1 timeline.
- **Fix:** add the "arbitrary `.sentinel` compiles on Windows today?" item to §16 (it is the addendum's only time-sensitive blocking risk); optionally note the first-surface default as resolved-but-flippable.

---

## Correctly carried (translated well — not gaps; noted to scope the review)

- **Diagnostics triad** — research §1's explicit "PRD gap authors miss" (Problems list ↔ inline squiggle ↔ output-pane-click sharing ONE model) is carried faithfully as FR-8/FR-9/FR-10 + the §3 Glossary "triad" + the addendum's precedence rule (build supersedes keystroke). This is the strongest translation in the doc.
- **Off-thread / streamed (not batched) builds** — research §1/§3 → FR-4 (child process off UI thread), FR-5 ("live, line by line, not batched at exit"), and the hard NFR-PERF-3. Fully preserved, correctly marked a hard requirement rather than an assumption.
- **Supply-chain apex / threat triad** — brief's "executes untrusted code / holds secrets / supply-chain apex" → PRD §5 threat model and §1, intact.
- **C-rule / B-path / A-destination + exposed-surface taxonomy** — carried into §5, FR-13/14/15, Glossary, and the PRD addendum's C-ABI mechanism. Faithful.
- **NFR numbers** (keystroke <10ms, cold-start <1s, 120FPS, large-file 20k cap, rope buffer) — carried with honest `[ASSUMPTION]` tagging and an Assumptions Index. Good.
- **Tone for product text** — research/brief "assurance, not noise" → §9 "precise, calm, non-alarmist." (Carried for *product output*; see G2 — lost as a *buyer-facing strategic stance*.)

---

## OVER-CARRY — PRD claims not supported by the sources

Minor; nothing material invented, but two items go beyond the inputs:

- **OC1 — "<300 MB" memory budget (NFR-PERF-6).** The PRD states a concrete `< 300 MB` target. Research §3 only says "well under the Electron baseline (~1–1.5GB)… set a v1 budget" — it gives **no specific number**. The 300 MB figure is PRD-invented; it is tagged `[ASSUMPTION]`, so this is acceptable, but it is *not* "research-grounded" the way the §17 Assumptions Index implies (the index claims the perf targets including <300MB are "research-grounded inferences" — the 300 number is an inference with no source anchor).
- **OC2 — FR-7 "Run the built executable."** The brief's v1 scope says "Build & run via the local `snc.exe`; output and errors captured." Running the produced binary (separate exit-code/stdout capture, UJ-1 climax) is a reasonable reading but slightly expands "build & run" into a discrete run-the-artifact feature the brief states more loosely. Low concern — consistent with intent.

---

## Bottom line

The PRD is a **faithful functional translation** — the mechanics survive (triad, streamed builds, C-rule, toolchain readiness). What it **silently drops is the qualitative/strategic top layer**: the "app is the argument" thesis (G1), the CISO-comfort/trust tone (G2), and the migration-**playbook-as-case-study** deliverable (G3) — three of the brief's sharpest differentiators reduced to a list item or a metric. Plus one research table-stake (**SBOM**, G4) and the addendum's **time-sensitive Windows-compilation open risk** (G5). Recommend a §1/§5 framing pass to re-inject G1–G3, a one-line SEC-3 addition for SBOM, and a §16 entry for the Windows-compilation question.
