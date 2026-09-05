#include "Generators/Voronoi2D/VoronoiSiteIndex.h"

#include "Algo/BinarySearch.h"
#include "Algo/Sort.h"
#include "GridBudget.h"

namespace VoronoiSiteIndexInternal
{
	/** Buckets FindNextCandidateSite will sweep before a sweep costs more than the clip it saves the caller. */
	static constexpr int64 MaxSweptBuckets = 64;

	/** Buckets per axis for an extent, never fewer than one so a degenerate axis still addresses a row. */
	static int32 AxisBucketCount(const double Extent, const double InBucketSize)
	{
		if (!(Extent > 0.0) || !(InBucketSize > 0.0))
		{
			return 1;
		}
		return FMath::Max(1, static_cast<int32>(FMath::CeilToDouble(Extent / InBucketSize)));
	}
} // namespace VoronoiSiteIndexInternal

void FVoronoiSiteIndex::Build(const TArrayView<const FVector2D> InSites, const FBox2D& InBounds)
{
	Sites.Reset();
	BucketStart.Reset();
	BucketSites.Reset();
	GridBounds = FBox2D(ForceInit);
	BucketSize = 0.0;
	BucketsX = 0;
	BucketsY = 0;

	if (InSites.Num() == 0)
	{
		return;
	}

	Sites.Append(InSites.GetData(), InSites.Num());

	if (InBounds.bIsValid)
	{
		GridBounds = InBounds;
	}
	for (const FVector2D& Site : Sites)
	{
		GridBounds += Site;
	}

	const FVector2D Size = GridBounds.GetSize();
	const double	Area = static_cast<double>(Size.X) * static_cast<double>(Size.Y);

	// One site per bucket on average: fewer buckets degenerate the ring walks into linear scans, more cost more than
	// the sites they index.
	BucketSize = (Area > 0.0) ? FMath::Sqrt(Area / static_cast<double>(Sites.Num())) : 0.0;
	if (!(BucketSize > 0.0))
	{
		// Collinear sites have no area, so the count spreads along the one axis with an extent; fully coincident
		// sites have neither and land in a single bucket answering every query with a linear walk.
		const double LongestAxis = FMath::Max(static_cast<double>(Size.X), static_cast<double>(Size.Y));
		BucketSize = (LongestAxis > 0.0) ? (LongestAxis / static_cast<double>(Sites.Num())) : 1.0;
		if (!(BucketSize > 0.0))
		{
			BucketSize = 1.0;
		}
	}

	BucketsX = VoronoiSiteIndexInternal::AxisBucketCount(Size.X, BucketSize);
	BucketsY = VoronoiSiteIndexInternal::AxisBucketCount(Size.Y, BucketSize);

	// A sliver extent collapses the area-derived bucket size, so coarsen until the table fits the grid budget.
	while (static_cast<int64>(BucketsX) * static_cast<int64>(BucketsY) > PGGrid::MaxGridCells)
	{
		const double Overshoot =
			static_cast<double>(static_cast<int64>(BucketsX) * static_cast<int64>(BucketsY)) / static_cast<double>(PGGrid::MaxGridCells);
		BucketSize *= FMath::Max(FMath::Sqrt(Overshoot), 1.0001);
		BucketsX = VoronoiSiteIndexInternal::AxisBucketCount(Size.X, BucketSize);
		BucketsY = VoronoiSiteIndexInternal::AxisBucketCount(Size.Y, BucketSize);
	}

	const int32 NumBuckets = BucketsX * BucketsY;
	BucketStart.SetNumZeroed(NumBuckets + 1);

	TArray<int32> BucketOfSite;
	BucketOfSite.SetNumUninitialized(Sites.Num());
	for (int32 SiteIdx = 0; SiteIdx < Sites.Num(); ++SiteIdx)
	{
		int32 BX = 0;
		int32 BY = 0;
		GetBucketCoords(Sites[SiteIdx], BX, BY);
		BucketOfSite[SiteIdx] = BucketIndexAt(BX, BY);
		++BucketStart[BucketOfSite[SiteIdx] + 1];
	}

	for (int32 Bucket = 0; Bucket < NumBuckets; ++Bucket)
	{
		BucketStart[Bucket + 1] += BucketStart[Bucket];
	}

	// Ascending fill order leaves every bucket list ascending, so queries break ties toward the lowest index and
	// FindNextCandidateSite can binary-search and bail early.
	BucketSites.SetNumUninitialized(Sites.Num());
	TArray<int32> Cursor = BucketStart;
	for (int32 SiteIdx = 0; SiteIdx < Sites.Num(); ++SiteIdx)
	{
		BucketSites[Cursor[BucketOfSite[SiteIdx]]++] = SiteIdx;
	}
}

void FVoronoiSiteIndex::GetBucketCoords(const FVector2D& Point, int32& OutX, int32& OutY) const
{
	if (BucketsX <= 0 || BucketsY <= 0)
	{
		OutX = 0;
		OutY = 0;
		return;
	}

	// Clamp in double before narrowing: a query far outside the grid overflows the int32 conversion.
	const double LocalX = (static_cast<double>(Point.X) - GridBounds.Min.X) / BucketSize;
	const double LocalY = (static_cast<double>(Point.Y) - GridBounds.Min.Y) / BucketSize;
	OutX = static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(LocalX), 0.0, static_cast<double>(BucketsX - 1)));
	OutY = static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(LocalY), 0.0, static_cast<double>(BucketsY - 1)));
}

int32 FVoronoiSiteIndex::GetMaxRingFrom(const int32 BX, const int32 BY) const
{
	if (!IsValid())
	{
		return 0;
	}
	return FMath::Max(FMath::Max(BX, BucketsX - 1 - BX), FMath::Max(BY, BucketsY - 1 - BY));
}

double FVoronoiSiteIndex::BucketMinDistSq(const int32 X, const int32 Y, const FVector2D& Point) const
{
	const double MinX = GridBounds.Min.X + static_cast<double>(X) * BucketSize;
	const double MinY = GridBounds.Min.Y + static_cast<double>(Y) * BucketSize;
	const double DX = FMath::Max3(MinX - static_cast<double>(Point.X), 0.0, static_cast<double>(Point.X) - (MinX + BucketSize));
	const double DY = FMath::Max3(MinY - static_cast<double>(Point.Y), 0.0, static_cast<double>(Point.Y) - (MinY + BucketSize));
	return DX * DX + DY * DY;
}

double FVoronoiSiteIndex::MaxGridCornerDistSq(const FVector2D& Point) const
{
	const FVector2D Corners[4] = {
		GridBounds.Min, FVector2D(GridBounds.Max.X, GridBounds.Min.Y), GridBounds.Max, FVector2D(GridBounds.Min.X, GridBounds.Max.Y)
	};

	double MaxSq = 0.0;
	for (const FVector2D& Corner : Corners)
	{
		MaxSq = FMath::Max(MaxSq, FVector2D::DistSquared(Point, Corner));
	}
	return MaxSq;
}

void FVoronoiSiteIndex::ForEachBucketInRing(const int32 BX, const int32 BY, const int32 Ring, TFunctionRef<void(int32)> Visit) const
{
	if (!IsValid() || Ring < 0)
	{
		return;
	}

	if (Ring == 0)
	{
		if (BX >= 0 && BX < BucketsX && BY >= 0 && BY < BucketsY)
		{
			Visit(BucketIndexAt(BX, BY));
		}
		return;
	}

	const int32 MinX = BX - Ring;
	const int32 MaxX = BX + Ring;
	const int32 MinY = BY - Ring;
	const int32 MaxY = BY + Ring;

	for (int32 X = FMath::Max(MinX, 0); X <= FMath::Min(MaxX, BucketsX - 1); ++X)
	{
		if (MinY >= 0)
		{
			Visit(BucketIndexAt(X, MinY));
		}
		if (MaxY <= BucketsY - 1)
		{
			Visit(BucketIndexAt(X, MaxY));
		}
	}

	for (int32 Y = FMath::Max(MinY + 1, 0); Y <= FMath::Min(MaxY - 1, BucketsY - 1); ++Y)
	{
		if (MinX >= 0)
		{
			Visit(BucketIndexAt(MinX, Y));
		}
		if (MaxX <= BucketsX - 1)
		{
			Visit(BucketIndexAt(MaxX, Y));
		}
	}
}

void FVoronoiSiteIndex::SortAppendedRange(TArray<int32>& OutSites, const int32 FirstAppended)
{
	const int32 Count = OutSites.Num() - FirstAppended;
	if (Count > 1)
	{
		Algo::Sort(TArrayView<int32>(OutSites.GetData() + FirstAppended, Count));
	}
}

void FVoronoiSiteIndex::GatherRing(const int32 BX, const int32 BY, const int32 Ring, TArray<int32>& OutSites) const
{
	const int32 FirstAppended = OutSites.Num();

	ForEachBucketInRing(BX, BY, Ring, [this, &OutSites](const int32 Bucket) {
		for (int32 Slot = BucketStart[Bucket]; Slot < BucketStart[Bucket + 1]; ++Slot)
		{
			OutSites.Add(BucketSites[Slot]);
		}
	});

	SortAppendedRange(OutSites, FirstAppended);
}

void FVoronoiSiteIndex::GatherSitesInBox(const FBox2D& Box, TArray<int32>& OutSites) const
{
	if (!IsValid() || !Box.bIsValid)
	{
		return;
	}

	int32 MinBX = 0;
	int32 MinBY = 0;
	int32 MaxBX = 0;
	int32 MaxBY = 0;
	GetBucketCoords(FVector2D(FMath::Min(Box.Min.X, Box.Max.X), FMath::Min(Box.Min.Y, Box.Max.Y)), MinBX, MinBY);
	GetBucketCoords(FVector2D(FMath::Max(Box.Min.X, Box.Max.X), FMath::Max(Box.Min.Y, Box.Max.Y)), MaxBX, MaxBY);

	const int32 FirstAppended = OutSites.Num();
	for (int32 Y = MinBY; Y <= MaxBY; ++Y)
	{
		for (int32 X = MinBX; X <= MaxBX; ++X)
		{
			const int32 Bucket = BucketIndexAt(X, Y);
			for (int32 Slot = BucketStart[Bucket]; Slot < BucketStart[Bucket + 1]; ++Slot)
			{
				OutSites.Add(BucketSites[Slot]);
			}
		}
	}

	SortAppendedRange(OutSites, FirstAppended);
}

int32 FVoronoiSiteIndex::FindNearestSite(const FVector2D& Point) const
{
	if (!IsValid())
	{
		return INDEX_NONE;
	}

	int32 BX = 0;
	int32 BY = 0;
	GetBucketCoords(Point, BX, BY);
	const int32 MaxRing = GetMaxRingFrom(BX, BY);

	int32 Best = INDEX_NONE;

	// Float on purpose: FVoronoiDiagram2D::FindClosestCellBySite compares at float width and keeps the lower index
	// on a tie, and this query has to answer exactly what that scan answers.
	float BestDistSq = 0.0f;

	for (int32 Ring = 0; Ring <= MaxRing; ++Ring)
	{
		if (Best != INDEX_NONE)
		{
			// A ring bucket sits at least (Ring - 1) bucket widths away; the bound is loosened a hair because a
			// site just past it can round to the same float BestDistSq and win the tie on the lower index.
			const double RingMinDist = static_cast<double>(Ring - 1) * BucketSize;
			if (RingMinDist > 0.0 && RingMinDist * RingMinDist > static_cast<double>(BestDistSq) * (1.0 + 1e-6))
			{
				break;
			}
		}

		ForEachBucketInRing(BX, BY, Ring, [this, &Point, &Best, &BestDistSq](const int32 Bucket) {
			for (int32 Slot = BucketStart[Bucket]; Slot < BucketStart[Bucket + 1]; ++Slot)
			{
				const int32 Candidate = BucketSites[Slot];
				const float DistSq = static_cast<float>(FVector2D::DistSquared(Point, Sites[Candidate]));
				if (Best == INDEX_NONE || DistSq < BestDistSq || (DistSq == BestDistSq && Candidate < Best))
				{
					Best = Candidate;
					BestDistSq = DistSq;
				}
			}
		});
	}

	return Best;
}

int32 FVoronoiSiteIndex::FindNextCandidateSite(const FVector2D& Center, const double RadiusSq, const int32 MinIndex, const int32 IgnoreIndex) const
{
	if (!IsValid() || !(RadiusSq > 0.0) || MinIndex >= Sites.Num())
	{
		return INDEX_NONE;
	}

	const int32 FirstAllowed = FMath::Max(MinIndex, 0);
	auto		NextAllowedIndex = [this, FirstAllowed, IgnoreIndex]() -> int32 {
		   const int32 First = (FirstAllowed == IgnoreIndex) ? FirstAllowed + 1 : FirstAllowed;
		   return Sites.IsValidIndex(First) ? First : INDEX_NONE;
	};

	// A disc that swallows the whole grid contains every site, so the answer is the next allowed index.
	if (RadiusSq >= MaxGridCornerDistSq(Center))
	{
		return NextAllowedIndex();
	}

	const double Radius = FMath::Sqrt(RadiusSq);

	int32 MinBX = 0;
	int32 MinBY = 0;
	int32 MaxBX = 0;
	int32 MaxBY = 0;
	GetBucketCoords(FVector2D(Center.X - Radius, Center.Y - Radius), MinBX, MinBY);
	GetBucketCoords(FVector2D(Center.X + Radius, Center.Y + Radius), MaxBX, MaxBY);

	// A disc this wide costs more to sweep than the clip it saves, so hand back the next index unnarrowed;
	// skipping nothing is always sound.
	if (static_cast<int64>(MaxBX - MinBX + 1) * static_cast<int64>(MaxBY - MinBY + 1) > VoronoiSiteIndexInternal::MaxSweptBuckets)
	{
		return NextAllowedIndex();
	}

	int32 Best = INDEX_NONE;
	for (int32 Y = MinBY; Y <= MaxBY; ++Y)
	{
		for (int32 X = MinBX; X <= MaxBX; ++X)
		{
			const int32 Bucket = BucketIndexAt(X, Y);
			const int32 SlotEnd = BucketStart[Bucket + 1];
			if (BucketStart[Bucket] == SlotEnd || BucketMinDistSq(X, Y, Center) >= RadiusSq)
			{
				continue;
			}

			const TArrayView<const int32> BucketView(BucketSites.GetData() + BucketStart[Bucket], SlotEnd - BucketStart[Bucket]);
			const int32					  FirstSlot = BucketStart[Bucket] + Algo::LowerBound(BucketView, FirstAllowed);

			for (int32 Slot = FirstSlot; Slot < SlotEnd; ++Slot)
			{
				const int32 Candidate = BucketSites[Slot];
				if (Best != INDEX_NONE && Candidate >= Best)
				{
					// The bucket is ascending, so nothing further in it can beat the standing answer.
					break;
				}
				if (Candidate == IgnoreIndex)
				{
					continue;
				}
				if (FVector2D::DistSquared(Center, Sites[Candidate]) < RadiusSq)
				{
					Best = Candidate;
					break;
				}
			}
		}
	}

	return Best;
}

double FVoronoiSiteIndex::GetNarrowingRadiusSqLimit() const
{
	if (!IsValid())
	{
		return 0.0;
	}

	// A disc of radius R covers at most ceil(2R / BucketSize) + 1 buckets per axis, so solving the square-box span
	// back for R gives the radius below which FindNextCandidateSite sweeps instead of giving up. The square-box
	// bound is conservative: a caller that skips a sweepable disc loses pruning, never an answer.
	const int32	 MaxSpan = FMath::Max(1, static_cast<int32>(FMath::Sqrt(static_cast<double>(VoronoiSiteIndexInternal::MaxSweptBuckets))));
	const double MaxRadius = static_cast<double>(MaxSpan - 1) * BucketSize * 0.5;
	return MaxRadius * MaxRadius;
}
