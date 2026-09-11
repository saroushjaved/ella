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
#ifdef SmokeBuild
  #define EllaAppId "{{60C916FC-2D86-4B98-A6D8-490A9F542C2E}"
#else
  #define EllaAppId "{{81FB878B-06E2-4B25-9E91-180F305451E2}"
#endif

[Setup]
AppName=ELLA
AppVersion={#AppVersion}
DefaultDirName={autopf}\ELLA
DefaultGroupName=ELLA
OutputDir={#OutputDir}
OutputBaseFilename=ella-win64-{#AppVersion}-installer
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
PrivilegesRequired=lowest
AppId={#EllaAppId}
CloseApplications=yes
RestartApplications=no
LicenseFile={#SourceDir}\LICENSE.txt
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
