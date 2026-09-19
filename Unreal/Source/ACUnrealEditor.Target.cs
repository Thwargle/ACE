using UnrealBuildTool;
using System.Collections.Generic;

public class ACUnrealEditorTarget : TargetRules
{
	public ACUnrealEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ACUnreal");
	}
}
