---
name: SentinelIDE
description: Native Windows-first IDE for the Sentinel language, built in Sentinel. Dark-primary "Claude-desktop" coral identity modeled on SQLTerminal-Win32; native Win32 + Common Controls v6, OS light/dark follow. This DESIGN.md owns the visual identity.
title: "SentinelIDE — DESIGN"
status: spec-reconciled          # intended-v1 SPEC, reconciled to shipped v0.1.7 (was: draft)
created: 2026-06-27
updated: 2026-09-02              # reconciliation pass — see Reconciliation note + Gap index
reconciled_to: "Sentinel-IDE v0.1.7 (build 164), phases 1–42"
sources:
  - "{planning_artifacts}/REALITY-DELTA.md"                              # RD-nn rows cited throughout
  - "{planning_artifacts}/GAP-REGISTER.md"                               # the single D1 gap register (GAP-An/Bn)
  - "{planning_artifacts}/prds/prd-SentinelIDE-2026-06-27/prd.md"        # §9 Aesthetic, FRs
  - "{planning_artifacts}/briefs/brief-SentinelIDE-2026-06-27/brief.md"  # dark/coral posture
code_embodiment: "src/host/win32/Theme.h"                                 # the palette below IS this file
verified_against:
  - "src/host/win32/Theme.h"                 # every color value below, dark + light
  - "src/host/win32/MainWindow.cpp"          # toolbar, scheme selector, tab strip, gutter, status bar, chip
  - "src/host/win32/SigningDialog.cpp"       # the Signing & Trust panel
  - "src/host/win32/ProjectSettingsDialog.cpp"  # the Project Settings form
  - "src/core/Project.h"                     # targets, tiers, manifest
  - "src/core/Signing.h"                     # SignState, trust manifest, .sig
visual_reference: "G:/SQLTerminal-Win32/src/ui/Theme.h"                   # original palette source
# ── CONVENTION ──────────────────────────────────────────────────────────────
# Dark is the PRIMARY expression (Claude-desktop dark). Base tokens are the DARK
# values; `-light` variants are the OS light-mode follow. The app follows the OS
# setting (Theme.h: themeOverride -1 follow / 0 light / 1 dark), user-overridable
# in Settings. All non-diagnostic colors are sourced verbatim from Theme.h; the
# `diag-*` family was DERIVED for SentinelIDE and has since been adopted verbatim
# into Theme.h (see Colors — every value below is now code-verified).
#
# ── STATUS MARKERS (added 2026-09-02) ───────────────────────────────────────
# This spine is an intended-v1 SPEC. Every component carries a status:
#   SHIPPED           — verified present in v0.1.7 at the cited file.
#   NOT BUILT — RD-nn — designed, still intended, DOES NOT EXIST in v0.1.7.
#   PARTIAL  — RD-nn  — some of it ships; the marker says which part does not.
#   DEAD     — RD-nn  — described a model the product does not have; retired,
#                       kept only as a redirect so a reader is not left guessing.
# A `status:` line in a component block is normative. Nothing below may be read
# as a description of the shipped product unless it says SHIPPED.
colors:
  # Surfaces — tonal layering, deepest → most elevated (dark base)
  window-bg: '#161618'          # deepest surface: editor canvas, window erase
  panel-bg: '#1A1A1C'           # panels: project tree, problems list, dialogs
  panel-elev-bg: '#202023'      # elevated bands: tab strip, status bar
  alt-row-bg: '#1C1C1F'         # zebra-stripe / current-line band  [NOT BUILT — unused in v0.1.7]
  hover-bg: '#2A2A2D'           # hover highlight (shipped use: Build button while building)
  border: '#303034'             # hairline separators (depth is borders, not shadow)
  # Text
  text-primary: '#E6E6E8'
  text-secondary: '#9A9AA0'
  text-muted: '#6A6A70'
  # Accent (coral) — primary action / active state AND the Sentinel-safety signal
  accent: '#D97757'             # Claude coral
  accent-text: '#28120A'        # text drawn on coral
  selection-bg: '#5C3426'       # muted coral: selection / active row  [NOT BUILT — unused in v0.1.7]
  selection-text: '#F7EEE9'     # [NOT BUILT — unused in v0.1.7]
  # Syntax (Theme.h)
  syn-keyword: '#C98FD6'        # orchid — keywords incl. secret/effects/borrow
  syn-number: '#9FD19A'         # green — numeric literals
  syn-string: '#E0966B'         # warm orange — string literals
  syn-comment: '#769676'        # muted green — comments
  # Diagnostics (derived for SentinelIDE; now VERIFIED verbatim in Theme.h)
  diag-error: '#E06C75'         # generic compile error
  diag-warning: '#E5C07B'       # warning
  diag-info: '#6CA8C4'          # info / hint (cool, recessive vs the warm palette)
  diag-security: '#D97757'      # Sentinel safety findings (secret/borrow/effect) — coral, == accent by intent
  # Signing / trust (VERIFIED in Theme.h as `trustVerified`)
  # NAME IS FIXED (owner decision D3): do NOT rename to `trust-signed`. It is cited
  # as {colors.trust-verified} from EXPERIENCE.md and implemented as Theme.h trustVerified.
  trust-verified: '#7FB37A'     # the "Signed" state (asserts origin+integrity only — NOT identity)
  # ── Light-mode follow (Theme.h makeLightTheme) ──
  window-bg-light: '#FFFFFF'        # system COLOR_WINDOW (OS-derived)
  panel-bg-light: '#FFFFFF'         # system COLOR_WINDOW
  panel-elev-bg-light: '#F4F4F6'
  alt-row-bg-light: '#F5F5F5'
  hover-bg-light: '#E8E8EA'
  border-light: '#D6D6DA'
  text-primary-light: '#000000'     # system COLOR_WINDOWTEXT (OS-derived)
  text-secondary-light: '#606066'
  text-muted-light: '#8C8C92'
  accent-light: '#C15F3C'           # coral darkened for light bg
  accent-text-light: '#FFFFFF'
  selection-bg-light: '#FAE8E0'
  selection-text-light: '#4A1B0C'
  syn-keyword-light: '#C7256C'
  syn-number-light: '#800080'
  syn-string-light: '#C41A16'
  syn-comment-light: '#228B22'
  diag-error-light: '#C0392B'
  diag-warning-light: '#8A6D00'
  diag-info-light: '#2C6E9B'
  diag-security-light: '#C15F3C'    # == accent-light
  trust-verified-light: '#2E7D32'
typography:
  ui:                             # native chrome — menus, tree, dialogs, labels
    fontFamily: 'Segoe UI'
    fontSize: 15px                # @96dpi; DPI-scaled via MulDiv (MainWindow makeFont)
    fontWeight: '400'
    lineHeight: '1.4'
    status: SHIPPED               # MainWindow.cpp createFonts — makeFont(15, "Segoe UI", FW_NORMAL)
  ui-emphasis:                    # active labels, section headers
    fontFamily: 'Segoe UI'
    fontSize: 15px
    fontWeight: '600'
    status: NOT BUILT             # no 15px/600 face exists in v0.1.7; see ui-section for what ships
  ui-section:                     # DIALOG SECTION HEADERS — the shipped emphasis face
    fontFamily: 'Segoe UI'
    fontSize: 12px
    fontWeight: '600'
    case: 'upper'                 # "PROJECT" / "BUILD" / "SIGNING" / "FILE SIGNATURE" / "TRUSTED KEYS"
    color: '{colors.accent}'
    status: SHIPPED               # ProjectSettingsDialog.cpp:151 + :90; SigningDialog.cpp:198
  ui-heading:                     # the headline line of a message dialog
    fontFamily: 'Segoe UI'
    fontSize: 17px
    fontWeight: '600'
    color: '{colors.text-primary}'
    status: SHIPPED               # SaveChangesDialog.cpp:71, UpdateDialog.cpp:76
  ui-small:                       # status bar, captions, list metadata, scheme selector
    fontFamily: 'Segoe UI'
    fontSize: 12px
    fontWeight: '400'
    status: SHIPPED
  display:                        # the empty-state wordmark on the editor canvas
    fontFamily: 'Segoe UI'
    fontSize: 30px
    fontWeight: '600'
    color: '{colors.accent}'
    status: SHIPPED               # MainWindow.cpp:137 g.title; painted at :330
  editor:                         # code surface + output pane
    fontFamily: 'Cascadia Code'
    fontSize: 14px                # @96dpi. Shipped: gutter/scheme-path at 14px (MulDiv-scaled);
                                  # RichEdit body at 11pt (≈14.7px @96dpi), Output at 10pt (≈13.3px)
    fontWeight: '400'
    lineHeight: '1.45'
    note: 'Default Cascadia Code (ligatures on). User-overridable in Settings (any installed mono); Cascadia Mono = ligatures off; Consolas = reference fallback if Cascadia absent.'
    status: SHIPPED               # Settings.h editorFont default "Cascadia Code"
rounded:
  # Native Win32 / Common Controls v6 — corners are square to subtly rounded. [ASSUMPTION] (Theme.h defines no radii)
  none: '0px'
  DEFAULT: '2px'                  # hairline-rounded fields / buttons
  sm: '2px'
  md: '4px'                       # dialogs, primary buttons (Win11 native softening)
spacing:
  # Derived from reference dialog metrics (12px margins, ~22-26px control heights). [ASSUMPTION]
  # Dialog metrics are SHIPPED (M=16px margin, 22px fields, 26px buttons, 28px dialog
  # buttons); row-h is the native TreeView/ListView default — never set explicitly.
  '1': '4px'
  '2': '8px'
  '3': '12px'                     # default panel padding / dialog margin (reference)
  '4': '16px'                     # SHIPPED as the dialog margin M in every themed dialog
  '6': '24px'
  '8': '32px'
  gutter: '12px'                  # panel inner padding
  row-h: '22px'                   # tree / list row height  [ASSUMPTION — native default, not set]
  control-h: '26px'               # buttons / inputs (SHIPPED: btnH = 26px, fields 22px)
components:
  editor:
    status: PARTIAL               # RD-14, RD-17
    background: '{colors.window-bg}'
    text: '{colors.text-primary}'
    font: '{typography.editor}'
    gutter-bg: '{colors.panel-bg}'
    gutter-text: '{colors.text-muted}'
    gutter-default: 'off'         # line numbers are OFF by default; Ctrl+L toggles (Settings.lineNumbers)
    error-line-bg: 'blend({colors.window-bg}, {colors.diag-error}, 24%)'   # SHIPPED — whole-line tint
    current-line-bg: '{colors.alt-row-bg}'       # NOT BUILT — no current-line band exists
    selection-bg: '{colors.selection-bg}'        # NOT BUILT — RichEdit paints the system highlight
    selection-text: '{colors.selection-text}'    # NOT BUILT — same
  tab-active:
    status: NOT BUILT             # RD-17 — one buffer; there are no tabs. See file-label.
    background: '{colors.window-bg}'
    text: '{colors.text-primary}'
    indicator: '{colors.accent}'      # 2px top border in coral
  tab-inactive:
    status: NOT BUILT             # RD-17 — a second file replaces the first
    background: '{colors.panel-elev-bg}'
    text: '{colors.text-secondary}'
    dirty-glyph: '{colors.text-secondary}'   # • dot when unsaved
  file-label:                     # what actually occupies the tab strip in v0.1.7
    status: SHIPPED               # MainWindow.cpp:321-326
    band-bg: '{colors.panel-elev-bg}'
    text: '{colors.text-primary}'            # '{colors.text-secondary}' when nothing is open ("untitled")
    dirty-glyph: '●  '                        # LEADING, in the label color
    indicator: '{colors.accent}'              # 1px coral rule at the band's top, label-width only
    bottom-rule: '{colors.border}'
  project-tree:
    status: PARTIAL               # icons + structure ship; the hover/selection tokens do not
    background: '{colors.panel-bg}'
    text: '{colors.text-primary}'
    icons: 'ImageList — S-shield (project, res 100), shell folder, .sentinel (res 101), shell .toml'
    hover-bg: '{colors.hover-bg}'            # NOT BUILT — native DarkMode_Explorer hover is used
    selected-bg: '{colors.selection-bg}'     # NOT BUILT — native theme selection is used
    selected-text: '{colors.selection-text}' # NOT BUILT — same
    row-height: '{spacing.row-h}'
  tree-view-switcher:             # Project | Files band above the tree
    status: SHIPPED               # MainWindow.cpp:311-317
    background: '{colors.panel-bg}'
    active-text: '{colors.text-primary}'
    inactive-text: '{colors.text-secondary}'
    font: '{typography.ui-small}'
    indicator: '{colors.accent}'             # 1px underline beneath the active word
    bottom-rule: '{colors.border}'
  output-pane:
    status: SHIPPED               # colouring is CONTENT-keyed, not stream-keyed
    background: '{colors.window-bg}'
    font: '{typography.editor}'
    text: '{colors.text-primary}'
    error-text: '{colors.diag-error}'        # line contains '×' or "error"
    warning-text: '{colors.diag-warning}'    # line contains "warning"
    echo-text: '{colors.text-secondary}'     # the '> <command>' echo
    note-text: '{colors.text-muted}'         # parenthetical build notes (tier, MSVC env)
    ok-text: '{colors.trust-verified}'       # '[done · exit 0]' and '[signed · <name>.sig]' — see Signing
    link: 'RichEdit CFE_LINK'                # NOT '{colors.accent}': the span is handed to RichEdit,
                                             # which paints its own link color + underline
  problems-list:
    status: PARTIAL               # RD-13 — the list ships; severity does not exist
    background: '{colors.panel-bg}'
    header-bg: '{colors.panel-elev-bg}'
    columns: 'Message · File · Line'         # SHIPPED — there is no Severity column
    row-text: '{colors.text-primary}'        # SHIPPED — every row renders identically
    alt-row-bg: '{colors.alt-row-bg}'        # NOT BUILT — no zebra striping
    error-text: '{colors.diag-error}'        # NOT BUILT — RD-13
    warning-text: '{colors.diag-warning}'    # NOT BUILT — RD-13
    security-text: '{colors.diag-security}'  # NOT BUILT — RD-13
    row-height: '{spacing.row-h}'
  squiggle-error:
    status: NOT BUILT             # RD-14 — no squiggles anywhere; whole-line tint instead
    underline: '{colors.diag-error}'        # wavy
  squiggle-warning:
    status: NOT BUILT             # RD-14
    underline: '{colors.diag-warning}'
  squiggle-security:                         # secret-leak / borrow / effect
    status: NOT BUILT             # RD-14 — the UJ-2 flagship rendering does not exist
    underline: '{colors.diag-security}'      # coral wavy
    gutter-glyph: 'shield'                   # + lock/shield marker, {colors.diag-security}
  diagnostic-badge-security:                 # the distinct Sentinel-safety marker
    status: NOT BUILT             # RD-13/RD-14 — Diag has no severity, so nothing can be badged
    color: '{colors.diag-security}'
    glyph: 'shield-lock'
  toolbar:
    status: SHIPPED               # MainWindow.cpp:365-405
    background: '{colors.panel-bg}'
    bottom-rule: '{colors.border}'
    order: '≡ · Build · Run · Save · Undo · Redo · scheme-selector · derived-command echo'
    build-idle: '{components.button-primary}'
    build-running: 'fill {colors.hover-bg}, label {colors.text-secondary} — "Building…"'
    run-save-undo-redo: '{components.button-default}'   # 1px FrameRect, no fill
    save-dirty: '{colors.accent}'            # "●  Save" goes coral when there are unsaved edits
    save-clean: '{colors.text-muted}'
    undo-redo-glyphs: '↶ ↷'                  # {colors.text-primary} enabled / {colors.text-muted} disabled
    command-echo: '{typography.editor}, {colors.text-muted}'   # read-only; see Do's and Don'ts
  scheme-selector:                # Xcode-style [● target ▾ │ tier ▾] — the project/tier IA
    status: SHIPPED               # RD-10; MainWindow.cpp:383-405, menus at :1221 / :1229
    frame: '{colors.border}'                 # 1px FrameRect around both zones
    divider: '{colors.border}'                # vertical hairline between target and tier zones
    label: '{typography.ui-small}, {colors.text-primary}'
    chevron: '▾'                              # target chevron shown only when >1 target exists
    target-dot:                               # 6px square, keyed to target TYPE
      executable: '{colors.accent}'
      library: '{colors.diag-info}'
      shared: '{colors.diag-warning}'
    tier-dot:                                 # 6px square, keyed to release TIER
      development: '{colors.text-secondary}'
      experimental: '{colors.trust-verified}'  # DEVIATION — see Colors, "Shipped deviations"
      stable: '{colors.accent}'                # DEVIATION — coral outside its three roles
      hardened: '{colors.diag-warning}'
    derived-path: '{typography.editor}, {colors.text-muted}'   # "→ target\<tier>\<name>.exe"
    menus: 'native CreatePopupMenu, radio-checked on the active item'
  status-bar:
    status: SHIPPED
    background: '{colors.panel-elev-bg}'
    top-rule: '{colors.border}'
    font: '{typography.ui-small}'
    zone-caret: '{colors.text-secondary}'    # "Ln 1, Col 1" at the left
    zone-message: '{colors.text-muted}'      # transient message / open path
    zone-sign: '{components.status-signing}' # the chip — right side, left of the version
    zone-version: '{colors.text-muted}'      # "Sentinel    0.1.7 (build 164)", far right
  button-primary:                            # Build / dialog default
    status: SHIPPED
    background: '{colors.accent}'
    text: '{colors.accent-text}'
    radius: '{rounded.md}'
    height: '{spacing.control-h}'
  button-default:
    status: SHIPPED
    background: 'none'                       # toolbar buttons are an unfilled 1px frame
    text: '{colors.text-primary}'
    border: '{colors.border}'
    radius: '{rounded.md}'
    height: '{spacing.control-h}'
  dialog:
    status: SHIPPED
    background: '{colors.panel-bg}'
    titlebar-bg: '{colors.panel-elev-bg}'    # DWM immersive dark caption (attr 35/36/34)
    titlebar-text: '{colors.text-secondary}'
    border: '{colors.border}'
    radius: '{rounded.md}'
    field-bg: '{colors.window-bg}'           # EDIT / LISTBOX (Theme.h dialogCtlColor)
    hint-text: '{colors.text-muted}'
    used-for: 'About · Settings · Project Settings · Signing & Trust · Save changes · Seal password · Software update'
    not-used-for: 'toolchain remediation [NOT BUILT — RD-19] · find/replace [NOT BUILT — RD-18]'
  app-menu-button:                           # ≡ in the toolbar
    status: SHIPPED
    glyph: 'hamburger'
    color: '{colors.text-primary}'
    hover-bg: '{colors.hover-bg}'            # NOT BUILT — no hover state is painted
  menu-popup:                                # native CreatePopupMenu + TrackPopupMenu
    status: PARTIAL                          # the popup ships; its surface is the OS's, not ours
    background: '{colors.panel-elev-bg}'     # INTENT — actual surface comes from uxtheme ForceDark
    border: '{colors.border}'                # INTENT — same
    radius: '{rounded.md}'                   # INTENT — same
    item-text: '{colors.text-primary}'       # INTENT — same
    item-hover-bg: '{colors.hover-bg}'       # INTENT — same
    accelerator-text: '{colors.text-muted}'  # SHIPPED as '\t'-separated accelerators in the item string
    separator: '{colors.border}'
    theming: 'uxtheme SetPreferredAppMode(ForceDark) + FlushMenuThemes (ordinals 135/136)'
  status-signing:                            # status-bar signing indicator — FOUR states
    status: SHIPPED                          # RD-04, RD-05; MainWindow.cpp:414-430
    bound-to: 'the OPEN SOURCE FILE (<file>.sig), never a build artifact'
    signed:   '✓ Signed             — {colors.trust-verified}'
    invalid:  '⚠ Signature invalid  — {colors.diag-error}'
    checking: '…  verifying          — {colors.text-muted}'
    unsigned: '⊘ Unsigned           — {colors.text-secondary}'
    present-unverified: '{colors.text-secondary} + the Signed glyph, distinct copy'   # NOT BUILT — see below
    click: 'opens {components.signing-panel}'
    known-defect: 'the Unknown state (no .sentinel file open) falls through to the Unsigned branch and paints "⊘ Unsigned", while {components.signing-panel} labels the same state "—  no file open" (RD-05, §8.3)'
    known-defect-2: 'a .sig that this snc build CANNOT verify is currently shown as "✓ Signed" in {colors.trust-verified} — the green claims a verification that never ran; present-unverified exists to fix that and is NOT BUILT'
  signing-panel:                             # "Signing & Trust" — the real ADR-0061 surface
    status: SHIPPED                          # RD-01; SigningDialog.cpp
    background: '{components.dialog}'
    width: '620px @96dpi'
    section-header: '{typography.ui-section}'   # "FILE SIGNATURE" / "TRUSTED KEYS · sentinel-trust.toml"
    state-line: 'Segoe UI 20px/600, colored by {components.status-signing}'
    detail-text: '{colors.text-primary}'        # + "Key: <16-hex>…     Grants: …"
    hint-text: '{colors.text-muted}'
    key-row: 'Key file field + [Browse…] [Generate Key…]'   # a PATH to sentinel.key — no passphrase
    grants-row: 'Grants field (comma-separated) + [Sign File] (default) [Verify]'
    trust-list: 'LVS_REPORT + gridlines — Name · Trusted key · Grants (ceiling)'
    import-button: '[Import current key as trusted…]'
    capability-gating: 'Generate Key / Sign File disabled when snc lacks keygen_core/sign_core; Verify disabled when snc cannot verify'
    absent-by-design: 'no certificate, no passphrase, no subject/issuer, no validity window'
  project-settings-form:                     # the manifest editor — the missing IA region's form
    status: SHIPPED                          # RD-11; ProjectSettingsDialog.cpp
    background: '{components.dialog}'
    section-header: '{typography.ui-section}'   # PROJECT · BUILD · TARGETS · SIGNING
    layout: 'label column ({colors.text-primary}) + field column, 22px fields, {spacing.4} margin'
    radio-groups: 'type (Executable/Library/Shared) · signing require (off/warn/strict)'
    tier-combo: 'Development / Experimental / Stable / Hardened'
    targets-section: 'shown only for a manifest with explicit [[target]] blocks — selector + Name/Entry/Type'
    hint-text: '{colors.text-muted}'
    buttons: '[Cancel] [Save] — {components.button-default} / {components.button-primary}'
  message-dialog:                            # the shared shape of the three decision dialogs
    status: SHIPPED                          # SaveChangesDialog.cpp, UpdateDialog.cpp, PasswordDialog.cpp
    background: '{components.dialog}'
    width: '440px @96dpi (420px for password)'
    margin: '18px'
    heading: '{typography.ui-heading}'
    body: '{typography.ui}, {colors.text-secondary}'
    buttons: '28px tall, right-aligned, default rightmost'
  save-changes-dialog:
    status: SHIPPED                          # RD-25 — the phase-39 guard's one modal
    base: '{components.message-dialog}'
    heading: 'Save changes to “<file>”?'
    buttons: '[Cancel] [Don''t Save] [Save·default]'   # three answers; Cancel aborts the caller
  password-dialog:
    status: SHIPPED                          # RD-23 — sealed projects
    base: '{components.message-dialog}'
    fields: 'one ES_PASSWORD field, two when sealing (double entry)'
    error-text: '{colors.diag-error}'
    buttons: '[Cancel] [Seal·default] or [Cancel] [Unlock·default]'
  update-dialog:
    status: SHIPPED                          # RD-21 — WinSparkle appcast
    base: '{components.message-dialog}'
    heading: 'An update is available'
    buttons: '[Skip this version] (left-aligned) … [Install now] [Later·default]'
  import-key-dialog:
    status: DEAD                             # RD-01 — Authenticode-era. See {components.signing-panel}.
    replaced-by: '{components.signing-panel}'
    reason: 'ADR-0061 has no key files to import, no passphrase and no key identity to display'
---

# SentinelIDE — DESIGN.md

> Visual-identity spine (Google Labs design.md). Owns *how it looks*. Behavior, IA, and
> flows live in [EXPERIENCE.md](EXPERIENCE.md), which references these tokens by `{path.to.token}`.
> **This spine wins on conflict** with any mock, wireframe, or import.
> Palette verified verbatim against the code embodiment `src/host/win32/Theme.h`.

> **RECONCILIATION NOTE (2026-09-02).** This document was written 2026-06-27/28, before the
> Win32 prototype existed. It has been reconciled against shipped **v0.1.7 (build 164)** using
> [REALITY-DELTA.md](../../REALITY-DELTA.md), which is cited by row id (`RD-01`…) throughout.
> It remains an **intended-v1 SPEC**: designed-but-unbuilt visuals are kept, not deleted, and
> every one of them is marked `[NOT BUILT — RD-nn]` at the point of the claim and listed again
> in the [Gap register](#gap-register). **Nothing here describes the shipped product unless it
> says SHIPPED.** Three things changed in kind rather than in wording: the signing surfaces
> (Authenticode → ADR-0061), the project/tier IA (added — it was entirely absent), and the
> signing chip (six states bound to an artifact → four states bound to the open file).

## Brand & Style

SentinelIDE is a **native Windows desktop IDE** — not a web app in a window, not Electron. Its whole argument is that the dangerous parts are built in Sentinel and structurally cannot carry the bug; the visual identity has to *earn that seriousness without performing it*. The posture is **calm, precise, and quietly confident** — assurance, not noise (PRD §9). A bank's engineer should feel they're using a sober, fast, professional tool; the coral warmth keeps it from reading cold or clinical.

The reference is **SQLTerminal-Win32**: native Win32 + Common Controls v6 — a RichEdit editor, a tree sidebar, virtual lists, themed dialogs, a status bar. We borrow its **dark/coral "Claude-desktop" look**, *not* its code. Dark is the **primary** expression; light is the OS-follow variant (the app tracks the Windows light/dark setting, user-overridable in Settings).

The aesthetic discipline is **native restraint**. Depth comes from tonal layering and hairline borders, not web-style drop-shadows. Corners are square to subtly rounded. There are no gradients, no decorative chrome, no celebratory motion. The one warm signal — coral — is spent deliberately: on the primary action, on the active thing, and on the product's headline moment, *Sentinel's safety guarantee appearing in the editor* **[NOT BUILT — RD-14: that moment has no rendering yet; see Diagnostics]**.

## Colors

The palette is a **dark-primary, single-accent** system: a deep neutral grey ladder, warm-grey text, one coral accent, Theme.h's syntax colors, and the diagnostic set. Base tokens are the **dark** values; `-light` variants follow the OS into light mode. Light-mode `window-bg`/`panel-bg`/`text-primary` are **OS-derived** (`COLOR_WINDOW` / `COLOR_WINDOWTEXT`) so the app sits naturally in the user's system — the `#FFFFFF`/`#000000` shown are the typical resolved values.

**Verification (2026-09-02).** Every colour value in the frontmatter — all 22 dark tokens and all 22 light ones — was checked against `src/host/win32/Theme.h` and matches exactly, including the originally-`[ASSUMPTION]` `diag-*` family and `trust-verified`. The assumption markers are therefore **discharged**: this palette is no longer proposed, it is the code. What is *not* true of every token is that it is used — see "Defined but unused" below (RD §7 confirms the palette; the staleness in this document was always in component behaviour, never in colour).

**Surfaces — depth by tone.** Four greys stack to build hierarchy without shadow: `{colors.window-bg}` `#161618` is the deepest layer (the editor canvas and window); `{colors.panel-bg}` `#1A1A1C` is panels (tree, problems, dialog bodies); `{colors.panel-elev-bg}` `#202023` is elevated bands (the tab strip, the status bar); `{colors.alt-row-bg}` is zebra striping. `{colors.hover-bg}` lifts a row on hover; `{colors.border}` `#303034` draws the hairlines that separate regions. *This tonal ladder is the elevation system* (see Elevation & Depth).

**Text.** `{colors.text-primary}` `#E6E6E8` for content; `{colors.text-secondary}` for labels, inactive words, the caret readout; `{colors.text-muted}` for line numbers, the derived-command echo, and de-emphasized metadata.

**Coral — the one accent.** `{colors.accent}` `#D97757` (the Claude coral) has exactly **three sanctioned roles** and no others:
1. **Primary action** — the Build button, dialog default button (`{colors.accent-text}` text on the fill).
2. **Active state** — the active-file top rule, the Project|Files and Problems|Output underlines, the selected tree/list row (intended as the muted-coral `{colors.selection-bg}`).
3. **The Sentinel-safety signal** — `{colors.diag-security}` is the *same coral by intent*, always paired with a shield/lock glyph so it never reads as mere decoration. Making the language's headline guarantee render in the brand color is the point (UJ-2). **[NOT BUILT — RD-13/RD-14: no severity, no shield, no coral diagnostic anywhere in v0.1.7.]**

Coral is **never** used for generic chrome, decorative fills, gradients, or ordinary state badges.

**Shipped deviations from the coral rule (v0.1.7).** Recorded, not blessed — the rule above still governs new work, and these are the four places to revisit if the rule is to hold:
- **Dialog section headers** (`PROJECT`, `BUILD`, `SIGNING`, `FILE SIGNATURE`, `TRUSTED KEYS`) are painted in `{colors.accent}` — coral as a *structural label*, a fourth role. It is consistent across all four themed dialogs, so it reads as deliberate; it should either be sanctioned as a role or moved to `{colors.text-secondary}`.
- **Scheme-selector dots** spend `{colors.accent}` on the *executable* target type and on the *Stable* tier — coral as an identity marker, not an action or a finding.
- **The Experimental tier dot** reuses `{colors.trust-verified}`. This is the one deviation with a real cost: it puts the signing green on a control that has nothing to do with signing, which is exactly the valence-inversion the signing section argues against. Prefer a neutral for it.
- **`[done · exit 0]`** in the Output pane is printed in `{colors.trust-verified}`. Same objection, smaller blast radius.

**Defined but unused (v0.1.7).** `{colors.alt-row-bg}`, `{colors.selection-bg}` and `{colors.selection-text}` exist in `Theme.h` and are referenced by no drawing code: there is no current-line band, no zebra striping, and both the tree and the Problems list use the native `DarkMode_Explorer` selection rather than the muted coral. `{colors.hover-bg}` survives in exactly one place — the Build button while a build runs. These are `[NOT BUILT]`, not wrong: the tokens are correct and the intent stands.

**Syntax (Theme.h).** `{colors.syn-keyword}` orchid carries keywords — including the security-relevant `secret`, effect, and borrow keywords; `{colors.syn-number}` green for numbers; `{colors.syn-string}` warm-orange for strings; `{colors.syn-comment}` muted-green for comments. (Light mode shifts to higher-contrast hues per Theme.h: magenta keywords, purple numbers, red strings, forest-green comments.) **SHIPPED** — the highlighter applies all four.

**Diagnostics.** Tuned **muted on purpose** to honor the non-alarmist tone: `{colors.diag-error}` `#E06C75` (a rose-red, distinct from coral so errors ≠ the safety signal), `{colors.diag-warning}` `#E5C07B` gold, `{colors.diag-info}` `#6CA8C4` a recessive cool blue. `{colors.diag-security}` is coral — reserved for Sentinel's own findings (`secret`-leak, borrow, effect) and always shield-marked. **Rule:** an ordinary compile error is `diag-error` red; a Sentinel *safety* finding is `diag-security` coral + shield. The difference is intentional and product-defining.

> **[NOT BUILT in v0.1.7 — RD-13.]** `struct Diag { file; line; col; msg; }` has **no severity field**. The Problems list is three columns — Message · File · Line — and every row is drawn in `{colors.text-primary}`. There is no security class, no shield, and no per-severity colour anywhere in the product. `{colors.diag-security}` is currently spent only on the scheme selector's *executable* dot (see Shipped deviations), which is the opposite of its purpose.
>
> **What ships instead:** after a build, the lines carrying diagnostics in the open file get their whole-line background tinted `blend({colors.window-bg}, {colors.diag-error}, 24%)`, cleared on the next edit. It is severity-blind — a `secret` leak and a missing semicolon look identical. `{colors.diag-error}` and `{colors.diag-warning}` do reach the screen, but only as *Output pane line colours*, keyed by whether the text contains `×`/`error`/`warning` — not by a structured severity.

**Signing / trust.** `{colors.trust-verified}` (a calm green) marks a **Signed** file in the signing status indicator — it asserts the file carries a signature that verifies against its `.sig` (origin + byte integrity), **not** that the key or its holder is vetted (UI copy says "Signed," never "Verified"; per adversarial-review A2). *(Token name is fixed by owner decision **D3** — do not rename to `trust-signed`. It is cited as `{colors.trust-verified}` from EXPERIENCE.md and implemented as `Theme.h` `trustVerified`.)* Coral is **deliberately not** reused here: coral's valence in this system is *"a safety finding needs your attention,"* and "signed = good" would invert it. An **unsigned** file is a neutral state (`{colors.text-secondary}`), not an alarm; a **broken** signature is `{colors.diag-error}`; **verification-in-progress** is `{colors.text-muted}` — a recessive wait, not an active coral state.

## Typography

Two type families, both native to Windows:

- **`{typography.ui}` Segoe UI** — all chrome: menus, the project tree, dialogs, the Problems list, labels. Three emphasis faces sit above it, each with exactly one job: `{typography.ui-section}` (12px/600, upper-case, coral) heads a section *inside* a form dialog; `{typography.ui-heading}` (17px/600, primary) is the headline of a decision dialog ("Save changes to …?", "An update is available"); `{typography.display}` (30px/600, coral) is the empty-state wordmark on the editor canvas. `{typography.ui-small}` (12px) carries the status bar, the tree's view switcher, the dock tabs and the scheme selector. This is the platform's own UI font; using it is what makes the app feel native rather than ported.
  - `{typography.ui-emphasis}` (15px/600) is **[NOT BUILT]** — no 15px semibold face is created anywhere in v0.1.7. Keep it as the intended face for in-chrome emphasis (active labels), and mint nothing new before checking whether `ui-section` already covers the case.
- **`{typography.editor}` Cascadia Code** — the editor and the output pane. Ligatures **on** by default. It ships with Windows, is purpose-built for code, and is **user-overridable in Settings** to any installed monospace (Cascadia Mono for ligatures-off; Consolas — the SQLTerminal reference font — as the fallback if Cascadia is absent). The same face also draws the line-number gutter, the toolbar's derived-command echo and the scheme selector's output path, so a font change moves all four together.

All chrome sizes are 96-dpi design values, **DPI-scaled** per-monitor via `MulDiv`. The two RichEdit surfaces are sized in points instead (editor 11pt ≈ 14.7px @96dpi, Output 10pt ≈ 13.3px), which is why the Output pane reads one step smaller than the editor. Type is the only place text size is set; never simulate hierarchy with color alone.

## Layout & Spacing

Native desktop density — **compact, information-dense, no wasted air**. The spacing scale (`{spacing.1}`–`{spacing.8}`) is derived from the reference's dialog metrics: `{spacing.4}` (16px) is the shipped dialog margin, `{spacing.3}` (12px) the panel padding, `{spacing.gutter}` the panel inner padding. Native control metrics: `{spacing.control-h}` (26px) buttons, 22px fields, 28px for a dialog's bottom-row buttons. Two dialog margins ship, and the difference is meaningful: **16px** for the wide form dialogs (Settings, Project Settings, Signing & Trust) and **18px** for the narrow message dialogs, which are airier because they carry one sentence and a decision. `{spacing.row-h}` (22px) remains an `[ASSUMPTION]` — the tree and list use the native default row height and never set one.

The shell is a classic **three-region IDE layout** — tree sidebar, editor area with a file-label band, and a bottom dock (Problems / Output) — separated by draggable splitters and bounded by `{colors.border}` hairlines, under a **toolbar** band that carries the app menu, the build/run/edit buttons and the **scheme selector**, over a **status bar** that carries the caret readout, a message, the signing chip and the version. (The information architecture and panel behavior are owned by [EXPERIENCE.md](EXPERIENCE.md); this section governs only density and separation.) Regions are resizable; the editor area is the priority surface and never collapses.

## Elevation & Depth

**Depth is tonal, not cast.** This is the defining native choice and the sharpest break from web idiom: SentinelIDE has **no drop-shadows as hierarchy**. Layers are distinguished by the surface ladder — `{colors.window-bg}` → `{colors.panel-bg}` → `{colors.panel-elev-bg}` — and by `{colors.border}` hairlines. A header band is "above" its rows because it is one tone lighter, not because it floats. Hover and selection are tonal shifts (`{colors.hover-bg}`, `{colors.selection-bg}`), never shadow — though in v0.1.7 both of those are mostly **[NOT BUILT]**: the native control themes supply selection, and only the Build button paints a hover tone.

The only true overlays are **native dialogs and popups** (themed via DWM immersive dark mode — dark caption, `{colors.border}` border). They get the OS's own shadow because they are genuinely separate windows; that is the *one* place a shadow is correct, and it comes from the platform, not from us.

## Shapes

Native Win32 geometry: **square to subtly rounded** `[ASSUMPTION]` (Theme.h specifies no radii). `{rounded.none}` for the editor canvas, splitters, and panel edges; `{rounded.DEFAULT}`/`{rounded.sm}` (2px) for fields and list selection; `{rounded.md}` (4px) for buttons and dialog corners, matching Windows 11's gentle native softening. **No pill shapes, no large web-style radii** — crispness reads "professional tool," which is the whole brand. (In v0.1.7 the toolbar's own buttons are drawn as flat `FrameRect`/`fillRect` rectangles — the radii are honoured by the native controls in dialogs, not by the owner-drawn toolbar.)

## Components

Visual specs (anatomy, color, sizing, state). Behavioral rules live in [EXPERIENCE.md](EXPERIENCE.md).
**Each bullet's status marker is normative** — see the CONVENTION block in the frontmatter.

### Editor & diagnostics

- **Editor** (`{components.editor}`) — **PARTIAL.** `{colors.window-bg}` canvas, `{typography.editor}` text, syntax per the `syn-*` tokens: all SHIPPED. The line-number gutter (`{colors.text-muted}` on `{colors.panel-bg}`) SHIPPED but is **off by default** (Ctrl+L). Current-line banding with `{colors.alt-row-bg}` and the coral `{colors.selection-bg}`/`{colors.selection-text}` selection are **[NOT BUILT]** — RichEdit paints the system highlight and no current-line band exists. What the editor *does* paint beyond spec is the post-build **error-line tint**, `blend({colors.window-bg}, {colors.diag-error}, 24%)` across the whole line, cleared on edit.
- **File label** (`{components.file-label}`) — **SHIPPED.** The `{colors.panel-elev-bg}` band above the editor holds one filename (or `untitled`), a **leading** `●` dirty glyph, and a coral top rule the width of the label. This is what occupies the space the tab strip was designed for.
- **Tabs** (`{components.tab-active}` / `{components.tab-inactive}`) — **[NOT BUILT in v0.1.7 — RD-17.]** Design intent, unchanged and still wanted: active tab `{colors.window-bg}` (continuous with the editor) + a 2px coral top-border; inactive `{colors.panel-elev-bg}` + `{colors.text-secondary}`; a `•` dirty glyph. The product has **one buffer** — opening a second file replaces the first — so none of this renders.
- **Problems list** (`{components.problems-list}`) — **PARTIAL — RD-13.** SHIPPED: a `SysListView32` on `{colors.panel-bg}` with a `{colors.panel-elev-bg}` header band and three columns, **Message · File · Line**, every row in `{colors.text-primary}`. **[NOT BUILT]:** the severity keying — error rows `{colors.diag-error}`, warnings `{colors.diag-warning}`, **Sentinel-safety rows `{colors.diag-security}` with a leading shield glyph** — and the zebra `{colors.alt-row-bg}`. There is no severity on a diagnostic to key any of it from.
- **Squiggles** (`{components.squiggle-error}` / `-warning` / `-security`) — **[NOT BUILT in v0.1.7 — RD-14, GAP-A1.]** Wavy underline at the diagnostic span: red, gold, and **coral + a gutter shield** for `secret`/borrow/effect findings. This is the UJ-2 flagship rendering and **it does not exist**: there are no squiggles anywhere, the gutter paints line numbers only, and the shipped substitute is the severity-blind whole-line tint above. **A visual constraint belongs on the record here:** the editor is RichEdit 4.1, whose wavy underline (`CFU_UNDERLINEWAVE`) takes a **16-entry palette index**, not an RGB — so these three colours, and `{colors.diag-security}` coral in particular, **cannot be expressed through the control's own API**. Building this means custom-drawing over the editor, not setting a character format. Do not spec a coral squiggle as if it were a format flag.
- **Security badge** (`{components.diagnostic-badge-security}`) — **[NOT BUILT in v0.1.7 — RD-13/RD-14.]** The shield-lock marker that distinguishes a Sentinel safety finding from a compile error.
- **Output pane** (`{components.output-pane}`) — **SHIPPED, with one correction.** `{colors.window-bg}`, monospace. Line colour is keyed by **content, not by stream**: a line containing `×` or `error` is `{colors.diag-error}`, `warning` is `{colors.diag-warning}`, the leading `> <command>` echo is `{colors.text-secondary}`, build notes are `{colors.text-muted}`, everything else `{colors.text-primary}`. (The old `stderr-text` token claimed a stdout/stderr split the product never made — both streams are merged before colouring.) A parsed `file:line[:col]` is handed to RichEdit as `CFE_LINK`, so it renders in **RichEdit's own link colour + underline, not `{colors.accent}`** — an open item if the coral link is wanted.

### Chrome

- **Toolbar** (`{components.toolbar}`) — **SHIPPED.** A `{colors.panel-bg}` band under the title bar with a `{colors.border}` bottom rule, holding, left to right: the `≡` app-menu button, **Build** (coral fill; `{colors.hover-bg}` + "Building…" while running), **Run**, **Save** (which goes `●  Save` in `{colors.accent}` when there are unsaved edits, `{colors.text-muted}` otherwise), **Undo/Redo** `↶ ↷` glyph buttons greyed to `{colors.text-muted}` when unavailable, the **scheme selector**, and the derived-command echo in `{typography.editor}`/`{colors.text-muted}`.
- **Scheme selector** (`{components.scheme-selector}`) — **SHIPPED — RD-10. New IA region; it appears in no earlier version of this spine.** An Xcode-style two-zone control, `[● target ▾ │ tier ▾]`, framed in `{colors.border}` with a hairline divider. The left zone shows a 6px **type dot** (executable `{colors.accent}`, library `{colors.diag-info}`, shared `{colors.diag-warning}`) and the active target's name, with a `▾` only when the manifest declares more than one `[[target]]`. The right zone shows a **tier dot** and the tier name — Development `{colors.text-secondary}`, Experimental `{colors.trust-verified}`, Stable `{colors.accent}`, Hardened `{colors.diag-warning}` (the first two are flagged under Shipped deviations). Both zones open native popup menus with the active item radio-checked. To the right of the control the derived output path — `→ target\<tier>\<name>.exe` — is echoed in `{typography.editor}`/`{colors.text-muted}`. When no project is loaded the whole control is replaced by a `snc build <file>` echo in the same muted mono.
- **Project tree** (`{components.project-tree}`) — **PARTIAL.** `SysTreeView32` on `{colors.panel-bg}` with a real 4-icon ImageList — an **S-shield** for the project root (resource 100), the shell folder icon, a custom `.sentinel` file icon (resource 101), the shell `.toml` icon. (The earlier "file-type icons in `{colors.text-secondary}`" was wrong: these are OS/resource icons, not tinted glyphs.) In a manifest-backed project the tree is structured — root (project name, click opens Project Settings) → manifest → `sentinel-trust.toml` → **Targets** group (only when the manifest declares more than one) → **Sources**. Hover `{colors.hover-bg}` and the coral `{colors.selection-bg}`/`{colors.selection-text}` selection are **[NOT BUILT]**; the native `DarkMode_Explorer` theme supplies both.
- **Tree view switcher** (`{components.tree-view-switcher}`) — **SHIPPED.** A `Project | Files` pair in `{typography.ui-small}`, active word `{colors.text-primary}` and underlined in `{colors.accent}`, inactive `{colors.text-secondary}`, over a `{colors.border}` rule. A folder with no manifest opens straight into **Files**.
- **Status bar** (`{components.status-bar}`) — **SHIPPED.** `{colors.panel-elev-bg}` band, `{typography.ui-small}`, four zones: the caret readout (`Ln n, Col n`) in `{colors.text-secondary}`, a transient message in `{colors.text-muted}`, then right-aligned the **signing chip** and the version (`Sentinel    0.1.7 (build 164)`) in `{colors.text-muted}`. Per the *About-dialog-only* decision the status bar carries no security/trust *metric* — the chip is a state, not a score.
- **Buttons** — `{components.button-primary}` coral fill for Build and dialog defaults; `{components.button-default}` for everything else. Note the toolbar's variant is an **unfilled 1px `{colors.border}` frame**, not a `{colors.panel-elev-bg}` fill; dialog buttons are native and filled.
- **App-menu button** (`{components.app-menu-button}`) — a `≡` glyph in the toolbar (SHIPPED in `{colors.text-primary}`; the `{colors.hover-bg}` hover is **[NOT BUILT]**) that opens the **menu popup**.
- **Menu popup** (`{components.menu-popup}`) — **PARTIAL.** A native `CreatePopupMenu`/`TrackPopupMenu` popup with `\t`-separated accelerators, forced dark by `uxtheme`'s `SetPreferredAppMode(ForceDark)` + `FlushMenuThemes` (ordinals 135/136). Its surface, hover and separator colours therefore come from the **OS**, not from our tokens — the token values in the frontmatter are the *intent* the OS's dark menu happens to approximate, and must not be read as painted values. Faithful to the reference's popup style — a popup, not a permanent menu bar.
- **Dialogs** (`{components.dialog}`) — **SHIPPED.** Themed native windows: `{colors.panel-bg}` body, `{colors.panel-elev-bg}` DWM dark caption, `{colors.border}` border, `{rounded.md}` corners, `{colors.window-bg}` field interiors, `{colors.text-muted}` hints. In v0.1.7 these are **About, Settings, Project Settings, Signing & Trust, Save changes, Seal password, and Software update**. The two the old spine listed — **toolchain remediation [NOT BUILT — RD-19]** and **find/replace [NOT BUILT — RD-18]** — do not exist. They split into two shapes: the wide **form** dialogs (Settings, Project Settings, Signing & Trust — a label/field grid under `{typography.ui-section}` headers) and the narrow **message** dialogs below.
- **Message dialogs** (`{components.message-dialog}`) — **SHIPPED.** One shape, three instances, all 440px wide (420px for the password) with an 18px margin, a `{typography.ui-heading}` headline, `{colors.text-secondary}` body, and 28px buttons right-aligned with the default rightmost:
  - `{components.save-changes-dialog}` — **RD-25.** `Save changes to “<file>”?` over `[Cancel] [Don't Save] [Save]`. **Three answers, not two** — Cancel means the caller changes nothing, which is what makes it safe to route every discarding path through it.
  - `{components.password-dialog}` — **RD-23.** One password field, two when sealing (double entry), with a `{colors.diag-error}` error line above the buttons.
  - `{components.update-dialog}` — **RD-21.** `An update is available` over a left-aligned `[Skip this version]` and a right-aligned `[Install now] [Later]`, with **Later** as the default — the calm choice is the one Return picks.

### Signing & trust

- **Signing status indicator** (`{components.status-signing}`) — **SHIPPED, with two structural corrections.**
  - **It is bound to the open source file, not to a build artifact (RD-04).** The chip reads `<open file>.sig`, runs `snc verify` asynchronously, and recomputes on file-open and on **save** — **not on edit**, so a signed file being edited keeps showing `✓ Signed` until saved (a known staleness window, GAP-A). A build never touches it. The earlier claim that "the chip is bound to the current artifact and is never stale" describes a different product.
  - **Four states, not six (RD-05):** `✓ Signed` `{colors.trust-verified}` · `⚠ Signature invalid` `{colors.diag-error}` · `…  verifying` `{colors.text-muted}` · `⊘ Unsigned` `{colors.text-secondary}`. Clicking it opens the Signing & Trust panel.
  - **A fifth state is specified and not built: `present-unverified`.** When the resolved `snc` cannot verify, a file that merely *has* a `.sig` is currently painted `✓ Signed` in `{colors.trust-verified}` — the green asserts a verification that never ran, and only the panel's detail line ("Signature present — this snc build can't verify it") says so. The chip needs a distinct treatment for it: the signature glyph in `{colors.text-secondary}` with its own copy, never the green. **[NOT BUILT in v0.1.7 — this is the honesty gap the four-state model leaves open.]**
  - The former `key-loaded` and `no-key` sub-tokens are **DEAD, not deferred** — the IDE never holds a key (see the honesty note below), so neither state has anything to refer to. `signing` (in-progress) survives as `checking` but in `{colors.text-muted}`, not `{colors.accent}`: verification is a wait, not an action. `failed` survives as `invalid`, same `{colors.diag-error}`.
  - **Known defect (RD-05, §8.3):** the `Unknown` state — no `.sentinel` file open — falls through the status bar's `default:` branch and paints `⊘ Unsigned`, while the Signing & Trust panel labels the identical state `—  no file open`. Two surfaces disagree about the same state; the chip is the one that is wrong.
- **Signing & Trust panel** (`{components.signing-panel}`) — **SHIPPED — RD-01. This replaces the import-key dialog entirely.** A themed 620px modal in two sections, each headed in `{typography.ui-section}`:
  - **FILE SIGNATURE** — the open file's name, a 20px/600 **state line** coloured by the chip's own state model, and a detail line that resolves to either "Verified — the file bytes match the signature" or `snc`'s message, plus `Key: <first 16 hex>…` and any `Grants:`. Then a **Key file** row (a path to a `sentinel.key`, with `[Browse…]` and `[Generate Key…]`) and a **Grants** row (comma-separated capabilities) with `[Sign File]` as the default button and `[Verify]` beside it.
  - **TRUSTED KEYS · sentinel-trust.toml** — a gridlined report list of **Name · Trusted key · Grants (ceiling)** read from the project's trust manifest, under an `[Import current key as trusted…]` button that appends a `[[keys]]` block for the open file's signing key.
  - **What is deliberately absent:** no certificate, no `.pfx`/`.p12`/PEM import, **no passphrase**, no subject/issuer, no validity window, no expiry, no revocation. ADR-0061 signing is Ed25519 over raw file bytes with a detached `.sig` carrier; there are no certificates in the model, so there is nothing for those controls to show. A key is 64 hex characters and the UI shows the first 16.
  - **Capability gating is part of the visual spec:** `[Generate Key…]` and `[Sign File]` are disabled when the resolved `snc` lacks its `keygen_core`/`sign_core` helpers, and `[Verify]` is disabled when that `snc` cannot verify — with the detail line explaining which. A disabled button here is honesty, not breakage.
- **Import-key dialog** (`{components.import-key-dialog}`) — **DEAD — RD-01.** Retained only as a redirect: it specified a key file + passphrase + key identity, none of which exist in the product's signing model. Superseded by `{components.signing-panel}`. Any mock still showing it (including `mockups/key-signing.html`) is stale; this spine wins.
- **Honesty note — where the old disclosure went.** The retired dialog carried a `{colors.diag-info}`-bordered note that key handling "is not a hardened Sentinel surface yet." That disclosure is now **wrong in the IDE's favour** and must not simply be re-drawn: the IDE never holds key material at all — `snc` signs in a child process and `sentinel.key` lives on disk in the project. Two *different* honesty constraints replace it, and both are visual:
  1. **Green must not overclaim.** `{colors.trust-verified}` asserts that a signature verifies against the file's bytes — origin and integrity. It asserts nothing about who holds the key. Copy stays "Signed", never "Verified".
  2. **The Output pane's green `[signed · <name>.sig]` is not a verification.** On a successful build of a project with `signing.sign = true`, the IDE runs `snc sign` on the artifact and prints that line in `{colors.trust-verified}` **off the signer's exit code, without re-reading or re-verifying the artifact** (RD-06 — the re-filed adversarial HIGH B1). Until that path re-verifies, the green there is weaker than the green in the chip, and no surface may present the two as equivalent.

### Project & build

- **Project Settings dialog** (`{components.project-settings-form}`) — **SHIPPED — RD-11. New; it appears in no earlier version of this spine.** A themed modal form over the project manifest, in four `{typography.ui-section}` sections — **PROJECT** (name, version, type radios, entry combo), **BUILD** (source dir, lib paths, links, default **tier** combo), **TARGETS** (shown only when the manifest declares explicit `[[target]]` blocks: a target selector plus Name / Entry / Type), and **SIGNING** (a `off | warn | strict` radio group, a trust-manifest path field, and a "Sign the built artifact" checkbox), over `[Cancel]` `[Save]`. Layout is a label column in `{colors.text-primary}` against a field column, 22px fields, `{spacing.4}` margins, hints in `{colors.text-muted}`. It is reached from the tree's project root node and from the app menu.
- **Release tiers are a first-class axis** — **RD-09.** Every build resolves a **target × tier** pair: four tiers, **Development / Experimental / Stable / Hardened**, each with its own dot colour in the scheme selector and its own output directory `target/<dev|experimental|stable|hardened>/`. The earlier spine had no build-configuration axis at all (and, for the record, the missing words were never "debug"/"release" — they were these four). **Do not draw tiers as an optimization or hardening level:** `snc` has no tier flag yet, the IDE says so in the Output pane on every project build, and any visual that implies a Hardened build is *harder* would overclaim.
- **The signing gate is a build-time state, not a post-build action** — **RD-03.** `off | warn | strict` in the SIGNING section maps to `--require-signatures <mode> --trust <manifest>` on the composed `snc build` line. Two limits constrain any visual that promises enforcement: capability grants are a **recorded ceiling, not an enforced gate** (upstream `snc` only ever detects `ffi`), and `--lib`/`--shared` builds **never invoke the trust gate at all** — so a Library or Shared target can display `signing: strict` and enforce nothing. Never render `strict` as a badge, a lock, or anything else that reads as a guarantee.

## Gap index

The single D1 gap register is [GAP-REGISTER.md](../../GAP-REGISTER.md) — it owns the `GAP-An`/`GAP-Bn`
ids, the cost shapes and the ownership calls. **This section is only the visual slice of it**: what
*this spine* specifies that **does not exist in v0.1.7**. These stay as intended-v1 spec; none of them
may be described as shipped, and each is marked in place above as well.

| Item | Tokens / components | Ref |
|---|---|---|
| Diagnostic **severity** styling — error / warning / security row colours | `{components.problems-list}` error/warning/security-text, `alt-row-bg` | RD-13, GAP-A1 |
| **Squiggles** and the **gutter shield** — the UJ-2 flagship rendering (and RichEdit cannot colour them) | `{components.squiggle-error}` / `-warning` / `-security`, `{components.diagnostic-badge-security}` | RD-14, GAP-A1 |
| **Multi-tab editing** visuals | `{components.tab-active}`, `{components.tab-inactive}` | RD-17, GAP-A3 |
| **Toolchain-remediation dialog** | `{components.dialog}` used-for list | RD-19, GAP-A8 |
| **Find/replace dialog** | `{components.dialog}` used-for list | RD-18, GAP-A4 |
| **Current-line band**, coral **selection**, list **zebra striping**, tree/menu **hover** | `{colors.alt-row-bg}`, `{colors.selection-bg}`, `{colors.selection-text}`, `{colors.hover-bg}` (outside the Build button) | — |
| **"Signature present, not verified" chip state** — today it paints as green `✓ Signed` | `{components.status-signing}` present-unverified | RD-05 |
| **15px/600 emphasis face** | `{typography.ui-emphasis}` | — |
| **Coral output links** | `{components.output-pane}` link | — |

**Retired as DEAD** (not deferred — they describe a model the product does not have): `{components.import-key-dialog}` and the `key-loaded` / `no-key` sub-states of `{components.status-signing}`, both because ADR-0061 keeps no key in the IDE (RD-01, RD-05). The RFC-3161 timestamping question that hung over the signing indicator is likewise dissolved: without certificates nothing expires, so nothing needs a timestamp.

**Added because it ships and was specified nowhere:** `{components.scheme-selector}` (RD-10), `{components.project-settings-form}` (RD-11), `{components.signing-panel}` (RD-01), `{components.toolbar}`, `{components.tree-view-switcher}`, `{components.file-label}`, `{components.message-dialog}` with its `save-changes` (RD-25), `password` (RD-23) and `update` (RD-21) variants, `{typography.ui-section}`, `{typography.ui-heading}`, `{typography.display}`, and `{components.editor}` `error-line-bg`. These are the tokens EXPERIENCE.md asked this spine to mint.

## Do's and Don'ts

| Do | Don't |
|---|---|
| Source colors from `src/host/win32/Theme.h`; treat this spine as truth | Invent palette values or hand-pick new accents |
| Build depth from the surface ladder + hairline borders | Add drop-shadows for hierarchy (native = tonal) |
| Spend coral on action, active state, and the safety signal only | Use coral for decoration, chrome, or generic badges |
| Render Sentinel safety findings as coral + shield **when that lands** | Style a `secret`-leak like an ordinary red error — or describe today's severity-blind line tint as if it did |
| Keep diagnostics muted and calm (non-alarmist) | Use saturated/screaming reds or alarm iconography |
| Use Segoe UI (chrome) + Cascadia Code (editor), DPI-scaled | Ship a web font stack or fixed pixel sizes |
| Square-to-subtle corners; native control metrics | Pills, large radii, or roomy web-app whitespace |
| Follow the OS light/dark setting; offer an override | Hard-code dark and ignore the user's system theme |
| Use `{colors.trust-verified}` (green) for "signed"; coral stays for safety findings | Reuse coral for signing status (it inverts coral's valence) — or spend `{colors.trust-verified}` on things that are not signatures |
| Open menus as native popups from the `≡` button | Add a permanent web-style menu bar (reference uses popups) |
| Show the signing chip as the state of the **open file**, and say "Signed" | Bind a signature state to a build artifact, imply it is fresh after a build, or say "Verified" |
| Draw signing as keys, grants and a trust manifest | Draw certificates, passphrases, subject/issuer, or a validity window — the model has none |
| Show target × tier as one control with the derived output path | Present a tier as an optimization or hardening level, or `strict` as a guarantee |
| Mark anything unbuilt in place, with its `RD-nn` | Let a reader mistake this spec for the product |
