**The Direct2D editor is now the default**, and it no longer gives anything up against the old one.

0.1.8 offered it as an opt-in. This release makes it what you get, and closes the one feature it was missing.

### What changed

- **New editor by default.** No word wrap with real horizontal scrolling, syntax colouring, error-line tints after a failed build, and a system caret that Narrator, Magnifier's *follow the text cursor* and IME candidate lists can track. Only the lines on screen are laid out, so a 20,000-line file costs about what a 200-line one does.
- **Dragging text within a file works.** Drag a selection to move it, Ctrl-drag to copy, and text dropped from other applications is accepted. This was the one thing the old editor did that the new one did not — the reason 0.1.8 shipped it as a preview rather than as the default.

### If you prefer the old editor

Untick **Settings ▸ Use the Direct2D editor** and restart, or launch with `--richedit`. Both still work, and an explicit opt-out outranks the new default — if you unticked the box in 0.1.8, this release leaves you on the old editor.

### Unchanged

Dropping a file on the editor still opens it, with the usual unsaved-changes prompt. The Output pane, the Problems list, the `file:line:col` jump, signing, sealing and the build toolchain are untouched.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
