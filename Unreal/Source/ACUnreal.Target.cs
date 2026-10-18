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

		// Game packages load the explicitly declared client/VR plugins and their
		// dependencies. Retail effects use the DAT renderer, not Niagara; loading
		// unused default engine plugins can trigger its UE 5.8 type-hash assertion.
		// The editor target retains the default plugins for offline asset tools.
		bAllowEnginePluginsEnabledByDefault = false;
	}
}
