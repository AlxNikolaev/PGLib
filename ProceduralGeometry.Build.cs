using UnrealBuildTool;

public class ProceduralGeometry : ModuleRules
{
    public ProceduralGeometry(ReadOnlyTargetRules target) : base(target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // Disable unity builds — this module has many small files with file-scoped
        // symbols (test flags, log categories) that collide under unity grouping.
        bUseUnity = false;

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