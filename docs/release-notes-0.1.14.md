**Hardens opening a sealed project you were sent by someone else.**

Sealed projects exist so you can hand an encrypted project to someone with a password. That means the file itself is written by whoever sealed it — and encryption proves it was not altered in transit, not that its author meant you well. This release treats it that way.

### What changed

- **A file could write outside the folder you unsealed into.** A path containing a colon (`ab:c`) slipped past the safety check and, on NTFS, wrote a hidden alternate data stream — content that would never appear when you browsed the unsealed project. Now refused.
- **A rejected archive used to leave files behind.** If a container was refused partway through, everything extracted before that point stayed on disk, and the cleanup only removed empty folders. The whole index is now checked before anything is written, so a refused file leaves nothing.
- **Two ways to hang or exhaust the machine are closed.** A small file could claim an enormous archive and make the unsealer commit gigabytes before failing; a file could declare thousands of unlock slots, each costing a full key-derivation on a wrong password. Both are refused up front, and both now say *why* rather than reporting a generic bad-header error.

Files this IDE produces are unaffected — none of these limits is reachable by a normal sealed project.

### Under the hood

The container header, unlock-slot table and archive index are now read in Sentinel rather than C++. This is the first port that parses genuinely hostile input rather than files the IDE wrote itself, which is the point of the exercise. The encryption is untouched and still runs through Windows CNG.

### Unchanged

Sealing, unsealing, passwords and the sealed-file format itself. Existing sealed projects open exactly as before.

---

Installs per-user, no admin required. Existing installs update in place via **≡ ▸ Check for Updates…**, the About box, or the background check.
