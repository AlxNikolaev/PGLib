#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"
#include "OrganicDungeon2DVisualizer.generated.h"

class UProceduralMeshComponent;

/** Editor visualizer for UOrganicDungeonGenerator2D — rebuilds on construction. */
UCLASS()
class PROCEDURALGEOMETRY_API AOrganicDungeon2DVisualizer : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* GridMeshComponent;

	UPROPERTY(EditAnywhere,
		Category = "Visualization",
		meta = (ToolTip = "Material for the grid mesh. Use an unlit material with a VertexColor node wired to BaseColor."))
	UMaterialInterface* DebugMaterial;

	UPROPERTY(EditInstanceOnly)
	FString Seed;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 10, ToolTip = "Fine grid cell size in world units. Smaller = smoother/organic."))
	int32 GridSize = 50;

	UPROPERTY(EditInstanceOnly, meta = (ToolTip = "Organic dungeon generation parameters."))
	FOrganicDungeonConfig Config;

	// --- Layer toggles ---
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Wall/corridor/room cell fill."))
	bool bShowGridCells = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Corridor centerline splines."))
	bool bShowCorridorSplines = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Doorway (punch-point) markers used by corridors."))
	bool bShowDoorways = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Room center markers / oriented footprint outlines."))
	bool bShowRoomBounds = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Metrics overlay near the grid bounds."))
	bool bShowMetrics = true;

	/** Level instances spawned by SpawnLevelInstances(), tracked so they can be cleared/respawned. */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedInstances;

public:
	AOrganicDungeon2DVisualizer();

	/** Generates the layout (same seed as the preview) and spawns a level instance for each placed room. */
	UFUNCTION(CallInEditor, Category = "Organic Dungeon", meta = (DisplayName = "Spawn Level Instances"))
	void SpawnLevelInstances();

	/** Destroys all level instances previously spawned by this visualizer. */
	UFUNCTION(CallInEditor, Category = "Organic Dungeon", meta = (DisplayName = "Clear Spawned Instances"))
	void ClearSpawnedInstances();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	/** Builds a flat quad mesh section from a set of grid cell positions. */
	void BuildCellMeshSection(int32 SectionIndex,
		const TArray<FIntPoint>&	CellPositions,
		float						CellSize,
		const FVector2D&			GridOriginLocal,
		const FLinearColor&			Color,
		float						ZOffset);
};
