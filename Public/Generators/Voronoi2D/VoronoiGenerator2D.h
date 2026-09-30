#pragma once

#include "CoreMinimal.h"
// The debug-draw guard below must test UE_ENABLE_DEBUG_DRAWING, owned here, and not ENABLE_DRAW_DEBUG: only
// DrawDebugHelpers.h defines the latter, so any translation unit lacking it would compile the declaration away.
#include "EngineDefines.h"
#include "VoronoiGenerator2D.generated.h"

class FVoronoiSiteIndex;

USTRUCT()
struct PROCEDURALGEOMETRY_API FVoronoiCell2D
{
	GENERATED_BODY()

	/** Convex counter-clockwise polygon vertices in diagram XY space. */
	UPROPERTY()
	TArray<FVector2D> Vertices;

	/** Indices of cells sharing a positive-length edge with this polygon. */
	UPROPERTY()
	TArray<int32> Neighbors;

	/** Input site that owns this polygon, in diagram XY space. */
	UPROPERTY()
	FVector2D SiteLocation;

	/** Index into the owning diagram's Cells and Sites arrays. */
	UPROPERTY()
	int32 CellIndex;

	/** Whether clipping produced a usable polygon. */
	UPROPERTY()
	bool bIsValid;

	/** Whether the polygon touches the diagram's outer bounds. */
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

	/** Clipped cells, parallel to Sites. */
	UPROPERTY()
	TArray<FVoronoiCell2D> Cells;

	/** World-space XY bounds used to clip every cell. */
	UPROPERTY()
	FBox2D Bounds = FBox2D(ForceInit);

	/** Ordered input sites in diagram XY space. */
	UPROPERTY()
	TArray<FVector2D> Sites;

	/** Seed used for random or relaxed site generation; explicit sites need no random draws. */
	UPROPERTY()
	FString Seed;

#if UE_ENABLE_DEBUG_DRAWING
	void DrawDebug(const UWorld* World, float Duration = 5.0f, float ZHeight = 0.0f) const;
#endif
	int32 FindCellContainingPoint(const FVector2D& Point) const;
	bool  GetSharedEdge(int32 CellA, int32 CellB, FVector2D& OutStart, FVector2D& OutEnd) const;
	int32 FindClosestCellBySite(const FVector2D& Point) const;

	/**
	 * Indexed form of FindCellContainingPoint: probes the cell of the site nearest Point, then falls back to
	 * the full scan. Index must have been built over this diagram's Sites. The answer can only differ from the
	 * unindexed scan when two valid cells overlap, which a well-formed diagram cannot produce.
	 */
	int32 FindCellContainingPoint(const FVector2D& Point, const FVoronoiSiteIndex& Index) const;

	/** Indexed form of the nearest-site scan; resolves ties to the lowest index exactly as the scan does. */
	int32 FindClosestCellBySite(const FVector2D& Point, const FVoronoiSiteIndex& Index) const;
};

/** Shared-edge utilities used by both FVoronoiDiagram2D and the runtime FVoronoiGridDiagram. */
namespace VoronoiUtils
{
	/**
	 * The vertex-coincidence tolerance for a diagram spanning Bounds, and the single owner of that value. Cells
	 * are clipped independently per site, so every consumer asking whether two cells share an edge must weld
	 * with the value the pass that filled Cell.Neighbors used, or it refuses edges the diagram claims exist.
	 */
	PROCEDURALGEOMETRY_API float ComputeAdjacencyTolerance(const FBox2D& Bounds);

	/**
	 * Whether two independently clipped vertices denote the same point. The adjacency pass and GetSharedEdge
	 * must use identical arithmetic, or a pair on the threshold becomes neighbours and is then refused an edge.
	 */
	PROCEDURALGEOMETRY_API bool VerticesCoincide(const FVector2D& A, const FVector2D& B, float Tolerance);

	/**
	 * Finds the shared edge between two cells: true when they share two coincident vertices within Tolerance
	 * that are farther apart than a corner-only contact. Tolerance is normally ComputeAdjacencyTolerance.
	 */
	PROCEDURALGEOMETRY_API bool GetSharedEdge(
		const TArray<FVector2D>& VertsA, const TArray<FVector2D>& VertsB, float Tolerance, FVector2D& OutStart, FVector2D& OutEnd);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Clips performed by builds initiated on the calling thread since reset, including all relaxation passes.
	 * Independent per-cell worker counters are joined and aggregated on that caller, with no shared worker increment.
	 */
	PROCEDURALGEOMETRY_API void	 ResetHalfPlaneClipCount();
	PROCEDURALGEOMETRY_API int64 GetHalfPlaneClipCount();
	/** Exercises the production adjacency predicate on explicit cells; optionally reproduces the old3x3 omission.
	 * Test-only, resets neighbors and mutates only the supplied value; no generation/seed/UObject state. */
	PROCEDURALGEOMETRY_API void BuildNeighborsForTest(FVoronoiDiagram2D& Diagram, bool bIncludeTwoBucketRing);
#endif
} // namespace VoronoiUtils

UCLASS()
class PROCEDURALGEOMETRY_API UVoronoiGenerator2D final : public UObject
{
	GENERATED_BODY()

	/** Current clip bounds; callers set them before generating sites or cells. */
	UPROPERTY()
	FBox2D Bounds;

	/** Author-supplied deterministic seed, or a generated seed when an RNG entry point receives none. */
	UPROPERTY()
	FString Seed;

	FRandomStream RandomStream;

	float MinSiteDistance;
	int32 RelaxationIterations;

	// Opt-in nearby preclips; legacy callers retain their original clipping order.
	bool bSpatialClipOrder = false;
#if WITH_DEV_AUTOMATION_TESTS
	bool bSerialCellsForTest = false;
#endif

public:
	UVoronoiGenerator2D();

	/** Sets the clipping rectangle for subsequent generation; caller supplies valid finite bounds.
	 * Returns this generator for fluent
	 * configuration and does not generate cells. */
	UVoronoiGenerator2D* SetBounds(const FBox2D& InBounds);
	/** Sets and initializes the deterministic RNG seed; an empty seed is replaced at the next RNG entry.
	 * Returns this generator and does not
	 * consume random site draws. */
	UVoronoiGenerator2D* SetSeed(const FString& InSeed);
	/** Sets Poisson minimum site spacing in centimetres, clamped to at least one.
	 * Returns this generator; explicit-site generation does not use
	 * this spacing. */
	UVoronoiGenerator2D* SetMinSiteDistance(float Distance);
	/** Sets the number of centroid-relaxation passes, clamped to zero or greater.
	 * Returns this generator; only GenerateRelaxed applies the
	 * passes. */
	UVoronoiGenerator2D* SetRelaxationIterations(int32 Iterations);

	/** Opt into nearby preclips when a valid index exists: pruning enabled and at least MinSitesForSpatialPruning (currently 64) sites.
	 * Improves dense ordered grids; index availability changes floating-point clip order and can change vertices. */
	UVoronoiGenerator2D* SetSpatialClipOrder(bool bEnabled);
#if WITH_DEV_AUTOMATION_TESTS
	/** Holds the same per-cell algorithm to a single thread for exact serial/parallel fixtures.
	 * Test-only per-instance option; set before generation, never concurrently with its build. */
	void SetSerialCellsForTest(bool bSerial) { bSerialCellsForTest = bSerial; }
#endif

	/** Clips caller-supplied sites against Bounds and builds shared-edge adjacency.
	 * Does not draw RNG values; returns a diagram even when no
	 * sites produce valid cells. */
	FVoronoiDiagram2D GenerateFromSites(const TArray<FVector2D>& SiteLocations) const;
	/** Draws NumSites from Bounds, optionally with Poisson spacing, then builds a diagram.
	 * Re-derives the RNG from Seed per call;
	 * invalid/empty sampling can return an empty diagram. */
	FVoronoiDiagram2D GenerateRandomSites(int32 NumSites, bool bUsePoissonDisc = false);
	/** Draws Poisson sites and applies configured centroid-relaxation passes before clipping.
	 * Re-derives the RNG from Seed per call; returns an
	 * empty diagram if sampling finds no sites. */
	FVoronoiDiagram2D GenerateRelaxed(int32 NumSites);

private:
	void InitializeRandomStream();

	/**
	 * Re-derives RandomStream from Seed at every RNG entry point, substituting a seed first when there is none.
	 * Re-deriving unconditionally is what makes two generate calls on one instance agree, and it covers the
	 * instance whose Seed arrived by property copy or deserialization (Seed is reflected, RandomStream is not).
	 */
	void EnsureSeeded();

	void ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram, bool bComputeNeighbors = true) const;

	/**
	 * Clips the cell against all relevant bisectors, with optional local preclips before ascending completion.
	 * With default ascending clip order, a non-null Index skips only proven no-ops, preserving exact vertices.
	 * Spatial preclips need a valid index (pruning enabled and at least MinSitesForSpatialPruning, currently 64, sites);
	 * changing index availability can change the floating-point vertex list when spatial clip order is enabled.
	 */
	void ComputeCellForSite(FVoronoiCell2D& OutCell,
		int32								SiteIndex,
		const TArray<FVector2D>&			AllSites,
		const FVoronoiSiteIndex*			Index
#if WITH_DEV_AUTOMATION_TESTS
		,
		int64& ClipCount
#endif
	) const;

	void RelaxSites(TArray<FVector2D>& Sites);
};
