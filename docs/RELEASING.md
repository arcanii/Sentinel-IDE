# Releasing Sentinel-IDE

Auto-update uses **WinSparkle** reading an **Ed25519-signed appcast hosted on
GitHub**, the same scheme as the sibling apps (RabbitEars / SQLTerminal-Win32).
The moving parts:

| Piece | Where |
|---|---|
| Update engine | `third_party/winsparkle/` (0.9.3, x64 prebuilt) + `src/host/win32/Updater.cpp` |
| Feed | `appcast.xml` at the repo root, served by `raw.githubusercontent.com` off `main` |
| Update artifact | `Sentinel-IDE-<ver>-setup.exe`, attached to a GitHub Release |
| Feed generator | `scripts/make-appcast.ps1` |

> **The repository must stay public.** WinSparkle fetches the appcast and the
> installer with plain unauthenticated HTTPS. Against a private repo GitHub answers
> **404**, and the failure is silent — every client reports "no updates" forever.
> If Sentinel-IDE is ever made private again, the feed and release assets must move
> to a public host first, and `kAppcastUrl` in `Updater.cpp` updated to match.

---

## One-time setup (not yet done)

Auto-update is **wired up but inactive**: `kEdDsaPublicKey` in
`src/host/win32/Updater.cpp` is still the placeholder `@@SENTINEL_IDE_ED25519_PUBLIC_KEY@@`.
While that is the case the IDE refuses to run any check, logs a warning at startup,
and hides the **Check for Updates…** menu item — deliberately, so it can never appear
to work while being unable to verify a signature.

To activate:

1. **Get the tool.** It is **not** in `third_party/winsparkle/` — we vendored only the DLL,
   import library and headers.

   **On this machine it is already unpacked at `E:\util\WinSparkle-0.9.3\`**, so the tool is:
   ```
   E:\util\WinSparkle-0.9.3\bin\winsparkle-tool.exe
   ```
   (verified 2026-07-19: reports `0.9.3`, and that copy's `x64\Release\WinSparkle.dll` is
   byte-identical to the DLL vendored in `third_party/`.)

   Otherwise download the **WinSparkle release zip** matching the version we vendor, **0.9.3**
   (<https://github.com/vslavik/winsparkle/releases> → `WinSparkle-0.9.3.zip`, 27,194,631
   bytes) and unpack it outside the repo — it is release tooling, not a build input.

   > Older WinSparkle shipped separate `generate_keys.exe` / `sign_update.exe`. **0.9.3 has
   > neither**; everything is subcommands of the single `winsparkle-tool.exe`. Documentation
   > elsewhere (including RabbitEars' `docs/RELEASING.md`) still names the old tools.
   >
   > ⚠ **Do not use `bin\legacy_generate_keys.bat` / `legacy_sign_update.bat`**, which sit right
   > beside the tool and look like the old names. They are **not** wrappers around it — they are
   > the deprecated **DSA** path: `openssl dsaparam 4096` producing `dsa_priv.pem`/`dsa_pub.pem`,
   > and SHA-1 DSA signing. A DSA key is useless here: `win_sparkle_set_eddsa_public_key` rejects
   > it (you would get `Updater: WinSparkle rejected the EdDSA public key` in the log), DSA
   > signatures in an appcast are ignored once an EdDSA key is set, and WinSparkle's own header
   > marks DSA deprecated and due for removal. They also need `openssl` on PATH.
   >
   > Worth knowing: the zip's binaries are **not Authenticode-signed**. Verify the download by
   > cross-checking a file you already trust — `x64\Release\WinSparkle.dll` in the zip is
   > byte-identical (SHA-256 `A69ACFCB…8B37`) to `third_party/winsparkle/bin/x64/WinSparkle.dll`
   > in this repo, which arrived independently. Confirmed 2026-07-19.

2. **Generate a dedicated key pair.**
   ```
   winsparkle-tool.exe generate-key --file <path>\sentinel-ide.key
   ```
   This writes a **44-byte private key FILE** at the path you give and prints the base64 public
   key, along with the exact `win_sparkle_set_eddsa_public_key("…")` line to paste.

   > ⚠ **The private key is a file on disk** — it is *not* stored in the Windows credential
   > store. Put it somewhere outside this repository. `.gitignore` covers `*.key`, so naming it
   > `something.key` is protected if it ever lands in the tree, but a name like `mykey.txt`
   > is **not** — do not rely on the ignore rule as the primary defence.

   Use a dedicated pair rather than the family key that signs RabbitEars and the macOS apps:
   a compromise here then cannot ship a malicious update for those products.

3. **Back up that key file before doing anything else**, offline and durably. If it is lost,
   **no existing install can ever be updated again** — clients reject anything signed by a
   different key, and the only recovery is a manual re-install by every user.

   To recover just the public half later (the private key file is the only thing that matters
   to keep):
   ```
   winsparkle-tool.exe public-key --private-key-file <path>\sentinel-ide.key
   ```

4. **Paste the public key** into `kEdDsaPublicKey` in `src/host/win32/Updater.cpp`, replacing
   `@@SENTINEL_IDE_ED25519_PUBLIC_KEY@@`. That value is *meant* to be public and committed —
   it is the trust anchor every installed client checks downloads against.

   Paste the bare base64 line only, e.g. `sKPprIa95Hw+DX3bMoxWMsyC0w9vc4MzEpgx7TBDP1I=` —
   no `ed25519:` prefix, no quotes beyond the existing ones, no trailing whitespace.

5. **Rebuild and confirm it took.** `scripts\build.bat`, run the app, then check
   `%LOCALAPPDATA%\SentinelIDE\logs\sentinelide.log`:

   | Log line | Meaning |
   |---|---|
   | `Updater: initialised (0.1.0.<n>)` | key accepted; **Check for Updates…** now appears in ≡ and the About box |
   | `Updater: WinSparkle rejected the EdDSA public key` | the string is not valid base64 — re-copy it |
   | `Updater: no EdDSA public key compiled in` | still the placeholder |

   The rejection case is checked deliberately: `win_sparkle_set_eddsa_public_key` returns 0
   for a malformed key and WinSparkle would otherwise fall back to an `EdDSAPub`/`EDDSA`
   Windows resource that this exe does not ship — leaving the updater running with **no trust
   anchor at all**. Verified by compiling a deliberately truncated key.

**Never commit the private key.** It **is** a real file — `winsparkle-tool generate-key` writes
one wherever `--file` points. Keep it outside this repository entirely. `.gitignore` covers
`*.key` as a backstop (verified: `sentinel-ide.key` is ignored, `mykey.txt` is **not**), but
treat that as a safety net, never as the plan.

> **Alternative worth knowing:** WinSparkle can also read the public key from a Windows
> resource named `EdDSAPub` of type `EDDSA` instead of a compile-time call. We use the explicit
> `win_sparkle_set_eddsa_public_key()` call because it is greppable, diffable, and its failure
> is checkable — but embedding it in `SentinelIDE.rc` is a supported option if that is ever
> preferable.

---

## Per release

1. **Commit everything first.** The build number is stamped at build time, so the
   version the binary reports only corresponds to a commit if the build follows it.

2. **Build the installer.**
   ```cmd
   cmd /c scripts\make-installer.bat
   ```
   → `build\installer\Sentinel-IDE-0.1.0.<n>-setup.exe`

   The installer is **x64-only** (`ArchitecturesAllowed=x64compatible`) and runs Setup in
   **64-bit mode** (`ArchitecturesInstallIn64BitMode=x64compatible`), so a per-machine install
   lands in `C:\Program Files`, not `C:\Program Files (x86)`. Both directives are load-bearing —
   Inno's Setup.exe is a 32-bit process, and without the second one `{commonpf}` resolves through
   WOW64 to the x86 directory no matter how the payload was built.

   The installer reads its version from the built exe's **FileVersion** resource
   (`GetVersionNumbersString`), so the filename, the setup's own version, and
   `SENTINEL_FILEVERSION_STR` cannot disagree — which is what `-Version` below must match.
   If `build\Sentinel-IDE.exe` is missing, ISCC stops with an explicit `#error` rather than
   producing a mis-versioned installer.

   > **Caveat on the build number.** It now names a shipped artifact, but it is *not*
   > reproducible: `build.bat` increments `packaging/build_number.txt` **before** compiling,
   > so a failed build still burns a number, and nothing ties a number to a commit. You
   > cannot rebuild a given `Sentinel-IDE-0.1.0.<n>-setup.exe` after the fact. Before relying
   > on these as release identities, consider deriving the number from
   > `git rev-list --count HEAD` or moving the increment after `BUILD_OK`.

3. **Sign the installer** — use the script, not the tool directly:
   ```
   pwsh scripts\sign-release.ps1              # newest installer in build\installer\
   pwsh scripts\sign-release.ps1 -Appcast     # ...and regenerate appcast.xml in one step
   ```

   Tell it where the private key is **once**, via an environment variable so nothing
   machine-specific is committed to this public repo:
   ```
   setx SENTINEL_SIGN_KEY "<path>\sentinel-ide.key"
   ```
   (or pass `-KeyFile <path>`).

   The script does four things `winsparkle-tool sign` alone will not:

   - **Verifies the signature against the public key parsed out of `Updater.cpp`.** Signing with
     the wrong key file still produces a valid-*looking* signature; you would only find out when
     every client rejected the update. A mismatch aborts and prints nothing usable.
   - **Refuses a stale installer** — compares the installer's `ProductVersion` against
     `build\Sentinel-IDE.exe`'s `FileVersion` and stops if the app has been rebuilt since
     packaging, which would otherwise publish a feed pointing at the wrong bits.
   - **Takes the version from the app exe, not the installer.** WinSparkle compares
     `SENTINEL_FILEVERSION_STR` — the app's version — against `sparkle:version`. The installer's
     own `FileVersion` is blank (Inno sets only `ProductVersion`), so reading it yields nothing.
   - **Refuses a key stored inside the repository**, which is one `git add -A` from being public
     forever.

   All four guards are exercised and confirmed working (2026-07-19), as is the success path:
   `verify : Valid signature.  (against the key compiled into Updater.cpp)`.

4. **Generate the appcast:**
   ```cmd
   pwsh scripts\make-appcast.ps1 -Version 0.1.0.<n> ^
        -SetupExe build\installer\Sentinel-IDE-0.1.0.<n>-setup.exe -Signature <base64>
   ```
   `-Version` **must** equal the running build's `SENTINEL_FILEVERSION_STR`. WinSparkle
   compares that string against `sparkle:version`; a mismatch either never offers the
   update or offers it on an endless loop.

5. **Publish the GitHub Release** tagged `v0.1.0`, with the setup exe attached as an
   asset. The appcast enclosure URL points at exactly that asset.

6. **Commit and push `appcast.xml`** on `main`. `raw.githubusercontent.com` serves it
   within seconds.

7. **Verify before announcing** — fetch the feed and the enclosure yourself:
   ```powershell
   Invoke-WebRequest https://raw.githubusercontent.com/arcanii/Sentinel-IDE/main/appcast.xml
   Invoke-WebRequest -Method Head <enclosure url>
   ```
   A 404 enclosure is the worst outcome: clients see an update, then fail to download it.

Installed builds pick it up via **≡ ▸ Check for Updates…**, the **Check for Updates…** button in
the **About box** (next to the version it reports), or WinSparkle's periodic background check.

---

## How it behaves in the app

- `initUpdater(hwnd)` runs after the main window exists (`runApp`), because WinSparkle's
  shutdown request needs a window to post `WM_CLOSE` to.
- WinSparkle asks the user's permission before its first automatic check.
- On install, WinSparkle asks the app to quit, then **waits on the process handle**
  before running the installer. If the process does not exit, the installer cannot
  overwrite the locked exe and the update fails.

  This is why the **About box hosting a Check-for-Updates button is not incidental** — it is the
  dialog most likely to be open when WinSparkle asks the app to quit, and why every modal dialog's
  nested message loop **re-posts `WM_QUIT`**
  (`GetMessageW` returns 0 for it *and consumes it* — a nested loop that swallows it
  leaves `runApp`'s outer loop blocked forever with the process still alive). There is
  also a 3-second force-exit watchdog in `Updater.cpp` as a backstop. Both matter: unlike
  a purely user-triggered check, a *background* check can request shutdown while any
  dialog is open.
- WinSparkle's own update UI is native/light and does not follow the dark theme. Cosmetic,
  and not currently worth skinning.

## Not yet covered

- **Authenticode signing** of the exe and installer. Without it SmartScreen may warn on
  first download. The Ed25519 appcast signature is the *update-integrity* signature
  WinSparkle verifies — it is a completely separate mechanism from Authenticode and does
  not suppress SmartScreen. Needs an OV/EV code-signing certificate (hardware token or a
  cloud signer such as Azure Trusted Signing). When one exists: sign the exe **before**
  packaging, re-pack, sign the installer, then take the Ed25519 signature over the final
  signed bytes — Authenticode first, `winsparkle-tool sign` second, always with `/tr` timestamping.
- **Delta updates** — WinSparkle downloads the full installer each time.
- **Release channels** (beta/stable) — one feed today.
