# Sentinel-lang: capability requests from Sentinel-IDE

_Filed 2026-07-19 by the Sentinel-IDE team (`arcanii/Sentinel-IDE`) against `G:\Sentinel-lang`._
_Every claim below was reproduced on this machine; the method is in section 1._

> **For Sentinel-lang maintainers.** This is a prioritised list of what Sentinel-IDE needs
> before its crypto core can move from C++ into Sentinel. It is written to be actionable:
> each item states what we tried, what we measured, what we did instead, and what "done"
> would look like. Where we were wrong, we say so — four of our own initial conclusions were
> refuted by an adversarial pass and have been removed rather than filed.

---

## 1. Context

Sentinel-IDE is a native Win32 C++ IDE for Sentinel. Its stated thesis is "a thin native host that shrinks over time as more moves into Sentinel," and it is deliberately built as a forcing function: every time we try to move a real subsystem out of C++ and into Sentinel, whatever we hit is a genuine toolchain gap rather than a hypothetical one. Today the shipped binary contains **zero Sentinel code** — the only `.sentinel` file in the repo is a build-time LOC counter. Our concrete near-term target is `src/core/Seal.h`: the project-sealing crypto core, currently AES-256-GCM + PBKDF2-HMAC-SHA256 (600 000 iterations) via Windows CNG, to be rebuilt as a Sentinel C-ABI static library that the host links. The `.sealed` v2 format already reserves `aead_alg = 2` for a ChaCha20-Poly1305 variant (`src/core/Seal.h:17`, `:246`), so the migration path exists in the format. This document is what we found when we actually tried to walk it.

**How we measured.** All figures below were produced on an AMD Ryzen 9 9950X3D (16C/32T), Windows 11 Pro 26200, using `G:\Sentinel-lang\target\release\snc.exe` against `SNC_LIB_PATH=G:\Sentinel-lang\sentinel_library`, linked with MSVC 14.51 x64 via `vcvars64.bat`. Sentinel timings come from `kernel32!GetTickCount64` called from inside the program via `extern "C"`, wrapping only the workload; C baselines use `QueryPerformanceCounter`. Reported values are medians of 7–11 runs; run-to-run variance is ±12% and does not affect any conclusion here. Every crypto result was cross-checked against an independent implementation (NIST/RFC vectors, .NET `SHA256`/`HMACSHA256`/`AesGcm`, Python `hashlib`, or Windows CNG) and is byte-exact unless stated. `G:\Sentinel-lang` was not modified by any of this work; the one compiler experiment (§5) was done on a scratchpad clone.

**A note on what is *not* in this document.** We ran an adversarial pass over our own findings before writing it, and four things we initially believed turned out to be false: that PBKDF2 at 600 k iterations is unusably slow in Sentinel, that Sentinel cannot implement AES-256-GCM decryption, that a Sentinel static library would not link into our real GUI/Debug/CRT configuration, and that directory traversal forces archive/extract to stay in the host. All four were refuted by working code. None of them appear below as asks. What remains is what survived.

---

## 2. Executive summary

**Class legend.** *Impossible today* — no language or runtime surface exists. *Impractically slow* — works and is correct, but the measured cost rules it out. *Possible but awkward* — we shipped around it; the workaround is a recurring tax. *Defect* — behaviour contradicts documentation or invites a bug.

| # | Ask | Priority | Class | One-line impact |
|---|---|---|---|---|
| R1 | Explicit secure-zero for `[secret u8]` / `Vec<secret u8>` | **P0** | Impossible today | Moving key handling out of CNG is a net security regression until a KEK/DEK buffer can be wiped. |
| R2 | Host-controllable failure path for runtime aborts across the C ABI | **P0** | Impossible today | A bounds violation kills a `/subsystem:windows` IDE silently — no dialog, no save prompt, `__try/__except` does not recover. |
| R3 | `--emit-header` must not mark `&mut [u8]` params `const` | **P1** | Defect | Header invites writes through read-only memory; we reproduced an access violation with zero compiler diagnostic. ~3-line fix. |
| R4 | `chacha20poly1305_open` in `std/security/aead.sentinel` | **P1** | Possible but awkward | The stdlib seals but cannot open; we must restate three private helpers to complete an AEAD you already ship half of. |
| R5 | Bulk `[secret u8]` throughput: opt-in `-O2`, bounds-check elision, stdlib LTO | **P1** | Impractically slow | ChaCha20-Poly1305 at 7.5–10.2 MiB/s puts a 100 MB seal at ~9–13 s; this single number decides whether payload crypto can move at all. |
| R6 | Hoist `k_constants()` out of the SHA-256 hot path | **P1** | Defect | 64 `push`es into a fresh `Vec<secret i32>` per `sha256()` call; the equivalent fix in our own code was 5.9×. Benefits every SHA-256/HMAC/HKDF consumer. |
| R7 | PBKDF2-HMAC-SHA256 + an HMAC midstate API in the stdlib | **P1** | Possible but awkward | Same 600 k derivation costs 6.7 s naive vs 1.19 s with midstate; every consumer will otherwise rediscover this. |
| R8 | Emit and document the required Windows system libraries | **P1** | Defect | Our real link line fails with 33 unresolved externals; the correct list exists in the driver source but is never surfaced. |
| R9 | A `std::fs`-shaped stdlib module | **P1** | Possible but awkward | Archive/extract works today in pure Sentinel — but every consumer hand-decodes `WIN32_FIND_DATAW` byte offsets. |
| R10 | `snc build --shared` on Windows | P2 | Possible but awkward | Static lib is sufficient; a DLL would decouple our rebuild cycle. We built one by hand to confirm it is a driver gap, not a codegen one. |
| R11 | `?[T]` nullable arrays (or generic enums) | P2 | Possible but awkward | No fallible function can return an optional buffer; struct-shaped `Result` works but is unenforced convention. |
| R12 | Capturing closures | P2 | Impossible today | The one genuine language gap for IDE architecture — event handlers, visitors, incremental recompute. Long horizon. |
| R13 | AES-256 key schedule + `pub` GF(2⁸)/GHASH layer | P2 | Impractically slow | We wrote AES-256-GCM open and it works — at ~353 KiB/s. Included so you have the data; we are not blocked on it. |
| R14 | Chunked / streaming AEAD API | P2 | Possible but awkward | One-shot API peaks at ~3× payload memory; we can frame chunks ourselves. |
| R15 | Documentation corrections (ADR 0059, `SENTINEL_DESIGN2.md`) | P2 | Defect | Two docs currently state things the source contradicts; we planned against one of them and lost time. |

---

## 3. P0 — blocks the port outright

### R1. Explicit secure-zero for `[secret u8]` and `Vec<secret u8>`

**What we need.** A way, expressible in Sentinel, to zero a secret byte buffer — a builtin (`secure_zero(b: &mut [secret u8])`), a `Zeroize`-on-drop guarantee for array-shaped secrets, or both.

**Why.** `Seal.h` holds three things we are contractually obliged to destroy: the password bytes, the derived KEK, and the unwrapped DEK. In the current C++ implementation those live in stack/heap buffers we control and can `SecureZeroMemory`. If we port the AEAD+KDF core to Sentinel and cannot wipe them, the port makes the product *less* secure than the code it replaces — which is not a trade we can defend, and it is the only item on this list that would force us to abandon the port outright rather than work around it.

**Current state, verified in source.** `secure_zero` exists only on the Rust side: `crates/sentinel-broker/src/secret.rs:151` (volatile write loop plus `compiler_fence`). It is **not** in the builtin table — `crates/sentinel-resolve/src/lib.rs:2968–3007` registers 43 builtins, none of which are crypto or zeroize. Automatic scrubbing fires only at `Shared<secret T>` / `Mutex<secret T>` last-drop (`crates/sentinel-runtime/src/lib.rs:1749`, `:2045`) and only over `size_of::<i64>()` — a *scalar* payload. Every key, DEK and KEK we would hold is `[secret u8]` or `Vec<secret u8>` and is therefore never wiped.

**Related observation, offered as a diagnostic rather than a second complaint.** We also confirmed that `[secret u8]` arrays are not `VirtualLock`'d: 16 threads × 3 × 8 MiB of secret HMAC ran with a process minimum working set of only 204 800 bytes and never tripped the `secret_locked_pages` budget (`crates/sentinel-runtime/src/lib.rs:1510`). That is good news for us — the fail-closed abort risk we were worried about is a non-issue for byte-array crypto — but it appears to be the *same* root cause as the missing scrub: array-shaped secrets do not go through the locked-cell path at all. A fix that brings array secrets into that path would likely address both, and we would rather have the scrub than the lock if you have to choose.

**Workaround.** None that works. Overwriting a `[secret u8]` in a loop is not a guarantee — nothing prevents the optimizer, or the runtime's own copy on declassify, from leaving a residue, and we cannot inspect the result from inside the language. We are currently keeping key material in C++ and passing only derived, already-declassified bytes across the boundary, which defeats the point of the port.

**Done looks like.** A Sentinel program can allocate a `[secret u8]` of 32 bytes, write a known pattern, call the zeroize surface, and a C++ host reading the same memory through the `&mut [u8]` it supplied observes all zeros — with a test in `examples/security/` that exits 42.

---

### R2. A host-controllable failure path for runtime aborts across the C ABI

**What we need.** Either (a) a host-installable hook the runtime calls before aborting — `sentinel_set_panic_handler(void(*)(const char*))` or similar — or (b) an in-band error return for recoverable runtime faults, principally bounds violations, on `export "C"` entry points.

**Why.** Sentinel-IDE is a `/subsystem:windows` GUI application. Its whole job is to hold the user's unsaved work. Today, a bounds violation inside a Sentinel export terminates the host process:

```
sentinel: index out of bounds: idx=16, len=16
exit=-1073740791          (0xC0000409 fast-fail)
```

That message goes to stderr, which a GUI host does not have. **Wrapping the call in `__try/__except(EXCEPTION_EXECUTE_HANDLER)` does not recover** — we tested it explicitly and the process died identically. So the observable behaviour to an IDE user is: the application vanishes. No dialog, no crash reporter, no save prompt, no log line. For a subsystem whose failure mode is "you mis-sized an output buffer by one," that is the wrong blast radius.

To be fair to the runtime, we confirmed two things in its favour. Bounds are genuinely *enforced* against the marshalled length — handing a 16-byte output buffer to an export that writes a 32-byte tag stops cleanly at index 16 with **zero bytes spilled** past the window, so this is a fail-stop, not a silent overflow. And the host's own SEH is unaffected: our test host still caught its own `0xC0000005` normally, so Rust's std is not hijacking exception handling. The problem is narrowly that the abort is unconditional and invisible.

**Workaround.** Make buffer sizing a caller-side invariant enforced by hand at every call site, with no type-system or compile-time support. That is exactly the discipline that fails in practice on the twentieth call site.

**Done looks like.** A C++ host registers a callback, calls an export with a deliberately undersized `&mut [u8]`, receives the diagnostic string in its callback, and the process is still alive to show a dialog and flush unsaved buffers. Even a hook that is *required* to terminate afterwards would be a large improvement over the current silence.

---

## 4. P1 — makes the port viable

### R3. `--emit-header` must not declare `&mut [u8]` parameters `const`

**What we need.** `emit_c_header` should read `RefData.mutable` and emit `uint8_t*` for `&mut [u8]`, reserving `const uint8_t*` for `&[u8]`.

**Why, and why this is not cosmetic.** `is_byte_slice_ref_header` (`crates/sentinel-driver/src/main.rs:1683`) matches on `rd.inner` and never consults `RefData.mutable` — a field that exists and is documented "`true` for `&mut T`" at `crates/sentinel-types/src/lib.rs:470–472`. So the emitter at `main.rs:1728–1730` produces, for a function whose third slice is an out-param:

```c
int64_t sen_xor_into(const uint8_t*, int64_t, const uint8_t*, int64_t, int64_t, int64_t);
```

Nothing in that signature distinguishes an input from an output. It compiles, because `uint8_t*` converts implicitly to `const uint8_t*`, and it works — until a caller passes something genuinely read-only. We passed a `static const unsigned char[32]` (a real `.rdata` object) as the out-param and got:

```
rdata_buf addr=00007FF7C65F2740, [0]=01
exit=-1073741819          (0xC0000005 access violation)
```

with **zero compile-time diagnostic under `/W4 /permissive-`**. The generated header actively invites this. The arity flattening compounds it: each slice expands to a `(ptr, len)` pair, so `sen_copy_xor(src, srclen, dst, dstlen, n, k)` presents six mutually indistinguishable `const uint8_t*` / `int64_t` slots. The compiler catches wrong *arity* (`C2660`); nothing catches wrong *direction*.

For a crypto core this is precisely the wrong direction to be wrong in, and it is the cheapest fix on this entire list.

**Workaround.** We post-process the generated header before including it. It works and it is three lines of build script, but it means we cannot consume `--emit-header` output directly, which is the feature's whole purpose.

**Done looks like.** `snc build --lib x.sentinel --emit-header x.h` on a function taking `&[u8]` and `&mut [u8]` emits `const uint8_t*` for the first and `uint8_t*` for the second, and a C++ TU that passes a `const` buffer to the out-param fails to compile.

---

### R4. `chacha20poly1305_open` in `std/security/aead.sentinel`

**What we need.** The decrypt-and-verify half of RFC 8439, alongside the seal half you already ship. Failing that, make `derive_otk` (`aead.sentinel:40`), `pad16` (`:76`) and `push_le64` (`:64`) `pub`.

**Why.** `chacha20poly1305_encrypt` exists at `std/security/aead.sentinel:92` returning the `AeadResult` struct at `:32`. There is no corresponding open. Since `aead_alg = 2` in the `.sealed` format is exactly ChaCha20-Poly1305, and unsealing is the operation that matters most (it runs on every project open), a seal-only AEAD is not usable for us as shipped.

**What we did, and why this is P1 rather than P0.** We wrote a generic RFC 8439 open using only `pub` surface — `chacha20_xor` and `poly1305` — compiled it with the shipped `snc.exe`, and ran it: **exit 42**. The authentic RFC 8439 §2.8.2 record verified, the 114-byte plaintext recovered byte-exact, and a single flipped ciphertext byte rejected as a forgery. It passed the constant-time checker. So nothing cryptographic is missing and we are not blocked — but the three helpers we had to restate are ~30 lines of security-relevant code that now lives in our tree instead of yours, unversioned and untested against your vectors. (`derive_otk` is also replaceable via the trick at `std/net/ssh_cipher.sentinel:110` — the counter-0 keystream over 32 zero bytes *is* the one-time key.)

**For the record, on the SSH functions.** `ssh_open_verify` (`std/net/ssh_cipher.sentinel:134`) and `ssh_open_payload` (`:151`) do decrypt and verify, but they implement `chacha20-poly1305@openssh.com`, not RFC 8439: a 64-byte key split into K₁/K₂, a separately-encrypted 4-byte length field, SSH padding, a sequence-number nonce, and **no AAD parameter at all**. They are not reusable for us. Their value was as a reference pattern — `ssh_open_verify:140–146` is exactly the branch-free tag-compare-then-single-declassify shape we adopted.

**Done looks like.** `pub fn chacha20poly1305_open(key, nonce, aad, ciphertext, tag) -> ...` with an `examples/security/` program that verifies the RFC 8439 §2.8.2 vector and rejects a single-bit ciphertext mutation, exiting 42. We are happy to contribute our implementation as the starting point.

---

### R5. Bulk throughput for `[secret u8]` buffers

**What we need.** In descending order of value to us: (a) bounds-check elision on provably-in-range indices, plus a `Vec::with_capacity`-shaped API to eliminate `sentinel_realloc` traffic; (b) `internalize` + LTO so `pub` stdlib functions can be inlined across the module boundary; (c) an **opt-in** `snc build -O2` flag that runs the LLVM pass pipeline.

**Why this is the deciding number.** Seal's pipeline is archive → LZMS-compress → AEAD-encrypt. A realistic project payload is tens to hundreds of MB. Measured, on the shipped compiler:

| Workload | Sentinel | C `/O2` | Windows CNG | vs C `/O2` |
|---|---|---|---|---|
| ChaCha20, 4 MiB | 484 ms → **8.26 MiB/s** | 776 MiB/s | — | 94× |
| ChaCha20-Poly1305, 4 MiB | 531 ms → **7.53 MiB/s** | 625 MiB/s | — | 83× |
| SHA-256, 8 MiB | 219 ms → **36.5 MiB/s** | 18.0 ms (444 MiB/s) | 3.33 ms (2402 MiB/s) | 12.2× |
| AES-256-GCM, 512 KiB | 1.449 s → **353 KiB/s** | — | 2513.6 MiB/s | — |

At 7.53 MiB/s, sealing a 100 MB project payload costs roughly **13 seconds** of AEAD alone. That is the number that decides whether the payload path can move to Sentinel at all, independent of every other item on this list.

**What the cause is, precisely.** Two loop benchmarks isolate it. A serial xorshift64 chain (2×10⁸ iterations) runs at **exact native parity** — 234 ms Sentinel vs 235 ms C `/O2`, identical checksum `7020083024144090930` — because it is latency-bound and the redundant load/store traffic hides under the dependency chain. A throughput-bound integer accumulate (5×10⁸) gives Sentinel 188 ms vs C `/Od` 196 ms vs C `/O2` 72 ms: **scalar codegen is exactly `-O0` quality, no better and no worse.** But the crypto workloads are 4.6–5.7× slower than *unoptimized* C. That extra factor is not scalar codegen — it is `Vec<secret u8>` heap arrays, bounds checks, and un-inlined per-operation helper calls. `dumpbin /disasm` on our SHA-256 object shows `Sentinel$ct$ct_rotr32` — a three-instruction rotate — compiled as a **15-instruction out-of-line function with a 32-byte stack frame**, called 6× per round × 64 rounds; `compress_block` is 1312 instructions containing 34 `sentinel_panic_oob` calls and 10 `__chkstk` calls.

Critically, **`secret` itself is not the cost.** A 150 M-operation dependent chain written identically over public `i64` and over `secret i32` measured **63 ms vs 62 ms**. Secret scalars are plain LLVM SSA values. The penalty is entirely in the array/`Vec` representation, which is why (a) and (b) above are ranked ahead of (c).

**On the optimizer, and why we are asking for a flag rather than a default.** `OptimizationLevel::None` is hardcoded at `crates/sentinel-codegen/src/lib.rs:1561`, and — more consequentially — the LLVM middle-end pipeline is never run at all: between `module.verify()` (`:1544`) and `write_to_file` (`:1567`) there is no `run_passes`, no `PassManager`, no `PassBuilder`. A repo-wide grep returns zero production hits, so even `mem2reg` never runs, despite codegen relying on it (comment at `lib.rs:9366`; ADR 0045, `docs/decisions/0045-self-host-port-codegen.md:28`). There is no CLI flag, no environment variable (`SNC_LIB_PATH` is the only one the compiler reads, `main.rs:846`), and no config file that changes this.

We tested the fix on a scratchpad clone — 1 line modified and 8 added, using the `run_passes` API inkwell 0.5 already exposes. It compiled in 43 s. Measured: SHA-256 219 → **63 ms** (3.5×), HMAC 11.88 → **5.78 µs** (2.1×), ChaCha20-Poly1305 531 → **391 ms** (1.4×). All 24 programs in `examples/security/` still build and exit 42, including the RFC 8439 vector; every benchmark checksum unchanged. Compile time moved +3.5%. `compress_block` dropped 1312 → 379 instructions with all six helpers inlined.

But we are deliberately **not** asking you to flip the default, for two reasons. First, the constant-time objection is partly legitimate: the CT verifier runs on MIR before LLVM (`docs/ct-model.md:48–53`, README:79), and your docs already record post-optimization verification as future work. `-O2` does not violate the property as *stated*, but it widens the gap between the verified MIR and the emitted machine code. An opt-in flag keeps the CT default at `None`. Second, and more honestly: for the workload we cared about most, **fixing the library beat patching the compiler** — see R6 and R7, where library-level changes got 600 k-iteration PBKDF2 to 390 ms against the patched compiler's 3.47 s. We would rather you spend the effort on (a) and (b).

Threading the level through `compile_to_object` / `compile_to_object_for_module` touches 2 public signatures and 4 call sites (`main.rs:1273`, `:1356`, `:1533`, `:2405`) — roughly 40–60 lines with flag, help text and a test.

**Done looks like.** ChaCha20-Poly1305 over a 4 MiB buffer within ~10× of C `/O2` (i.e. ≥60 MiB/s), with the RFC 8439 vector still passing and the CT checker still rejecting a secret-dependent branch. Any single one of (a)/(b)/(c) landing is progress we will measure and report.

---

### R6. Hoist `k_constants()` out of the SHA-256 hot path

**What we need.** `k_constants()` at `std/security/sha256.sentinel:73` builds a 64-element `Vec<secret i32>` via 64 `push` calls **on every single `sha256()` invocation** — which means twice per HMAC. Hoist the table to module level, or pass it in as a borrow.

**Why we are confident this matters.** We made the identical mistake in our own PBKDF2 (a 64-element array literal inside the compress function) and hoisting it took **6953 ms → 1188 ms — a 5.9× improvement on that change alone**, on the shipped compiler with no other edits. We have not directly instrumented your `k_constants` in isolation, so we present this as a strong inference from an equivalent measurement rather than a direct one — but the code shape is the same and it is on the hottest path in the library.

This is the highest leverage-per-line item we found anywhere in the repo. It is roughly five lines, it needs no compiler change, and it speeds up **every** consumer of SHA-256, HMAC-SHA256, HKDF, and anything downstream of them — not just our use case.

**Done looks like.** A microbenchmark of `sha256()` over a small fixed input shows a multiple-× improvement in calls/second, and the NIST vectors still pass.

---

### R7. PBKDF2-HMAC-SHA256, and an HMAC midstate API

**What we need.** `pbkdf2_hmac_sha256(password, salt, iterations, dk_len)` in `std/security/`, implemented over an HMAC midstate API — i.e. a way to precompute and reuse the SHA-256 state after `(K₀ ^ ipad)` and `(K₀ ^ opad)`.

**Why.** `Seal.h` uses PBKDF2-HMAC-SHA256 at 600 000 iterations (`kSealPbkdf2Iters`, `src/core/Seal.h:244`) to derive the KEK. PBKDF2 is absent from the stdlib, so every consumer writes it themselves — and the naive way to write it over `hmac_sha256` (`std/security/hmac.sentinel:25`) is very expensive, because that function re-does the key padding and both 2-block hashes from scratch on every call. PBKDF2's inner loop is `U = HMAC(P, U)` with a fixed 32-byte message, so precomputing the two midstates halves the compressions per iteration from four to two. This is standard in every serious PBKDF2 implementation and is purely a library design choice, not a language limitation.

**Measured, all on the shipped `snc.exe`, no flags, no compiler patch, constant-time proof intact:**

| Implementation | 600 000 iterations |
|---|---|
| Naive: `hmac_sha256` in a loop | **6672–7078 ms** |
| Midstate + K-table hoisted, ordinary hand-written Sentinel (~120 lines, no code generation) | **1188–1203 ms** |
| Midstate + generated unrolled scalar compress | **390–406 ms** |
| Portable C `/O2`, identical algorithm, same machine | 160–169 ms |
| Windows CNG `BCryptDeriveKeyPBKDF2` (SHA-NI) | 93.8 ms |

All Sentinel variants emit `669cfe52482116fd…`, byte-exact against `hashlib.pbkdf2_hmac('sha256', b'password', b'salt', 600000, 32)`, and also verified at c=1 (`120fb6cf`), c=2 (`ae4d0c95`), c=4096 (`c5e478d5`) and c=100000 (`0394a2ed`).

**We want to be explicit that this refutes something we originally believed.** Our first pass concluded that Sentinel could not do 600 k PBKDF2 at usable speed. That was wrong — it measured your stdlib's HMAC, not the language's capability. **390 ms is roughly what OWASP intends 600 k iterations to cost**, and 1.2 s is defensible for a vault unlock with no code generation involved. The per-compression cost moves 2.95 µs (stdlib) → 0.99 µs (loops, K hoisted) → 0.325 µs (unrolled). The scalar-vs-array finding from R5 is the mechanism: PBKDF2's entire working set is 16 words, so moving hot state out of `Vec`/arrays into scalars removes the penalty completely.

**One caveat we want on the record so nobody over-generalises it.** Our fast version is specialised — correctly, for this shape — to key ≤ 64 bytes (no key pre-hash), a 32-byte message, and dkLen ≤ 32. That is exactly `Seal.h`'s use. A general-purpose SHA-256 still needs the generic path, and **this result does not transfer to streaming workloads** that must touch `[secret u8]` per byte. R5 stands independently.

**Done looks like.** `pbkdf2_hmac_sha256` in the stdlib passing the four canonical vectors above, with 600 000 iterations of a 32-byte derivation completing in under ~1.5 s on comparable hardware. We will contribute both our 120-line midstate implementation and our vectors.

---

### R8. Emit and document the required Windows system libraries

**What we need.** The generated header (or `--lib` output, or the ADR) should state the system libraries a host must link. Ideally `--emit-header` emits them as `#pragma comment(lib, ...)` under `_MSC_VER`.

**Why.** The list exists in your source as `WINDOWS_NATIVE_LIBS` (`crates/sentinel-driver/src/main.rs:1902–1909`) and is used by the executable link path (`:1973`) — but it is never emitted into the generated header, never printed by `--lib`, and is not in ADR 0059. A host integrator rediscovers it from linker errors, which is how we found it. Sentinel-IDE's current link line is:

```
comctl32 dwmapi uxtheme gdi32 user32 shell32 ole32 bcrypt cabinet advapi32
```

(`CMakeLists.txt:89–90`), and it is **not sufficient**: linking a Sentinel `--lib` against it fails with **33 unresolved externals** — `__imp_WSASend`, `__imp_WSAStartup`, `__imp_NtWriteFile`, `__imp_GetUserProfileDirectoryW`, `__imp_freeaddrinfo`, and so on. The fix is to add `legacy_stdio_definitions.lib ntdll.lib userenv.lib ws2_32.lib dbghelp.lib`.

Worse than merely undocumented, it is **inconsistent**: a value-only Sentinel library that never allocates links fine without any of them. Whether you need them depends on which Rust std paths your particular Sentinel code happens to pull in, so a host cannot determine the requirement by inspection — it can only discover it by failing. Our conclusion for other integrators is "always link all six," which is a workaround, not an answer.

**Done looks like.** `snc build --lib --emit-header` produces a header that either declares the dependencies via `#pragma comment(lib, ...)` or contains a comment listing them, such that a host following the header alone links successfully on first attempt.

---

### R9. A `std::fs`-shaped stdlib module

**What we need.** A Sentinel module wrapping directory enumeration, file metadata, and handle-based streaming IO — `FindFirstFileW`/`FindNextFileW`, `GetFileAttributesExW`, `CreateFileW`/`ReadFile`/`WriteFile`/`SetFilePointerEx`, with `GetLastError` surfaced.

**Why this is a stdlib ask and not a language ask.** We want to be clear that we are *not* reporting a blocker here, because we initially thought it was one and it isn't. The builtin IO surface is genuinely minimal — `read_file` / `write_file` only (`crates/sentinel-resolve/src/lib.rs:2811`, `:2823`), whole-file, truncating, no append, no seek, no handles, no metadata — and both abort the process on failure (`crates/sentinel-runtime/src/lib.rs:243–247`, `:289–293`), which is documented at `crates/sentinel-types/src/lib.rs:5480–5482`.

But all of it is reachable through `extern "C"` today, and we proved it end to end. We wrote a complete recursive archiver/extractor in 100% Sentinel — no host callbacks, no C shim, no compiler changes — that walked `G:\Sentinel-lang\sentinel_library` breadth-first, streamed every file in 64 KiB chunks via `CreateFileA`/`ReadFile`, produced a 369 184-byte archive, extracted it to a fresh tree creating directories, and round-tripped byte-identically. Independently verified (not by our own comparison code): `src_files=47 dst_files=47`, `src_dirs=12 dst_dirs=12`, `TREES IDENTICAL (path+size+sha256 for every file)`, `src_bytes=367131 dst_bytes=367131`. Total 423 ms. A second version over the wide API, with a hand-written UTF-8↔UTF-16 codec including surrogate pairs, correctly round-tripped `café.txt`, `日本語.txt`, `рус.txt` and `emoji😀.txt`.

Two things this settles. **Recoverable IO errors are a property of the `read_file` builtin, not of the language.** Against a file held under exclusive lock, a missing file, and `C:/Windows/System32/config/SAM`, `CreateFileW` + `GetLastError` returned `32` (`ERROR_SHARING_VIOLATION`), `2` (`ERROR_FILE_NOT_FOUND`) and `5` (`ERROR_ACCESS_DENIED`), with the process alive and exiting 42 — precise, non-racy errors from the actual open, with none of the TOCTOU problem of a probe-then-read guard. And **performance is not an argument here**: this workload is syscall-bound, not compute-bound, so none of the R5 penalties apply. Enumerating 203 entries / 3.6 MB took **47 ms** warm; native `bsdtar` creating the same tree took 116 ms.

**So the ask is narrow.** Nothing in the compiler stops us; what is missing is that every consumer hand-decodes the 320-byte `WIN32_FIND_DATA` structure by indexing a `[u8]` buffer at literal offsets (attributes at 0, size at 32, filename at 44). That is fragile, unshared, and gets rewritten identically by everyone who needs it. It is a few hundred lines of stdlib.

**Two gotchas worth folding into that module's design.** Declare Win32 HANDLEs as `i64`, not `ptr` — the register ABI is identical and `INVALID_HANDLE_VALUE` (-1) then compares cleanly, whereas `ptr` is opaque, supports only `is_null`, and cannot be cast (`sentinel::types::non_integer_cast`). And `GetLastError` must be called *immediately*; an intervening `print()` clobbers it (we observed `6` where `3` was correct). Also note that `extern "C" link("kernel32") { ... }` self-links with no `--link` flag, as `sentinel_library/std/sys/win32.sentinel:17` already does — so such a module would be drop-in for consumers.

**What we did not prove**, and flag as remaining work for whoever writes this: symlinks and reparse points, hardlinks, ACLs, `>MAX_PATH` via the `\\?\` prefix, and restoring timestamps with `SetFileTime`. All are ordinary kernel32 calls on the same proven path; none look like blockers, but none are demonstrated. Our archive also materialises whole in memory — incremental streaming to disk is reachable but unbuilt, and byte-at-a-time `push` accumulation is the real cost driver at GB scale. Separately, the absence of `Vec<[u8]>` forced us to flatten worklists to NUL-separated byte buffers; it works, but it is a design tax on every collection-of-strings.

**Done looks like.** A `std::fs` module that can enumerate a directory tree, stat entries including size and mtime, open/seek/read/write by handle, and return Win32 error codes without aborting — with a test that round-trips a tree containing non-ASCII filenames.

---

## 5. P2 — quality of life

### R10. `snc build --shared` on Windows

Refused at `crates/sentinel-driver/src/main.rs:1627–1634`, whose own comment explains why: exporting the `export "C"` symbols needs a `.def` file or `dllexport`, deferred to an ADR 0060 Phase 2 follow-up. We confirmed the blocker is exactly that and nothing deeper: the emitted object is already PIC, and we linked a working DLL by hand with `link /DLL` plus `/EXPORT:` per symbol, loaded it with `LoadLibrary`, resolved four exports, and exercised buffer write-back and owned returns through it successfully. Note that `/DLL` additionally requires the CRT libs (`msvcrt`/`vcruntime`/`ucrt`) — our first attempt failed with 16 unresolved CRT symbols. Adding a Windows branch is a contained driver change: a `link.exe /DLL` invocation plus `/EXPORT:` generation from `typed.exports`. **Done looks like:** `snc build --shared x.sentinel -o x.dll` produces a DLL whose exports resolve via `GetProcAddress`. Static linking works for us today, so this is convenience, not need.

### R11. `?[T]` nullable arrays, or generic enums

`let line: ?[u8]` fails with `sentinel::types::unknown_type` / `unknown type ?(nullable)`. Optionals work only for word scalars — we verified `?i64` with `unwrap_or`, `is_some` and `null` all behaving correctly. The consequence is that no fallible function can return an optional buffer, which is the natural shape for `unseal(...) -> ?[u8]`. `docs/decisions/0035-file-io-stdlib.md:130` (D5) already names the dependency: a real `Result` wants D.1b generic enums (ADR 0032 A3), and `?[T]` wants the ADR 0015 D6 deferral lifted. **Workaround:** return a struct-shaped result (`{ ok, len, data }`); verified working. It is unenforced convention rather than a type-system guarantee, but it is genuinely adequate. **Done looks like:** `?[u8]` type-checks and `is_some`/`unwrap_or` work over it.

### R12. Capturing closures

Lambda syntax is rejected (`unexpected Pipe, expected expression`). ADR 0070 shipped non-capturing first-class function values, which do work — we verified `Fn<T,R>` passed by name, invoked both via `apply(f, x)` and directly as `f(x)`. Captures remain deferred (ADR 0024 D10). This is the one item on this list we consider a genuine *language* gap rather than a library or tooling gap, and it is structural for IDE architecture specifically: event handlers, callbacks, visitors and incremental-recompute graphs all want a captured environment. Non-capturing `Fn` plus an explicit context struct threaded as a parameter covers it, but that is a pervasive design tax on every callback-shaped API rather than a local workaround. **We are filing it as P2 deliberately** — it does not touch the Seal port at all, and we would rather you spend near-term effort on R1–R9. We raise it now so we can design our interfaces around its absence rather than retrofit later. **Done looks like:** a closure capturing a local by value can be stored in an `Fn<T,R>` and invoked after the defining scope returns.

### R13. AES-256 key schedule and a `pub` GF(2⁸)/GHASH layer

We are including this for completeness and to correct our own earlier analysis, not because we need it.

`key_expansion` (`std/security/aes.sentinel:124`) is hardcoded to AES-128 — `while w < 44` at `:136`, 44 words / 11 round keys — and `aes128_encrypt_block` (`:252`) runs a fixed 10 rounds. There is no AES-192/256 path and no GCM decrypt (`std/security/aes_gcm.sentinel:152` is encrypt-only). Every internal you would reuse is private, which the compiler enforces: `snc: sub_byte is private to module std::security::aes`.

**We originally reported that Sentinel therefore could not implement AES-256-GCM decryption. That was wrong on two counts.** First, the reasoning leaned on the absence of an inverse cipher — but **GCM decryption never invokes the inverse cipher**; GCM is CTR mode, so decryption is `ct XOR AES_encrypt(counter)` plus GHASH and a tag compare, forward direction only. Second, we then wrote it: 341 lines total, of which only ~112 are new logic (a 45-line AES-256 key schedule with `w < 60`, `w mod 8` and the extra `m == 4` SubWord step, plus a 67-line GCM-open driver); the other ~230 are mechanical verbatim copies of your already-verified private helpers. It builds clean, passes the constant-time verifier, and against a live .NET `AesGcm` reference vector with `Seal.h`'s exact parameter shape (32-byte key, 12-byte IV, 20-byte AAD, 45-byte payload) it recovers the plaintext byte-exact and rejects a single flipped ciphertext byte.

**The real problem is speed, and it is decisive:** 353 KiB/s, versus Windows CNG AES-256-GCM at 2513.6 MiB/s — roughly **7300× slower**, 44.2 µs per 16-byte block. The cause is structural rather than incidental: the table-free S-box computes `gf_inv` as 13 `gf_mul` × 8 branch-free iterations ≈ 104 inner steps *per byte*, × 16 bytes × 14 rounds. (`aes_enc_block` also clones the key and re-runs the full key schedule on *every block* — the same flaw the shipped AES-128 GCM has — but hoisting that recovers only ~19%, so it is not the main term.) The R5 optimizer patch would leave it ~3500× behind. For a 100 MB payload that is ~4.8 minutes against 0.04 s.

**Two nuances that matter.** The 32-byte DEK key-wrap in Seal's password slot is only 4 block encryptions ≈ **177 µs — entirely viable in Sentinel today** with the code above; only the payload path is hopeless. And this *strengthens* the ChaCha recommendation on better grounds than we originally had: Sentinel's own ChaCha20-Poly1305 runs 22× faster than Sentinel AES-256-GCM. The argument for `aead_alg = 2` is not "AES doesn't exist" — it can be written in an afternoon — but "Sentinel's arithmetic S-box makes AES structurally 22× worse than the ChaCha you already ship."

We also checked and closed the obvious escape hatch: calling CNG's `BCryptDecrypt` from Sentinel via `extern "C" link("bcrypt")` would give full speed, but it requires `BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO`, which embeds pointers, and Sentinel's `ptr` is opaque — `h as i64` fails with `sentinel::types::non_integer_cast`. That route needs a small C shim; it is not reachable from pure Sentinel.

**If you want any of this**, we will contribute the AES-256 key schedule and the GCM open driver. Making `sub_byte` (`:112`), `gf_mul` (`:47`), `key_expansion` (`:124`), `gf128_mul` (`aes_gcm.sentinel:110`), `ghash_step` (`:138`) and `aes_enc_block` (`:88`) `pub` would let future implementers skip the 230 lines of copying. But we are not asking you to make AES fast, and we are not blocked.

### R14. Chunked / streaming AEAD

`chacha20poly1305_encrypt` materialises the full plaintext as `[secret u8]` and makes two complete declassify passes (`std/security/aead.sentinel:132–137`, `:154–158`), so peak memory runs around 3× payload size. For a multi-hundred-MB project archive that is the difference between comfortable and not. We can define our own chunk framing above the one-shot API, so this is genuinely P2 — but an incremental API would be better for everyone. **Done looks like:** an init/update/finalize AEAD shape whose peak allocation is O(chunk), verified against the RFC 8439 vector fed in multiple chunks.

### R15. Documentation corrections

Three places where the docs and the source disagree, all of which cost us time:

- **`docs/SENTINEL_DESIGN2.md:388`** states "The standard library exposes Argon2id…" and `:394` calls it "the standard password hashing and low-entropy key derivation primitive." **This is false against the source.** Argon2 appears nowhere outside design prose and `docs/BACKLOG.md:178`; a repo-wide search finds no implementation. We planned a password-KDF design around this sentence before checking. `docs/decisions/0030-go-no-go-tls-handshake.md:176` is the accurate one — it lists Argon2id as ecosystem/future work. (BLAKE2 is likewise absent entirely, zero hits repo-wide, but no doc claims otherwise.)
- **ADR 0059 (`docs/decisions/0059-c-abi-export.md`)** is stale in both directions. It repeatedly states that the caller-provides-buffer convention (`&mut [u8]` out-params) is "STILL deferred" (A7, A8, A9) — **it is not deferred; it works**, and it is the single most useful thing in the whole C-ABI feature (see §6). And it says `--shared` emits `.dylib` and is macOS-only, with A4 deferring the Linux path, while `archive_lib` (`crates/sentinel-driver/src/main.rs:1566`) already has three branches including a working Windows `lib.exe` path that the ADR never mentions.
- ADR 0059 should also carry the Windows system-library requirement from R8.

We are happy to send PRs for all three.

---

## 6. What already works — and works well

This section is not a courtesy. The reason this document is as short as it is, and the reason we are committing to the port at all, is that the hard parts already work. Everything below is verified by us, on this hardware, with the shipped compiler.

**The C-ABI static library path is production-viable today.** `snc build --lib` on Windows produces an archive that MSVC links and calls with no wrapper — the generated header carries `#ifdef __cplusplus / extern "C"` guards, so C++ consumes it directly. Multi-module works: a library `use`-ing `std::security::sha256` and `std::security::hmac` built and linked cleanly.

**Correctness across the boundary is exact, including at scale.** `sha256("abc")` = `ba7816bf…f20015ad` (NIST), HMAC-SHA256 on RFC 4231 TC1 = `b0344c61…2e32cff7`, and SHA-256 of 100 000 `'a'` characters cross-checked against Windows CNG via `Get-FileHash` — exact match. Large-input hashing is correct.

**`&mut [u8]` write-back is real, zero-copy, and survives every attack we could construct.** The wrapper stores the caller's raw pointer straight into the Sentinel fat pointer (`crates/sentinel-codegen/src/lib.rs:1376–1387`), so Sentinel's writes land directly in the host's memory with no hidden copy-in/copy-out under any pattern we tried, including `&mut [u8]` rebinding and interleaved read-then-write running sums. We tested it in **Sentinel-IDE's exact build configuration** — `/subsystem:windows` with `wWinMain`, Debug `/MDd /RTC1 /Od /Zi`, `/W4 /permissive- /EHsc /utf-8`, `UNICODE`, `/MANIFEST:NO` — and got a clean pass on all of: write-back into a stack buffer with bytes past the window untouched; into a `HeapAlloc` buffer at offset 100 with bytes outside `[100,132)` untouched; through a 2-deep private call chain; a full 4 KiB `VirtualAlloc` page with its guard page intact; `src == dst` aliasing behaving as in-place; zero-length slices and `nullptr+0`; an 8 MiB write-back in a single call; and 8 host threads × 200 HMACs into caller buffers. There is no 4 KB limit or any other page-bounded restriction — length is a marshalled `int64_t`.

**It links into the real product, not just a toy.** We relinked Sentinel-IDE's actual production objects plus a Sentinel `seal_core.lib` → link exit 0. No duplicate symbols, no `LNK2038`, no `LNK4098`, and it works across **all four CRT models** (`/MD`, `/MDd`, `/MT`, `/MTd`). The cause is clean: neither the Sentinel object nor `sentinel_runtime.lib` emits a `.drectve` section at all, so there is nothing to conflict.

**The 15 MB archive is a red herring.** Adding an unreferenced `seal_core.lib` grew our real host from 1 310 208 to 1 310 720 bytes — **+512 bytes**. A console host that actually calls SHA-256, HMAC and the buffer exports is 161 792 bytes total. The realistic cost of a Sentinel crypto core in our binary is **~130–150 KB**; the linker discards the rest.

**Runtime integration is undemanding.** No global init is required — every test called exports directly on first use, and the runtime's globals are `OnceLock`-lazy. The ownership contract works as documented: `sentinel_free_bytes` on every returned buffer, no leaks or faults observed. `sentinel_runtime.lib` is merged into the archive by `lib.exe` (`main.rs:1568–1575`), so the host never links it separately.

**Threading is clean.** 32 threads × 3000 iterations (192 000 constant-time secret calls plus 96 000 alloc/free round-trips) across three consecutive runs: zero mismatches, every time. Plus 16 threads × 300 concurrent `sha256` calls, all matching the NIST vector. No global mutable state is reachable on the export path.

**Bounds are genuinely enforced.** A 16-byte output buffer handed a 32-byte tag stops at index 16 with zero bytes spilled past the window. (The *reporting* of that condition is R2; the enforcement itself is correct.)

**The constant-time verifier is live and it is not decorative.** It gates exports — injecting a single `if m0 == m1` on secret data into an otherwise-working unrolled SHA-256 compress function is rejected with `snc: `if` on a `secret bool` condition would leak via timing`. Every fast implementation we built passed it. Module privacy is enforced too (`snc: sub_byte is private to module std::security::aes`). We tested both because we wanted to know whether the security properties survive the aggressive optimisation we were doing to the library code. They do.

**`secret` is not a performance tax on scalars.** 150 M-operation dependent chains: public `i64` 63 ms, `secret i32` 62 ms. Secret scalars are plain LLVM SSA values. This is the most useful single fact we learned, because it means the crypto performance problem is a representation problem, not a security-model problem — and therefore fixable.

**Scalar codegen reaches native parity where the workload allows it.** The xorshift64 chain: 234 ms Sentinel vs 235 ms C `/O2`, identical checksums.

**The primitives that do ship are solid.** ChaCha20 (`std/security/chacha20.sentinel:37`, `:91`), Poly1305 (`std/security/poly1305.sentinel:60`), ChaCha20-Poly1305 seal (`std/security/aead.sentinel:92`), SHA-256 (`std/security/sha256.sentinel:190`), HMAC-SHA256 (`std/security/hmac.sentinel:25`, correctly returning `secret` so the caller does a CT compare), HKDF-SHA256, SHA-512, SHA3/SHAKE/KMAC, X25519/Ed25519/X448/Ed448, three constant-time compares (`Sentinel/ct.sentinel:73`, `:151`, `std/security/sealed_kex.sentinel:92`), the `ct_mask`/`ct_select`/`ct_diff`/`ct_combine`/`ct_rot*` toolkit, and a CSPRNG (`std/sys/random_windows.sentinel:22`). The AES S-box is table-free field inversion — the right call cryptographically, even though it is what makes R13 slow. This is a genuinely good foundation; our AEAD open and our PBKDF2 both composed entirely out of it.

**Outward FFI works better than the docs suggest.** `extern "C" link("kernel32") { … }` self-links with no `--link` flag. `arg_count()` / `arg(i)` work (`crates/sentinel-resolve/src/lib.rs:2925–2928`), including under the custom LLVM entry point. Non-capturing `Fn<T,R>` values work. Heap allocation is unconditional — runtime-sized buffers returned by value from a function are fine. Structs with `[u8]` fields work and cover the multi-return use case that tuples would. Between them, these are why R9 is a stdlib ask instead of a compiler ask.

---

## 7. What Sentinel-IDE commits to

We are not asking for work we will not consume. Concretely, per item:

**On R4 landing (`chacha20poly1305_open`), with R1 (secure-zero):** we implement Seal's AEAD + KDF core as a Sentinel `--lib` that the host links, add `aead_alg = 2` (ChaCha20-Poly1305) to the `.sealed` v2 writer — the format already reserves the id at `src/core/Seal.h:246` — and keep CNG AES-256-GCM as the read path for existing v2 files, which the skippable-slot design already accommodates. That ships the first Sentinel code in the Sentinel-IDE binary, and we will say so publicly. R1 is a hard precondition: we will not move key material into Sentinel while we cannot wipe it.

**On R6 and R7 landing (SHA-256 K-table, PBKDF2 + midstate):** we drop our vendored PBKDF2 and consume the stdlib one, and we contribute back our 120-line midstate implementation, our unrolled variant, and our vector set (c = 1, 2, 4096, 100 000, 600 000) as stdlib tests.

**On R3 landing (header `const` fix):** we delete our header post-processing build step and consume `--emit-header` output directly, which is what we want the supported path to be.

**On R2 landing (abort hook):** we wire it into Sentinel-IDE's existing crash handler so a runtime fault produces a dialog and a save prompt rather than a vanished window. This is what lets us put Sentinel code on paths the user's unsaved work depends on.

**On R5 progressing (bulk throughput):** we move the payload AEAD into Sentinel and retire the CNG payload path once ChaCha20-Poly1305 clears roughly 60 MiB/s. Below that we keep the payload in the host and ship the key-wrap and KDF in Sentinel only — which is still a real win, and is our fallback plan if R5 stalls.

**On R8 landing (Windows libs documented):** we update `CMakeLists.txt` to the documented list and stop treating it as tribal knowledge.

**On R9 landing (`std::fs`):** we port archive and extract — the first two stages of Seal's pipeline — into Sentinel. We will contribute our working `FindFirstFileW` traversal, our UTF-8↔UTF-16 codec with surrogate-pair handling, and our `CreateFileW`-based streaming reader as a starting point, along with the `i64`-not-`ptr` HANDLE convention and the `GetLastError`-must-be-immediate note as documentation.

**Regardless of what lands:** we will re-run this entire benchmark suite against each `snc` release and publish the numbers, including regressions. The IDE exists partly to be this signal, and a benchmark that only gets run once is not a signal. We will also file every future gap in this same format — reproduced, measured, with the workaround attempted and the failed hypotheses stated — because four of our initial conclusions turned out to be wrong, and we would rather send you fewer, better-verified asks than more of them.
