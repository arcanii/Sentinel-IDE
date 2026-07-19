# SPDX-License-Identifier: GPL-3.0-or-later
# Sign a built installer with the Sentinel-IDE Ed25519 release key, then VERIFY the
# signature against the public key actually compiled into the shipped binary.
#
#   pwsh scripts\sign-release.ps1
#   pwsh scripts\sign-release.ps1 -SetupExe build\installer\Sentinel-IDE-0.1.0.37-setup.exe
#   pwsh scripts\sign-release.ps1 -Appcast          # also regenerate appcast.xml
#
# With no -SetupExe it picks the newest installer in build\installer\.
#
# The verify step is the point of this script. `winsparkle-tool sign` will happily sign
# with the WRONG key file and print a perfectly valid-looking signature; you would only
# find out when every client rejected the update. This re-derives the expected public key
# from src\host\win32\Updater.cpp — the exact trust anchor the shipped exe checks against —
# and refuses to hand back a signature that does not verify under it.
#
# The private key is never read, printed, or copied by this script; it is passed to
# winsparkle-tool by path only.
[CmdletBinding()]
param(
    [string]$SetupExe,                          # default: newest in build\installer\
    [string]$KeyFile,                           # default: $KeyFileDefault below, or $env:SENTINEL_SIGN_KEY
    [string]$Tool,                              # default: $ToolDefault below
    [switch]$Appcast                            # also run make-appcast.ps1 with the signature
)
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# The Ed25519 private key file, produced by
#     winsparkle-tool.exe generate-key --file <path>\sentinel-ide.key
#
# Configured per machine via the SENTINEL_SIGN_KEY environment variable, so nothing
# machine-specific is committed to this public repo:
#
#     setx SENTINEL_SIGN_KEY "<path>\sentinel-ide.key"      (set once; new shells only)
#
# Precedence: -KeyFile  >  $env:SENTINEL_SIGN_KEY  >  $KeyFileDefault.
# $KeyFileDefault stays EMPTY here on purpose — if you fill it in locally for
# convenience, don't commit that change. Wherever the key lives, keep it outside this
# repository; the script refuses to sign with a key found inside it.
$KeyFileDefault = ''
# ---------------------------------------------------------------------------
$ToolDefault = 'E:\util\WinSparkle-0.9.3\bin\winsparkle-tool.exe'

$repo = Split-Path -Parent $PSScriptRoot

if (-not $KeyFile) { $KeyFile = $env:SENTINEL_SIGN_KEY }
if (-not $KeyFile) { $KeyFile = $KeyFileDefault }
if (-not $Tool)    { $Tool    = $ToolDefault }

# --- preconditions ---------------------------------------------------------
if (-not $KeyFile) {
    throw @"
No private key configured. Do one of:
  - set `$KeyFileDefault at the top of scripts\sign-release.ps1
  - set the SENTINEL_SIGN_KEY environment variable
  - pass -KeyFile <path>
Generate one with: $ToolDefault generate-key --file <path>\sentinel-ide.key
"@
}
if (-not (Test-Path -LiteralPath $KeyFile)) { throw "Private key not found: $KeyFile" }
if (-not (Test-Path -LiteralPath $Tool))    { throw "winsparkle-tool.exe not found: $Tool  (see docs\RELEASING.md)" }

# A key inside the repo is one `git add -A` from being published forever.
$keyFull = (Resolve-Path -LiteralPath $KeyFile).Path
if ($keyFull.StartsWith((Resolve-Path -LiteralPath $repo).Path, [StringComparison]::OrdinalIgnoreCase)) {
    throw "REFUSING: the private key is inside the repository ($keyFull). Move it outside before signing."
}

if (-not $SetupExe) {
    $newest = Get-ChildItem (Join-Path $repo 'build\installer\*.exe') -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $newest) { throw "No installer in build\installer\ — run: cmd /c scripts\make-installer.bat" }
    $SetupExe = $newest.FullName
}
if (-not (Test-Path -LiteralPath $SetupExe)) { throw "Installer not found: $SetupExe" }
$SetupExe = (Resolve-Path -LiteralPath $SetupExe).Path

# --- the public key the SHIPPED binary will check against ------------------
# Parsed from source rather than passed in, so this cannot drift from what users run.
$updaterCpp = Join-Path $repo 'src\host\win32\Updater.cpp'
$m = Select-String -LiteralPath $updaterCpp -Pattern 'kEdDsaPublicKey\[\]\s*=\s*"([^"]+)"'
if (-not $m) { throw "Could not find kEdDsaPublicKey in $updaterCpp" }
$pubKey = $m.Matches[0].Groups[1].Value
if ($pubKey.StartsWith('@@')) {
    throw "Updater.cpp still has the placeholder public key — activate auto-update first (docs\RELEASING.md)."
}

# Version the appcast must advertise. This MUST be the APP exe's FileVersion, because
# that is what win_sparkle_set_app_details reports (SENTINEL_FILEVERSION_STR) and therefore
# what WinSparkle compares against sparkle:version. The INSTALLER's own resource is the
# wrong source twice over: Inno leaves its FileVersion blank (only ProductVersion is set),
# and it reflects whatever was packaged, which drifts the moment you rebuild the app.
$appExe = Join-Path $repo 'build\Sentinel-IDE.exe'
if (-not (Test-Path -LiteralPath $appExe)) { throw "Built app not found: $appExe — run scripts\build.bat" }
$version = (Get-Item -LiteralPath $appExe).VersionInfo.FileVersion
if (-not $version -or -not $version.Trim()) { throw "Could not read FileVersion from $appExe" }
$version = $version.Trim()

# The .iss stamps the installer's ProductVersion from the app exe at PACKAGE time. If it no
# longer matches the app exe, the app has been rebuilt since this installer was made — so the
# installer does not contain the binary whose version we are about to advertise. Signing it
# would publish a feed pointing at the wrong bits.
$setupVer = (Get-Item -LiteralPath $SetupExe).VersionInfo.ProductVersion
if ($setupVer) { $setupVer = $setupVer.Trim() }
if ($setupVer -and $setupVer -ne $version) {
    throw @"
STALE INSTALLER — the app has been rebuilt since this installer was packaged.
  installer contains : $setupVer   ($(Split-Path $SetupExe -Leaf))
  current app build  : $version   (build\Sentinel-IDE.exe)
Re-run: cmd /c scripts\make-installer.bat
(Signing this would publish an appcast advertising $version while shipping $setupVer.)
"@
}

Write-Host "installer : $(Split-Path $SetupExe -Leaf)"
Write-Host "version   : $version"
Write-Host "key file  : $KeyFile"
Write-Host "public key: $pubKey"
Write-Host ""

# --- sign ------------------------------------------------------------------
$signature = (& $Tool sign --private-key-file $KeyFile $SetupExe 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or -not $signature) { throw "Signing failed: $signature" }

# --- verify against the compiled-in key ------------------------------------
$verify = (& $Tool verify --public-key $pubKey --signature $signature $SetupExe 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw @"
SIGNATURE DOES NOT VERIFY against the public key compiled into Updater.cpp.
  tool said : $verify
  public key: $pubKey
  key file  : $KeyFile
The key file almost certainly does not match the shipped public key. Publishing this
signature would make every client reject the update. Nothing has been written.
"@
}
Write-Host "verify    : $verify  (against the key compiled into Updater.cpp)" -ForegroundColor Green
Write-Host ""
Write-Host "sparkle:edSignature"
Write-Host $signature

# --- optionally regenerate the appcast -------------------------------------
if ($Appcast) {
    Write-Host ""
    & (Join-Path $PSScriptRoot 'make-appcast.ps1') -Version $version -SetupExe $SetupExe -Signature $signature
} else {
    Write-Host ""
    Write-Host "Next: pwsh scripts\make-appcast.ps1 -Version $version ``"
    Write-Host "           -SetupExe `"$SetupExe`" -Signature <the line above>"
    Write-Host "      (or re-run this script with -Appcast to do it in one step)"
}
