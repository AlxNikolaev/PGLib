#pragma once

#include "CoreMinimal.h"
#include "VoronoiGenerator2D.generated.h"

class FVoronoiSiteIndex;

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

	/**
	 * Indexed form of FindCellContainingPoint: probes the cell of the site nearest Point and confirms
	 * containment with the same polygon test, falling back to the full scan when that cell does not hold the
	 * point. Index must have been built over this diagram's Sites.
	 *
	 * The answer can only differ from the unindexed scan when two valid cells genuinely overlap and the
	 * overlapping pair is not led by the nearest site, which a well-formed diagram cannot produce: cells are
	 * disjoint half-plane intersections around distinct sites.
	 */
	int32 FindCellContainingPoint(const FVector2D& Point, const FVoronoiSiteIndex& Index) const;

	/** Indexed form of the nearest-site scan; resolves ties to the lowest index exactly as the scan does. */
	int32 FindClosestCellBySite(const FVector2D& Point, const FVoronoiSiteIndex& Index) const;
};

/**
 * Shared-edge utilities used by both FVoronoiDiagram2D and the runtime FVoronoiGridDiagram.
 */
namespace VoronoiUtils
{
	/**
	 * The vertex-coincidence tolerance for a diagram spanning Bounds, and the single owner of that value.
	 * Cell polygons are clipped independently per site, so vertices that are geometrically the same point
	 * differ by accumulated rounding. Every consumer that asks "do these two cells share an edge" must weld
	 * with the value the pass that filled Cell.Neighbors used, or it silently refuses edges the diagram
	 * itself claims exist (a missing wall segment, a dropped MST candidate), so both sides call this.
	 */
	PROCEDURALGEOMETRY_API float ComputeAdjacencyTolerance(const FBox2D& Bounds);

	/**
	 * Whether two independently clipped vertices denote the same point at the given tolerance.
	 * The adjacency pass and GetSharedEdge must answer that question with identical arithmetic, or a pair
	 * sitting exactly on the threshold can be recorded as neighbours and then refused an edge.
	 */
	PROCEDURALGEOMETRY_API bool VerticesCoincide(const FVector2D& A, const FVector2D& B, float Tolerance);

	/**
	 * Finds the shared edge between two Voronoi cells given their vertex lists.
	 * Returns true (and fills OutStart/OutEnd) when the cells share at least two coincident
	 * vertices within Tolerance that are farther apart than a corner-only contact.
	 * Tolerance: caller-supplied, normally ComputeAdjacencyTolerance over the diagram's bounds.
	 */
	PROCEDURALGEOMETRY_API bool GetSharedEdge(
		const TArray<FVector2D>& VertsA, const TArray<FVector2D>& VertsB, float Tolerance, FVector2D& OutStart, FVector2D& OutEnd);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Half-plane clips the cell build has performed on the calling thread since the last reset. What spatial
	 * pruning claims is a claim about this count, so the suite asserts on it; a duration on a shared build
	 * machine measures the machine as much as the algorithm and is only ever recorded, never asserted.
	 *
	 * The tally is per-thread because the substrate build runs the cell build from ParallelFor workers: a shared
	 * counter would serialise them and would make the number depend on how the work happened to be split.
	 */
	PROCEDURALGEOMETRY_API void	 ResetHalfPlaneClipCount();
	PROCEDURALGEOMETRY_API int64 GetHalfPlaneClipCount();
#endif
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

	/**
	 * Re-derives RandomStream from Seed at every RNG entry point, substituting a seed first when there is none.
	 * Seeding is the caller's contract: the substituted value is recorded on Seed (and therefore on
	 * FVoronoiDiagram2D::Seed) so the diagram can still be reproduced, but a generator that reaches the substitution
	 * has produced a layout no other machine will agree on. Re-deriving unconditionally is what makes two generate
	 * calls on one instance agree, and it covers the instance whose Seed arrived by property copy or deserialization
	 * (Seed is reflected, RandomStream is not) rather than through SetSeed.
	 */
	void EnsureSeeded();

	void ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram, bool bComputeNeighbors = true) const;

	/**
	 * Clips the cell of AllSites[SiteIndex] against the bisectors of the other sites, in ascending site order.
	 * A non-null Index skips the sites whose bisector provably cannot touch the working polygon; the sequence
	 * of clips that do happen, and therefore the resulting vertex list, is the same either way.
	 */
	void ComputeCellForSite(FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites, const FVoronoiSiteIndex* Index) const;

	void RelaxSites(TArray<FVector2D>& Sites);
};
