using UnrealBuildTool;

public class ACUnreal : ModuleRules
{
	public ACUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"ACEClient",
			"ProceduralMeshComponent",
			"RenderCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Landscape"
		});
	}
}
