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
#define AppVersion "0.1.0"
#define AppExe "Sentinel-IDE.exe"
#define AppPublisher "Anie"
#define AppUrl "https://github.com/"

[Setup]
AppId={{9C3E7A14-2B6D-4F09-AE81-5D2C7B3F8A60}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppSupportURL={#AppUrl}
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
Source: "..\README.md";       DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\LICENSE";         DestDir: "{app}"; Flags: ignoreversion
Source: "..\examples\*";      DestDir: "{app}\examples"; Excludes: "target\*,*.o,*.obj,*.exe,*.sealed"; Flags: ignoreversion recursesubdirs createallsubdirs

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
