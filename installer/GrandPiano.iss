; Inno Setup script - builds GrandPiano-Setup.exe
; Installs the VST3 where FL Studio (and every other DAW) looks for it.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\GrandPiano"
#endif

[Setup]
AppId={{6B1F2E7A-3C44-4D7B-9A1E-6F0C2B8D9E51}
AppName=Grand Piano
AppVersion={#AppVersion}
AppPublisher=GrandPianoAudio
DefaultDirName={autopf}\Grand Piano
DefaultGroupName=Grand Piano
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=GrandPiano-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=Grand Piano

[Components]
Name: "vst3"; Description: "VST3 plugin (for FL Studio and other DAWs)"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#SourceDir}\Grand Piano.vst3\*"; DestDir: "{commoncf64}\VST3\Grand Piano.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Grand Piano.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#SourceDir}\INSTALL-WINDOWS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Grand Piano (Standalone)"; Filename: "{app}\Grand Piano.exe"; Components: standalone
Name: "{group}\Uninstall Grand Piano"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Grand Piano.exe"; Description: "Open Grand Piano now (to download the piano samples)"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked

[Messages]
FinishedLabel=Grand Piano is installed.%n%nIn FL Studio open Options > Manage plugins and click "Find installed plugins". Grand Piano then appears under Generators.%n%nThe first time you open it, click "Get Samples" to download the piano recordings (about 740 MB).
