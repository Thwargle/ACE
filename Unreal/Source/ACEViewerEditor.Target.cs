using UnrealBuildTool;
using System.Collections.Generic;

public class ACEViewerEditorTarget : TargetRules
{
	public ACEViewerEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ACEViewer");
	}
}
