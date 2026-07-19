; Sentinel-IDE — Inno Setup script.
;
; Builds a per-user setup.exe (no admin by default): installs the exe + examples +
; docs, a Start-Menu shortcut, and registers the .sntproject / .sentinel file
; associations (the same ProgIDs the app's own "Register File Associations" uses).
; Uninstall reverses all of it.
;
; Requires Inno Setup 6 (winget install JRSoftware.InnoSetup). Build via
; scripts\make-installer.bat → build\installer\Sentinel-IDE-<ver>-setup.exe.

#define AppName "Sentinel-IDE"
#define AppExe "Sentinel-IDE.exe"
#define AppPublisher "Anie"
#define AppUrl "https://github.com/arcanii/Sentinel-IDE"

; Version is READ FROM THE BUILT EXE, not hard-coded, so the installer and its
; filename can never disagree with the binary inside it. scripts\build.bat stamps
; the exe's FileVersion as 0.1.0.<build>, where <build> is derived from the git
; commit count — so the same commit always produces the same installer name.
;
; This matters more now that auto-update ships: WinSparkle compares the running
; build's SENTINEL_FILEVERSION_STR against the appcast's sparkle:version, and
; docs\RELEASING.md requires -Version to equal it. Deriving both from the same
; resource removes the chance of hand-typing a mismatch.
;
; MUST be the FileVersion, NOT the ProductVersion: packaging\SentinelIDE.rc
; hard-codes PRODUCTVERSION 0,1,0,0, so reading ProductVersion would silently pin
; every installer to 0.1.0.0 — precisely the bug this change exists to remove.
#define AppExePath SourcePath + "\..\build\" + AppExe
#if !FileExists(AppExePath)
  #error Build the app first (scripts\build.bat) - the installer reads its version from build\Sentinel-IDE.exe
#endif
#define AppVersion GetVersionNumbersString(AppExePath)

[Setup]
AppId={{9C3E7A14-2B6D-4F09-AE81-5D2C7B3F8A60}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppSupportURL={#AppUrl}
; Sentinel-IDE.exe is x64-only (CMake builds x64; third_party\winsparkle is the x64
; slice). Both directives are required, and they do different jobs:
;
;   ArchitecturesAllowed          — refuse to install where the exe cannot run.
;   ArchitecturesInstallIn64BitMode — put Setup itself into 64-bit mode.
;
; The second one is the one that is easy to forget and the reason this app used to
; install into "C:\Program Files (x86)": Setup is a 32-bit process, so WITHOUT this
; directive it runs in 32-bit mode and {autopf}/{commonpf} resolve through WOW64 to
; the x86 Program Files — the wrong directory for a 64-bit binary. It also means
; [Registry] writes under HKLM go to the redirected Wow6432Node view, so a
; per-machine install's file associations land where 64-bit Explorer may not look.
;
; "x64compatible" rather than "x64" so the x64 build is still offered on ARM64,
; where it runs under Windows' x64 emulation (Inno 6.3+ spelling; plain "x64" is
; the deprecated form and excludes ARM64).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
LicenseFile=..\LICENSE
OutputDir=..\build\installer
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile=app.ico
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ChangesAssociations=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked
Name: "assoc"; Description: "Associate .sntproject and .sentinel files with {#AppName}"

[Files]
Source: "..\build\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
; WinSparkle (auto-update) is loaded at process start, so a missing DLL is a launch
; failure rather than a degraded feature — it must ship with every installed build.
; Sourced from build\ (CMake copies it beside the exe) so the installer always
; carries the same DLL the build was linked against.
Source: "..\build\WinSparkle.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";       DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\LICENSE";         DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
; NB: *.sig is deliberately NOT excluded — examples\crypto.sentinel.sig is what makes
; the installed demo show a real "Signed" trust chip. Dropping it would silently
; downgrade the example that exists to demonstrate ADR-0061.
;
; "crypto" and "hello" are EXTENSIONLESS PE binaries that `snc build` drops beside
; the source (examples\crypto is an MZ image, not a folder), so the *.exe rule above
; does not catch them — the same trap that made .gitignore miss them. Without these
; two names the installer ships whatever build artifacts happen to be lying around
; in the developer's examples\ directory.
Source: "..\examples\*";      DestDir: "{app}\examples"; Excludes: "target\*,*.o,*.obj,*.exe,*.sealed,crypto,hello"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";            Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}";  Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";      Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; ProgIDs + extension associations. HKA = HKCU for a per-user install, HKLM for
; per-machine. Mirrors src\core\FileAssoc.h; DefaultIcon uses the exe's embedded
; icons (res 100 = app, 101 = .sentinel file) by negative resource id.
Root: HKA; Subkey: "Software\Classes\SentinelIDE.Project"; ValueType: string; ValueData: "Sentinel Project"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\SentinelIDE.Project\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},-100"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\SentinelIDE.Project\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\SentinelIDE.Source"; ValueType: string; ValueData: "Sentinel Source"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\SentinelIDE.Source\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},-101"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\SentinelIDE.Source\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.sntproject"; ValueType: string; ValueData: "SentinelIDE.Project"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.sntproject\OpenWithProgids"; ValueType: string; ValueName: "SentinelIDE.Project"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.sentinel"; ValueType: string; ValueData: "SentinelIDE.Source"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.sentinel\OpenWithProgids"; ValueType: string; ValueName: "SentinelIDE.Source"; ValueData: ""; Flags: uninsdeletevalue; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
