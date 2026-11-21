; Inno Setup script for the Windows Hub installer (askp-Hub-Windows-Setup.exe).
; Build on Windows with Inno Setup 6:  iscc /DAppExe="path\to\askp Hub.exe" packaging\windows\hub.iss
; The setup runs once as administrator. It installs the Hub and creates
; Common Files\VST3\askp and Common Files\CLAP\askp writable by normal users,
; so the Hub can install and update plugins later without admin prompts.
#ifndef AppExe
  #define AppExe "..\..\build\Hub_artefacts\Release\askp Hub.exe"
#endif
#define AppVersion "0.2.0"

[Setup]
AppId={{8300EC93-7637-4FDC-AABD-A6D2AE06A8D1}
AppName=askp Hub
AppVersion={#AppVersion}
AppPublisher=askp
DefaultDirName={autopf}\askp Hub
DefaultGroupName=askp Hub
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=askp-Hub-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=askp Hub

[Dirs]
Name: "{commoncf64}\VST3\askp"; Permissions: users-modify
Name: "{commoncf64}\CLAP\askp"; Permissions: users-modify

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; DestName: "askp Hub.exe"; Flags: ignoreversion

[Icons]
Name: "{group}\askp Hub"; Filename: "{app}\askp Hub.exe"
Name: "{autodesktop}\askp Hub"; Filename: "{app}\askp Hub.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"

[Run]
Filename: "{app}\askp Hub.exe"; Description: "Open askp Hub"; Flags: nowait postinstall skipifsilent
