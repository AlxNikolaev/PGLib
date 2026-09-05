#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/**
 * Uniform bucket grid over a Voronoi site set: the one spatial acceleration structure shared by the cell
 * build and by the point-location queries on a finished diagram.
 *
 * It is a plain value type owned by the caller rather than a member of the diagram on purpose. Diagrams are
 * copied between passes and read from ParallelFor workers, so an index cached inside one would be both a
 * data race and bytes retained for the lifetime of the level; a caller builds one per pass and drops it.
 *
 * Every query resolves distance ties to the lowest site index, which is the answer the linear scans it
 * replaces produce, so substituting the index cannot move a single placement.
 */
class PROCEDURALGEOMETRY_API FVoronoiSiteIndex
{
public:
	/**
	 * Buckets InSites for roughly one site per bucket. InBounds is the caller's clip box; the grid is grown
	 * to cover any site outside it, because a site the grid cannot reach would be silently missing from every
	 * answer instead of merely costing an extra bucket.
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
	 * Appends every site held by a bucket that Box overlaps, in ascending index order. Buckets are only
	 * partly covered by Box, so the result is a superset of the sites inside it: callers that need an exact
	 * answer keep their own containment filter, and the index only narrows how many sites that filter sees.
	 */
	void GatherSitesInBox(const FBox2D& Box, TArray<int32>& OutSites) const;

	/**
	 * The site nearest Point, ties to the lowest index. INDEX_NONE only when nothing was built.
	 * Distances are compared at the same float width FVoronoiDiagram2D::FindClosestCellBySite uses, so the two
	 * answer identically even on the near-ties where float and double disagree.
	 */
	int32 FindNearestSite(const FVector2D& Point) const;

	/**
	 * The next site index at or above MinIndex, other than IgnoreIndex, that may lie within RadiusSq of
	 * Center; INDEX_NONE when no site at or above MinIndex can. The guarantee is one-sided on purpose: every
	 * index the query steps over is proven to be at or beyond RadiusSq, which is what lets a caller skip that
	 * range outright, but the index it returns is only a candidate. When the disc covers so much of the grid
	 * that narrowing it would cost more than the caller saves, the query answers with the next index and lets
	 * the caller test it: the caller was going to do that work in the unindexed case anyway.
	 */
	int32 FindNextCandidateSite(const FVector2D& Center, double RadiusSq, int32 MinIndex, int32 IgnoreIndex) const;

	/**
	 * Largest squared radius for which FindNextCandidateSite still narrows instead of answering with the next
	 * index. A caller in a tight loop tests its radius against this first so it only pays for the query while the
	 * query can pay it back; above the limit the answer is the loop counter the caller already holds.
	 * Zero when nothing was built, which keeps such a caller on its unindexed path.
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
