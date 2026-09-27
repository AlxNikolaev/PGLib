#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "CellularAutomataGenerator2D.generated.h"

/**
 * Full state of the cellular automata pipeline. Production API: the runtime cave cluster generator carves and rebuilds it
 * when corridor carving is on and builds its diagram at CellSize. Under the compatibility contract: Grid, RegionIds, Regions, SurvivingRegions,
 * GridWidth, GridHeight, CellSize, bDegradedResolution and Diagram. CenterRegionId carries no compatibility guarantee.
 */
struct PROCEDURALGEOMETRY_API FCellularAutomataGridData
{
	TArray<bool>			  Grid;				// true = floor, false = wall
	TArray<int32>			  RegionIds;		// Per-cell region ID (-1 = wall)
	TArray<TArray<FIntPoint>> Regions;			// List of cell coordinates per region
	TArray<bool>			  SurvivingRegions; // true = survived culling, false = culled
	int32					  CenterRegionId;	// Region containing grid center (-1 if none)
	int32					  GridWidth;
	int32					  GridHeight;
	float					  CellSize;
	bool					  bDegradedResolution = false; // true when cell size was enlarged to fit the cell budget
	FLayoutDiagram2D		  Diagram;					   // Final merged diagram
};

UCLASS()
class PROCEDURALGEOMETRY_API UCellularAutomataGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	float		  FillProbability;
	int32		  Iterations;
	TArray<int32> BirthRule;
	TArray<int32> SurvivalRule;
	int32		  MinRegionSize;
	bool		  bKeepCenterRegion;

public:
	UCellularAutomataGenerator2D();

	virtual UCellularAutomataGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UCellularAutomataGenerator2D* SetSeed(const FString& InSeed) override;
	virtual UCellularAutomataGenerator2D* SetGridSize(int32 InSize) override;
	virtual UCellularAutomataGenerator2D* SetCenter(const FVector2D& InCenter) override;

	UCellularAutomataGenerator2D* SetFillProbability(float InProbability);
	UCellularAutomataGenerator2D* SetIterations(int32 InIterations);
	UCellularAutomataGenerator2D* SetBirthRule(const TArray<int32>& InRule);
	UCellularAutomataGenerator2D* SetSurvivalRule(const TArray<int32>& InRule);
	UCellularAutomataGenerator2D* SetMinRegionSize(int32 InSize);
	UCellularAutomataGenerator2D* SetKeepCenterRegion(bool bKeep);

	virtual FLayoutDiagram2D Generate() override;

	/**
	 * Production API: runs the pipeline and returns the grid data with the final diagram, the effective (possibly
	 * coarsened) CellSize and bDegradedResolution. The runtime cave generator needs it for both and for corridor carving.
	 */
	FCellularAutomataGridData GenerateWithGridData();

	/**
	 * Rolls Probability independently for every pair of surviving regions the diagram does not list as neighbours (pairs
	 * touching one wall cell are skipped) and carves a straight corridor between the closest floor cells of each winning pair,
	 * modifying Grid and RegionIds in place. No connectivity guarantee at any probability. Width is in grid cells and stamps
	 * a square brush 2*floor(Width/2)+1 cells on a side; the outer grid ring stays wall. Diagram is left stale: run RebuildDiagram().
	 */
	static void CarveCorridors(FCellularAutomataGridData& GridData, float Probability, int32 Width, FRandomStream& InRandomStream);

	/**
	 * Re-floods RegionIds, Regions and CenterRegionId from Grid and rebuilds Diagram at GridData.CellSize.
	 * SurvivingRegions keeps its pre-rebuild labelling.
	 */
	void RebuildDiagram(FCellularAutomataGridData& GridData);

private:
	/** Core generation pipeline shared by Generate() and GenerateWithGridData(). */
	FCellularAutomataGridData GenerateInternal();

	static uint16 RuleToBitmask(const TArray<int32>& Rule);
	int32		  CountWallNeighbors(const TArray<bool>& Grid, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const;

	// Region merging pipeline. InCellSize is passed rather than read off GridSize because a run that trips the
	// cell budget generates at a coarsened pitch that must not outlive the call.
	FLayoutDiagram2D  BuildDiagramFromRegions(const TArray<bool>& Grid,
		 const TArray<int32>&									  RegionIds,
		 const TArray<TArray<FIntPoint>>&						  Regions,
		 int32													  CenterRegionId,
		 int32													  GridWidth,
		 int32													  GridHeight,
		 float													  InCellSize);
	TArray<FVector2D> TraceBoundaryPolygon(
		const TArray<FIntPoint>& Region, const TArray<int32>& RegionIds, int32 RegionId, int32 GridWidth, int32 GridHeight, float CellSize) const;
	static float	  ComputePolygonArea(const TArray<FIntPoint>& Loop);
	TArray<FVector2D> SimplifyAndConvert(const TArray<FIntPoint>& Loop, float CellSize) const;
};
