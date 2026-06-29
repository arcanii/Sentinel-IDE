# PRD Quality Review — SentinelIDE

## Overall verdict

This is a strong, unusually disciplined chain-top PRD: it has a real thesis (the IDE is simultaneously a useful tool, a proof, a forcing function, and a migration playbook, all justified by the IDE sitting on Sentinel's exact threat model), every choice is stated as a decision rather than smoothed to neutral, and the Glossary + ID discipline make it genuinely source-extractable downstream. The two things at risk are both in the dimension the rubric says to be unforgiving on — **Done-ness clarity**: FR-8 and FR-9 ship without the "Consequences (testable)" block that every other FR carries, and a handful of FR consequences lean on cross-section pointers ("see NFR perf, §6") rather than restating the bound inline. Mechanically the document is clean except one genuinely broken cross-reference (the Assumptions Index points the perf metrics at "§4.2", which is Build & Run, not the §6 NFR section).

## Decision-readiness — strong

A decision-maker can act on this. The build posture is declared **locked** up front (§0: B-path / C-rule / A-destination) and the PRD explicitly frames itself as turning that locked posture into requirements rather than re-litigating it — that is the right altitude for a chain-top doc. Trade-offs are named with what was given up, not just what was chosen: "**Not Electron** — the native-perf and security story depends on bypassing a browser engine" (§7); "no fixed v1 ceiling" on % native is repeatedly defended as a deliberate choice (§4.5, OQ-6, §10) rather than an omission. Counter-metrics are present and pointed (SM-C1: "Do **not** grow % native to ship features faster"; SM-C2: "Do **not** trade keystroke latency for feature count"), which is exactly where this product's pressure to cheat lives.

The Open Questions are mostly genuinely open and correctly typed: OQ-2 (debugger approach), OQ-3 (LSP seam), OQ-4 (extension sandbox), OQ-5 (which secrets first) are real unknowns routed to the right owner (architecture / later increment). OQ-1 and OQ-6 are marked "resolved" and OQ-5/OQ-3 as "narrowed", which is honest bookkeeping rather than rhetorical-question theater.

One soft spot: `[NOTE FOR PM]` callouts appear only at the debugger fast-follow (§11, §12.2). The rubric wants them "at real tensions." The most under-examined tension in the PRD — that v1 ships at a Sentinel/native mix the addendum's reasoning implies is heavily native (the example figure used throughout is "Sentinel 18% / Native 82%"), for a product whose entire pitch is "built in Sentinel" — gets no PM callout. The PRD defends "no ceiling" well, but it never flags for the PM that the proof claim is deliberately narrowed to *one* surface (untrusted-input parsing) while the headline reads "built in Sentinel." That narrowing is stated (§1 "the narrow, honest claim"), but the messaging risk it creates for the CISO audience is not surfaced as a decision.

### Findings
- **medium** Strongest tension not flagged for PM (§1, §4.5, §10) — v1 ships majority-native ("Sentinel 18% / Native 82%" used as the running example) while the product's identity is "built in Sentinel"; the honest narrowing to one hardened surface is stated but the buyer-messaging risk is never raised as a `[NOTE FOR PM]`. *Fix:* add a `[NOTE FOR PM]` at §1 or §10 naming the gap between the headline ("built in Sentinel") and the v1 reality (one surface hardened, mix majority-native), and how it should be positioned to the CISO audience.

## Substance over theater — strong

Very little furniture. The personas are three, not the rubric's red-flag "more than four," and each one drives content: the security buyer persona (§2.1) is the reason §5 exists at full depth; the language-team persona is the reason FR-16 and §10 exist; the developer persona drives FR-1..FR-11. None is decorative.

The differentiation is earned, not template-filled: the claim "an IDE sits on Sentinel's exact threat model — it executes untrusted code on every build, holds secrets, and is the top of a software supply chain" (§1, §5) is a real, specific argument for *why this product proves the language*, not a generic novelty boast. The Vision (§1) could not swap into another PRD — it is welded to Sentinel's specific guarantees and the four-ends framing.

NFR theater is mostly avoided because the numbers are concrete and attributed (NFR-PERF-1 "< 10 ms (target ~2 ms); verify with Typometer", NFR-PERF-6 "< 300 MB") and honestly tagged `[ASSUMPTION]` with a named provenance (research §3) and a named confirmer (Bryan). NFR-PERF-3 is explicitly marked "Hard requirement, not assumption," which is precisely the kind of distinction that separates real NFRs from boilerplate. No finding here.

## Strategic coherence — strong

The PRD has a thesis and bets the feature set on it. The thesis — "The proof and the tool are the same artifact" (§13) — is stated, and the prioritization follows from it rather than from ease. The single most important loop is named as such ("FR-4..FR-7 … The single most important loop", §4.2), and the security contract is called "The centerpiece" (§5) and given the most depth, which matches a thesis about proving a security language to security buyers.

Success Metrics validate the thesis rather than measuring activity: SM-2 measures the *proof* (untrusted-input surface 100% Sentinel + passes checks), SM-3 measures the *forcing function* (gap list produced), SM-5 measures the *A-destination trend*. Critically, the PRD resists the obvious vanity metric — there is no DAU/MAU; SM-1 is framed as preference/daily-driver adoption by the core devs, which is the engagement-quality signal the thesis actually needs. The MVP scope kind is coherent: it reads as a problem-solving + proof MVP, and §12 scopes to exactly the surfaces that make the narrow claim true. No finding.

## Done-ness clarity — adequate

This is the dimension to be unforgiving on, and it is where the PRD's otherwise-excellent discipline has gaps. Most FRs are exemplary: they carry an explicit **Consequences (testable)** block with verifiable conditions ("The invoked command line is shown to the user"; "Exit code is displayed; the run does not block the UI"; "clearing it clears all three"). The SEC NFRs are binary and checkable (SEC-1 "zero non-Sentinel parsing of attacker-influenceable bytes"). That is the right pattern.

But two FRs ship with **no Consequences block at all** — FR-8 (Inline squiggles) and FR-9 (Problems list) — breaking the pattern every neighbor follows. FR-8's body does smuggle one testable detail ("zero-width range squiggles the word at that position"), and FR-9 says "selecting one navigates to its source range," so they are not untestable — but a story author scanning for the **Consequences (testable)** anchor will find a hole exactly where two of the three triad members live, and the triad is flagged as the hard part (FR-10 reconciliation, addendum §"Diagnostics reconciliation"). Given how load-bearing the Diagnostics triad is, the inconsistency is more than cosmetic.

A second, smaller issue: a few consequences defer their bound to another section instead of restating it, which weakens "each section makes sense pulled out alone." FR-1's "Edits to a 10,000-line file remain responsive (see NFR perf, §6)" pushes the actual bound (the rope/B-tree O(log n) target in NFR-PERF-5) into §6; pulled out alone, FR-1 says only "responsive," which is the adjective the rubric warns about. FR-3's lone consequence ("The tree reflects on-disk structure; opening a node opens the file in a tab") is thin for an FR that also promises "a fuzzy open-file finder" — the finder has no testable consequence at all.

The one genuinely soft phrase to flag per the rubric's "flag every one" instruction: FR-12 / §9 use "actionable guidance" and "copy-pasteable remediation" — appropriate in tone, but there is no testable bound on what makes guidance "actionable" beyond "not a cryptic link error." For a v1-build-gating UX (§14 rates it medium, "gates v1 build/run UX"), an acceptance hook ("guidance names the missing artifact and the exact command to stage it") would harden it.

### Findings
- **high** FR-8 and FR-9 have no `Consequences (testable)` block (§4.3) — every other FR in the PRD carries one; the two missing ones are two of the three Diagnostic-triad members, the part the addendum flags as hardest. *Fix:* add Consequences to FR-8 (e.g., "a diagnostic at range R squiggles exactly R; a zero-width range squiggles the token at that offset") and FR-9 (e.g., "every Diagnostic in the model appears as exactly one Problems-list row; selecting a row moves the cursor to its source range; resolving the Diagnostic removes the row").
- **medium** FR-3 fuzzy finder has no testable consequence (§4.3, FR-3) — the FR promises a "fuzzy open-file finder" but the single consequence covers only tree-open. *Fix:* add a consequence for the finder (e.g., "typing a substring filters to matching `.sentinel` files; Enter opens the top match in a tab").
- **low** FR-1 defers its only quantitative bound to §6 (§4.1, FR-1) — "remain responsive (see NFR perf, §6)" reads as an adjective when pulled out alone. *Fix:* restate the bound inline (e.g., "edits to a 10,000-line file apply in O(log n) per NFR-PERF-5").
- **low** "Actionable guidance" lacks an acceptance bound (FR-12 §4.4; §9) — appropriate tone, but no testable definition of "actionable" for a v1-gating UX. *Fix:* add "guidance names the specific missing/mismatched artifact and the exact command(s) to stage it."

## Scope honesty — strong

Omissions are explicit and do real work. §11 Non-Goals is substantive and per-item reasoned, not a list of obvious exclusions: "Not a debugger in v1" carries its own deferral rationale and PM note; "Not GUI-in-Sentinel in v1 — the Native host stays native; the footprint shrinks from v2" ties the non-goal back to the A-destination thesis. §12.2 restates the MVP cut cleanly and consistently with §11 (no drift between the two scope statements — checked).

`[ASSUMPTION]` discipline is real: inferred values carry the tag inline and are indexed in §17, with a named confirmer (Bryan) and a named source (research §3 / §4). The PRD distinguishes assumption from hard requirement explicitly (NFR-PERF-3 "Hard requirement, not assumption"). De-scoping is proposed in the open (secret-handling "the next Hardened surface after v1", §12.2; OQ-5 narrows it to "a later increment").

Open-items density is appropriate for the stakes: 6 Open Questions (4 genuinely open, 2 resolved/bookkept), ~8 `[ASSUMPTION]` tags (all research-grounded perf/mechanism inferences, not hand-waving on core scope), 2 `[NOTE FOR PM]`. For a green-light-to-build chain-top PRD this is on the right side of the line — the open items cluster in deferred/post-v1 territory (debugger, LSP seam, extension sandbox, secret surface), not in the v1 build path. No finding.

## Downstream usability — strong

This PRD is built to be source-extracted, which is the right instinct for a chain-top that feeds UX → architecture → stories. The Glossary (§3) is thorough and the domain nouns are used identically across FRs, UJs, and SM definitions — "Surface", "Hardened surface", "Untrusted-input surface", "Sentinel/native mix", "Diagnostic model", "Toolchain readiness" all resolve to one definition and are used consistently (spot-checked "Untrusted-input surface" across §3, FR-13, SEC-1/2, §12.1, SM-2 — stable). The deliberate split of *capability* (PRD body) from *mechanism* (addendum) is exactly what the architecture phase needs, and the addendum cross-refs back by FR number ("realization of FR-13/FR-14").

ID continuity is clean: FR-1..FR-17 contiguous with no gaps or duplicates; UJ-1..UJ-4; SM-1..SM-5 plus SM-C1/SM-C2; SEC-1..SEC-3; NFR-PERF-1..6 + NFR-REL-1; OQ-1..OQ-6 — all contiguous. Cross-references overwhelmingly resolve (FR→UJ, SM→FR, FR→§, addendum→FR). Each UJ has a named protagonist carrying context inline (Devon the systems engineer for UJ-1/2/3, Priya the language lead for UJ-4) and ends with an explicit "Realizes FR-…" mapping — no floating UJs, and the realized-FR sets are accurate against the FRs they name.

The one genuine defect is mechanical (see Mechanical notes): the Assumptions Index entry for the perf metrics points at the wrong section. Because §17 is the architecture phase's roundtrip anchor, a wrong pointer there costs more than the same typo would in body prose — hence flagged rather than buried.

### Findings
- **medium** Assumptions Index mis-locates the perf NFRs (§17, line "§4.2 / NFR-PERF") — the perf metrics live in **§6 (Cross-Cutting NFRs)**; "§4.2" is *Build & Run* and contains no NFR-PERF entries. A downstream reader doing the assumptions roundtrip lands in the wrong section. *Fix:* change "§4.2 / NFR-PERF" to "§6 / NFR-PERF".

## Shape fit — strong

The shape matches the product. This is a multi-stakeholder product with meaningful UX (developers in the editor *and* security buyers evaluating the proof *and* the language team consuming feedback), so the rubric makes UJs with named protagonists load-bearing — and the PRD delivers exactly that, with four right-sized UJs that each have a climax and an FR mapping. It is not over-formalized (no UJ sprawl, no ceremony for its own sake) and not under-formalized (the security centerpiece §5 gets full depth because the buyer audience demands it). The chain-top obligations — Glossary discipline, ID continuity, source-extractability — are met (see Downstream usability). The capability-vs-mechanism split (body vs addendum) is the correct response to "this feeds architecture." No finding.

## Mechanical notes

- **Glossary drift:** none material. Domain nouns are used identically across sections (checked "Untrusted-input surface", "Sentinel/native mix", "Diagnostic model", "Hardened surface"). Minor stylistic variance only: the metric is written both as "Sentinel/native mix" and, in prose, "% native" / "% native ceiling" — but §3 explicitly reconciles these ("'% native' denotes the native portion of this mix"), so this is defined, not drift.
- **ID continuity:** clean. FR-1..FR-17, UJ-1..UJ-4, SM-1..SM-5 + SM-C1/SM-C2, SEC-1..SEC-3, NFR-PERF-1..6 + NFR-REL-1, OQ-1..OQ-6 — all contiguous, unique, no gaps.
- **Broken cross-ref (1):** §17 Assumptions Index cites "§4.2 / NFR-PERF" for the perf-metric assumptions; the NFR-PERF entries are in **§6**, and §4.2 is Build & Run. (Carried as a medium finding under Downstream usability.) All other sampled cross-refs resolve (FR↔UJ realizes-mappings, SM→FR validates-mappings, ADR 0059 / §14 ↔ addendum).
- **Assumptions Index roundtrip:** essentially holds. Every inline `[ASSUMPTION]` has an index entry — NFR-PERF metrics (§17 ✓, but mis-located per above), FR-2 highlighting (✓), SEC-3 signing/sandbox (✓), Typometer-in-CI §14 (✓), SM-1/SM-4 targets (✓). NFR-PERF-3 is correctly *excluded* (explicitly "not assumption"). No orphan index entries.
- **UJ protagonist naming:** every UJ names its protagonist inline and carries enough context to stand alone (Devon ×3, Priya ×1). No floating UJs.
- **Done-ness pattern break:** FR-8 and FR-9 omit the `Consequences (testable)` block that all 15 other FRs carry (carried as a high finding under Done-ness clarity — noted here too because it is also a structural-consistency defect a mechanical pass would catch).
- **Required sections:** all expected chain-top sections present for the stakes — Vision, Target User + JTBD + Non-Users + UJs, Glossary, Features/FRs, Security model, NFRs, Platform, Integration, Non-Goals, MVP scope, Why Now, Risk, Success Metrics + counter-metrics, Open Questions, Assumptions Index. Addendum present and correctly scoped to mechanism.
