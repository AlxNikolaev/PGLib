using UnrealBuildTool;

public class ProceduralGeometry : ModuleRules
{
    public ProceduralGeometry(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            [
                "Core"
            ]
        );

        PrivateDependencyModuleNames.AddRange(
            [
                "CoreUObject",
                "Engine",
                "ProceduralMeshComponent"
            ]
        );
    }
}