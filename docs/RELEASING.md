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

1. **Generate a dedicated key pair.** Download the WinSparkle release zip
   (<https://github.com/vslavik/winsparkle/releases>) and run its `generate_keys.exe`.
   It stores the private key in the **Windows credential store** and prints the base64
   public half.

   A dedicated pair — rather than reusing the family key that signs RabbitEars and the
   macOS apps — means a compromise here cannot be used to ship a malicious update for
   those products.

2. **Paste the public key** into `kEdDsaPublicKey` in `src/host/win32/Updater.cpp`.
   That value is *meant* to be public and committed; it is the trust anchor.

3. **Back up the private key** somewhere durable. If it is lost, no existing install
   can ever be updated again — they will reject anything signed by a new key, and the
   only path forward is a manual re-install by every user.

4. **Never commit the private key.** `.gitignore` covers `*.key`, but the credential
   store is the intended home.

---

## Per release

1. **Commit everything first.** The build number is stamped at build time, so the
   version the binary reports only corresponds to a commit if the build follows it.

2. **Build the installer.**
   ```cmd
   cmd /c scripts\make-installer.bat
   ```
   → `build\installer\Sentinel-IDE-0.1.0-setup.exe`

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

3. **Sign the installer** with the private key:
   ```cmd
   sign_update.exe build\installer\Sentinel-IDE-0.1.0-setup.exe
   ```
   (`sign_update.exe` ships in the same WinSparkle release zip.) Copy the printed
   base64 signature.

4. **Generate the appcast:**
   ```cmd
   pwsh scripts\make-appcast.ps1 -Version 0.1.0.<n> ^
        -SetupExe build\installer\Sentinel-IDE-0.1.0-setup.exe -Signature <base64>
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

Installed builds pick it up via **≡ ▸ Check for Updates…** and WinSparkle's periodic
background check.

---

## How it behaves in the app

- `initUpdater(hwnd)` runs after the main window exists (`runApp`), because WinSparkle's
  shutdown request needs a window to post `WM_CLOSE` to.
- WinSparkle asks the user's permission before its first automatic check.
- On install, WinSparkle asks the app to quit, then **waits on the process handle**
  before running the installer. If the process does not exit, the installer cannot
  overwrite the locked exe and the update fails.

  This is why every modal dialog's nested message loop **re-posts `WM_QUIT`**
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
  signed bytes — Authenticode first, `sign_update` second, always with `/tr` timestamping.
- **Delta updates** — WinSparkle downloads the full installer each time.
- **Release channels** (beta/stable) — one feed today.
