#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/**
 * Uniform bucket grid over a Voronoi site set, shared by the cell build and by point-location queries on a
 * finished diagram. A plain value type the caller builds per pass and drops: diagrams are copied between
 * passes and read from ParallelFor workers, so an index cached inside one would be a data race. Every query
 * resolves distance ties to the lowest site index, matching the linear scans it replaces.
 */
class PROCEDURALGEOMETRY_API FVoronoiSiteIndex
{
public:
	/**
	 * Buckets InSites for roughly one site per bucket. InBounds is the caller's clip box; the grid grows to
	 * cover any site outside it, since a site the grid cannot reach is missing from every answer.
	 */
	void Build(TArrayView<const FVector2D> InSites, const FBox2D& InBounds);

	bool IsValid() const { return BucketsX > 0 && BucketsY > 0 && Sites.Num() > 0; }

	/** Bucket holding Point, clamped to the grid so a query from outside the built extent still resolves. */
	void GetBucketCoords(const FVector2D& Point, int32& OutX, int32& OutY) const;

	/** Largest ring index that still touches the grid when expanding around bucket (BX, BY). */
	int32 GetMaxRingFrom(int32 BX, int32 BY) const;

	/** Appends the sites of the ring-Ring square shell around bucket (BX, BY), ascending within the ring. */
	void GatherRing(int32 BX, int32 BY, int32 Ring, TArray<int32>& OutSites) const;

	/**
	 * Appends every site held by a bucket that Box overlaps, in ascending index order. The result is a superset
	 * of the sites inside Box, so callers needing an exact answer keep their own containment filter.
	 */
	void GatherSitesInBox(const FBox2D& Box, TArray<int32>& OutSites) const;

	/**
	 * The site nearest Point, ties to the lowest index; INDEX_NONE only when nothing was built. Distances are
	 * compared at the float width FVoronoiDiagram2D::FindClosestCellBySite uses, so the two answer identically.
	 */
	int32 FindNearestSite(const FVector2D& Point) const;

	/**
	 * The next site index at or above MinIndex, other than IgnoreIndex, that may lie within RadiusSq of Center;
	 * INDEX_NONE when none can. The guarantee is one-sided: every index stepped over is proven to be at or
	 * beyond RadiusSq, but the index returned is only a candidate the caller must still test.
	 */
	int32 FindNextCandidateSite(const FVector2D& Center, double RadiusSq, int32 MinIndex, int32 IgnoreIndex) const;

	/**
	 * Largest squared radius for which FindNextCandidateSite still narrows instead of answering with the next
	 * index; a caller tests its radius against this before paying for the query. Zero when nothing was built.
	 */
	double GetNarrowingRadiusSqLimit() const;

private:
	int32 BucketIndexAt(int32 X, int32 Y) const { return Y * BucketsX + X; }

	/** Squared distance from Point to the closest point of bucket (X, Y)'s rectangle; 0 when inside. */
	double BucketMinDistSq(int32 X, int32 Y, const FVector2D& Point) const;

	/** Squared distance from Point to the farthest corner of the grid: the radius that swallows every site. */
	double MaxGridCornerDistSq(const FVector2D& Point) const;

	void ForEachBucketInRing(int32 BX, int32 BY, int32 Ring, TFunctionRef<void(int32)> Visit) const;

	/** Sorts the tail of OutSites appended since FirstAppended, so a gather's order never depends on bucket traversal. */
	static void SortAppendedRange(TArray<int32>& OutSites, int32 FirstAppended);

	TArray<FVector2D> Sites;

	/** Prefix offsets into BucketSites, one per bucket plus a terminator. */
	TArray<int32> BucketStart;

	/** Site indices grouped by bucket, ascending within each bucket. */
	TArray<int32> BucketSites;

	FBox2D GridBounds = FBox2D(ForceInit);
	double BucketSize = 0.0;
	int32  BucketsX = 0;
	int32  BucketsY = 0;
};
