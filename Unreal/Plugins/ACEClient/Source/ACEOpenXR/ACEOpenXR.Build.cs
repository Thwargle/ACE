using UnrealBuildTool;

public class ACEOpenXR : ModuleRules
{
    public ACEOpenXR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "OpenXRHMD", "AugmentedReality" });
    }
}
