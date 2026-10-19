; The Windows installer for Audio Tech Pro (Inno Setup 6): makes "Audio Tech Pro <version> Setup.exe".
; Installs the app into Program Files with a Start menu shortcut (and a desktop one if you like),
; and adds Audio Tech Pro to Settings > Apps, where it can be uninstalled.
;
;   iscc /DAppVersion=0.73.0 /DBuildDir=..\..\build-windows\AudioTechPro_artefacts\Release installer\windows\AudioTechPro.iss
; (scripts\make-windows-installer.bat does this after building)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build-windows\AudioTechPro_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\.."
#endif

[Setup]
AppId={{6A1F3C52-8E0B-4D7A-9B21-3C5D7E9F1A20}
AppName=Audio Tech Pro
AppVersion={#AppVersion}
AppPublisher=Audio Tech Pro
DefaultDirName={autopf}\Audio Tech Pro
DefaultGroupName=Audio Tech Pro
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Audio Tech Pro.exe
OutputDir={#OutputDir}
OutputBaseFilename=Audio Tech Pro {#AppVersion} Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequired=admin
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Make a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#BuildDir}\Audio Tech Pro.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\Audio Tech Pro"; Filename: "{app}\Audio Tech Pro.exe"
Name: "{group}\Uninstall Audio Tech Pro"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Audio Tech Pro"; Filename: "{app}\Audio Tech Pro.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Audio Tech Pro.exe"; Description: "Open Audio Tech Pro now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Its settings, autosaves and themes (your projects and recordings in Music are kept)
Type: filesandordirs; Name: "{userappdata}\Audio Tech Pro"
Type: files; Name: "{userappdata}\Audio Tech Pro.settings"
