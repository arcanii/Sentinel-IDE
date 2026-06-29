---
title: "Adversarial Review — SentinelIDE Code-Signing PRD Update"
status: final
created: 2026-06-28
reviewer: "adversarial-general (cynical/skeptical)"
target: "PRD update adding developer code-signing (FR-19/20/21, UJ-5, SEC-5, SM-C3) + cross-artifacts"
verdict: CONDITIONAL
---

# Adversarial Review: the developer code-signing update

**Method:** BMad adversarial-general. Default to skepticism. A claim that is not *earned* by the text is treated as an overclaim until proven otherwise. The product's stated load-bearing rule is HONESTY — "the artifact is genuinely signed; the signing-key surface is not yet hardened." I attacked the update specifically on whether it keeps that line, whether the new FRs are testable, whether SEC-4 / the v1 feature / SEC-5 stay distinct, and whether the PRD stays consistent with the brief surface map, EXPERIENCE.md, and DESIGN.md.

**Overall verdict: CONDITIONAL.** The honesty framing is, to its credit, unusually disciplined and repeated — the authors clearly anticipated the "you implied the key is safe" attack and pre-empted it in §5, the §5 `[NOTE FOR PM]`, SM-C3, FR-19, FR-21, the EXPERIENCE Voice table, and the mockup honesty note. That work is real and mostly holds. But the update **over-claims at exactly one place that matters most (the UJ-5 climax), silently omits three things a CISO will ask about on first contact (timestamping, cert expiry/revocation, key lifetime/persistence), leaks a key-detail contradiction between the mockup and the PRD's "what 'signed' verifies" silence, and leaves the SEC-4-vs-v1-vs-SEC-5 distinction *asserted everywhere but operationalized nowhere*.** None of these is fatal; all are cheap to fix in text; several will be expensive if they reach architecture/epics unfixed. Hence CONDITIONAL, not PASS.

---

## A. The overclaims (the honesty rule is the product — these are the most serious)

### A1 — [HIGH] UJ-5 climax claims the supply-chain *apex* is now "done," contradicting §5 which says it is only "partly addressed."
**Location:** §2.3 UJ-5 climax; cross-check §5 threat-model pillar 3 + §5 `[NOTE FOR PM]`.

UJ-5 says: *"the supply-chain-apex story (§5, pillar 3) becomes something Devon *does*, not just something the IDE claims."* The EXPERIENCE Flow 5 climax repeats it verbatim. This is the single biggest unearned claim in the update.

§5 itself disagrees. Pillar 3 is defined as *"everything authored through the IDE inherits its integrity; compromise here propagates downstream"* and the §5 `[NOTE FOR PM]` explicitly states: *"**Pillar 3 (supply chain):** partly addressed now via artifact signing, with provenance (SLSA/SBOM/reproducible builds) on the SEC-4 roadmap."*

So the PRD simultaneously says (a) pillar 3 is the *whole* supply chain and is only *partly* addressed, and (b) signing makes the apex story "something Devon does." Signing one output artifact with an unhardened key does **not** make "everything authored through the IDE inherit its integrity" — the IDE's *own* integrity (the thing pillar 3 is actually about: a compromised IDE signs malware just as happily) is the SEC-4 roadmap, not v1. The climax conflates "the developer signed their artifact" with "the supply chain apex is handled." A skeptical CISO reads UJ-5, then reads the §5 note, and catches the PRD overclaiming against itself. **This is precisely the failure SM-C3 was written to prevent, committed in the marquee journey.**

**Fix:** UJ-5/Flow 5 climax must claim the *narrow, true* thing: "the artifact Devon ships is now genuinely signed — one concrete step on the supply-chain pillar; the IDE's own provenance (SEC-4) and key hardening (SEC-5) come next." Drop "the apex story becomes something Devon does."

### A2 — [MED] "the produced executable is signed and ... 'Signed' at a glance" silently equates *the act of signing* with *trustworthy provenance*, and never says what "Signed" actually verifies.
**Location:** §2.3 UJ-5; FR-20; FR-21; §3 glossary "Code signing."

The glossary defines code signing as *"signing a built artifact ... so its origin and integrity can be verified."* Nowhere does the PRD state **what a verifier actually checks** or **what "Signed" guarantees to the person downstream**. Specifically missing:
- Signing proves the artifact was signed *by whoever held this key + passphrase this session*. It does **not** prove the key belongs to a vetted identity, that the key wasn't stolen, or that the build is reproducible. Given that the PRD *itself* admits the key sits in a not-yet-hardened surface, "Signed" is a weaker claim than the word implies — yet the UX spends the **trust-verified green** on it (DESIGN `trust-verified`, "signed / verified").
- The honesty rule polices the *key* ("not Sentinel-protected"), but does **not** police the *meaning of the green checkmark*. A reader can honestly accept "the key isn't hardened" and still over-read "Signed ✓ verified" as "this software is trustworthy." The product reserved coral away from signing precisely to avoid valence-inversion (DESIGN Do/Don't) — good — but then picked **green = "verified,"** which over-promises in the *other* direction.

**Fix:** Define in the glossary and FR-20 what "Signed" verifies (origin = this key/identity, integrity = unmodified-since-signing) and what it does **not** (identity vetting, key custody, build provenance). Consider softening the indicator label/semantics from "verified" to "signed" (the DESIGN token is literally named `trust-verified` and described as "signed / verified" — that conflation is baked into the design tokens, see D3).

### A3 — [MED] FR-19 honesty note says the key "is held by the native host **this session**" — but the PRD never states what that means for persistence, and the implication (key gone on restart) is nowhere confirmed.
**Location:** §2.3 UJ-5 ("held by the native host this session"); FR-19 consequence; EXPERIENCE State "Key loaded" ("sits in the not-yet-hardened native host **this session**"); mockup honesty note ("holds your key **for this session**").

"This session" is doing enormous unstated work. It is the *only* hint about key lifetime in the entire PRD, and it is never promoted to a requirement. Does "this session" mean:
- the key (and decrypted private key material) lives in process memory and is discarded on app exit? (the charitable reading)
- the passphrase is re-prompted every launch? (huge UX consequence, never stated)
- nothing is persisted to disk *at all*? (a security-relevant claim that, if true, should be a testable FR consequence, and if false, is a silent hole)

This is the §(e) "silently omitted scope the reader would assume present" failure: **key persistence across sessions, where/how long the key lives in memory** are exactly named in the brief's hunt list and are *skipped*. The phrase "this session" gestures at it without committing to anything testable. Architecture will have to invent the answer, and whatever they invent becomes an unreviewed security decision about a secret.

**Fix:** Add an FR-19 consequence: state explicitly whether key material persists across sessions (and where), whether the passphrase is re-prompted, and that decrypted private-key bytes live only in native-host memory for the session and are zeroized on unload/exit. If that is the next-increment's job, say so — but name it, don't leave it in an adverb.

---

## B. Untestable / hand-wavy FR consequences

### B1 — [HIGH] FR-20 "the artifact stays unsigned and the cause + fix are surfaced — the IDE never reports an artifact as signed when it is not" is an absolute safety claim with no verification path.
**Location:** FR-20 consequence 3; mirrored in SM-C3, EXPERIENCE State "Signing failed," and Flow 5 failure beat.

"**never** reports an artifact as signed when it is not" is the load-bearing integrity promise of the whole feature, stated as an absolute. But:
- There is **no consequence describing how the IDE confirms a signature actually took** before flipping the chip to "Signed." Does it shell out to `signtool`/equivalent and re-verify the PE? Trust the signer's exit code? Re-read the artifact's signature block? The difference is the entire distance between "never lies" and "lies whenever the signer exits 0 but didn't sign." As written, the promise is **asserted, not specified, and therefore not testable.**
- The honest failure cases listed ("wrong passphrase, key/algorithm mismatch") are the *easy* ones. The dangerous case — signer reports success but the artifact is unsigned/corrupt, or a stale "Signed" chip from a *previous* build persists after a new unsigned build — is exactly the case the absolute claim must cover and exactly the case left unspecified. (Note the mockup shows "Signed" in the status bar with `Build OK` in the output — what guarantees the chip is about *this* artifact and not the last one?)

**Fix:** Add a testable consequence: "Before showing 'Signed,' the IDE verifies the produced artifact carries a valid signature (re-reads the signature, not just the signer exit code); the chip is bound to the current build's artifact and resets to 'Unsigned' on any new build until re-verified." Without this, FR-20's headline promise is ungradeable — and SM-C3 inherits the same hole.

### B2 — [MED] FR-19 "displays the loaded key's identity (e.g., subject/issuer)" is under-specified against a mockup that shows far more (algorithm, expiry).
**Location:** FR-19 consequence 1 ("subject/issuer"); vs mockup `key-signing.html` L187 ("RSA-3072 · SHA-256 · valid to 2027-04-01").

The FR commits only to "subject/issuer." The mockup the FR cites as authority (`DESIGN import-key-dialog`, and Flow 5's reference mock) displays **algorithm, hash, and expiry date**. Either the FR under-specifies (and expiry/algorithm are silently in scope via the mock) or the mock over-promises UI the FR doesn't require. EXPERIENCE/DESIGN both say "spines win on conflict" and the spine says only "identity" — so the mock's expiry line is, strictly, unmandated. This matters because **expiry is the doorway to the revocation/expiry gap (C2 below).** Pick one: either commit to surfacing validity/expiry (and then handle expired keys), or scrub it from the mock to avoid implying a capability v1 doesn't promise.

### B3 — [MED] FR-21's six-state enumeration is asserted as mutually exclusive ("exactly one live state") but the states are not disjoint and transitions are undefined.
**Location:** FR-21 consequence 1 ("renders exactly one live state of: no key · key loaded · signing · signed · unsigned · failed"); DESIGN `status-signing` (same six).

"Exactly one live state" is testable only if the states partition cleanly. They don't:
- **"key loaded"** vs **"unsigned"** vs **"signed"** are not on the same axis — "key loaded" is a *key* state; "signed/unsigned/failed" are *last-artifact* states. With a key loaded and Sign-on-Build off and a successful build, is the chip "key loaded" or "unsigned"? Both are true. The PRD never gives the precedence.
- What is the chip after a **failed** signing when the user then edits and triggers a **fast keystroke** diagnostic but hasn't rebuilt — does "failed" persist, or revert to "key loaded"? Undefined.
- EXPERIENCE State Patterns lists treatments for the states but **also never defines the transition table or precedence** — it has rows for "No signing key," "Key loaded," "Build signed," "Build unsigned," "Signing in progress," "Signing failed," which is *six treatments* but maps imperfectly onto FR-21's *six states* (e.g., FR-21 "key loaded" vs EXPERIENCE "Key loaded" OK, but neither resolves the key-loaded-vs-unsigned overlap).

**Fix:** Provide the state machine (inputs: key present?, sign-on-build?, last-build signed/failed/none) → exactly one chip state, with transitions. Until then "exactly one live state" is not testable.

### B4 — [LOW] "Sign now" operand is ambiguous — *which* artifact?
**Location:** FR-20 ("Sign now signs the current artifact on demand"); EXPERIENCE Signing dialog.

"The current artifact" is undefined when there are zero builds this session, when the last build *failed*, or when the source has changed since the last build (so the on-disk exe is stale relative to the editor). Signing a stale artifact and showing green "Signed" would violate FR-20's own "never reports signed when it is not" spirit (the bytes are signed, but they're not the code on screen). Define "current artifact" and the no-artifact / stale-artifact behavior.

---

## C. Silently omitted scope a reader assumes present (the §(e) hunt)

### C1 — [HIGH] Timestamping is entirely absent. Authenticode without a trusted timestamp produces signatures that **expire with the certificate** — directly defeating the glossary's "origin and integrity can be verified."
**Location:** Absent everywhere. Relevant: §3 glossary "Code signing"; FR-20; mockup L187 ("valid to 2027-04-01").

This is the most consequential silent omission. Real-world Authenticode signing without an RFC-3161 timestamp means the signature is only valid until the signing certificate expires (the mock literally shows a cert "valid to 2027-04-01"); *with* a timestamp the signature survives cert expiry. A PRD whose entire selling point is supply-chain honesty, that ships Authenticode signing, and **never mentions timestamping** has skipped the single most important correctness detail of Authenticode. Downstream a developer signs a release, the cert lapses, every signature retroactively goes invalid, and "Signed" was a lie with a fuse on it. This is not gold-plating — it is table stakes for the feature to mean what the glossary says it means.

**Fix:** Either (a) make RFC-3161 timestamping a v1 FR-20 consequence (recommended — it's cheap and it's what makes "Signed" durable), or (b) explicitly name "no timestamping in v1" as an Out-of-Scope item *with the honest caveat that signatures expire with the cert*, so the honesty rule survives. Silence is the one unacceptable option.

### C2 — [MED] Certificate expiry / revocation / validity is never handled, despite the mock surfacing an expiry date.
**Location:** Absent. Mock shows "valid to 2027-04-01" (L187); brief hunt-list item ("revocation/expiry").

The PRD never says what happens when the imported key's cert is **expired**, **not-yet-valid**, or **revoked**. Does import reject it? Warn? Sign anyway and show green? Given the product's honesty posture, signing with an expired/revoked cert and displaying "Signed · verified" green would be a direct honesty violation. The mock raises the expectation (it shows expiry) and the PRD doesn't discharge it.

**Fix:** Add an FR-19/FR-20 consequence for expired/not-yet-valid/revoked certs (at minimum: warn and refuse the green "verified" state; ideally name CRL/OCSP as out-of-scope explicitly).

### C3 — [MED] "What 'signed' verifies" is named as a CISO probe point in §5 prose but never actually answered. (See A2.) This is both an overclaim risk *and* an omission.

### C4 — [LOW] Key *removal* / unload semantics are shown in the mock ("Remove key" button, L198) but never specified in any FR.
**Location:** Mock L198; EXPERIENCE Signing dialog (no remove behavior); FRs (silent).

The mock has a "Remove key" action. No FR mentions removing/unloading a key, what it does to in-memory key material (zeroize?), or what the chip shows after (back to "no key"). For a secret-handling surface, the *unload* path is security-relevant and is currently UI-only with no requirement behind it.

---

## D. Contradictions with brief surface map / EXPERIENCE / DESIGN

### D1 — [MED] Brief addendum still frames Secrets as "the immediate next surface" / "First hardened surface ... vs secret-handling" — a residue of the *old* timeline that now contradicts the new "v1 ships signing UX" reality.
**Location:** brief addendum "Exposed-surface map" (updated) vs brief addendum "Open sub-decision (defaulted in the brief; user may flip)" L60-62.

The exposed-surface map line *was* updated correctly: *"Secrets ... v1 ships developer code-signing UX (import / sign / status); the signing-key handling surface is hardened next (was 'next surface'; see PRD §5 SEC-5 and FR-19..21)."* Good. **But the same file's "Open sub-decision" section still reads:** *"First hardened surface = untrusted-input parsing (default) vs secret-handling. The brief defaults to parsing ... with secrets named as the immediate next surface."* That is the pre-update framing. After the update, secrets are **no longer merely "the immediate next surface"** — the *UX* ships in v1 and only the *hardening* is next. The two sections of the same addendum now tell different stories about where secrets sit on the timeline. This is exactly the §(f) "remaining inconsistency between the old 'secrets = next surface' framing and the new 'v1 UX, harden next' timeline."

**Fix:** Update the "Open sub-decision" paragraph to reflect that the sub-decision is *resolved* (parsing is the first *hardened* surface; signing UX ships in v1 unhardened; key hardening is next), or delete it as stale.

### D2 — [LOW] EXPERIENCE header provenance fib: Flow 5's mock comment and the EXPERIENCE doc disagree about whether a PRD UJ exists.
**Location:** mockup `key-signing.html` L8 ("Flow 5 (new requirement, **no PRD UJ yet**)") and L9; vs PRD §2.3 UJ-5 (exists) and EXPERIENCE Flow 5 ("mirroring the PRD UJ names verbatim").

The mockup's own header comment says "Flow 5 (new requirement, no PRD UJ yet)" — written before UJ-5 was added to the PRD. Now UJ-5 *does* exist. Minor, but it's a stale artifact comment that a reader auditing provenance will trip on (it implies the UX led the PRD, which is fine, but the comment is now factually wrong). Scrub it.

### D3 — [MED] The DESIGN token is named `trust-verified` and documented as the "signed / **verified**" state — baking the A2 overclaim into the design system.
**Location:** DESIGN colors `trust-verified` ("'signed / verified' state"); DESIGN Components "Signing status indicator" ("**signed** trust-verified"); DESIGN Do/Don't ("Use ... (green) for 'signed'").

The design system did the *hard* honesty work (coral deliberately not reused, valence reasoning spelled out) but then named the token `trust-**verified**` and glosses the green state as "signed / verified." "Verified" is a stronger word than the feature earns (it verifies *a* signature against *an unhardened* key, not the trustworthiness of the software). The honesty rule polices coral-reuse and the key-hardening claim but **does not police this word** anywhere. Because tokens propagate into code and copy, this is where the over-read will calcify. Consistency check: PRD FR-21 says the indicator state is "**signed**" (good, narrow); DESIGN calls the same thing "signed / **verified**" (broader). The PRD and DESIGN disagree on the word, and DESIGN's is the looser one.

**Fix:** Rename/redescribe to `trust-signed` or document the token as "artifact signed (origin+integrity), **not** identity-vetted." Align with FR-21's narrower "signed."

### D4 — [INFO] Authenticode / not-commit-signing decision: **consistent** across artifacts (verified, no finding).
**Location:** §3 glossary; §4.8 description; FR-21; §5; EXPERIENCE Proof & Co-evolution Surfaces ("Authenticode ... not commit-signing"); Flow 5 climax ("Authenticode-signed"); mock title.

Credit where due: the "Windows Authenticode of the produced PE, **not** source/commit signing" decision is stated identically in the PRD glossary, §4.8, EXPERIENCE, and the mock. No drift. The one nit: the PRD calls the signed thing "the executable `snc` produces"; EXPERIENCE calls it "the **PE** that `snc` produces"; the glossary's "snc" entry says `build` links "to executable." All consistent. (However — see C1: deciding *Authenticode* and then omitting *timestamping* is the substantive gap, not the commit-signing distinction.)

### D5 — [LOW] Component-name consistency check: **passes.** `import-key-dialog` and `status-signing` (DESIGN) ↔ "Import Signing Key (dialog)" / "Signing status" (EXPERIENCE IA) ↔ FR-19/FR-21 citations all line up. The six signing states match between FR-21 and DESIGN `status-signing` token keys (no-key/key-loaded/signing/signed/unsigned/failed). No naming drift found. (The *semantic* problems in B3 remain, but the names are consistent.)

---

## E. SEC-4 vs v1-feature vs SEC-5 — the three-way distinction (the §(c) hunt)

### E1 — [MED] The distinction is *asserted* in many places but the PRD never gives the one-line table that would prevent the muddle, and §5 pillar-3 prose re-muddles it.
**Location:** §3 glossary "Code signing"; §4.8 description; SEC-4; SEC-5; OQ-5; §17 assumptions; vs §5 threat-model pillar 3 + §5 `[NOTE FOR PM]`.

The PRD repeats the disclaimer "distinct from the IDE's own release provenance (SEC-4)" in at least five places (glossary, §4.8, SEC-4, SEC-5, §17). The *repetition itself is a tell*: the authors know these three are easy to confuse and are compensating with boilerplate rather than one clear separation. The three things:
- **SEC-4** = the IDE *signs its own releases* (Sigstore/SLSA/SBOM) — roadmap.
- **v1 feature (FR-19..21)** = *developers sign their artifacts* with an imported key — ships now.
- **SEC-5** = the *honesty boundary* around the v1 feature's key handling — ships now (as disclosure), hardens next.

Where it re-muddles: §5 pillar 3 (supply chain) is "partly addressed now via artifact signing **with provenance (SLSA/SBOM/reproducible builds) on the SEC-4 roadmap**." This sentence puts the v1 *developer* feature and the SEC-4 *IDE-provenance* roadmap in the **same breath under the same pillar**, which is exactly how a reader collapses them — and is what produces the A1 overclaim ("Devon *does* the apex story"). The disclaimers say "distinct"; the pillar-3 narrative treats them as one continuum.

**Fix:** Add a 3-row table (Who signs what / status / what it proves) once, in §4.8 or §5, and have every other mention point to it instead of re-asserting "distinct." Rewrite pillar-3 to separate "the developer's artifact (v1)" from "the IDE's own provenance (SEC-4)" instead of chaining them.

### E2 — [LOW] SEC-4 is tagged `[ASSUMPTION] (roadmap)` and also bundles an unrelated fourth thing — the plugin sandbox — into the same NFR.
**Location:** SEC-4 ("an extension model — when it exists — is sandboxed by design"); cross §11 / SEC-3 / OQ-4.

SEC-4 is "IDE release provenance," but it tacks on "extension model ... sandboxed by design," which is the SEC-3 / OQ-4 plugin-sandbox concern, not provenance. Two unrelated roadmap items in one NFR muddies traceability (SM-2 validates SEC-4 against provenance; the sandbox clause has no home metric). Minor, pre-existing, but the signing update is a good moment to split it.

---

## F. Risk / metrics / open-question coherence (the new SM-C3, OQ-5, §14)

### F1 — [MED] SM-C3 is a counter-metric with **no measurement** — it states a prohibition but no signal that would detect a violation.
**Location:** §15 SM-C3 ("Do not let the signing UX imply more than v1 delivers ... The 'Signed' signal must never read as 'the key is Sentinel-protected'").

SM-C1 and SM-C2 are gradeable (% native trend; keystroke latency). SM-C3 is a vibe — "must never read as." How is "reads as" measured? There is no instrument (no copy-audit checklist, no user-comprehension check, no review gate). As written it is an exhortation, not a metric, and it polices only the *key-protection* over-read — **not** the A1 apex over-read or the A2/D3 "verified" over-read, which are the ones the update actually commits. A counter-metric that doesn't catch the violations present in its own document is not doing its job.

**Fix:** Make SM-C3 operational: e.g., "a copy/UX review each release confirms (checklist) that no signing surface states or implies (a) the key is Sentinel-protected, (b) the IDE's own supply chain is hardened, or (c) 'verified' beyond origin+integrity." Tie it to the SEC-5 per-release security review that already exists.

### F2 — [LOW] §14's signing risk says the secret "sits in the native host, outside Sentinel's guarantees" and calls severity "accepted for v1" — but never states the *blast radius* of that acceptance.
**Location:** §14 "Signing key held in a not-yet-hardened native surface (v1)."

"Accepted for v1, honestly stated" accepts a risk without sizing it. What is actually at stake if the not-yet-hardened native host is exploited while a key is loaded? (Theft of a live code-signing private key = attacker can sign malware as the developer = the *exact* supply-chain compromise the product exists to prevent.) The risk entry is honest about *existence* but silent about *severity of consequence*, which is what "accepted" should be weighed against. Given the product thesis, "a code-signing key can be exfiltrated from the unhardened surface" is a sharp irony that deserves explicit naming, not a mild "outside Sentinel's guarantees."

### F3 — [INFO] OQ-5 and §12.2 and §17: the "v1 UX, harden next" split is stated **consistently** (verified). OQ-5 ("first secret is the signing key ... holds it in v1 ... not-yet-hardened ... hardening next increment"), §12.2 Out-of-Scope ("Hardening of the signing-key handling surface — next increment"), §17 assumptions all agree. No drift on the *timeline* claim itself. (The omissions in C1–C4 are *within* that scope, not contradictions of it.)

---

## G. Summary of what the update got right (so the CONDITIONAL is fair)

- The core honesty discipline — "artifact signed; key not hardened" — is stated consistently across PRD §5/FR-19/FR-21/SM-C3, EXPERIENCE Voice + State patterns, DESIGN honesty-note, and the mock. That is genuinely well-defended against the obvious "you implied the key is safe" attack.
- Coral-reuse is correctly forbidden for signing (DESIGN), preserving the safety-signal valence — a real, thoughtful call.
- The Authenticode / not-commit-signing decision is unambiguous and consistent everywhere (D4).
- SEC-5 correctly creates a *named, enumerated, per-release-reviewed* boundary rather than hiding the key surface — the right shape, even if under-specified.
- The timeline split (v1 UX / harden next) is internally consistent across OQ-5, §12.2, §17 (F3).

The update fails PASS not because the honesty thesis is wrong, but because it (1) breaks its own thesis at the UJ-5 climax (A1), (2) leaves the feature's correctness-defining details — timestamping, expiry/revocation, "what signed verifies," key lifetime, sign-confirmation — as silence or adverbs (C1, C2, A2, A3, B1), and (3) ships a counter-metric (SM-C3) that can't detect the very over-reads the document commits (F1, D3). All are text-level fixes; none requires re-architecting the feature.

---

## Prioritized fix list (do these before architecture/epics consume this)

1. **[HIGH] Add timestamping** (RFC-3161) to FR-20 as v1, or name it Out-of-Scope *with* the "signatures expire with the cert" honesty caveat. (C1)
2. **[HIGH] Rewrite the UJ-5 / Flow 5 climax** to the narrow true claim; stop saying signing makes the supply-chain apex "something Devon does." (A1)
3. **[HIGH] Specify how FR-20 confirms a signature took** before showing "Signed," and bind the chip to the current artifact. (B1)
4. **[MED] Define what "Signed" verifies** (origin+integrity) and does not (identity, custody, provenance) in glossary + FR-20; reconcile the `trust-verified`/"verified" wording with FR-21's narrower "signed." (A2, D3)
5. **[MED] Handle expired / revoked / not-yet-valid certs** (FR-19/20), and reconcile the mock's expiry line with the FR. (C2, B2)
6. **[MED] State key lifetime/persistence** as a testable FR-19 consequence (memory-only? zeroized? passphrase re-prompt? disk?). (A3, C4)
7. **[MED] Provide the signing-state machine** so FR-21's "exactly one live state" is testable; resolve key-loaded-vs-unsigned overlap. (B3)
8. **[MED] One 3-row SEC-4 / v1 / SEC-5 table**; rewrite §5 pillar-3 to stop chaining the v1 feature to the SEC-4 roadmap. (E1)
9. **[MED] Make SM-C3 measurable** (per-release copy/UX checklist tied to the SEC-5 review); extend it to cover the apex and "verified" over-reads. (F1)
10. **[LOW] Update the brief addendum "Open sub-decision"** to retire the stale "secrets = immediate next surface" framing. (D1)
11. **[LOW] Scrub stale provenance comments** (mock "no PRD UJ yet"); size the §14 blast radius. (D2, F2)
