using UnrealBuildTool;

public class ACERetailRender : ModuleRules
{
    public ACERetailRender(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "CoreUObject", "Projects", "RenderCore", "Renderer", "RHI" });
    }
}
