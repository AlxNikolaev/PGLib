#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Generators/CellularAutomata2D/CellularAutomataConfig.h"
#include "CellularAutomata2DVisualizer.generated.h"

class UProceduralMeshComponent;

UCLASS()
class PROCEDURALGEOMETRY_API ACellularAutomata2DVisualizer : public AActor
{
	GENERATED_BODY()

	/** Generated preview grid mesh, owned by this actor and refreshed on construction. */
	UPROPERTY(VisibleAnywhere, Category = "Visualization")
	UProceduralMeshComponent* GridMeshComponent;

	/** Material for the preview mesh; VertexColor drives the wall/floor region colours. */
	UPROPERTY(EditAnywhere,
		Category = "Visualization",
		meta = (ToolTip = "Material for grid mesh. Use an unlit material with VertexColor node connected to BaseColor for colored regions."))
	UMaterialInterface* DebugMaterial;

	/** XY preview bounds in centimetres; the cave grid is generated inside this region. */
	UPROPERTY(EditInstanceOnly, Category = "Cave Preview", meta = (Units = "cm"))
	FBox2D Bounds;

	/** Non-empty by default: an empty seed makes the generator invent a fresh one on every construction, so the
	 *  preview would redraw a different cave on every property tweak. */
	UPROPERTY(EditInstanceOnly, Category = "Cave Preview")
	FString Seed = TEXT("Preview");

	/** Preview cell pitch in centimetres; smaller cells cost more generation and mesh work. */
	UPROPERTY(EditInstanceOnly, Category = "Cave Preview", meta = (ClampMin = 10, Units = "cm"))
	int32 GridSize = 100;

	/** Cave preset and advanced parameters resolved into native CA values on construction. */
	UPROPERTY(
		EditInstanceOnly, Category = "Cave Preview", meta = (ToolTip = "Cave generation parameters. Resolved into raw CA values on construction."))
	FCellularAutomataConfig CaveConfig;

	/** Fills wall and floor grid cells as coloured preview rectangles. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Filled rectangles for wall/floor cells."))
	bool bShowGridCells = true;

	/** Tints each surviving floor region independently. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Per-region distinct hue tinting on floor cells."))
	bool bShowRegionColors = true;

	/** Draws fine lines along grid-cell boundaries. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Thin lines at cell boundaries."))
	bool bShowGridLines = false;

	/** Draws thick outlines around the surviving region polygons. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Thick outlines around region polygons."))
	bool bShowRegionBoundaries = true;

	/** Draws connections between neighbouring region centres. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Lines between neighboring region centers."))
	bool bShowAdjacencyGraph = false;

	/** Highlights the preview grid's central cell or region. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Highlighted cell/region at grid center."))
	bool bShowCenterMarker = true;

	/** Shows regions removed by minimum-size filtering with a translucent tint. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Semi-transparent overlay for culled regions."))
	bool bShowCulledRegions = false;

	/** Shows generation metrics next to the grid bounds. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Show generation metrics overlay near grid bounds."))
	bool bShowMetrics = true;

	/** Smooths displayed boundary outlines with Chaikin subdivision. */
	UPROPERTY(EditInstanceOnly, Category = "Visualization Layers", meta = (ToolTip = "Apply Chaikin subdivision to region boundary outlines."))
	bool bSmoothBoundaries = true;

	/** Number of boundary subdivisions when smoothing is enabled; higher values cost more preview geometry. */
	UPROPERTY(EditInstanceOnly,
		Category = "Visualization Layers",
		meta = (ClampMin = 1,
			ClampMax = 5,
			EditCondition = "bSmoothBoundaries",
			ToolTip = "Number of Chaikin subdivision iterations for boundary smoothing."))
	int32 SmoothingIterations = 2;

public:
	ACellularAutomata2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
