# SPDX-License-Identifier: GPL-3.0-or-later
# Generate the WinSparkle appcast.xml at the repo root for a release.
#
# The installer must ALREADY be signed with the Ed25519 private key — this script
# only assembles the feed around the signature you pass in. It never sees the key.
#
#   pwsh scripts\make-appcast.ps1 -Version 0.1.0.25 `
#        -SetupExe build\installer\Sentinel-IDE-0.1.0-setup.exe -Signature <base64>
#
# -Version MUST equal the running build's SENTINEL_FILEVERSION_STR (marketing.build,
# e.g. 0.1.0.25). WinSparkle compares that string against sparkle:version, so a feed
# claiming a version the build does not report either never offers the update or
# offers it forever. See docs/RELEASING.md.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Version,    # marketing.build — must match SENTINEL_FILEVERSION_STR
    [Parameter(Mandatory)][string]$SetupExe,   # path to the built AND signed installer
    [Parameter(Mandatory)][string]$Signature,  # sparkle:edSignature from sign_update.exe
    [string]$Repo = 'arcanii/Sentinel-IDE',
    [string]$Tag,                              # GitHub release tag; defaults to v<marketing version>
    [string]$Notes                             # release-notes URL; defaults to the release page
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $SetupExe)) { throw "Setup exe not found: $SetupExe" }
if ($Version -notmatch '^\d+\.\d+\.\d+\.\d+$') {
    throw "-Version must be marketing.build with four parts (e.g. 0.1.0.25); got '$Version'"
}
if ($Signature -match '^\s*$' -or $Signature -eq 'REPLACE_ME') {
    throw "-Signature is required: sign the installer first (see docs/RELEASING.md)"
}

# The tag carries the MARKETING version (v0.1.0); the appcast carries marketing.build.
if (-not $Tag) { $Tag = 'v' + ($Version -replace '\.\d+$', '') }

$len   = (Get-Item -LiteralPath $SetupExe).Length
$file  = Split-Path -Path $SetupExe -Leaf
$url   = "https://github.com/$Repo/releases/download/$Tag/$file"
if (-not $Notes) { $Notes = "https://github.com/$Repo/releases/tag/$Tag" }
$pub   = (Get-Date).ToUniversalTime().ToString(
    'ddd, dd MMM yyyy HH:mm:ss +0000', [System.Globalization.CultureInfo]::InvariantCulture)
$out   = Join-Path (Split-Path -Path $PSScriptRoot -Parent) 'appcast.xml'

$xml = @"
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>Sentinel-IDE Updates</title>
    <link>https://raw.githubusercontent.com/$Repo/main/appcast.xml</link>
    <description>Most recent updates to Sentinel-IDE</description>
    <language>en</language>
    <item>
      <title>Version $Version</title>
      <sparkle:releaseNotesLink>$Notes</sparkle:releaseNotesLink>
      <pubDate>$pub</pubDate>
      <enclosure url="$url"
                 sparkle:version="$Version"
                 sparkle:edSignature="$Signature"
                 length="$len"
                 type="application/octet-stream"/>
    </item>
  </channel>
</rss>
"@

# UTF-8 with NO BOM, deterministically across Windows PowerShell 5.1 (whose
# Set-Content -Encoding UTF8 emits a BOM) and pwsh 7 — a BOM ahead of the XML
# declaration makes some parsers reject the feed outright.
[System.IO.File]::WriteAllText($out, $xml + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Wrote $out"
Write-Host "  version=$Version  length=$len"
Write-Host "  url=$url"
Write-Host ""
Write-Host "Next:"
Write-Host "  1. Create GitHub release '$Tag' and upload $file as an asset."
Write-Host "  2. Commit + push appcast.xml on main (raw.githubusercontent.com serves the feed)."
Write-Host "  3. Confirm the URL above resolves BEFORE announcing — a 404 enclosure makes"
Write-Host "     every client report an update it then fails to download."
