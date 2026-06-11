#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#if ENABLE_DRAW_DEBUG
	#include "DrawDebugHelpers.h"
#endif
#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "ProceduralGeometry.h"

float FVoronoiCell2D::GetArea() const
{
	if (Vertices.Num() < 3)
		return 0.0f;

	float Area = 0.0f;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& V1 = Vertices[i];
		const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];
		Area += V1.X * V2.Y - V2.X * V1.Y;
	}
	return FMath::Abs(Area) * 0.5f;
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

bool VoronoiUtils::GetSharedEdge(
	const TArray<FVector2D>& VertsA, const TArray<FVector2D>& VertsB, float Tolerance, FVector2D& OutStart, FVector2D& OutEnd)
{
	TArray<FVector2D> Shared;
	for (const FVector2D& VA : VertsA)
	{
		for (const FVector2D& VB : VertsB)
		{
			if (FVector2D::Distance(VA, VB) < Tolerance)
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

	if (BestDistSq <= Tolerance * Tolerance)
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

	const float MaxExtent = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
	const float Tolerance = FMath::Max(MaxExtent * 1e-4f, UE_KINDA_SMALL_NUMBER);

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

UVoronoiGenerator2D::UVoronoiGenerator2D()
{
	MinSiteDistance = 10.0f;
	RelaxationIterations = 0;
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
	InitializeRandomStream();
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Super::SetBounds(InBounds);
	return this;
}

UVoronoiGenerator2D* UVoronoiGenerator2D::SetSeed(const FString& InSeed)
{
	Super::SetSeed(InSeed);
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

// =============================================================================================================
// TODO(perf): Replace this O(N^2) Voronoi with Boost.Polygon's Fortune sweepline (O(N log N)).
//
// Current cost: ComputeCellForSite clips the bounds box by the perpendicular bisector against EVERY other site,
// so the whole diagram is O(N^2), and GenerateRelaxed multiplies that by the Lloyd iteration count. At the
// CellDungeon fine-substrate scale (sites clamped to ~4096, ~8 relax iters) that is ~100M+ half-plane clips per
// generation; Boost's sweepline is ~hundreds of K. This method is the single hot spot — FVoronoiGridGenerator and
// every consumer go through GenerateFromSites, so swapping it here speeds up everything with NO consumer changes.
//
// Feasibility: Boost 1.85 ships with UE 5.6 (Engine/Source/ThirdParty/Boost). Header-only —
//   - add `AddEngineThirdPartyPrivateStaticDependencies(Target, "Boost")` to ProceduralGeometry.Build.cs,
//   - `#include <boost/polygon/voronoi.hpp>` in THIS .cpp only (heavy header; keep it out of the public .h),
//   - it's a *system* include path so Boost's warnings won't trip -WarningsAsErrors.
//
// Modification points (keep FVoronoiDiagram2D / FVoronoiCell2D output identical = drop-in):
//   1. Quantize float sites to integers (Boost needs exact-integer input); map cells back via cell.source_index().
//   2. Build boost::polygon::voronoi_diagram<double> over the integer sites.
//   3. CLIP to Bounds — the real work: Boost leaves boundary cells with infinite edges (edge.is_infinite(),
//      vertex0/vertex1 null). Intersect those rays with the FBox2D and Sutherland-Hodgman-clip the cell polygon
//      (see Boost's "voronoi clipping" example). Mark bIsBoundaryCell when any incident edge is infinite.
//   4. Cell polygon: walk cell.incident_edge()->next(); emit vertices CCW (consumers assume convex CCW).
//   5. Neighbors: edge.twin()->cell()->source_index() — drops the separate O(N^2) edge-sharing pass below.
//   Unchanged: Lloyd relaxation (now cheap), fan-triangulation (mesh factory), GetSharedEdge, seed/determinism.
//
// Plan: implement behind a cvar with the current path kept as an oracle; validate parity with the existing
// automation (ProceduralGeometry Voronoi tests, Determinism / NoOverlap / OneConnectedRegion, CellDungeon suite,
// VoronoiGridGeneratorTests) + a perf microbench, then flip the default. Risks: integer-quantization precision
// (near-coincident sites collapse — pick a scale with headroom) and clipping correctness (foundation mesh +
// adjacency depend on the exact bounded polygons).
// =============================================================================================================
void UVoronoiGenerator2D::ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram, bool bComputeNeighbors) const
{
	OutDiagram.Cells.Empty();
	OutDiagram.Cells.Reserve(Sites.Num());

	const TArray<FVector2D> BoundingPoly = { FVector2D(Bounds.Min.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Max.Y),
		FVector2D(Bounds.Min.X, Bounds.Max.Y) };

	for (int32 i = 0; i < Sites.Num(); ++i)
	{
		FVoronoiCell2D Cell(BoundingPoly);
		ComputeCellForSite(Cell, i, Sites);
		OutDiagram.Cells.Add(Cell);
	}

	if (!bComputeNeighbors)
	{
		return;
	}

	{
		const float MaxExtent = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
		const float Tolerance = FMath::Max(MaxExtent * 1e-4f, UE_KINDA_SMALL_NUMBER);
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

		// Verify each candidate pair by counting vertex pairs that are within Tolerance of each
		// other. Pairs with 2+ coincident vertices share a Voronoi edge; corner-only contacts
		// share 1 and are excluded.
		const float ToleranceSq = Tolerance * Tolerance;
		for (const TPair<int32, int32>& Pair : CandidatePairs)
		{
			const FVoronoiCell2D& A = OutDiagram.Cells[Pair.Key];
			const FVoronoiCell2D& B = OutDiagram.Cells[Pair.Value];
			int32				  SharedCount = 0;
			for (const FVector2D& VA : A.Vertices)
			{
				for (const FVector2D& VB : B.Vertices)
				{
					if (FVector2D::DistSquared(VA, VB) < ToleranceSq)
					{
						++SharedCount;
						if (SharedCount >= 2)
						{
							break;
						}
					}
				}
				if (SharedCount >= 2)
				{
					break;
				}
			}
			if (SharedCount >= 2)
			{
				OutDiagram.Cells[Pair.Key].Neighbors.AddUnique(Pair.Value);
				OutDiagram.Cells[Pair.Value].Neighbors.AddUnique(Pair.Key);
			}
		}
	}
}

void UVoronoiGenerator2D::ComputeCellForSite(FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites) const
{
	OutCell.SiteLocation = AllSites[SiteIndex];
	OutCell.CellIndex = SiteIndex;

	TArray<FVector2D> Scratch;
	Scratch.Reserve(AllSites.Num() + 4);

	for (int32 j = 0; j < AllSites.Num(); ++j)
	{
		if (j == SiteIndex)
			continue;

		const FVector2D MidPoint = (AllSites[SiteIndex] + AllSites[j]) * 0.5f;
		const FVector2D Normal = (AllSites[j] - AllSites[SiteIndex]).GetSafeNormal();

		if (!FGeometryUtils::ClipPolygonByHalfPlane(OutCell.Vertices, Scratch, MidPoint, Normal))
		{
			OutCell.bIsValid = false;
			return;
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