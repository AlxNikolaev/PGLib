#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "DrawDebugHelpers.h"
#include "GeometryUtils/GeometryFunctionLibrary.h"

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
	if (Vertices.Num() == 0)
		return FVector2D::ZeroVector;

	FVector2D Centroid = FVector2D::ZeroVector;
	float	  SignedArea = 0.0f;

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& V1 = Vertices[i];
		const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];
		const float		 CrossProduct = V1.X * V2.Y - V2.X * V1.Y;
		SignedArea += CrossProduct;
		Centroid += (V1 + V2) * CrossProduct;
	}

	if (FMath::Abs(SignedArea) > KINDA_SMALL_NUMBER)
	{
		Centroid /= (3.0f * SignedArea);
	}
	else
	{
		for (const FVector2D& V : Vertices)
		{
			Centroid += V;
		}
		Centroid /= Vertices.Num();
	}

	return Centroid;
}

bool FVoronoiCell2D::ContainsPoint(const FVector2D& Point) const
{
	if (Vertices.Num() < 3)
		return false;

	int32 CrossingCount = 0;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& V1 = Vertices[i];
		const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];

		if ((V1.Y <= Point.Y && V2.Y > Point.Y) || (V1.Y > Point.Y && V2.Y <= Point.Y))
		{
			float T = (Point.Y - V1.Y) / (V2.Y - V1.Y);
			if (Point.X < V1.X + T * (V2.X - V1.X))
			{
				CrossingCount++;
			}
		}
	}

	return (CrossingCount % 2) == 1;
}

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
	const float		  Tolerance = 0.01f;

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
		OutStart = SharedVertices[0];
		OutEnd = SharedVertices[1];
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

FVoronoiDiagram2D UVoronoiGenerator2D::GenerateRandomSites(const int32 NumSites, const bool bUsePoissonDisc) const
{
	TArray<FVector2D> Sites;

	if (bUsePoissonDisc)
	{
		Sites = GeneratePoissonDiscSites(NumSites);
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
	TArray<FVector2D> Sites = GeneratePoissonDiscSites(NumSites);

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

	for (int32 i = 0; i < OutDiagram.Cells.Num(); ++i)
	{
		for (int32 j = i + 1; j < OutDiagram.Cells.Num(); ++j)
		{
			FVector2D EdgeStart, EdgeEnd;
			if (OutDiagram.GetSharedEdge(i, j, EdgeStart, EdgeEnd))
			{
				OutDiagram.Cells[i].Neighbors.AddUnique(j);
				OutDiagram.Cells[j].Neighbors.AddUnique(i);
			}
		}
	}
}

FVoronoiCell2D UVoronoiGenerator2D::ComputeCellForSite(FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites) const
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
			return OutCell;
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
	return OutCell;
}

TArray<FVector2D> UVoronoiGenerator2D::GeneratePoissonDiscSites(const int32 TargetCount) const
{
	TArray<FVector2D> Sites;
	TArray<FVector2D> ActiveList;

	FVector2D FirstSite;
	FirstSite.X = RandomStream.FRandRange(Bounds.Min.X, Bounds.Max.X);
	FirstSite.Y = RandomStream.FRandRange(Bounds.Min.Y, Bounds.Max.Y);
	Sites.Add(FirstSite);
	ActiveList.Add(FirstSite);

	while (ActiveList.Num() > 0 && Sites.Num() < TargetCount)
	{
		const int32		RandomIndex = RandomStream.RandRange(0, ActiveList.Num() - 1);
		const FVector2D CurrentSite = ActiveList[RandomIndex];
		bool			bFoundNewSite = false;

		for (int32 Attempt = 0; Attempt < VORO_SITE_GEN_MAX_ATTEMPTS; ++Attempt)
		{
			const float Angle = RandomStream.FRand() * 2.0f * PI;
			const float Distance = RandomStream.FRandRange(MinSiteDistance, MinSiteDistance * 2.0f);

			FVector2D NewSite;
			NewSite.X = CurrentSite.X + Distance * FMath::Cos(Angle);
			NewSite.Y = CurrentSite.Y + Distance * FMath::Sin(Angle);

			if (Bounds.IsInside(NewSite))
			{
				bool bFarEnough = true;
				for (const FVector2D& Existing : Sites)
				{
					if (FVector2D::Distance(Existing, NewSite) < MinSiteDistance)
					{
						bFarEnough = false;
						break;
					}
				}
				if (!bFarEnough)
				{
					continue;
				}
				Sites.Add(NewSite);
				ActiveList.Add(NewSite);
				bFoundNewSite = true;
				break;
			}
		}

		if (!bFoundNewSite)
		{
			ActiveList.RemoveAt(RandomIndex);
		}
	}

	return Sites;
}

void UVoronoiGenerator2D::RelaxSites(TArray<FVector2D>& Sites)
{
	FVoronoiDiagram2D TempDiagram;
	TempDiagram.Bounds = Bounds;
	TempDiagram.Sites = Sites;

	ComputeVoronoiCells(Sites, TempDiagram);

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