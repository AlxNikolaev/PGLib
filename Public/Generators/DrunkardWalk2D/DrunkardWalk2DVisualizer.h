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

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
	friend class FVariatSelectionBudgetMetadataTest;
#endif

	/** Generated preview mesh, owned by this actor and refreshed on construction. */
	UPROPERTY(VisibleAnywhere, Category = "Visualization")
	UProceduralMeshComponent* GridMeshComponent;

	/** Material for coloured room and corridor preview cells; VertexColor supplies their tint. */
	UPROPERTY(EditAnywhere,
		Category = "Visualization",
		meta = (ToolTip = "Material for grid mesh. Use an unlit material with VertexColor node connected to BaseColor for colored regions."))
	UMaterialInterface* DebugMaterial;

	/** XY preview bounds in centimetres; the visualizer frames the generated walk here. */
	UPROPERTY(EditInstanceOnly, Category = "Dungeon Preview", meta = (Units = "cm"))
	FBox2D Bounds;

	/** Non-empty by default: an empty seed makes the generator invent a fresh one on every construction, so the
	 *  preview would redraw a different dungeon on every property tweak. */
	UPROPERTY(EditInstanceOnly, Category = "Dungeon Preview")
	FString Seed = TEXT("Preview");

	/** Preview cell pitch in centimetres; smaller cells produce denser visual geometry. */
	UPROPERTY(EditInstanceOnly, Category = "Dungeon Preview", meta = (ClampMin = 10, Units = "cm"))
	int32 GridSize = 100;

	/** Room pool and corridor parameters resolved for the requested room budget on construction. */
	UPROPERTY(EditInstanceOnly,
		Category = "Dungeon Preview",
		meta = (ToolTip = "Dungeon generation parameters. Resolved into raw DW values on construction.", SelectionBudgetMin = "TotalRooms"))
	FDrunkardWalkConfig Config;

	/** Explicit budget, resolved by the same allocator used in runtime locations. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization", meta = (ClampMin = "0"))
	int32 TotalRooms = 8;

	/** Displays filled wall and floor cells. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Wall/floor cell fill."))
	bool bShowGridCells = true;

	/** Gives each room a distinct hue when room highlighting is enabled. */
	UPROPERTY(EditInstanceOnly,
		Category = "Visualization Layers",
		meta = (EditCondition = "bShowRoomHighlights", ToolTip = "Distinct hue per room. Off: every room shares one flat floor tint."))
	bool bShowRegionColors = true;

	/** Tints room cells separately from corridor cells. */
	UPROPERTY(EditInstanceOnly,
		Category = "Visualization Layers",
		meta = (ToolTip = "Tints room cells apart from corridor cells. Off: rooms render in the corridor color."))
	bool bShowRoomHighlights = true;

	/** Draws fine lines at grid-cell boundaries. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Thin lines at cell boundaries."))
	bool bShowGridLines = false;

	/** Draws each walker's trajectory with a distinct preview colour. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Lines showing each walker's trajectory, color per walker."))
	bool bShowWalkerPaths = true;

	/** Draws thick outlines of the generated region polygons. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Thick outlines around region polygons from Diagram."))
	bool bShowRegionBoundaries = true;

	/** Draws links between neighbouring region centres. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Lines between neighboring region centers."))
	bool bShowAdjacencyGraph = false;

	/** Highlights the preview grid's central cell. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Highlighted cell at grid center."))
	bool bShowCenterMarker = true;

	/** Shows generation metrics next to the grid bounds. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Show generation metrics overlay near grid bounds."))
	bool bShowMetrics = true;

	/** Smooths displayed region boundaries with Chaikin subdivision. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Apply Chaikin subdivision to region boundary outlines."))
	bool bSmoothBoundaries = true;

	/** Number of subdivisions when boundary smoothing is enabled; higher values cost more preview geometry. */
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
