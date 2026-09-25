using UnrealBuildTool;

public class ProceduralGeometry : ModuleRules
{
    public ProceduralGeometry(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.Add("VariatCore");

        PublicDependencyModuleNames.AddRange(
            [
                "Core",
                "CoreUObject",
                "Engine",
                "ProceduralMeshComponent"
            ]
        );
    }
}
