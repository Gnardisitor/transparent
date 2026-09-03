; Windows installer script. Run via packaging/build-installer.ps1, which
; passes /DAppVersion and /DStageDir.

#define AppName "Transparent"

#ifndef AppVersion
#define AppVersion "0.0.0"
#endif

#ifndef StageDir
#define StageDir "..\build\install-stage"
#endif

[Setup]
; Changing this GUID orphans previously installed versions.
AppId={{9F18FAAD-6AF2-4132-8DED-A79C6D6DE0C5}
AppName={#AppName}
AppVersion={#AppVersion}
; With PrivilegesRequired=lowest, {autopf} resolves to %LOCALAPPDATA%\Programs.
DefaultDirName={autopf}\{#AppName}
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
LicenseFile=..\LICENSE
OutputDir=..\build\dist
OutputBaseFilename=Transparent-{#AppVersion}-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\bin\transparent.exe
; Leaves %APPDATA%\transparent (models, settings) in place.

; The app looks for bundled models at <exeDir>\..\share\transparent\models
; (bundledModelsDefaultsDir in src/main.cpp), hence the share tree here.
[Files]
Source: "{#StageDir}\bin\*"; DestDir: "{app}\bin"; Flags: recursesubdirs ignoreversion
Source: "{#StageDir}\share\*"; DestDir: "{app}\share"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\bin\transparent.exe"
