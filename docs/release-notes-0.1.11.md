**Checking for updates asks before it installs again.**

### The fix

**≡ ▸ Check for Updates…** used to download the update, install it and close the app, with nothing in between. It reads like a question, so it now behaves like one: if an update exists you get **Install now / Later / Skip this version**, the same prompt the automatic hourly check already showed.

It also tells you when nothing happened, which it previously did not:

- up to date → it says so, and names the version you are running
- the check failed (no network, feed unreachable) → it says so, and points at the log

Previously both of those were silence, which is indistinguishable from a menu item that does nothing.

**Asking explicitly now overrides a previous "Skip this version".** Skipping a release used to leave the manual check permanently unable to offer it. The automatic check still respects your choice to skip.

### Also

An unrecognised command-line switch is ignored and logged rather than being mistaken for a file to open — see 0.1.10's notes, where this is described in full.

### Unchanged

Updates found by the automatic check behave exactly as before, and unsaved work is still saved automatically before an update restarts the app.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
