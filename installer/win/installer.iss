#define MyAppName "BlkFx"
#define MyAppVersion "0.2.0"
#define MyAppPublisher "Cognitoni"
#define MyAppURL "https://github.com/toni-lyttinen/CognitoniBlkFx"

[Setup]
AppId={{B4F3E5D1-2C8A-4A7E-9D6F-1B3C5E7F9A0B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={code:GetDefaultDir}
DisableDirPage=no
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\..\Builds
OutputBaseFilename=BlkFx_Win_Installer
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\BlkFx.vst3
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\..\build\BlkFx_artefacts\Release\VST3\BlkFx.vst3"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall BlkFx"; Filename: "{uninstallexe}"

[Code]
function GetDefaultDir(Param: string): string;
begin
  Result := ExpandConstant('{pf}\Common Files\VST3');
end;

function InitializeSetup: Boolean;
begin
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Success message
  end;
end;
