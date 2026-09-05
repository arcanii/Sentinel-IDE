**Find and Replace.**

### Find

**Ctrl+F** opens a bar under the tab strip. It shows how many matches there are and which one you are on, **F3** / **Shift+F3** step through them, and the search wraps at both ends. **Match case** and **Whole word** are there when you need them; by default it ignores case.

**Ctrl+H** adds a replace row in place, with **Replace** and **Replace All**. A Replace All is a **single undo** — one Ctrl+Z puts every occurrence back, not one per match.

**Escape** closes the bar and returns you to the editor.

One limit worth stating: case-insensitive matching covers ASCII and Latin-1, so `CAFÉ` finds `café`, but Greek, Turkish dotless-i and ß/SS are matched exactly rather than folded.

### The trust indicator no longer overstates itself

The status-bar chip read **✓ Signed** while you edited a signed file. A signature describes the file on disk, so the moment you changed the buffer the chip was vouching for text that nothing had checked. It now reads **✎ Edited since signing** until you save, and goes back to green on its own if the file still verifies.

### Also

Replacing text can no longer introduce a stray carriage return into the buffer — it was not reachable through the interface, but it was the one place in the editor where a replacement was not normalised, and on a signed file that would have been a byte change with no keypress.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
