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
                "ProceduralMeshComponent",
                // Vector floor/wall geometry for OrganicDungeon clusters:
                //   GeometryCore       — FGeneralPolygon2d / TPolygon2 / TVector2 interchange types.
                //   GeometryAlgorithms — PolygonsOffset (corridor ribbons), PolygonsUnion (region merge),
                //                        ConstrainedDelaunay2 (concave-with-holes floor cap triangulation).
                // GeometryAlgorithms is a Runtime module of the GeometryProcessing plugin (enabled in the
                // .uproject for all targets); GeometryCore is an engine Runtime module.
                "GeometryCore",
                "GeometryAlgorithms"
            ]
        );
    }
}