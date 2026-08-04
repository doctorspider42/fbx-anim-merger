; Inno Setup script for FBX Animation Merger.
;
; Not meant to be compiled by hand - tools/package.ps1 stages the payload and
; passes the version in:
;
;   ISCC.exe /DAppVersion=0.2.16 /DPayloadDir=..\build\package\app /DOutDir=..\dist installer\FbxAnimMerger.iss
;
; The install is deliberately per-user (PrivilegesRequired=lowest, so {autopf}
; resolves to %LOCALAPPDATA%\Programs). That is what lets the in-app updater run
; this installer silently without a UAC prompt, which is the whole point of having
; one. There is no all-users option for exactly that reason.

#define AppName "FBX Animation Merger"
#define AppPublisher "doctorspider42"
#define AppUrl "https://github.com/doctorspider42/fbx-anim-merger"
#define AppExe "FbxAnimMerger.exe"

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\build\package\app"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

[Setup]
; Never change AppId: it is what ties an upgrade to the copy already installed.
AppId={{6F2A81D4-1B0E-4C6B-9F4A-2E7C5D0B93A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
VersionInfoVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
DefaultDirName={autopf}\FbxAnimMerger
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
LicenseFile={#PayloadDir}\LICENSE
OutputDir={#OutDir}
OutputBaseFilename=FbxAnimMerger-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
; The updater launches this while the application is still shutting down, so let
; Restart Manager deal with whatever is still holding the .exe.
CloseApplications=yes
RestartApplications=no
; The PATH task writes to the user's environment block.
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addtopath"; Description: "Add fam-cli to this user's PATH"; GroupDescription: "Command line:"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\FbxAnimMerger.exe";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\fam-cli.exe";              DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\README.md";                DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\LICENSE";                  DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\THIRD_PARTY_LICENSES.md";  DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}";        Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";  Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
    ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}'))

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent
; The in-app updater passes /UPDATE, and only then does a silent run bring the
; application back. A scripted deployment gets no surprise window.
Filename: "{app}\{#AppExe}"; Flags: nowait; Check: RelaunchAfterSilentUpdate

[Code]
const
  EnvironmentKey = 'Environment';

function CommandLineHas(const Flag: string): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), Flag) = 0 then
    begin
      Result := True;
      exit;
    end;
end;

function RelaunchAfterSilentUpdate: Boolean;
begin
  Result := WizardSilent and CommandLineHas('/UPDATE');
end;

// True when Dir is not already one of the entries in the user's PATH. Without it
// a reinstall would append the same directory again on every run.
function NeedsAddPath(Dir: string): Boolean;
var
  CurrentPath: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', CurrentPath) then
  begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(CurrentPath) + ';') = 0;
end;

procedure RemoveFromPath(Dir: string);
var
  CurrentPath, Padded: string;
  At: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path', CurrentPath) then
    exit;
  // Padding both ends means the first and last entries match the same way as any
  // other, so there is only one case to get right.
  Padded := ';' + CurrentPath + ';';
  At := Pos(';' + Uppercase(Dir) + ';', Uppercase(Padded));
  if At = 0 then
    exit;
  Delete(Padded, At, Length(Dir) + 1);
  RegWriteExpandStringValue(HKEY_CURRENT_USER, EnvironmentKey, 'Path',
                            Copy(Padded, 2, Length(Padded) - 2));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
