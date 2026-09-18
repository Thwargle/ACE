using UnrealBuildTool;
using System.Collections.Generic;

public class ACEViewerTarget : TargetRules
{
	public ACEViewerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ACEViewer");
	}
}
