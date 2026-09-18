using UnrealBuildTool;

public class ACEWorldBakeEditor : ModuleRules
{
	public ACEWorldBakeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivatePCHHeaderFile = "Public/ACEWorldBakeEditor.h";

		PrivateIncludePathModuleNames.AddRange(
			new string[] 
			{
				"AssetRegistry",
				"AssetTools",
				"BlueprintGraph",
				"MainFrame",
				"WorldBrowser",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"ACEClient",
				"ACEWorldBakeDatTools",
				"ACEWorldBake",
				"BlueprintGraph",
				"BlueprintMaterialTextureNodes",
				"Core",
				"CoreUObject",
				"Engine",
				"Landscape",
				"LandscapeEditor",
				"Slate",
				"UnrealEd",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"LevelEditor",
				"EditorScriptingUtilities",
				"InterchangeCore",
				"InterchangeEngine",
				"ModelContextProtocolEngine",
				"PropertyEditor",
				"RenderCore",
				"RHI",
				"SlateCore",
				"SourceControl",
				"WorldBrowser",
			}
		);
	}
}
