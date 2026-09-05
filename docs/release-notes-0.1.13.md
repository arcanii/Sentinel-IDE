**Fixes two defects in how the update feed is read, and moves that reader into Sentinel.**

### The fixes

The code that decides whether an update is available had two problems, both found by an audit rather than by anything going wrong:

- **A version number with too many digits wrapped around.** A feed advertising a very large version could rank *below* the version you are running, suppressing a real update — or, the same arithmetic failing the other way, make an older release look newer.
- **The version string was never checked.** Whatever appeared in the feed was shown in the update prompt, written to the log, and — if you chose *Skip this version* — saved into your settings. A feed containing prose instead of a version was accepted as one.

Neither could be triggered by the published feed, which is generated and validated. They mattered because this is the only thing the IDE reads off the network.

Both are now impossible rather than fixed: a version that does not parse is not returned at all, so there is nothing to display or store, and it can never be treated as newer. The same applies in reverse — if the running version cannot be parsed, no update is offered rather than all of them.

### Under the hood

That reader is now written in Sentinel, like every other file reader in the IDE. It was the last one still in C++, and the only one fed by the network.

### Unchanged

How updates are found, offered and installed. **Check for Updates…** still asks before installing, still tells you when you are already current, and unsaved work is still saved automatically before an update restarts the app.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
