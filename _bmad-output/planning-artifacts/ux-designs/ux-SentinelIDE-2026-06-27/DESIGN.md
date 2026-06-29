---
name: SentinelIDE
description: Native Windows-first IDE for the Sentinel language, built in Sentinel. Dark-primary "Claude-desktop" coral identity modeled on SQLTerminal-Win32; native Win32 + Common Controls v6, OS light/dark follow. This DESIGN.md owns the visual identity.
title: "SentinelIDE — DESIGN"
status: draft
created: 2026-06-27
updated: 2026-06-28
sources:
  - "{planning_artifacts}/prds/prd-SentinelIDE-2026-06-27/prd.md"        # §9 Aesthetic, FRs
  - "{planning_artifacts}/briefs/brief-SentinelIDE-2026-06-27/brief.md"  # dark/coral posture
visual_reference: "G:/SQLTerminal-Win32/src/ui/Theme.h"                   # canonical palette source
# ── CONVENTION ──────────────────────────────────────────────────────────────
# Dark is the PRIMARY expression (Claude-desktop dark). Base tokens are the DARK
# values; `-light` variants are the OS light-mode follow. The app follows the OS
# setting (Theme.h: themeOverride -1 follow / 0 light / 1 dark), user-overridable
# in Settings. All non-diagnostic colors are sourced verbatim from Theme.h; the
# `diag-*` family is DERIVED for SentinelIDE (Theme.h had no diagnostics) — see Colors.
colors:
  # Surfaces — tonal layering, deepest → most elevated (dark base)
  window-bg: '#161618'          # deepest surface: editor canvas, window erase
  panel-bg: '#1A1A1C'           # panels: project tree, problems list, output rows
  panel-elev-bg: '#202023'      # elevated bands: tab strip, list headers, status bar
  alt-row-bg: '#1C1C1F'         # zebra-stripe row
  hover-bg: '#2A2A2D'           # hover highlight
  border: '#303034'             # hairline separators (depth is borders, not shadow)
  # Text
  text-primary: '#E6E6E8'
  text-secondary: '#9A9AA0'
  text-muted: '#6A6A70'
  # Accent (coral) — primary action / active state AND the Sentinel-safety signal
  accent: '#D97757'             # Claude coral
  accent-text: '#28120A'        # text drawn on coral
  selection-bg: '#5C3426'       # muted coral: text selection / active row
  selection-text: '#F7EEE9'
  # Syntax (Theme.h)
  syn-keyword: '#C98FD6'        # orchid — keywords incl. secret/effects/borrow
  syn-number: '#9FD19A'         # green — numeric literals
  syn-string: '#E0966B'         # warm orange — string literals
  syn-comment: '#769676'        # muted green — comments
  # Diagnostics (DERIVED — muted to honor the calm/non-alarmist tone, PRD §9) [ASSUMPTION]
  diag-error: '#E06C75'         # generic compile error
  diag-warning: '#E5C07B'       # warning
  diag-info: '#6CA8C4'          # info / hint (cool, recessive vs the warm palette)
  diag-security: '#D97757'      # Sentinel safety findings (secret/borrow/effect) — coral, == accent by intent
  # Signing / trust (DERIVED) [ASSUMPTION] — coral is NOT reused (its valence is "safety finding")
  trust-verified: '#7FB37A'     # the "Signed" state (asserts origin+integrity only — NOT identity/trust-verified)
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
  diag-error-light: '#C0392B'       # [ASSUMPTION] derived
  diag-warning-light: '#8A6D00'     # [ASSUMPTION] derived
  diag-info-light: '#2C6E9B'        # [ASSUMPTION] derived
  diag-security-light: '#C15F3C'    # [ASSUMPTION] derived (light coral)
  trust-verified-light: '#2E7D32'   # [ASSUMPTION] derived (signing "signed/verified")
typography:
  ui:                             # native chrome — menus, tree, dialogs, labels
    fontFamily: 'Segoe UI'
    fontSize: 15px                # @96dpi; DPI-scaled via MulDiv (Theme.h dpiScale)
    fontWeight: '400'
    lineHeight: '1.4'
  ui-emphasis:                    # active labels, section headers
    fontFamily: 'Segoe UI'
    fontSize: 15px
    fontWeight: '600'
  ui-small:                       # status bar, captions, list metadata
    fontFamily: 'Segoe UI'
    fontSize: 12px
    fontWeight: '400'
  editor:                         # code surface + output pane
    fontFamily: 'Cascadia Code'
    fontSize: 14px                # @96dpi, DPI-scaled
    fontWeight: '400'
    lineHeight: '1.45'
    note: 'Default Cascadia Code (ligatures on). User-overridable in Settings (any installed mono); Cascadia Mono = ligatures off; Consolas = reference fallback if Cascadia absent.'
rounded:
  # Native Win32 / Common Controls v6 — corners are square to subtly rounded. [ASSUMPTION] (Theme.h defines no radii)
  none: '0px'
  DEFAULT: '2px'                  # hairline-rounded fields / buttons
  sm: '2px'
  md: '4px'                       # dialogs, primary buttons (Win11 native softening)
spacing:
  # Derived from reference dialog metrics (12px margins, ~22-26px control heights). [ASSUMPTION]
  '1': '4px'
  '2': '8px'
  '3': '12px'                     # default panel padding / dialog margin (reference)
  '4': '16px'
  '6': '24px'
  '8': '32px'
  gutter: '12px'                  # panel inner padding
  row-h: '22px'                   # tree / list row height (native density)
  control-h: '26px'              # buttons / inputs
components:
  editor:
    background: '{colors.window-bg}'
    text: '{colors.text-primary}'
    font: '{typography.editor}'
    current-line-bg: '{colors.alt-row-bg}'
    selection-bg: '{colors.selection-bg}'
    selection-text: '{colors.selection-text}'
    gutter-bg: '{colors.panel-bg}'
    gutter-text: '{colors.text-muted}'
  tab-active:
    background: '{colors.window-bg}'
    text: '{colors.text-primary}'
    indicator: '{colors.accent}'      # 2px top border in coral
  tab-inactive:
    background: '{colors.panel-elev-bg}'
    text: '{colors.text-secondary}'
    dirty-glyph: '{colors.text-secondary}'   # • dot when unsaved
  project-tree:
    background: '{colors.panel-bg}'
    text: '{colors.text-primary}'
    icon: '{colors.text-secondary}'
    hover-bg: '{colors.hover-bg}'
    selected-bg: '{colors.selection-bg}'
    selected-text: '{colors.selection-text}'
    row-height: '{spacing.row-h}'
  output-pane:
    background: '{colors.window-bg}'
    font: '{typography.editor}'
    text: '{colors.text-primary}'
    stderr-text: '{colors.diag-error}'
    link: '{colors.accent}'           # clickable file:line, underlined
  problems-list:
    background: '{colors.panel-bg}'
    header-bg: '{colors.panel-elev-bg}'
    alt-row-bg: '{colors.alt-row-bg}'
    error-text: '{colors.diag-error}'
    warning-text: '{colors.diag-warning}'
    security-text: '{colors.diag-security}'
    row-height: '{spacing.row-h}'
  squiggle-error:
    underline: '{colors.diag-error}'        # wavy
  squiggle-warning:
    underline: '{colors.diag-warning}'
  squiggle-security:                         # secret-leak / borrow / effect
    underline: '{colors.diag-security}'      # coral wavy
    gutter-glyph: 'shield'                   # + lock/shield marker, {colors.diag-security}
  diagnostic-badge-security:                 # the distinct Sentinel-safety marker
    color: '{colors.diag-security}'
    glyph: 'shield-lock'
  status-bar:
    background: '{colors.panel-elev-bg}'
    text: '{colors.text-secondary}'
    font: '{typography.ui-small}'
  button-primary:                            # Build / Run / dialog default
    background: '{colors.accent}'
    text: '{colors.accent-text}'
    radius: '{rounded.md}'
    height: '{spacing.control-h}'
  button-default:
    background: '{colors.panel-elev-bg}'
    text: '{colors.text-primary}'
    border: '{colors.border}'
    radius: '{rounded.md}'
    height: '{spacing.control-h}'
  dialog:
    background: '{colors.panel-bg}'
    titlebar-bg: '{colors.panel-elev-bg}'    # DWM immersive dark caption
    titlebar-text: '{colors.text-secondary}'
    border: '{colors.border}'
    radius: '{rounded.md}'
  app-menu-button:                           # ≡ in the toolbar
    glyph: 'hamburger'
    color: '{colors.text-secondary}'
    hover-bg: '{colors.hover-bg}'
  menu-popup:                                # native CreatePopupMenu-style
    background: '{colors.panel-elev-bg}'
    border: '{colors.border}'
    radius: '{rounded.md}'
    item-text: '{colors.text-primary}'
    item-hover-bg: '{colors.hover-bg}'
    accelerator-text: '{colors.text-muted}'
    separator: '{colors.border}'
  status-signing:                            # status-bar signing indicator
    no-key: '{colors.text-muted}'
    key-loaded: '{colors.text-secondary}'
    signed: '{colors.trust-verified}'
    unsigned: '{colors.text-muted}'
    signing: '{colors.accent}'               # in-progress (active)
    failed: '{colors.diag-error}'
    glyph: 'signature'
  import-key-dialog:
    background: '{colors.panel-bg}'
    titlebar-bg: '{colors.panel-elev-bg}'
    border: '{colors.border}'
    radius: '{rounded.md}'
    honesty-note-border: '{colors.diag-info}'  # the "not yet hardened" disclosure
---

# SentinelIDE — DESIGN.md

> Visual-identity spine (Google Labs design.md). Owns *how it looks*. Behavior, IA, and
> flows live in [EXPERIENCE.md](EXPERIENCE.md), which references these tokens by `{path.to.token}`.
> **This spine wins on conflict** with any mock, wireframe, or import.
> Palette sourced verbatim from `G:/SQLTerminal-Win32/src/ui/Theme.h`; the `diag-*` family is derived (see Colors).

## Brand & Style

SentinelIDE is a **native Windows desktop IDE** — not a web app in a window, not Electron. Its whole argument is that the dangerous parts are built in Sentinel and structurally cannot carry the bug; the visual identity has to *earn that seriousness without performing it*. The posture is **calm, precise, and quietly confident** — assurance, not noise (PRD §9). A bank's engineer should feel they're using a sober, fast, professional tool; the coral warmth keeps it from reading cold or clinical.

The reference is **SQLTerminal-Win32**: native Win32 + Common Controls v6 — a RichEdit editor, a tree sidebar, virtual lists, themed dialogs, a status bar. We borrow its **dark/coral "Claude-desktop" look** (its `Theme.h` is our palette source of truth), *not* its code. Dark is the **primary** expression; light is the OS-follow variant (the app tracks the Windows light/dark setting, user-overridable in Settings).

The aesthetic discipline is **native restraint**. Depth comes from tonal layering and hairline borders, not web-style drop-shadows. Corners are square to subtly rounded. There are no gradients, no decorative chrome, no celebratory motion. The one warm signal — coral — is spent deliberately: on the primary action, on the active thing, and on the product's headline moment, *Sentinel's safety guarantee appearing in the editor*.

## Colors

The palette is a **dark-primary, single-accent** system: a deep neutral grey ladder, warm-grey text, one coral accent, Theme.h's syntax colors, and a derived diagnostic set. Base tokens are the **dark** values; `-light` variants follow the OS into light mode. Light-mode `window-bg`/`panel-bg`/`text-primary` are **OS-derived** (`COLOR_WINDOW` / `COLOR_WINDOWTEXT`) so the app sits naturally in the user's system — the `#FFFFFF`/`#000000` shown are the typical resolved values.

**Surfaces — depth by tone.** Four greys stack to build hierarchy without shadow: `{colors.window-bg}` `#161618` is the deepest layer (the editor canvas and window); `{colors.panel-bg}` `#1A1A1C` is panels (tree, problems, output); `{colors.panel-elev-bg}` `#202023` is elevated bands (tab strip, list headers, status bar); `{colors.alt-row-bg}` is zebra striping. `{colors.hover-bg}` lifts a row on hover; `{colors.border}` `#303034` draws the hairlines that separate regions. *This tonal ladder is the elevation system* (see Elevation & Depth).

**Text.** `{colors.text-primary}` `#E6E6E8` for content; `{colors.text-secondary}` for labels, inactive tabs, status bar; `{colors.text-muted}` for line numbers and de-emphasized metadata.

**Coral — the one accent.** `{colors.accent}` `#D97757` (the Claude coral) has exactly **three sanctioned roles** and no others:
1. **Primary action** — the Build/Run buttons, dialog default button (`{colors.accent-text}` text on the fill).
2. **Active state** — the active editor tab's top-border indicator; selected tree/list row uses the muted-coral `{colors.selection-bg}`.
3. **The Sentinel-safety signal** — `{colors.diag-security}` is the *same coral by intent*, always paired with a shield/lock glyph so it never reads as mere decoration. Making the language's headline guarantee render in the brand color is the point (UJ-2).

Coral is **never** used for generic chrome, decorative fills, gradients, or ordinary state badges.

**Syntax (Theme.h).** `{colors.syn-keyword}` orchid carries keywords — including the security-relevant `secret`, effect, and borrow keywords; `{colors.syn-number}` green for numbers; `{colors.syn-string}` warm-orange for strings; `{colors.syn-comment}` muted-green for comments. (Light mode shifts to higher-contrast hues per Theme.h: magenta keywords, purple numbers, red strings, forest-green comments.)

**Diagnostics (DERIVED — `[ASSUMPTION]`, confirm on review).** Theme.h predates diagnostics, so this family is new and tuned **muted on purpose** to honor the non-alarmist tone: `{colors.diag-error}` `#E06C75` (a rose-red, distinct from coral so errors ≠ the safety signal), `{colors.diag-warning}` `#E5C07B` gold, `{colors.diag-info}` `#6CA8C4` a recessive cool blue. `{colors.diag-security}` is coral — reserved for Sentinel's own findings (`secret`-leak, borrow, effect) and always shield-marked. **Rule:** an ordinary compile error is `diag-error` red; a Sentinel *safety* finding is `diag-security` coral + shield. The difference is intentional and product-defining.

**Signing / trust (DERIVED — `[ASSUMPTION]`).** `{colors.trust-verified}` (a calm green) marks a **Signed** artifact in the signing status indicator — it asserts the artifact carries a signature (origin + integrity), **not** that the key/identity is vetted (UI copy says "Signed," never "Verified"; per adversarial-review A2/D3). *(Token name kept for now to stay consistent with the just-finalized PRD §9, which cites `trust-verified` by name — a rename to `trust-signed` would need a coordinated PRD edit.)* Coral is **deliberately not** reused here: coral's valence in this system is *"a safety finding needs your attention,"* and "signed = good" would invert it. An **unsigned** artifact is a neutral state (`{colors.text-muted}`), not an alarm; only a **failed** signing is `{colors.diag-error}`, and **signing-in-progress** borrows `{colors.accent}` as a transient active state.

## Typography

Two type families, both native to Windows:

- **`{typography.ui}` Segoe UI** — all chrome: menus, the project tree, dialogs, the Problems list, labels. `{typography.ui-emphasis}` (600) for section headers and active labels; `{typography.ui-small}` for the status bar and list metadata. This is the platform's own UI font; using it is what makes the app feel native rather than ported.
- **`{typography.editor}` Cascadia Code** — the editor and the output pane. Ligatures **on** by default. It ships with Windows, is purpose-built for code, and is **user-overridable in Settings** to any installed monospace (Cascadia Mono for ligatures-off; Consolas — the SQLTerminal reference font — as the fallback if Cascadia is absent).

All sizes are 96-dpi design values, **DPI-scaled** per-monitor via `MulDiv` (Theme.h `dpiScale`). Type is the only place text size is set; never simulate hierarchy with color alone.

## Layout & Spacing

Native desktop density — **compact, information-dense, no wasted air**. The spacing scale (`{spacing.1}`–`{spacing.8}`) is derived from the reference's dialog metrics `[ASSUMPTION]`: `{spacing.3}` (12px) is the default panel padding and dialog margin, `{spacing.gutter}` the panel inner padding. Native control metrics: `{spacing.row-h}` (22px) tree/list rows, `{spacing.control-h}` (26px) buttons and inputs.

The shell is a classic **three-region IDE layout** — tree sidebar, editor area with a tab strip, and a bottom dock (output / problems) — separated by draggable splitters and bounded by `{colors.border}` hairlines. (The information architecture and panel behavior are owned by [EXPERIENCE.md](EXPERIENCE.md); this section governs only density and separation.) Regions are resizable; the editor area is the priority surface and never collapses.

## Elevation & Depth

**Depth is tonal, not cast.** This is the defining native choice and the sharpest break from web idiom: SentinelIDE has **no drop-shadows as hierarchy**. Layers are distinguished by the surface ladder — `{colors.window-bg}` → `{colors.panel-bg}` → `{colors.panel-elev-bg}` — and by `{colors.border}` hairlines. A header band is "above" its rows because it is one tone lighter, not because it floats. Hover and selection are tonal shifts (`{colors.hover-bg}`, `{colors.selection-bg}`), never shadow.

The only true overlays are **native dialogs and popups** (themed via DWM immersive dark mode — dark caption, `{colors.border}` border). They get the OS's own shadow because they are genuinely separate windows; that is the *one* place a shadow is correct, and it comes from the platform, not from us.

## Shapes

Native Win32 geometry: **square to subtly rounded** `[ASSUMPTION]` (Theme.h specifies no radii). `{rounded.none}` for the editor canvas, splitters, and panel edges; `{rounded.DEFAULT}`/`{rounded.sm}` (2px) for fields and list selection; `{rounded.md}` (4px) for buttons and dialog corners, matching Windows 11's gentle native softening. **No pill shapes, no large web-style radii** — crispness reads "professional tool," which is the whole brand.

## Components

Visual specs (anatomy, color, sizing, state). Behavioral rules live in [EXPERIENCE.md](EXPERIENCE.md).

- **Editor** (`{components.editor}`) — `{colors.window-bg}` canvas, `{typography.editor}` text, line-number gutter in `{colors.text-muted}` on `{colors.panel-bg}`. Current line subtly banded with `{colors.alt-row-bg}`; selection in `{colors.selection-bg}`/`{colors.selection-text}`. Syntax per the `syn-*` tokens.
- **Tabs** (`{components.tab-active}` / `{components.tab-inactive}`) — active tab: `{colors.window-bg}` (continuous with the editor) + a **2px coral top-border** (`{colors.accent}`). Inactive: `{colors.panel-elev-bg}` + `{colors.text-secondary}`. Unsaved tabs show a `•` dirty glyph.
- **Project tree** (`{components.project-tree}`) — `SysTreeView32`, `{colors.panel-bg}`, hover `{colors.hover-bg}`, selected `{colors.selection-bg}`. File-type icons in `{colors.text-secondary}`; rows `{spacing.row-h}`.
- **Output pane** (`{components.output-pane}`) — `{colors.window-bg}`, monospace. stdout in `{colors.text-primary}`, **stderr in `{colors.diag-error}`**, `file:line[:col]` rendered as a `{colors.accent}` underlined link.
- **Problems list** (`{components.problems-list}`) — `SysListView32`, header band `{colors.panel-elev-bg}`. Error rows keyed `{colors.diag-error}`, warnings `{colors.diag-warning}`, **Sentinel-safety rows `{colors.diag-security}` with a leading shield glyph** (`{components.diagnostic-badge-security}`).
- **Squiggles** — wavy underline at the diagnostic span: `{components.squiggle-error}` red, `{components.squiggle-warning}` gold, `{components.squiggle-security}` **coral + a gutter shield** for `secret`/borrow/effect findings.
- **Status bar** (`{components.status-bar}`) — `{colors.panel-elev-bg}` band, `{typography.ui-small}` in `{colors.text-secondary}`. (Per the *About-dialog-only* decision, the status bar carries no security/trust badge — see EXPERIENCE.md.)
- **Buttons** — `{components.button-primary}` coral fill for Build/Run and dialog defaults; `{components.button-default}` `{colors.panel-elev-bg}` with a `{colors.border}` edge otherwise.
- **Dialogs** (`{components.dialog}`) — themed native windows: `{colors.panel-bg}` body, `{colors.panel-elev-bg}` DWM dark caption, `{colors.border}` border, `{rounded.md}` corners. Used for About, Settings, toolchain remediation, find/replace.
- **App-menu button** (`{components.app-menu-button}`) — a `≡` glyph in the toolbar in `{colors.text-secondary}` (hover `{colors.hover-bg}`); opens the **menu popup** (`{components.menu-popup}`): `{colors.panel-elev-bg}` surface, `{colors.hover-bg}` item hover, accelerators right-aligned in `{colors.text-muted}`, `{colors.border}` separators. Faithful to the reference's `CreatePopupMenu` style — a popup, not a permanent menu bar.
- **Signing status indicator** (`{components.status-signing}`) — a small status-bar chip with a state glyph: **signed** `{colors.trust-verified}`, **signing** `{colors.accent}`, **failed** `{colors.diag-error}`, **key-loaded / unsigned / no-key** in greys. This is the **one always-on security element** in the chrome; the proof *metric* (Sentinel/native mix) still lives only in About. Clicking it opens the signing dialog.
- **Import-key dialog** (`{components.import-key-dialog}`) — themed native dialog for the key file (`.pfx` / `.p12` / PEM) + passphrase, showing the loaded key's identity. Carries a `{colors.diag-info}`-bordered **honesty note**: key handling is not a hardened Sentinel surface yet (next increment) — never overclaimed.

## Do's and Don'ts

| Do | Don't |
|---|---|
| Source colors from Theme.h; treat this spine as truth | Invent palette values or hand-pick new accents |
| Build depth from the surface ladder + hairline borders | Add drop-shadows for hierarchy (native = tonal) |
| Spend coral on action, active state, and the safety signal only | Use coral for decoration, chrome, or generic badges |
| Render Sentinel safety findings as coral + shield | Style a `secret`-leak like an ordinary red error |
| Keep diagnostics muted and calm (non-alarmist) | Use saturated/screaming reds or alarm iconography |
| Use Segoe UI (chrome) + Cascadia Code (editor), DPI-scaled | Ship a web font stack or fixed pixel sizes |
| Square-to-subtle corners; native control metrics | Pills, large radii, or roomy web-app whitespace |
| Follow the OS light/dark setting; offer an override | Hard-code dark and ignore the user's system theme |
| Use `{colors.trust-verified}` (green) for "signed"; coral stays for safety findings | Reuse coral for signing status (it inverts coral's valence) |
| Open menus as native popups from the `≡` button | Add a permanent web-style menu bar (reference uses popups) |
