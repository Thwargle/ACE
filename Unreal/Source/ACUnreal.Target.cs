using UnrealBuildTool;
using System.Collections.Generic;

public class ACUnrealTarget : TargetRules
{
	public ACUnrealTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ACUnreal");
	}
}
