#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DrunkardWalk2DVisualizer.generated.h"

UCLASS()
class PROCEDURALGEOMETRY_API ADrunkardWalk2DVisualizer : public AActor
{
	GENERATED_BODY()

	UPROPERTY(EditInstanceOnly)
	FBox2D Bounds;

	UPROPERTY(EditInstanceOnly)
	FString Seed;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 10))
	int32 GridSize = 100;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 WalkLength = 500;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 NumWalkers = 1;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 0, ClampMax = 1))
	float BranchProbability = 0.0f;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 CorridorWidth = 1;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 0, ClampMax = 1))
	float RoomChance = 0.0f;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 RoomRadius = 1;

	// --- Visualization layer toggles ---

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Wall/floor cell fill."))
	bool bShowGridCells = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Per-region distinct hue tinting on floor cells."))
	bool bShowRegionColors = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Distinct tint overlay for room cells vs corridor cells."))
	bool bShowRoomHighlights = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Thin lines at cell boundaries."))
	bool bShowGridLines = false;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Lines showing each walker's trajectory, color per walker."))
	bool bShowWalkerPaths = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Thick outlines around region polygons from Diagram."))
	bool bShowRegionBoundaries = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Lines between neighboring region centers."))
	bool bShowAdjacencyGraph = false;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Highlighted cell at grid center."))
	bool bShowCenterMarker = true;

public:
	ADrunkardWalk2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
