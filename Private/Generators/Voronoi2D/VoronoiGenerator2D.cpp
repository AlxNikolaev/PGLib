#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if UE_ENABLE_DEBUG_DRAWING
	#include "DrawDebugHelpers.h"
#endif
#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "Generators/Voronoi2D/VoronoiSiteIndex.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"
#include "ProceduralGeometry.h"
#include "SeedHashing.h"

namespace VoronoiGenerator2DInternal
{
	/** Below this site count the index costs more to build than the clips it saves. */
	static constexpr int32 MinSitesForSpatialPruning = 64;

	/**
	 * Relative slack on the bisector-cannot-reach test: the bound is exact in real arithmetic, so only rounding of
	 * the midpoint and normal can flip a site on the threshold, and this margin keeps it on the clipped side.
	 */
	static constexpr double SecurityRadiusMargin = 1.0 + 1e-9;

	/**
	 * Clips between radius measurements while the radius is still too large to prune. Clipping never grows the
	 * radius, so a stale measurement is a valid loose bound and pruning starts at most this many clips late.
	 */
	static constexpr int32 RadiusRefreshStride = 8;

	static double MaxRadiusSq(const TArray<FVector2D>& Vertices, const FVector2D& Center)
	{
		double MaxSq = 0.0;
		for (const FVector2D& Vertex : Vertices)
		{
			MaxSq = FMath::Max(MaxSq, FVector2D::DistSquared(Center, Vertex));
		}
		return MaxSq;
	}

#if WITH_DEV_AUTOMATION_TESTS
	static thread_local int64 GVoronoi2DHalfPlaneClips = 0;
#endif
	/** Builds adjacency serially with the existing exact coincidence predicate and legacy pair order.
	 * Only an outer bucket ring adds previously omitted pairs; no representative welding or cell reorder. */
	static void BuildNeighbors(FVoronoiDiagram2D& OutDiagram,
		const FBox2D&							  Bounds
#if WITH_DEV_AUTOMATION_TESTS
		,
		bool bIncludeTwoBucketRing = true
#endif
	)

	{
		const float Tolerance = VoronoiUtils::ComputeAdjacencyTolerance(Bounds);
		// Rounded Tolerance/2 buckets can differ by two even when vertex distance is strictly below tolerance.
		const double QuantStep = static_cast<double>(Tolerance) * 0.5;

		using FVertKey = TPair<int64, int64>;
		TMap<FVertKey, TArray<int32>> VertexToCells;
		VertexToCells.Reserve(OutDiagram.Cells.Num() * 8);

		for (int32 i = 0; i < OutDiagram.Cells.Num(); ++i)
		{
			if (!OutDiagram.Cells[i].bIsValid)
				continue;
			for (const FVector2D& V : OutDiagram.Cells[i].Vertices)
			{
				const int64 BX = static_cast<int64>(FMath::RoundToDouble(static_cast<double>(V.X) / QuantStep));
				const int64 BY = static_cast<int64>(FMath::RoundToDouble(static_cast<double>(V.Y) / QuantStep));
				VertexToCells.FindOrAdd(FVertKey(BX, BY)).AddUnique(i);
			}
		}

		// The 3x3 probe catches coincident vertices that straddle a bucket boundary.
		TSet<TPair<int32, int32>> CandidatePairs;
		for (auto& [VertKey, CellList] : VertexToCells)
		{
			for (int64 dy = -1; dy <= 1; ++dy)
			{
				for (int64 dx = -1; dx <= 1; ++dx)
				{
					const FVertKey		 NeighborKey(VertKey.Key + dx, VertKey.Value + dy);
					const TArray<int32>* NeighborCells = VertexToCells.Find(NeighborKey);
					if (!NeighborCells)
					{
						continue;
					}
					for (const int32 CellA : CellList)
					{
						for (const int32 CellB : *NeighborCells)
						{
							if (CellA < CellB)
							{
								CandidatePairs.Add(TPair<int32, int32>(CellA, CellB));
							}
						}
					}
				}
			}
		}

		// Preserve the original 3x3 insertion order above; append only the previously missed outer ring.
#if WITH_DEV_AUTOMATION_TESTS
		if (bIncludeTwoBucketRing)
#endif
			for (const auto& [VertKey, CellList] : VertexToCells)
			{
				for (int64 dy = -2; dy <= 2; ++dy)
					for (int64 dx = -2; dx <= 2; ++dx)
					{
						if (FMath::Abs(dx) <= 1 && FMath::Abs(dy) <= 1)
							continue;
						const auto* OtherCells = VertexToCells.Find(FVertKey(VertKey.Key + dx, VertKey.Value + dy));
						if (!OtherCells)
							continue;
						for (int32 CellA : CellList)
							for (int32 CellB : *OtherCells)
								if (CellA < CellB)
									CandidatePairs.Add(TPair<int32, int32>(CellA, CellB));
					}
			}

		// A pair needs two distinct A-vertices that each coincide with a B-vertex; counting coincident pairs would
		// let a single corner contact qualify. The tolerance matches GetSharedEdge, so adjacency cannot claim an
		// edge that edge retrieval then refuses to return.
		TArray<FVector2D> SharedVerts;
		for (const TPair<int32, int32>& Pair : CandidatePairs)
		{
			const FVoronoiCell2D& A = OutDiagram.Cells[Pair.Key];
			const FVoronoiCell2D& B = OutDiagram.Cells[Pair.Value];

			SharedVerts.Reset();
			bool bSharesEdge = false;
			for (const FVector2D& VA : A.Vertices)
			{
				bool bCoincidesWithB = false;
				for (const FVector2D& VB : B.Vertices)
				{
					if (VoronoiUtils::VerticesCoincide(VA, VB, Tolerance))
					{
						bCoincidesWithB = true;
						break;
					}
				}
				if (!bCoincidesWithB)
				{
					continue;
				}

				for (const FVector2D& Existing : SharedVerts)
				{
					if (!VoronoiUtils::VerticesCoincide(Existing, VA, Tolerance))
					{
						bSharesEdge = true;
						break;
					}
				}
				if (bSharesEdge)
				{
					break;
				}
				SharedVerts.Add(VA);
			}

			if (bSharesEdge)
			{
				OutDiagram.Cells[Pair.Key].Neighbors.AddUnique(Pair.Value);
				OutDiagram.Cells[Pair.Value].Neighbors.AddUnique(Pair.Key);
			}
		}
	}

} // namespace VoronoiGenerator2DInternal

#if WITH_DEV_AUTOMATION_TESTS
void VoronoiUtils::ResetHalfPlaneClipCount()
{
	VoronoiGenerator2DInternal::GVoronoi2DHalfPlaneClips = 0;
}

int64 VoronoiUtils::GetHalfPlaneClipCount()
{
	return VoronoiGenerator2DInternal::GVoronoi2DHalfPlaneClips;
}
#endif

static TAutoConsoleVariable<int32> CVarVoronoiSpatialPruning(TEXT("r.ProcGen.Voronoi.SpatialPruning"),
	1,
	TEXT("Voronoi cell build: 1 = skip the bisectors that provably cannot cut the cell, 0 = clip against every ")
		TEXT("site. Pruned and exhaustive paths are identical in the default ascending clip order. Spatial clip order ")
			TEXT("needs a valid site index (pruning enabled and at least MinSitesForSpatialPruning, currently 64, sites); ")
				TEXT("changing index availability can change its floating-point vertices. Takes effect on the next generation ")
					TEXT("only where levels are generated, so it is not a client switch."),
	ECVF_Cheat | ECVF_RenderThreadSafe);

float FVoronoiCell2D::GetArea() const
{
	if (Vertices.Num() < 3)
		return 0.0f;

	// The shoelace sum accumulates in double to avoid float cancellation at fine substrate scale.
	double Area = 0.0;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& V1 = Vertices[i];
		const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];
		Area += double(V1.X) * V2.Y - double(V2.X) * V1.Y;
	}
	return static_cast<float>(FMath::Abs(Area) * 0.5);
}

FVector2D FVoronoiCell2D::GetCentroid() const
{
	return FGeometryUtils::GetPolygonCentroid(Vertices);
}

bool FVoronoiCell2D::ContainsPoint(const FVector2D& Point) const
{
	return FGeometryUtils::PointInPolygon(Vertices, Point);
}

#if UE_ENABLE_DEBUG_DRAWING
void FVoronoiDiagram2D::DrawDebug(const UWorld* World, const float Duration, const float ZHeight) const
{
	if (!World)
		return;

	for (const FVoronoiCell2D& Cell : Cells)
	{
		if (!Cell.bIsValid)
			continue;

		FColor		Color = Cell.bIsBoundaryCell ? FColor::Red : FColor::Green;
		const float Height = Cell.bIsBoundaryCell ? ZHeight - 10.f : ZHeight + 10.f;

		for (int32 i = 0; i < Cell.Vertices.Num(); ++i)
		{
			const FVector2D& V1 = Cell.Vertices[i];
			const FVector2D& V2 = Cell.Vertices[(i + 1) % Cell.Vertices.Num()];

			FVector Start(V1.X, V1.Y, Height);
			FVector End(V2.X, V2.Y, Height);

			DrawDebugLine(World, Start, End, Color, true, Duration, 0, 3.0f);
		}

		FVector SitePos(Cell.SiteLocation.X, Cell.SiteLocation.Y, Height);
		DrawDebugSphere(World, SitePos, 10.0f, 8, Color);
	}
}
#endif

int32 FVoronoiDiagram2D::FindCellContainingPoint(const FVector2D& Point) const
{
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (Cells[i].bIsValid && Cells[i].ContainsPoint(Point))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

float VoronoiUtils::ComputeAdjacencyTolerance(const FBox2D& Bounds)
{
	const float MaxExtent = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
	return FMath::Max(MaxExtent * 1e-4f, UE_KINDA_SMALL_NUMBER);
}

bool VoronoiUtils::VerticesCoincide(const FVector2D& A, const FVector2D& B, const float Tolerance)
{
	return FVector2D::Distance(A, B) < Tolerance;
}

bool VoronoiUtils::GetSharedEdge(
	const TArray<FVector2D>& VertsA, const TArray<FVector2D>& VertsB, float Tolerance, FVector2D& OutStart, FVector2D& OutEnd)
{
	TArray<FVector2D> Shared;
	for (const FVector2D& VA : VertsA)
	{
		for (const FVector2D& VB : VertsB)
		{
			if (VerticesCoincide(VA, VB, Tolerance))
			{
				Shared.AddUnique(VA);
				break;
			}
		}
	}

	if (Shared.Num() < 2)
	{
		return false;
	}

	int32 BestI = 0, BestJ = 1;
	float BestDistSq = -1.f;
	for (int32 i = 0; i < Shared.Num(); ++i)
	{
		for (int32 j = i + 1; j < Shared.Num(); ++j)
		{
			const float D = FVector2D::DistSquared(Shared[i], Shared[j]);
			if (D > BestDistSq)
			{
				BestDistSq = D;
				BestI = i;
				BestJ = j;
			}
		}
	}

	// The farthest-apart pair still coinciding means the cells meet at a single point, not along a border.
	if (VerticesCoincide(Shared[BestI], Shared[BestJ], Tolerance))
	{
		return false;
	}

	OutStart = Shared[BestI];
	OutEnd = Shared[BestJ];
	return true;
}

bool FVoronoiDiagram2D::GetSharedEdge(const int32 CellA, const int32 CellB, FVector2D& OutStart, FVector2D& OutEnd) const
{
	if (!Cells.IsValidIndex(CellA) || !Cells.IsValidIndex(CellB))
	{
		return false;
	}

	const float Tolerance = VoronoiUtils::ComputeAdjacencyTolerance(Bounds);

	return VoronoiUtils::GetSharedEdge(Cells[CellA].Vertices, Cells[CellB].Vertices, Tolerance, OutStart, OutEnd);
}

int32 FVoronoiDiagram2D::FindClosestCellBySite(const FVector2D& Point) const
{
	if (Sites.Num() == 0)
	{
		return INDEX_NONE;
	}

	int32 ClosestIndex = 0;
	float BestDistSq = FVector2D::DistSquared(Point, Sites[0]);

	for (int32 i = 1; i < Sites.Num(); ++i)
	{
		const float D = FVector2D::DistSquared(Point, Sites[i]);
		if (D < BestDistSq)
		{
			BestDistSq = D;
			ClosestIndex = i;
		}
	}

	return ClosestIndex;
}

int32 FVoronoiDiagram2D::FindCellContainingPoint(const FVector2D& Point, const FVoronoiSiteIndex& Index) const
{
	// Cells are clipped to Bounds, so nothing outside can be contained; the margin keeps a boundary point in the
	// slow path instead of answering INDEX_NONE.
	if (Bounds.bIsValid)
	{
		const double Margin = FMath::Max(1.0, static_cast<double>(Bounds.GetExtent().GetMax()) * 1e-4);
		const FBox2D Probe = Bounds.ExpandBy(Margin);
		if (!Probe.IsInside(Point))
		{
			return INDEX_NONE;
		}
	}

	const int32 NearestSite = Index.FindNearestSite(Point);
	if (Cells.IsValidIndex(NearestSite) && Cells[NearestSite].bIsValid && Cells[NearestSite].ContainsPoint(Point))
	{
		return NearestSite;
	}

	return FindCellContainingPoint(Point);
}

int32 FVoronoiDiagram2D::FindClosestCellBySite(const FVector2D& Point, const FVoronoiSiteIndex& Index) const
{
	const int32 Nearest = Index.FindNearestSite(Point);
	return (Nearest != INDEX_NONE) ? Nearest : FindClosestCellBySite(Point);
}

UVoronoiGenerator2D::UVoronoiGenerator2D()
{
	MinSiteDistance = 10.0f;
	RelaxationIterations = 0;
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
}

void UVoronoiGenerator2D::InitializeRandomStream()
{
	RandomStream = FRandomStream(static_cast<int32>(PGSeed::HashSeedString(Seed)));
}

void UVoronoiGenerator2D::EnsureSeeded()
{
	if (Seed.IsEmpty())
	{
		Seed = FGuid::NewGuid().ToString(EGuidFormats::Digits);

		UE_LOG(LogRoguelikeGeometry,
			Error,
			TEXT("[Voronoi] Generated without SetSeed; substituting '%s'. Every machine invents its own, so this diagram is reproducible only "
				 "by setting that string as the seed."),
			*Seed);
	}

	// Unconditional because Seed is reflected and RandomStream is not: an instance that received its Seed by property
	// copy or deserialization would otherwise draw from FRandomStream(0) while reporting that Seed.
	InitializeRandomStream();
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Bounds = InBounds;
	return this;
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetSeed(const FString& InSeed)
{
	Seed = InSeed;
	InitializeRandomStream();
	return this;
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetMinSiteDistance(const float Distance)
{
	MinSiteDistance = FMath::Max(1.0f, Distance);
	return this;
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetRelaxationIterations(const int32 Iterations)
{
	RelaxationIterations = FMath::Max(0, Iterations);
	return this;
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetSpatialClipOrder(const bool bEnabled)
{
	bSpatialClipOrder = bEnabled;
	return this;
}

FVoronoiDiagram2D UVoronoiGenerator2D::GenerateFromSites(const TArray<FVector2D>& SiteLocations) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcGen_VoronoiDiagram);
	FVoronoiDiagram2D Diagram;
	Diagram.Bounds = Bounds;
	Diagram.Sites = SiteLocations;
	Diagram.Seed = Seed;

	ComputeVoronoiCells(SiteLocations, Diagram);

	return Diagram;
}

FVoronoiDiagram2D UVoronoiGenerator2D::GenerateRandomSites(const int32 NumSites, const bool bUsePoissonDisc)
{
	EnsureSeeded();

	TArray<FVector2D> Sites;

	if (bUsePoissonDisc)
	{
		const TArray<FVector2D> BoundsPolygon = {
			Bounds.Min, FVector2D(Bounds.Max.X, Bounds.Min.Y), Bounds.Max, FVector2D(Bounds.Min.X, Bounds.Max.Y)
		};
		FGeometryUtils::PoissonDiskSampling(BoundsPolygon, MinSiteDistance, NumSites, RandomStream, Sites);
	}
	else
	{
		Sites.Reserve(NumSites);
		for (int32 i = 0; i < NumSites; ++i)
		{
			FVector2D Site;
			Site.X = RandomStream.FRandRange(Bounds.Min.X, Bounds.Max.X);
			Site.Y = RandomStream.FRandRange(Bounds.Min.Y, Bounds.Max.Y);
			Sites.Add(Site);
		}
	}

	return GenerateFromSites(Sites);
}

FVoronoiDiagram2D UVoronoiGenerator2D::GenerateRelaxed(const int32 NumSites)
{
	EnsureSeeded();

	const TArray<FVector2D> BoundsPolygon = { Bounds.Min, FVector2D(Bounds.Max.X, Bounds.Min.Y), Bounds.Max, FVector2D(Bounds.Min.X, Bounds.Max.Y) };
	TArray<FVector2D>		Sites;
	FGeometryUtils::PoissonDiskSampling(BoundsPolygon, MinSiteDistance, NumSites, RandomStream, Sites);

	for (int32 Iter = 0; Iter < RelaxationIterations; ++Iter)
	{
		RelaxSites(Sites);
	}

	return GenerateFromSites(Sites);
}

void UVoronoiGenerator2D::ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram, bool bComputeNeighbors) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ProcGen_VoronoiClipAndNeighbors);
	OutDiagram.Cells.SetNum(Sites.Num());

	const TArray<FVector2D> BoundingPoly = { FVector2D(Bounds.Min.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Max.Y),
		FVector2D(Bounds.Min.X, Bounds.Max.Y) };

	// Read once into a local so no single diagram is built half one way and half the other.
	const bool bUsePruning =
		CVarVoronoiSpatialPruning.GetValueOnAnyThread() != 0 && Sites.Num() >= VoronoiGenerator2DInternal::MinSitesForSpatialPruning;

	FVoronoiSiteIndex SiteIndex;
	if (bUsePruning)
	{
		SiteIndex.Build(Sites, Bounds);
	}
	const FVoronoiSiteIndex* IndexPtr = (bUsePruning && SiteIndex.IsValid()) ? &SiteIndex : nullptr;

#if WITH_DEV_AUTOMATION_TESTS
	TArray<int64> CellClips;
	CellClips.Init(0, Sites.Num());
#endif
	const auto BuildCell = [&](int32 Index) {
		auto& Cell = OutDiagram.Cells[Index];
		Cell = FVoronoiCell2D(BoundingPoly);
		ComputeCellForSite(Cell,
			Index,
			Sites,
			IndexPtr
#if WITH_DEV_AUTOMATION_TESTS
			,
			CellClips[Index]
#endif
		);
	};
	EParallelForFlags Flags = EParallelForFlags::None;
#if WITH_DEV_AUTOMATION_TESTS
	if (bSerialCellsForTest)
		Flags = EParallelForFlags::ForceSingleThread;
#endif
	ParallelFor(Sites.Num(), BuildCell, Flags);
#if WITH_DEV_AUTOMATION_TESTS
	// Each worker writes only its own counter. The join precedes caller-thread aggregation (including relaxation passes).
	for (int64 Count : CellClips)
		VoronoiGenerator2DInternal::GVoronoi2DHalfPlaneClips += Count;
#endif

	if (!bComputeNeighbors)
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ProcGen_VoronoiNeighbors);
	VoronoiGenerator2DInternal::BuildNeighbors(OutDiagram, Bounds);
}

// The cell is the bounds box clipped by the bisector against every other site. A site farther than 2R from a working
// polygon of radius R cannot cut it, so skipping it drops a provable no-op and leaves the clips performed, their order
// and every vertex bit-for-bit identical to the exhaustive path in the default ascending clip order. Spatial preclips
// require a valid index (pruning enabled and at least MinSitesForSpatialPruning, currently 64, sites); changing index
// availability can change their floating-point vertices. Deciding from the final cell radius would prune far more,
// but a clip that grazes an intermediate polygon still moves the vertices later clips intersect against.
void UVoronoiGenerator2D::ComputeCellForSite(FVoronoiCell2D& OutCell,
	int32													 SiteIndex,
	const TArray<FVector2D>&								 AllSites,
	const FVoronoiSiteIndex*								 Index
#if WITH_DEV_AUTOMATION_TESTS
	,
	int64& ClipCount
#endif
) const
{
	OutCell.SiteLocation = AllSites[SiteIndex];
	OutCell.CellIndex = SiteIndex;

	// The scratch only ever holds the working polygon — a handful of vertices — never one entry per site.
	TArray<FVector2D> Scratch;
	Scratch.Reserve(16);

	const FVector2D						Site = AllSites[SiteIndex];
	TArray<int32, TInlineAllocator<32>> PreclippedSites;
	// Ascending row-major sites can leave the working cell almost as large as Bounds until the next
	// row is visited, defeating safe pruning. Nearby preclips surround dense-grid sites immediately.
	// These shells are only acceleration: the normal loop below still considers every remaining site
	// or proves its bisector cannot touch the working polygon. There is no nearest-K correctness cutoff.
	if (bSpatialClipOrder && Index)
	{
		int32 BX = 0, BY = 0;
		Index->GetBucketCoords(Site, BX, BY);
		TArray<int32> Nearby;
		Nearby.Reserve(32);
		const int32 LastRing = FMath::Min(2, Index->GetMaxRingFrom(BX, BY));
		for (int32 Ring = 0; Ring <= LastRing; ++Ring)
		{
			Nearby.Reset();
			Index->GatherRing(BX, BY, Ring, Nearby);
			for (const int32 Other : Nearby)
			{
				if (Other == SiteIndex)
					continue;
				const FVector2D Normal = (AllSites[Other] - Site).GetSafeNormal();
				if (Normal.IsNearlyZero())
					continue;
#if WITH_DEV_AUTOMATION_TESTS
				++ClipCount;
#endif
				if (!FGeometryUtils::ClipPolygonByHalfPlane(OutCell.Vertices, Scratch, (Site + AllSites[Other]) * .5, Normal))
				{
					OutCell.bIsValid = false;
					return;
				}
				PreclippedSites.Add(Other);
			}
		}
	}
	double PolygonRadiusSq = VoronoiGenerator2DInternal::MaxRadiusSq(OutCell.Vertices, Site);

	// Above this radius the index only answers with the next index, which the loop counter already holds.
	const double NarrowingRadiusSqLimit = Index ? Index->GetNarrowingRadiusSqLimit() : 0.0;
	int32		 ClipsSinceRadiusRefresh = 0;

	for (int32 j = 0; j < AllSites.Num(); ++j)
	{
		bool bNarrowed = false;
		if (Index)
		{
			const double SecurityRadiusSq = 4.0 * PolygonRadiusSq * VoronoiGenerator2DInternal::SecurityRadiusMargin;
			if (SecurityRadiusSq <= NarrowingRadiusSqLimit)
			{
				j = Index->FindNextCandidateSite(Site, SecurityRadiusSq, j, SiteIndex);
				if (j == INDEX_NONE)
				{
					break;
				}
				bNarrowed = true;
			}
		}

		if (!bNarrowed && j == SiteIndex)
		{
			continue;
		}
		// Reapplying a bisector can split its existing edge at a roundoff-sized crossing. Adjacent
		// cells would then disagree on whole-edge endpoints, creating false placement boundaries.
		if (PreclippedSites.Contains(j))
		{
			continue;
		}

		const FVector2D MidPoint = (Site + AllSites[j]) * 0.5f;
		const FVector2D Normal = (AllSites[j] - Site).GetSafeNormal();

		// Coincident sites yield a zero-length normal, leaving the perpendicular bisector undefined.
		if (Normal.IsNearlyZero())
		{
			continue;
		}

#if WITH_DEV_AUTOMATION_TESTS
		++ClipCount;
#endif

		if (!FGeometryUtils::ClipPolygonByHalfPlane(OutCell.Vertices, Scratch, MidPoint, Normal))
		{
			OutCell.bIsValid = false;
			return;
		}

		if (Index)
		{
			// While narrowing, a tighter radius immediately buys more skipping, so measure after every clip.
			++ClipsSinceRadiusRefresh;
			if (bNarrowed || ClipsSinceRadiusRefresh >= VoronoiGenerator2DInternal::RadiusRefreshStride)
			{
				PolygonRadiusSq = VoronoiGenerator2DInternal::MaxRadiusSq(OutCell.Vertices, Site);
				ClipsSinceRadiusRefresh = 0;
			}
		}
	}

	for (const FVector2D& Vertex : OutCell.Vertices)
	{
		if (FMath::Abs(Vertex.X - Bounds.Min.X) < UE_KINDA_SMALL_NUMBER || FMath::Abs(Vertex.X - Bounds.Max.X) < UE_KINDA_SMALL_NUMBER
			|| FMath::Abs(Vertex.Y - Bounds.Min.Y) < UE_KINDA_SMALL_NUMBER || FMath::Abs(Vertex.Y - Bounds.Max.Y) < UE_KINDA_SMALL_NUMBER)
		{
			OutCell.bIsBoundaryCell = true;
			break;
		}
	}

	OutCell.bIsValid = OutCell.Vertices.Num() >= 3;
}

void UVoronoiGenerator2D::RelaxSites(TArray<FVector2D>& Sites)
{
	FVoronoiDiagram2D TempDiagram;
	TempDiagram.Bounds = Bounds;
	TempDiagram.Sites = Sites;

	ComputeVoronoiCells(Sites, TempDiagram, false);

	if (!ensureMsgf(TempDiagram.Cells.Num() == Sites.Num(),
			TEXT("[Voronoi] RelaxSites: cell count mismatch — expected %d, got %d; skipping relaxation step"),
			Sites.Num(),
			TempDiagram.Cells.Num()))
	{
		// ensureMsgf is stripped in Shipping, so the same message goes to the log.
		UE_LOG(LogRoguelikeGeometry,
			Error,
			TEXT("[Voronoi] RelaxSites: cell count mismatch — expected %d, got %d; skipping relaxation step"),
			Sites.Num(),
			TempDiagram.Cells.Num());
		return;
	}

	for (int32 i = 0; i < TempDiagram.Cells.Num(); ++i)
	{
		if (TempDiagram.Cells[i].bIsValid)
		{
			Sites[i] = TempDiagram.Cells[i].GetCentroid();

			Sites[i].X = FMath::Clamp(Sites[i].X, Bounds.Min.X, Bounds.Max.X);
			Sites[i].Y = FMath::Clamp(Sites[i].Y, Bounds.Min.Y, Bounds.Max.Y);
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void VoronoiUtils::BuildNeighborsForTest(FVoronoiDiagram2D& Diagram, bool bIncludeTwoBucketRing)
{
	for (auto& Cell : Diagram.Cells)
		Cell.Neighbors.Reset();
	VoronoiGenerator2DInternal::BuildNeighbors(Diagram, Diagram.Bounds, bIncludeTwoBucketRing);
}
#endif
