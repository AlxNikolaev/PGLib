#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "CellularAutomataGenerator2D.generated.h"

/**
 * Debug/visualization data from the cellular automata generation pipeline.
 * NOT a stable production API — use Generate() for production callers.
 * This struct exposes raw intermediate state for visualization and testing.
 * Fields and layout may change between tasks without backward compatibility guarantees.
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
	FLayoutDiagram2D		  Diagram; // The final merged diagram (existing output)
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

	// Covariant base class overrides
	virtual UCellularAutomataGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UCellularAutomataGenerator2D* SetSeed(const FString& InSeed) override;
	virtual UCellularAutomataGenerator2D* SetGridSize(int32 InSize) override;
	virtual UCellularAutomataGenerator2D* SetCenter(const FVector2D& InCenter) override;

	// Generator-specific config
	UCellularAutomataGenerator2D* SetFillProbability(float InProbability);
	UCellularAutomataGenerator2D* SetIterations(int32 InIterations);
	UCellularAutomataGenerator2D* SetBirthRule(const TArray<int32>& InRule);
	UCellularAutomataGenerator2D* SetSurvivalRule(const TArray<int32>& InRule);
	UCellularAutomataGenerator2D* SetMinRegionSize(int32 InSize);
	UCellularAutomataGenerator2D* SetKeepCenterRegion(bool bKeep);

	// Generation
	virtual FLayoutDiagram2D Generate() override;

	/** Returns the full intermediate grid data including the final diagram. For visualization and testing only. */
	FCellularAutomataGridData GenerateWithGridData();

	/**
	 * Carves corridors between disconnected surviving regions in the grid.
	 * Modifies Grid and RegionIds in place. Does not recompute Diagram — caller must call RebuildDiagram() afterward.
	 *
	 * @param GridData      The grid data to modify (from GenerateWithGridData()).
	 * @param Probability   Per-pair probability of carving a corridor (0 = never, 1 = always).
	 * @param Width         Width of the carved corridor in grid cells.
	 * @param InRandomStream Random stream for probabilistic decisions.
	 */
	static void CarveCorridors(FCellularAutomataGridData& GridData, float Probability, int32 Width, FRandomStream& InRandomStream);

	/**
	 * Rebuilds GridData.Diagram from the current Grid/RegionIds/Regions state.
	 * Call after CarveCorridors() to produce a valid diagram reflecting the modified grid.
	 *
	 * @param GridData The grid data whose Diagram field will be regenerated.
	 */
	void RebuildDiagram(FCellularAutomataGridData& GridData);

private:
	/** Core generation pipeline shared by Generate() and GenerateWithGridData(). */
	FCellularAutomataGridData GenerateInternal();

	static uint16 RuleToBitmask(const TArray<int32>& Rule);
	int32		  CountWallNeighbors(const TArray<bool>& Grid, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const;

	// Region merging pipeline
	FLayoutDiagram2D  BuildDiagramFromRegions(const TArray<bool>& Grid,
		 const TArray<int32>&									  RegionIds,
		 const TArray<TArray<FIntPoint>>&						  Regions,
		 int32													  CenterRegionId,
		 int32													  GridWidth,
		 int32													  GridHeight);
	TArray<FVector2D> TraceBoundaryPolygon(
		const TArray<FIntPoint>& Region, const TArray<int32>& RegionIds, int32 RegionId, int32 GridWidth, int32 GridHeight, float CellSize) const;
	static float	  ComputePolygonArea(const TArray<FIntPoint>& Loop);
	TArray<FVector2D> SimplifyAndConvert(const TArray<FIntPoint>& Loop, float CellSize) const;
};
