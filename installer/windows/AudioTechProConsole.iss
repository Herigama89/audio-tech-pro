; The Windows installer for Audio Tech Pro Console (Inno Setup 6): makes "Audio Tech Pro Console <version> Setup.exe".
; Installs the console into Program Files with a Start menu shortcut (and a desktop one if you like),
; and adds it to Settings > Apps, where it can be uninstalled.
;
;   iscc /DAppVersion=1.26.0 /DBuildDir=..\..\build-windows\AudioTechProConsole_artefacts\Release installer\windows\AudioTechProConsole.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build-windows\AudioTechProConsole_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\.."
#endif

[Setup]
AppId={{C3E5A1D7-4F2B-4B8E-9A61-7D2F0B9C5E14}
AppName=Audio Tech Pro Console
AppVersion={#AppVersion}
AppPublisher=Audio Tech Pro
DefaultDirName={autopf}\Audio Tech Pro Console
DefaultGroupName=Audio Tech Pro Console
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Audio Tech Pro Console.exe
OutputDir={#OutputDir}
OutputBaseFilename=Audio Tech Pro Console {#AppVersion} Setup
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
Source: "{#BuildDir}\Audio Tech Pro Console.exe"; DestDir: "{app}"; Flags: ignoreversion
; The ASIO driver your DAW opens ("Audio Tech Pro Console"): registered on install, unregistered on uninstall
Source: "{#BuildDir}\AudioTechProConsoleASIO.dll"; DestDir: "{app}"; Flags: ignoreversion regserver restartreplace uninsrestartdelete
Source: "{#BuildDir}\*.dll"; DestDir: "{app}"; Excludes: "AudioTechProConsoleASIO.dll"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\Audio Tech Pro Console"; Filename: "{app}\Audio Tech Pro Console.exe"
Name: "{group}\Uninstall Audio Tech Pro Console"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Audio Tech Pro Console"; Filename: "{app}\Audio Tech Pro Console.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Audio Tech Pro Console.exe"; Description: "Open Audio Tech Pro Console now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Its settings, plugin list and last session
Type: filesandordirs; Name: "{userappdata}\Audio Tech Pro Console"
Type: files; Name: "{userappdata}\Audio Tech Pro Console.settings"
