[Setup]
AppName=AC Prerequisite Detection Tests
AppVersion=1.0
DefaultDirName={tmp}\ACPrerequisiteTests
CreateAppDir=no
Uninstallable=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#TestOutput}
OutputBaseFilename={#TestName}
Compression=none

[Code]
#ifdef PrerequisiteTestHarness
var
  Vc64Version, Vc32Version, SystemGameInput, LocalGameInput: String;
  VcInstalled: Cardinal;
  IncompleteVc, CorruptVc: Boolean;

function ReadPrerequisiteDWord(RootKey: Integer; const SubKey, ValueName: String;
  var Value: Cardinal): Boolean;
var
  VersionText: String;
  Version: Int64;
  Major, Minor, Build, Revision: Word;
begin
  if SubKey <> 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' then
    RaiseException('Detection must query x64, not an x86 runtime');
  VersionText := '';
  if RootKey = HKLM64 then VersionText := Vc64Version;
  if RootKey = HKLM32 then VersionText := Vc32Version;
  Result := StrToVersion(VersionText, Version);
  if not Result then Exit;
  UnpackVersionComponents(Version, Major, Minor, Build, Revision);
  if ValueName = 'Installed' then Value := VcInstalled
  else if ValueName = 'Major' then begin
    Value := Major;
    if CorruptVc then Value := 70000;
  end
  else if ValueName = 'Minor' then Value := Minor
  else if ValueName = 'Bld' then Value := Build
  else if ValueName = 'Rbld' then begin
    Value := Revision;
    Result := not IncompleteVc;
  end
  else RaiseException('Unexpected registry value');
end;

function ReadPrerequisiteFileVersion(const Filename: String; var Version: Int64): Boolean;
begin
  Result := False;
  if Filename = ExpandConstant('{sys}\GameInputRedist.dll') then
    Result := StrToVersion(SystemGameInput, Version)
  else if Filename = ExpandConstant('{app}\GameInputRedist.dll') then
    Result := StrToVersion(LocalGameInput, Version)
  else if Filename <> ExpandConstant('{app}\ACUnreal\Binaries\Win64\GameInputRedist.dll') then
    RaiseException('Detection must not accept the legacy GameInput.dll');
end;
#endif

#include "..\Windows-Prerequisites.iss"

procedure Expect(const Name: String; Condition: Boolean);
begin
  if not Condition then RaiseException('FAIL: ' + Name);
  Log('PASS: ' + Name);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssInstall then Exit;
#ifdef PrerequisiteTestHarness
  VcInstalled := 1;
  Expect('No x64 runtime, including x86-only installs, requires installation', NeedsVisualCppRuntime());
  Vc64Version := '14.49.1.0';
  Expect('Older Visual C++ runtime requires update', NeedsVisualCppRuntime());
  Vc64Version := '{#VisualCppRuntimeVersion}';
  Expect('Equal Visual C++ runtime skips install', not NeedsVisualCppRuntime());
  Vc64Version := '14.99.1.0';
  Expect('Newer Visual C++ runtime skips downgrade', not NeedsVisualCppRuntime());
  Vc64Version := '15.0.0.0';
  Expect('Version comparison is numeric across major versions', not NeedsVisualCppRuntime());
  Vc64Version := '';
  Vc32Version := '{#VisualCppRuntimeVersion}';
  Expect('x64 registration in 32-bit registry view is detected', not NeedsVisualCppRuntime());
  Vc64Version := '14.0.1.0';
  Expect('An old registry view cannot hide a compatible view', not NeedsVisualCppRuntime());
  VcInstalled := 0;
  Expect('Uninstalled registration must not suppress install', NeedsVisualCppRuntime());
  VcInstalled := 1;
  IncompleteVc := True;
  Expect('Incomplete registration requires install', NeedsVisualCppRuntime());
  IncompleteVc := False;
  CorruptVc := True;
  Expect('Malformed DWORD version must not overflow into compatibility', NeedsVisualCppRuntime());
  CorruptVc := False;

  Expect('Missing GameInputRedist.dll requires installation', NeedsGameInputRuntime());
  SystemGameInput := '3.0.1.0';
  Expect('Old GameInput runtime requires update', NeedsGameInputRuntime());
  SystemGameInput := '{#GameInputRuntimeVersion}';
  Expect('Equal GameInput runtime skips install', not NeedsGameInputRuntime());
  SystemGameInput := '3.5.270.0';
  Expect('Newer GameInput DLL skips install despite changed MSI product IDs', not NeedsGameInputRuntime());
  SystemGameInput := '';
  LocalGameInput := '{#GameInputRuntimeVersion}';
  Expect('Compatible app-local GameInput runtime is detected', not NeedsGameInputRuntime());
  LocalGameInput := 'not-a-version';
  Expect('Unreadable file version requires installation', NeedsGameInputRuntime());
#else
  Expect('Live Visual C++ detection matches independent registry probe',
    NeedsVisualCppRuntime() = {#ExpectedVcNeeded});
  Expect('Live GameInput detection matches native System32 file probe',
    NeedsGameInputRuntime() = {#ExpectedGameInputNeeded});
#endif
  if not SaveStringToFile(ExpandConstant('{param:RESULTFILE}'), 'PASS', False) then
    RaiseException('Cannot write test result');
end;
