; The Windows installer for the Pitch Correction plug-in on its own (Inno Setup 6):
; makes "Audio Tech Pro Pitch Correction <version> Setup.exe". Installs "Pitch Correction.vst3" into
; C:\Program Files\Common Files\VST3 (where every VST3 host looks) and adds it to Settings > Apps.
;
;   iscc /DAppVersion=1.25.1 /DPluginDir=<folder holding Pitch Correction.vst3> installer\windows\PitchCorrection.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef PluginDir
  #define PluginDir "..\..\build-plugins\FxPitchCorrection_artefacts\Release\VST3"
#endif
#ifndef OutputDir
  #define OutputDir "..\.."
#endif

[Setup]
AppId={{B4E2A917-3C6D-4F80-A1B5-7D9E2C4F6A31}
AppName=Audio Tech Pro Pitch Correction
AppVersion={#AppVersion}
AppPublisher=Audio Tech Pro
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={commonappdata}\Audio Tech Pro\Pitch Correction Uninstall
OutputDir={#OutputDir}
OutputBaseFilename=Audio Tech Pro Pitch Correction {#AppVersion} Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequired=admin

[Files]
Source: "{#PluginDir}\Pitch Correction.vst3\*"; DestDir: "{commoncf64}\VST3\Pitch Correction.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Pitch Correction.vst3"

[Messages]
FinishedLabel=Pitch Correction is installed. Rescan plug-ins in your DAW (FL Studio: Options > Manage plugins > Find installed plugins, then star it).
