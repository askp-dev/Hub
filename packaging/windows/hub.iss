; Inno Setup script for the Windows Hub installer (fueledby-Hub-Windows-Setup.exe).
; Build on Windows with Inno Setup 6:  iscc /DAppExe="path\to\fueledby Hub.exe" packaging\windows\hub.iss
; The setup runs once as administrator. It installs the Hub and creates
; Common Files\VST3\fueledby and Common Files\CLAP\fueledby writable by normal users,
; so the Hub can install and update plugins later without admin prompts.
#ifndef AppExe
  #define AppExe "..\..\build\Hub_artefacts\Release\fueledby Hub.exe"
#endif
#define AppVersion "0.2.2"

[Setup]
AppId={{8300EC93-7637-4FDC-AABD-A6D2AE06A8D1}
AppName=fueledby Hub
AppVersion={#AppVersion}
AppPublisher=fueledby
DefaultDirName={autopf}\fueledby Hub
DefaultGroupName=fueledby Hub
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=fueledby-Hub-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=fueledby Hub
; Same AppId as the pre-rebrand "askp Hub", so this replaces it. Don't reuse its folder or menu group.
UsePreviousAppDir=no
UsePreviousGroup=no

[InstallDelete]
; What the pre-rebrand askp Hub left behind. Plugins in Common Files\VST3\askp and CLAP\askp are
; moved into the fueledby folders by the Hub itself on its first launch.
Type: filesandordirs; Name: "{autopf}\askp Hub"
Type: filesandordirs; Name: "{commonprograms}\askp Hub"
Type: filesandordirs; Name: "{userprograms}\askp Hub"
Type: files; Name: "{autodesktop}\askp Hub.lnk"
Type: files; Name: "{commondesktop}\askp Hub.lnk"

[Dirs]
Name: "{commoncf64}\VST3\fueledby"; Permissions: users-modify
Name: "{commoncf64}\CLAP\fueledby"; Permissions: users-modify

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; DestName: "fueledby Hub.exe"; Flags: ignoreversion

[Icons]
Name: "{group}\fueledby Hub"; Filename: "{app}\fueledby Hub.exe"
Name: "{autodesktop}\fueledby Hub"; Filename: "{app}\fueledby Hub.exe"; Tasks: desktopicon

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"

[Run]
Filename: "{app}\fueledby Hub.exe"; Description: "Open fueledby Hub"; Flags: nowait postinstall skipifsilent
