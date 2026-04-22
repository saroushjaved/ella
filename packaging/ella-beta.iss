; Inno Setup script for ELLA closed beta packaging
; Build with:
;   iscc /DAppVersion=0.9.0-beta.1 /DSourceDir=<staging_dir> /DOutputDir=<artifact_dir> packaging\ella-beta.iss

#ifndef AppVersion
  #define AppVersion "0.9.0-beta.1"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\beta-release"
#endif
#ifndef OutputDir
  #define OutputDir "..\artifacts"
#endif

[Setup]
AppName=ELLA
AppVersion={#AppVersion}
DefaultDirName={autopf}\ELLA
DefaultGroupName=ELLA
OutputDir={#OutputDir}
OutputBaseFilename=ella-beta-win64-{#AppVersion}-installer
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64
DisableProgramGroupPage=yes
SetupIconFile={#SourceDir}\ella_icon.ico
UninstallDisplayIcon={app}\ella_icon.ico

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\ELLA"; Filename: "{app}\appSecondBrain.exe"; IconFilename: "{app}\ella_icon.ico"
Name: "{group}\Uninstall ELLA"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\appSecondBrain.exe"; Description: "Launch ELLA"; Flags: nowait postinstall skipifsilent
