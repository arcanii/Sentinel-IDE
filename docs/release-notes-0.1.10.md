**The old editor is gone.** 0.1.9 made the Direct2D editor the default; this release removes the RichEdit one entirely.

### What this means for you

If you were still using the old editor — via **Settings ▸ Use the Direct2D editor** unticked, or by launching with `--richedit` — **that is no longer possible**. The checkbox and both flags are removed, and a leftover `d2d=0` in your settings is ignored and cleaned up. There is one editor now.

That is deliberate rather than casual: since 0.1.9 the two editors did the same things, including dragging text within a file, which was the last thing the old one did that the new one did not.

### Also in this release

- **An unrecognised command-line switch no longer eats your path.** `Sentinel-IDE.exe C:\project --richedit` used to open an empty window with no message — the flag was mistaken for the path. Any unknown switch is now ignored and logged instead. This bug was older than the flag: a typo like `--buidl` did the same thing.
- **If the editor cannot start, it says so.** A failed window class now exits with an explanation and the log's location rather than showing an empty pane. A transient Direct2D device failure keeps retrying, and after a second explains itself on screen — your text is still in memory and the file on disk is as you last saved it.

### Unchanged

The Output pane, the Problems list, clickable `file:line:col` links, signing, sealing and the build toolchain are all untouched.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
