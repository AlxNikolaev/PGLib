#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"
#include "DrunkardWalk2DVisualizer.generated.h"

class UProceduralMeshComponent;

UCLASS()
class PROCEDURALGEOMETRY_API ADrunkardWalk2DVisualizer : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere)
	UProceduralMeshComponent* GridMeshComponent;

	UPROPERTY(EditAnywhere,
		Category = "Visualization",
		meta = (ToolTip = "Material for grid mesh. Use an unlit material with VertexColor node connected to BaseColor for colored regions."))
	UMaterialInterface* DebugMaterial;

	UPROPERTY(EditInstanceOnly)
	FBox2D Bounds;

	/** Non-empty by default: an empty seed makes the generator invent a fresh one on every construction, so the
	 *  preview would redraw a different dungeon on every property tweak. */
	UPROPERTY(EditInstanceOnly)
	FString Seed = TEXT("Preview");

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 10))
	int32 GridSize = 100;

	UPROPERTY(EditInstanceOnly, meta = (ToolTip = "Dungeon generation parameters. Resolved into raw DW values on construction."))
	FDrunkardWalkConfig Config;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Wall/floor cell fill."))
	bool bShowGridCells = true;

	UPROPERTY(EditInstanceOnly,
		Category = "Visualization Layers",
		meta = (EditCondition = "bShowRoomHighlights", ToolTip = "Distinct hue per room. Off: every room shares one flat floor tint."))
	bool bShowRegionColors = true;

	UPROPERTY(EditInstanceOnly,
		Category = "Visualization Layers",
		meta = (ToolTip = "Tints room cells apart from corridor cells. Off: rooms render in the corridor color."))
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

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Show generation metrics overlay near grid bounds."))
	bool bShowMetrics = true;

	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Apply Chaikin subdivision to region boundary outlines."))
	bool bSmoothBoundaries = true;

	UPROPERTY(EditInstanceOnly,
		Category = "Visualization Layers",
		meta = (ClampMin = 1,
			ClampMax = 5,
			EditCondition = "bSmoothBoundaries",
			ToolTip = "Number of Chaikin subdivision iterations for boundary smoothing."))
	int32 SmoothingIterations = 2;

public:
	ADrunkardWalk2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
