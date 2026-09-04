**Fixes a keyboard bug in the update prompt that 0.1.11 introduced.**

### The fix

In 0.1.11, if you tabbed to **Install now** in the update prompt and pressed **Enter**, you got **Later** — an affirmative keypress quietly doing the opposite, with nothing to tell you. Mouse clicks and Space were unaffected.

It came from a guard added for a real problem: an update prompt left on screen could be accepted by a stray Enter, downloading and installing without anyone looking at it. That guard stays — but Enter now activates whatever button actually has focus, so deliberate keyboard use works and an unfocused dialog still cannot be accepted by accident.

### Also

- The update check now gives up after 8 seconds instead of however long the network takes. An unreachable feed previously took over 20 seconds to report failure.
- Clicking **Check for Updates…** while an accepted update is already installing no longer opens a dialog on an application that is about to restart.

### Unchanged

Everything else, including how updates are found, offered and installed. Unsaved work is still saved automatically before an update restarts the app.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
