; EPINOX VOX ENGINE - Windows installer (Inno Setup 6)
;
; Built by the GitHub workflow:
;   ISCC /DAppVersion=0.3.0 /DBuildDir=<build>\EpinoxVoxEngine_artefacts\Release EpinoxVoxEngine.iss
;
; The installer never asks for a restart: the plugin is a plain VST3 folder in the shared VST3 location,
; there are no drivers or services involved.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #error BuildDir is required (folder that contains VST3\ and Standalone\)
#endif

#define AppName "EPINOX VOX ENGINE"

[Setup]
AppId={{B7A3F0C2-6D1E-4E8B-9C57-2F4A8E1D6B30}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=EPINOX
AppPublisherURL=https://epinoxmusic.com
AppSupportURL=https://epinoxmusic.com
DefaultDirName={autopf}\EPINOX\VOX ENGINE
DefaultGroupName=EPINOX
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputDir=..\..\dist
OutputBaseFilename=EPINOX-VOX-ENGINE-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\EPINOX VOX ENGINE.exe
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
VersionInfoVersion={#AppVersion}
VersionInfoCompany=EPINOX
VersionInfoProductName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Files]
; VST3 plugin (system wide VST3 folder, scanned by Ableton Live 11 and 12)
Source: "{#BuildDir}\VST3\EPINOX VOX ENGINE.vst3\*"; DestDir: "{commoncf64}\VST3\EPINOX VOX ENGINE.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
; Standalone application
Source: "{#BuildDir}\Standalone\EPINOX VOX ENGINE.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\EPINOX VOX ENGINE.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\EPINOX VOX ENGINE.vst3"

[Code]
// Never ask the user to restart Windows.
function NeedRestart(): Boolean;
begin
  Result := False;
end;
