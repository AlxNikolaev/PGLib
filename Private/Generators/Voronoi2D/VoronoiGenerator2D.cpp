#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#if ENABLE_DRAW_DEBUG
	#include "DrawDebugHelpers.h"
#endif
#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "Generators/Voronoi2D/VoronoiSiteIndex.h"
#include "HAL/IConsoleManager.h"
#include "ProceduralGeometry.h"
#include "SeedHashing.h"

namespace VoronoiGenerator2DInternal
{
	/**
	 * Below this site count the index costs more to build than the clips it saves, and every diagram this
	 * small is off the hot path anyway.
	 */
	static constexpr int32 MinSitesForSpatialPruning = 64;

	/**
	 * Relative slack on the "this bisector cannot reach the polygon" test. The bound is exact in real
	 * arithmetic, so only the rounding of the midpoint and the normalized normal can flip a site that sits
	 * exactly on the threshold; a margin seven orders of magnitude above double rounding keeps such a site on
	 * the clipped side of the decision, where it behaves exactly as the exhaustive path treats it.
	 */
	static constexpr double SecurityRadiusMargin = 1.0 + 1e-9;

	/**
	 * Clips between two measurements of the working polygon's radius while that radius is still too large to
	 * prune anything. Clipping only ever keeps points of the polygon it was handed, so the radius never grows and
	 * a measurement taken a few clips ago is a valid (merely loose) bound; measuring after every clip costs more
	 * than the clips it saves in that regime, and the only consequence of the delay is that pruning starts at
	 * most this many clips late.
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
		TEXT("site. Both paths produce the same diagram; 0 exists so the automation suite can prove that. Takes ")
			TEXT("effect on the next generation, and only where levels are generated, so it is not a client switch."),
	ECVF_Cheat | ECVF_RenderThreadSafe);

float FVoronoiCell2D::GetArea() const
{
	if (Vertices.Num() < 3)
		return 0.0f;

	// Accumulate the shoelace sum in double to avoid float cancellation at fine substrate scale,
	// mirroring the precision used by the clipping pipeline; return float only at the end.
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

#if ENABLE_DRAW_DEBUG
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
	// Cells are clipped to Bounds, so nothing outside can be contained. The margin keeps a point sitting on the
	// boundary in the slow path rather than answering INDEX_NONE for a cell edge the scan would have matched.
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
	InitializeRandomStream();
}

void UVoronoiGenerator2D::InitializeRandomStream()
{
	if (Seed.IsEmpty())
	{
		Seed = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
	RandomStream = FRandomStream(static_cast<int32>(PGSeed::HashSeedString(Seed)));
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

FVoronoiDiagram2D UVoronoiGenerator2D::GenerateFromSites(const TArray<FVector2D>& SiteLocations) const
{
	FVoronoiDiagram2D Diagram;
	Diagram.Bounds = Bounds;
	Diagram.Sites = SiteLocations;
	Diagram.Seed = Seed;

	ComputeVoronoiCells(SiteLocations, Diagram);

	return Diagram;
}

FVoronoiDiagram2D UVoronoiGenerator2D::GenerateRandomSites(const int32 NumSites, const bool bUsePoissonDisc)
{
	TArray<FVector2D> Sites;

	if (bUsePoissonDisc)
	{
		// Build a polygon from the bounding box and use the shared O(N) Poisson-disc sampler.
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
	OutDiagram.Cells.Empty();
	OutDiagram.Cells.Reserve(Sites.Num());

	const TArray<FVector2D> BoundingPoly = { FVector2D(Bounds.Min.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Max.Y),
		FVector2D(Bounds.Min.X, Bounds.Max.Y) };

	// Read once into a local so no single diagram is built half one way and half the other. The cvar is
	// render-thread-safe, so the value a worker sees is the value that was set rather than a stale shadow, and a
	// toggle therefore reaches every diagram of the next generation together.
	const bool bUsePruning =
		CVarVoronoiSpatialPruning.GetValueOnAnyThread() != 0 && Sites.Num() >= VoronoiGenerator2DInternal::MinSitesForSpatialPruning;

	FVoronoiSiteIndex SiteIndex;
	if (bUsePruning)
	{
		SiteIndex.Build(Sites, Bounds);
	}
	const FVoronoiSiteIndex* IndexPtr = (bUsePruning && SiteIndex.IsValid()) ? &SiteIndex : nullptr;

	for (int32 i = 0; i < Sites.Num(); ++i)
	{
		FVoronoiCell2D Cell(BoundingPoly);
		ComputeCellForSite(Cell, i, Sites, IndexPtr);
		OutDiagram.Cells.Add(Cell);
	}

	if (!bComputeNeighbors)
	{
		return;
	}

	{
		const float Tolerance = VoronoiUtils::ComputeAdjacencyTolerance(Bounds);
		// Bucket granularity = Tolerance/2 so true clip coincidences (< Tolerance apart) hash
		// within ±2 buckets of each other; the 3x3 neighborhood probe below catches them all.
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

		// Tally candidate pairs via exact-bucket sharing first, then probe the 3x3 neighborhood
		// to catch coincident vertices that straddle a bucket boundary.
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

		// Verify each candidate pair by finding two DISTINCT A-vertices that each coincide with some
		// B-vertex. Counting coincident PAIRS instead would let a single corner contact qualify as soon
		// as one polygon carries that corner twice, so two cells meeting at a zero-width point would be
		// recorded as neighbours. Both the coincidence and the far-apart test go through
		// VoronoiUtils::VerticesCoincide at the same tolerance VoronoiUtils::GetSharedEdge will use, so
		// adjacency cannot claim an edge that edge retrieval then refuses to return.
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
}

// The cell of a site is the bounds box clipped by the perpendicular bisector against every other site, which
// is quadratic in the site count. Most of those clips do nothing: once the working polygon fits inside a disc
// of radius R around the site, a site farther than 2R away has its bisector at least R from the site, so every
// vertex tests on the inside and FGeometryUtils::ClipPolygonByHalfPlane copies the polygon through element for
// element. Skipping such a site therefore removes an operation that provably had no effect — the clips that do
// happen, their order, and the floating-point history of every vertex are unchanged, which is what lets the
// indexed and exhaustive paths be compared vertex-for-vertex rather than within a tolerance.
//
// R is remeasured as the cell shrinks, so the test tightens, and FVoronoiSiteIndex answers "lowest site index at
// or above j within 2R" without touching the sites outside that disc. The saving depends on the site ORDER, not
// just the count: for sites whose index order is unrelated to their position (Poisson sampling, random
// placement) the polygon collapses within the first handful of clips and the rest of the scan disappears. Sites
// emitted in row-major grid order keep a wide working polygon until the scan reaches their own row, so their
// early clips genuinely do cut and are all performed, and only the remainder of the scan is skipped.
//
// This stays quadratic in the worst case and is meant to: the equivalence is bit-for-bit because a skipped site
// could not have touched the CURRENT polygon, and while that polygon is still near the bounds box almost nothing
// qualifies. Deciding from the FINAL cell radius instead would prune far more, but a clip that only grazes an
// intermediate polygon still rewrites the endpoints later clips intersect against, so dropping it moves
// surviving vertices by a rounding step and the level with them.
void UVoronoiGenerator2D::ComputeCellForSite(
	FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites, const FVoronoiSiteIndex* Index) const
{
	OutCell.SiteLocation = AllSites[SiteIndex];
	OutCell.CellIndex = SiteIndex;

	// The scratch only ever holds the working polygon — a handful of vertices — never one entry per site.
	TArray<FVector2D> Scratch;
	Scratch.Reserve(16);

	const FVector2D Site = AllSites[SiteIndex];
	double			PolygonRadiusSq = VoronoiGenerator2DInternal::MaxRadiusSq(OutCell.Vertices, Site);

	// Above this radius the index answers "the next index", which the loop counter already holds, so asking it
	// would cost a square root and two bucket lookups for nothing. Testing the radius against the limit first is
	// what keeps the unprunable stretch of the scan as cheap as the exhaustive path it has to match.
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

		const FVector2D MidPoint = (Site + AllSites[j]) * 0.5f;
		const FVector2D Normal = (AllSites[j] - Site).GetSafeNormal();

		// Coincident sites yield a zero-length normal; the perpendicular bisector is undefined, so the
		// half-plane clip would be a no-op that leaves two overlapping degenerate cells. Skip it.
		if (Normal.IsNearlyZero())
		{
			continue;
		}

#if WITH_DEV_AUTOMATION_TESTS
		++VoronoiGenerator2DInternal::GVoronoi2DHalfPlaneClips;
#endif

		if (!FGeometryUtils::ClipPolygonByHalfPlane(OutCell.Vertices, Scratch, MidPoint, Normal))
		{
			OutCell.bIsValid = false;
			return;
		}

		if (Index)
		{
			// Once the query is narrowing, the polygon is small and a tighter radius immediately buys more
			// skipping, so it is worth measuring every clip. Before that the measurement only has to be frequent
			// enough to notice the radius crossing the limit.
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
		// UE_LOG is the shipping-build fallback: ensureMsgf is stripped in Shipping builds,
		// so this log line ensures the error is always captured regardless of build config.
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