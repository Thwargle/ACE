using UnrealBuildTool;

public class ACEWorldBakeDatTools : ModuleRules
{
	public ACEWorldBakeDatTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivatePCHHeaderFile = "Public/ACEWorldBakeDatTools.h";

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"ImageWrapper",
				"Landscape",
			}
		);
	}
}
