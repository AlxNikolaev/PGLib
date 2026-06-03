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

bool FVoronoiDiagram2D::GetSharedEdge(const int32 CellA, const int32 CellB, FVector2D& OutStart, FVector2D& OutEnd) const
{
	if (!Cells.IsValidIndex(CellA) || !Cells.IsValidIndex(CellB))
		return false;

	const FVoronoiCell2D& A = Cells[CellA];
	const FVoronoiCell2D& B = Cells[CellB];

	TArray<FVector2D> SharedVertices;
	// Scale tolerance relative to diagram bounds — proportional to MaxDimension * 5e-5
	const float MaxExtent = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
	const float Tolerance = MaxExtent * 1e-4f;

	for (const FVector2D& VA : A.Vertices)
	{
		for (const FVector2D& VB : B.Vertices)
		{
			if (FVector2D::Distance(VA, VB) < Tolerance)
			{
				SharedVertices.AddUnique(VA);
				break;
			}
		}
	}

	if (SharedVertices.Num() >= 2)
	{
		// Pick the two farthest-apart shared vertices — the true edge endpoints. With 3+ near-coincident
		// shared vertices, [0]/[1] could be an arbitrary, near-degenerate pair.
		int32 BestI = 0;
		int32 BestJ = 1;
		float BestDistSq = -1.0f;
		for (int32 i = 0; i < SharedVertices.Num(); ++i)
		{
			for (int32 j = i + 1; j < SharedVertices.Num(); ++j)
			{
				const float DistSq = FVector2D::DistSquared(SharedVertices[i], SharedVertices[j]);
				if (DistSq > BestDistSq)
				{
					BestDistSq = DistSq;
					BestI = i;
					BestJ = j;
				}
			}
		}

		// Corner-only contact collapses to a (near) point — not a real shared edge.
		if (BestDistSq <= Tolerance * Tolerance)
		{
			return false;
		}

		OutStart = SharedVertices[BestI];
		OutEnd = SharedVertices[BestJ];
		return true;
	}

	return false;
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

void UVoronoiGenerator2D::ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram) const
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

	// Build a vertex→cell multimap from the already-computed clip results so neighbor
	// detection is O(N·V) instead of the previous O(N²·V²) all-pairs scan.
	//
	// Two Voronoi cells share an edge iff they share exactly 2 distinct vertex positions
	// (the two endpoints of that edge).  Corner contacts share only 1 vertex; we exclude
	// those by requiring a shared-bucket count of at least 2.
	{
		const float MaxExtent = FMath::Max(Bounds.GetExtent().X, Bounds.GetExtent().Y);
		const float Tolerance = FMath::Max(MaxExtent * 1e-4f, UE_KINDA_SMALL_NUMBER);
		// Bucket granularity = Tolerance/2.  True floating-point coincidences from the
		// Sutherland-Hodgman clip are << Tolerance apart and always collapse to the same
		// bucket; corner contacts land in different buckets (>> Tolerance apart).
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
				const FVertKey Key(static_cast<int64>(FMath::RoundToDouble(static_cast<double>(V.X) / QuantStep)),
					static_cast<int64>(FMath::RoundToDouble(static_cast<double>(V.Y) / QuantStep)));
				VertexToCells.FindOrAdd(Key).Add(i);
			}
		}

		// Tally shared-bucket count per ordered cell pair (Lo < Hi).
		TMap<TPair<int32, int32>, int32> PairSharedCount;
		for (auto& [VertKey, CellList] : VertexToCells)
		{
			for (int32 a = 0; a < CellList.Num(); ++a)
			{
				for (int32 b = a + 1; b < CellList.Num(); ++b)
				{
					const int32 CellA = CellList[a];
					const int32 CellB = CellList[b];
					if (CellA == CellB)
						continue; // same cell appeared twice in this bucket (degenerate vertex)
					const int32 Lo = FMath::Min(CellA, CellB);
					const int32 Hi = FMath::Max(CellA, CellB);
					PairSharedCount.FindOrAdd(TPair<int32, int32>(Lo, Hi))++;
				}
			}
		}

		// Pairs with 2+ shared vertex buckets share a Voronoi edge → register as neighbors.
		for (auto& [PairKey, Count] : PairSharedCount)
		{
			if (Count >= 2)
			{
				OutDiagram.Cells[PairKey.Key].Neighbors.AddUnique(PairKey.Value);
				OutDiagram.Cells[PairKey.Value].Neighbors.AddUnique(PairKey.Key);
			}
		}
	}
}

void UVoronoiGenerator2D::ComputeCellForSite(FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites) const
{
	OutCell.SiteLocation = AllSites[SiteIndex];
	OutCell.CellIndex = SiteIndex;

	for (int32 j = 0; j < AllSites.Num(); ++j)
	{
		if (j == SiteIndex)
			continue;

		const FVector2D MidPoint = (AllSites[SiteIndex] + AllSites[j]) * 0.5f;
		const FVector2D Normal = (AllSites[j] - AllSites[SiteIndex]).GetSafeNormal();

		if (!FGeometryUtils::ClipPolygonByHalfPlane(OutCell.Vertices, MidPoint, Normal))
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

	ComputeVoronoiCells(Sites, TempDiagram);

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