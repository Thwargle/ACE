// Included in [Code]. Minimum versions come from the actual bundled packages.
#ifndef VisualCppRuntimeVersion
  #error VisualCppRuntimeVersion must come from the bundled vc_redist.x64.exe
#endif
#ifndef GameInputRuntimeVersion
  #error GameInputRuntimeVersion must come from the bundled GameInputRedist.dll
#endif

#ifndef PrerequisiteTestHarness
function ReadPrerequisiteDWord(RootKey: Integer; const SubKey, ValueName: String;
  var Value: Cardinal): Boolean;
begin
  Result := RegQueryDWordValue(RootKey, SubKey, ValueName, Value);
end;

function ReadPrerequisiteFileVersion(const Filename: String; var Version: Int64): Boolean;
begin
  Result := GetPackedVersion(Filename, Version);
end;
#endif

function PrerequisiteVersionAtLeast(InstalledVersion: Int64; const RequiredVersion: String): Boolean;
var
  MinimumVersion: Int64;
begin
  if not StrToVersion(RequiredVersion, MinimumVersion) then
    RaiseException('Invalid bundled prerequisite version: ' + RequiredVersion);
  Result := ComparePackedVersion(InstalledVersion, MinimumVersion) >= 0;
end;

function HasVisualCppRuntimeInView(RootKey: Integer): Boolean;
var
  Installed, Major, Minor, Build, Revision: Cardinal;
  Version: Int64;
  Key: String;
begin
  Result := False;
  // Always test the x64 runtime, even when its registration is in the 32-bit view.
  // An installed x86-only runtime cannot satisfy this 64-bit game's dependency.
  Key := 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64';
  if not ReadPrerequisiteDWord(RootKey, Key, 'Installed', Installed) then Exit;
  if Installed <> 1 then Exit;
  if not ReadPrerequisiteDWord(RootKey, Key, 'Major', Major) then Exit;
  if not ReadPrerequisiteDWord(RootKey, Key, 'Minor', Minor) then Exit;
  if not ReadPrerequisiteDWord(RootKey, Key, 'Bld', Build) then Exit;
  if not ReadPrerequisiteDWord(RootKey, Key, 'Rbld', Revision) then Exit;
  if (Major > 65535) or (Minor > 65535) or (Build > 65535) or (Revision > 65535) then Exit;
  Version := PackVersionComponents(Major, Minor, Build, Revision);
  Result := PrerequisiteVersionAtLeast(Version, '{#VisualCppRuntimeVersion}');
  Log('Visual C++ x64 runtime: ' + VersionToStr(Version));
end;

function NeedsVisualCppRuntime(): Boolean;
begin
  // Microsoft documents registration in both registry views across redist releases.
  Result := not (HasVisualCppRuntimeInView(HKLM64) or HasVisualCppRuntimeInView(HKLM32));
  if Result then
    Log('Visual C++ x64 prerequisite: missing or older than {#VisualCppRuntimeVersion}')
  else
    Log('Visual C++ x64 prerequisite: compatible runtime installed; skipping');
end;

function HasGameInputRuntimeFile(const Filename: String): Boolean;
var
  Version: Int64;
begin
  Result := False;
  if not ReadPrerequisiteFileVersion(Filename, Version) then Exit;
  Result := PrerequisiteVersionAtLeast(Version, '{#GameInputRuntimeVersion}');
  Log('GameInput runtime: ' + Filename + ' version ' + VersionToStr(Version));
end;

function NeedsGameInputRuntime(): Boolean;
begin
  // Match Unreal's GameInputRedist.dll check, not the older Windows GameInput.dll
  // or the MSI ProductVersion (10.x) which differs from the runtime version (3.x).
  // In 64-bit install mode {sys} refers to the native 64-bit system directory.
  Result := not (HasGameInputRuntimeFile(ExpandConstant('{sys}\GameInputRedist.dll')) or
    HasGameInputRuntimeFile(ExpandConstant('{app}\GameInputRedist.dll')) or
    HasGameInputRuntimeFile(ExpandConstant('{app}\ACUnreal\Binaries\Win64\GameInputRedist.dll')));
  if Result then
    Log('GameInput prerequisite: missing or older than {#GameInputRuntimeVersion}')
  else
    Log('GameInput prerequisite: compatible runtime installed; skipping');
end;
