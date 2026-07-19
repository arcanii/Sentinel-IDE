# Sentinel project model

A **Sentinel project** is a folder with a manifest — a **`<name>.sntproject`** file (the
native IDE project file, preferred) or a legacy **`sentinel.toml`** (`findManifest` picks
`*.sntproject` first, else `sentinel.toml`). Both use the same TOML schema below. A project
declares one or more **build targets** (each an **executable** | **library** | later
**shared**/dll), built at a chosen **release tier** (Development/Experimental/Stable/Hardened).
Source files, libraries, metadata, and signing confirmations live in / under the project.

## Creating & opening projects and files (the ≡ menu)

- **New Project…** opens a Save dialog for the `<name>.sntproject` location, then scaffolds the
  project (`createNewProject`): it creates the chosen directory **and any missing parent
  directories** (`SHCreateDirectoryExW`), a `src/` folder with a starter `src/main.sentinel`, and
  the `<name>.sntproject` manifest (a single executable target), then opens it.
- **Open Project…** picks the project **manifest** (`*.sntproject` or `sentinel.toml`) and loads
  its containing folder as the project.
- **New File…** creates a new `.sentinel` source via a Save dialog (defaulting to the project's
  `src/`, else the project/root folder), writes a one-line header, then opens it and refreshes the
  tree so it appears under **Sources**.

## The manifest (`*.sntproject`, else legacy `sentinel.toml`)

```toml
[project]
name    = "crypto-lib"
version = "0.1.0"
type    = "executable"          # executable | library   (later: shared/dll)
entry   = "crypto.sentinel"     # main module (exe) / root module (lib)
icon    = "../art/S2_icon.png"
authors = ["Bryan"]

[build]
src          = "."              # module search root
lib_paths    = []               # → snc --lib-path (ADR 0037)
links        = []               # → snc --link <native lib>
default_tier = "experimental"   # development | experimental | stable | hardened

[signing]
require = "off"                 # off | warn | strict  → snc --require-signatures
trust   = "sentinel-trust.toml" # the consumer trust manifest (ADR 0061)
sign    = false                 # snc sign the artifact → <artifact>.sig

# Optional build targets (Xcode-style). With NO [[target]] blocks the [project]
# above is the single implicit target; declared targets override it. The scheme
# selector (target ▾ · tier ▾) picks the active one.
[[target]]
name  = "crypto"
type  = "executable"
entry = "crypto.sentinel"

[[target]]
name  = "hello"
type  = "executable"
entry = "hello.sentinel"
```

The IDE reads the flat parts via the Win32 profile API (a TOML subset) and strips
quotes; `[[target]]` array-of-tables are hand-parsed (the profile API can't — section
names must be unique). `snc` parses the full TOML.

## Signing confirmations — `sentinel-trust.toml` (ADR 0061)

The **native Sentinel** supply-chain trust model (not Authenticode): Ed25519 keys
(`snc keygen`), detached `.sig` carriers (`snc sign` / `snc verify`), and a consumer
trust manifest listing the **trusted keys** and the **capability ceiling** each one is
bounded to. `snc build --require-signatures off|warn|strict --trust <manifest>` gates
the build against it.

The schema is exactly what snc's parser accepts — `crates/sentinel-trust/src/trust_model.rs`
is `#[serde(deny_unknown_fields)]` over an array of `[[keys]]` tables:

```toml
[[keys]]
name   = "Sentinel-IDE demo key"   # optional, diagnostics only
pubkey = "58ad2d8cf5294de180a25c2cb8046f422d114dbb5c8a3a91f6483b0b9c476ca5"
grants = ["secret", "constant_time", "alloc"]   # optional capability ceiling
```

> **Do not invent keys here.** `pubkey` must be **64 bare hex chars** — an `ed25519:`
> prefix parses and then never matches, silently yielding `UNTRUSTED` (configured-looking,
> enforcing nothing). There is no `[dependencies.<name>]`, no `sig`, no `policy`, and no
> `forbids` in v1; each is a **hard TOML parse error that aborts the build in `warn` *and*
> `strict`**. Four places must agree on this schema: this document,
> `examples/sentinel-trust.toml`, `core/Signing.h::loadTrust`, and `SigningDialog`'s importer.

**Honest scope.** Identity and byte-integrity are genuinely enforced. `grants` is parsed and
intersected for real, but snc's v1 capability extractor only ever detects `ffi` (from `extern`
blocks), so `secret`/`constant_time`/`alloc` are recorded *intent*, not an enforced gate.
Note also that `snc build --lib`/`--shared` never invoke the trust gate at all, so for
Library and Shared targets the manifest is not enforced regardless of `require`.

## How the IDE builds a project

Build/Run act on the **active target** (`name`/`type`/`entry`, with per-target `links`
falling back to the project's). Output goes to `target/<tier>/<name>.<ext>`.

| type | snc command |
|---|---|
| executable | `snc build <entry> -o target/<tier>/<name>.exe [--lib-path …] [--require-signatures … --trust …]` |
| library | `snc build --lib <entry> -o target/<tier>/<name>.a --emit-header <name>.h …` |
| shared | `snc build --shared …` (snc: not yet on Windows — ADR 0060 follow-up) |

## Build tiers (TIERED_RELEASES.md)

Sentinel's build configurations are **release tiers**, not just debug/release:

| Tier | Name | Meaning |
|---|---|---|
| D | **Development** | max instrumentation, all checks, no opt (≈ debug) |
| E | **Experimental** | all safety checks + optimized; default for new code (≈ release) |
| S | **Stable** | selective check elision via a signed trust profile; mature code |
| H | **Hardened** | *extra* checks: memory-poison, anti-tamper, ct-audit, CFI — security-critical |

Opening a folder with a manifest (`*.sntproject`, else legacy `sentinel.toml`) loads the project. The **top of the window** is an
Xcode-style **scheme selector** — two zones, **`target ▾ · tier ▾`**, plus the active
target's output path — and the titlebar shows `project › target (type) · <Tier>`. The
target dropdown lists all `[[target]]`s (also reachable from the **Targets** tree group);
the tier picker offers all four tiers and sets the output dir (`target/<tier>/`). `snc` has
**no tier flag yet** (TIERED_RELEASES is post-1.0), so every tier currently builds at `-O0`
into its own dir — another forcing-function gap the IDE surfaces (FR-16).
CLI: `Sentinel-IDE.exe <folder> --tier <name> [--build]`.

## Explorer views

The left explorer has two views (tabs at the top):

- **Project** — the project as a **Sentinel-icon** root node (`art/S2_icon.png`). Clicking it
  opens the structured **Project Settings** form (below). Below the root: the **manifest**
  (`*.sntproject` or `sentinel.toml`, opens the **raw** file in the editor), `sentinel-trust.toml`,
  a **Targets** group (one node per
  `[[target]]`; clicking activates it) when the project has more than one, and a **Sources** group
  of the `.sentinel` files. Opening a project lands the editor in the **active target's entry source**.
- **Files** — the raw on-disk file tree (folders + `.sentinel` files).

Both use a themed `WC_TREEVIEW` with an image list (project / folder / file / toml icons).

## Project Settings editor

A themed modal form (`src/host/win32/ProjectSettingsDialog.cpp`) edits `sentinel.toml` without the
raw TOML: **Project** (name, version, type radios, entry combo populated from the project's
`.sentinel` files), **Build** (source, lib_paths/links as comma-separated lists, default tier),
and **Signing** (require off/warn/strict radios, trust path, sign checkbox). Opened from the
project root node or `≡ ▸ Project Settings…` (also `--project-settings` on the CLI).

**Save** persists with `saveProject` (`src/core/Project.h`) and reloads. The writer is
**surgical**: it rewrites only the values of the keys the IDE manages and leaves every other
line untouched — comments, blank lines, and unmodeled keys like `icon`/`authors` **and the
`[[target]]` blocks** survive verbatim (TOML forbids duplicate section headers, so a missing
managed key is inserted inside its existing section). Output is UTF-8 + CRLF with a single
trailing newline.

## Signing & Trust panel (ADR 0061)

A status-bar **trust chip** reflects the open file's signature (✓ Signed / ⊘ Unsigned / ⚠
Signature invalid), computed by an async `snc verify`. Clicking it (or `≡ ▸ Signing & Trust…`,
or `--signing`) opens a panel (`src/host/win32/SigningDialog.cpp`) that runs the *real* ADR-0061
subcommands — **`snc keygen`** (Generate Key), **`snc sign … --grant <cap>`** (Sign File),
**`snc verify`** (Verify) — and views the consumer trust manifest (**Name · Trusted key ·
Grants (ceiling)**), with **Import current key as trusted** appending a `[[keys]]` table.

The most capable `snc` is auto-selected by `sncSigningCaps` (`core/Signing.h`), which reports
**verify** and **keygen/sign** as *separate* capabilities. They fail independently: `verify` is
built into snc, but `keygen`/`sign` shell out to `keygen_core.exe`/`sign_core.exe` — Sentinel
programs built beside the snc binary. A build can advertise `snc keygen` in its help text and
still fail at runtime with ``signing tool `keygen_core` not found``, so the probe checks for the
helpers on disk rather than trusting the help output. Each panel action is greyed against the
capability it actually needs.

## Known toolchain gaps (forcing-function — FR-16)

- **build→link — RESOLVED via MSVC-env injection.** snc shells out to the MSVC `link.exe`; the
  IDE auto-detects `vcvars64.bat` (Settings → **MSVC env**, else `findVcvars`/vswhere) and injects
  that environment into the build process, so the linker + import libraries are on PATH. `examples`
  builds at exit 0 → a runnable `crypto.exe`. (Set Settings → **Sentinel (snc)** to pick the
  compiler; blank = auto-detect, preferring a signing+link-capable build.)
- **ADR-0061 signing — split capability, verified 2026-07-19.** *Both* snc builds now carry the
  subcommands and accept `--require-signatures`/`--trust`, so the old "release is a signing-less
  C1.0b" note is obsolete. But they are not equivalent: `keygen`/`sign` shell out to
  `keygen_core.exe`/`sign_core.exe`, which exist only beside **`target\debug\`**. Measured:

  | | `verify` | `keygen` | `sign` |
  |---|---|---|---|
  | `target\release\` | works | fails (`keygen_core` not found`) | fails (`sign_core` not found`) |
  | `target\debug\` | works | works | works |

  `findSnc` therefore ranks by capability, not list order, and selects the debug build.
  `composeBuild` emits `--require-signatures`/`--trust` automatically once `require` is
  `warn`/`strict`.
- **`--lib`/`--shared` skip the trust gate entirely.** `run_trust_gate` is called only from
  snc's executable build path, and the library path silently discards the flags. Library and
  Shared targets show "signing: strict" in the IDE and enforce nothing. Upstream issue.
- **No tier flag** — `snc` builds at `-O0` regardless of tier; tiers only set the output dir.
