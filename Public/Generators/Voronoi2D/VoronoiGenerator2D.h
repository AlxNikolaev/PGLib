#pragma once

#include "CoreMinimal.h"
#include "VoronoiGenerator2D.generated.h"

USTRUCT()
struct PROCEDURALGEOMETRY_API FVoronoiCell2D
{
	GENERATED_BODY()

	// Convex, counter-clockwise vertices
	UPROPERTY()
	TArray<FVector2D> Vertices;

	UPROPERTY()
	TArray<int32> Neighbors;

	UPROPERTY()
	FVector2D SiteLocation;

	UPROPERTY()
	int32 CellIndex;

	UPROPERTY()
	bool bIsValid;

	UPROPERTY()
	bool bIsBoundaryCell;

	FVoronoiCell2D()
	{
		SiteLocation = FVector2D::ZeroVector;
		CellIndex = -1;
		bIsValid = false;
		bIsBoundaryCell = false;
	}

	explicit FVoronoiCell2D(const TArray<FVector2D>& InVerts)
	{
		SiteLocation = FVector2D::ZeroVector;
		CellIndex = -1;
		bIsValid = false;
		bIsBoundaryCell = false;
		Vertices = InVerts;
	}

	float	  GetArea() const;
	FVector2D GetCentroid() const;
	bool	  ContainsPoint(const FVector2D& Point) const;
};

USTRUCT()
struct PROCEDURALGEOMETRY_API FVoronoiDiagram2D
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVoronoiCell2D> Cells;

	UPROPERTY()
	FBox2D Bounds = FBox2D(ForceInit);

	UPROPERTY()
	TArray<FVector2D> Sites;

	UPROPERTY()
	FString Seed;

#if ENABLE_DRAW_DEBUG
	void DrawDebug(const UWorld* World, float Duration = 5.0f, float ZHeight = 0.0f) const;
#endif
	int32 FindCellContainingPoint(const FVector2D& Point) const;
	bool  GetSharedEdge(int32 CellA, int32 CellB, FVector2D& OutStart, FVector2D& OutEnd) const;
	int32 FindClosestCellBySite(const FVector2D& Point) const;
};

/**
 * Shared-edge utilities used by both FVoronoiDiagram2D and the runtime FVoronoiGridDiagram.
 * The tolerance must be pre-computed by the caller from its own bounds or grid cell size.
 */
namespace VoronoiUtils
{
	/**
	 * Finds the shared edge between two Voronoi cells given their vertex lists.
	 * Returns true (and fills OutStart/OutEnd) when the cells share at least two coincident
	 * vertices within Tolerance that are farther apart than a corner-only contact.
	 * Tolerance: caller-supplied, derived from Max(MaxExtent*1e-4, UE_KINDA_SMALL_NUMBER) for
	 * diagram-relative scales, or from GridCellSize*1e-3 for grid-relative scales.
	 */
	PROCEDURALGEOMETRY_API bool GetSharedEdge(
		const TArray<FVector2D>& VertsA, const TArray<FVector2D>& VertsB, float Tolerance, FVector2D& OutStart, FVector2D& OutEnd);
} // namespace VoronoiUtils

UCLASS()
class PROCEDURALGEOMETRY_API UVoronoiGenerator2D final : public UObject
{
	GENERATED_BODY()

	UPROPERTY()
	FBox2D Bounds;

	UPROPERTY()
	FString Seed;

	FRandomStream RandomStream;

	float MinSiteDistance;
	int32 RelaxationIterations;

public:
	UVoronoiGenerator2D();

	// Config
	UVoronoiGenerator2D* SetBounds(const FBox2D& InBounds);
	UVoronoiGenerator2D* SetSeed(const FString& InSeed);
	UVoronoiGenerator2D* SetMinSiteDistance(float Distance);
	UVoronoiGenerator2D* SetRelaxationIterations(int32 Iterations);

	// Generation
	FVoronoiDiagram2D GenerateFromSites(const TArray<FVector2D>& SiteLocations) const;
	FVoronoiDiagram2D GenerateRandomSites(int32 NumSites, bool bUsePoissonDisc = false);
	FVoronoiDiagram2D GenerateRelaxed(int32 NumSites);

private:
	void InitializeRandomStream();

	void ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram, bool bComputeNeighbors = true) const;
	void ComputeCellForSite(FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites) const;

	void RelaxSites(TArray<FVector2D>& Sites);
};
