#include "GeometryFunctionLibrary.h"

bool FGeometryUtils::SortPlaneVerticesByAngle(const TArray<FVector2D>& InVertices, TArray<FVector2D>& OutSortedVertices)
{
	if (InVertices.Num() < 3)
	{
		OutSortedVertices = InVertices;
		return false;
	}

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : InVertices)
	{
		Centroid += Vertex;
	}
	Centroid /= InVertices.Num();

	OutSortedVertices = InVertices;

	OutSortedVertices.Sort([&Centroid](const FVector2D& A, const FVector2D& B) {
		const float AngleA = FMath::Atan2(A.Y - Centroid.Y, A.X - Centroid.X);
		const float AngleB = FMath::Atan2(B.Y - Centroid.Y, B.X - Centroid.X);
		return AngleA < AngleB;
	});

	return true;
}

bool FGeometryUtils::ComputeBisector2D(const FVector2D& P1, const FVector2D& P2, const FVector2D& OutStart, const FVector2D& OutEnd)
{
	if (P1.Equals(P2, UE_KINDA_SMALL_NUMBER))
		return false;

	FVector2D Mid = (P1 + P2) / 2;

	FVector2D Direction = (P2 - P1).GetRotated(90.f);
	Direction.Normalize();

	return true;
}

bool FGeometryUtils::ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal)
{
	if (OutPolygon.Num() == 0)
	{
		return false;
	}

	TArray<FVector2D> Result;
	FVector2D		  Prev = OutPolygon.Last();
	double			  PrevSide = FVector2D::DotProduct(Prev - PlanePoint, PlaneNormal);

	for (const FVector2D& Curr : OutPolygon)
	{
		double CurrSide = FVector2D::DotProduct(Curr - PlanePoint, PlaneNormal);

		if (PrevSide <= 0.0 && CurrSide <= 0.0)
		{
			Result.Add(Curr);
		}
		else if (PrevSide <= 0.0 && CurrSide > 0.0)
		{
			float Alpha = PrevSide / (PrevSide - CurrSide);
			Result.Add(FMath::Lerp(Prev, Curr, Alpha));
		}
		else if (PrevSide > 0.0 && CurrSide <= 0.0)
		{
			float Alpha = PrevSide / (PrevSide - CurrSide);
			Result.Add(FMath::Lerp(Prev, Curr, Alpha));
			Result.Add(Curr);
		}

		Prev = Curr;
		PrevSide = CurrSide;
	}

	OutPolygon = Result;
	return OutPolygon.Num() >= 3;
}

// Polygon utilities for POI placement

bool FGeometryUtils::PointInPolygon(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point)
{
	if (PolygonVertices.Num() < 3)
	{
		return false;
	}

	// Use winding number algorithm
	int32		WindingNumber = 0;
	const int32 NumVertices = PolygonVertices.Num();

	for (int32 i = 0; i < NumVertices; ++i)
	{
		const FVector2D& V1 = PolygonVertices[i];
		const FVector2D& V2 = PolygonVertices[(i + 1) % NumVertices];

		if (V1.Y <= Point.Y)
		{
			if (V2.Y > Point.Y) // Upward crossing
			{
				const float CrossProduct = (V2.X - V1.X) * (Point.Y - V1.Y) - (V2.Y - V1.Y) * (Point.X - V1.X);
				if (CrossProduct > 0) // Point is left of edge
				{
					++WindingNumber;
				}
			}
		}
		else
		{
			if (V2.Y <= Point.Y) // Downward crossing
			{
				const float CrossProduct = (V2.X - V1.X) * (Point.Y - V1.Y) - (V2.Y - V1.Y) * (Point.X - V1.X);
				if (CrossProduct < 0) // Point is right of edge
				{
					--WindingNumber;
				}
			}
		}
	}

	return WindingNumber != 0;
}

float FGeometryUtils::DistanceToPolygonBoundary(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point)
{
	if (PolygonVertices.Num() < 3)
	{
		return 0.0f;
	}

	float		MinDistance = FLT_MAX;
	const int32 NumVertices = PolygonVertices.Num();

	for (int32 i = 0; i < NumVertices; ++i)
	{
		const FVector2D& V1 = PolygonVertices[i];
		const FVector2D& V2 = PolygonVertices[(i + 1) % NumVertices];

		const float Distance = DistanceToLineSegment(Point, V1, V2);
		MinDistance = FMath::Min(MinDistance, Distance);
	}

	return MinDistance;
}

bool FGeometryUtils::MaxInscribedCircle(const TArray<FVector2D>& PolygonVertices, FVector2D& OutCenter, float& OutRadius, float Epsilon)
{
	if (PolygonVertices.Num() < 3)
	{
		return false;
	}

	// Get polygon bounding box
	FVector2D MinBounds(FLT_MAX, FLT_MAX);
	FVector2D MaxBounds(-FLT_MAX, -FLT_MAX);

	for (const FVector2D& Vertex : PolygonVertices)
	{
		MinBounds.X = FMath::Min(MinBounds.X, Vertex.X);
		MinBounds.Y = FMath::Min(MinBounds.Y, Vertex.Y);
		MaxBounds.X = FMath::Max(MaxBounds.X, Vertex.X);
		MaxBounds.Y = FMath::Max(MaxBounds.Y, Vertex.Y);
	}

	// Grid-based polylabel algorithm
	struct FCell
	{
		FVector2D Center;
		float	  HalfSize;
		float	  Distance;
		float	  Potential;

		FCell(const FVector2D& InCenter, float InHalfSize, const TArray<FVector2D>& Polygon) : Center(InCenter), HalfSize(InHalfSize)
		{
			Distance = PointInPolygon(Polygon, Center) ? DistanceToPolygonBoundary(Polygon, Center) : -DistanceToPolygonBoundary(Polygon, Center);
			Potential = Distance + HalfSize * FMath::Sqrt(2.0f);
		}
	};

	// Priority queue for cells (max-heap by potential)
	TArray<FCell> CellQueue;

	// Initial grid size
	const float GridSize = FMath::Min(MaxBounds.X - MinBounds.X, MaxBounds.Y - MinBounds.Y) / 4.0f;

	// Start with bounding box center
	FCell BestCell(GetPolygonCentroid(PolygonVertices), 0, PolygonVertices);

	// Create initial grid
	const float CellSize = GridSize;
	for (float x = MinBounds.X; x < MaxBounds.X; x += CellSize)
	{
		for (float y = MinBounds.Y; y < MaxBounds.Y; y += CellSize)
		{
			FCell NewCell(FVector2D(x + CellSize * 0.5f, y + CellSize * 0.5f), CellSize * 0.5f, PolygonVertices);
			if (NewCell.Distance > BestCell.Distance)
			{
				BestCell = NewCell;
			}
			CellQueue.Add(NewCell);
		}
	}

	// Sort by potential (max-heap)
	CellQueue.Sort([](const FCell& A, const FCell& B) { return A.Potential > B.Potential; });

	// Main polylabel loop
	while (CellQueue.Num() > 0 && CellQueue[0].Potential - BestCell.Distance > Epsilon)
	{
		FCell CurrentCell = CellQueue[0];
		CellQueue.RemoveAt(0);

		// Don't subdivide further if we can't possibly get better
		if (CurrentCell.Potential - BestCell.Distance <= Epsilon)
		{
			continue;
		}

		// Subdivide cell into four
		const float NewHalfSize = CurrentCell.HalfSize * 0.5f;
		if (NewHalfSize < Epsilon)
		{
			continue;
		}

		TArray<FVector2D> SubCellCenters = { FVector2D(CurrentCell.Center.X - NewHalfSize, CurrentCell.Center.Y - NewHalfSize),
			FVector2D(CurrentCell.Center.X + NewHalfSize, CurrentCell.Center.Y - NewHalfSize),
			FVector2D(CurrentCell.Center.X - NewHalfSize, CurrentCell.Center.Y + NewHalfSize),
			FVector2D(CurrentCell.Center.X + NewHalfSize, CurrentCell.Center.Y + NewHalfSize) };

		for (const FVector2D& SubCenter : SubCellCenters)
		{
			FCell SubCell(SubCenter, NewHalfSize, PolygonVertices);
			if (SubCell.Distance > BestCell.Distance)
			{
				BestCell = SubCell;
			}
			if (SubCell.Potential - BestCell.Distance > Epsilon)
			{
				CellQueue.Add(SubCell);
			}
		}

		// Re-sort queue
		CellQueue.Sort([](const FCell& A, const FCell& B) { return A.Potential > B.Potential; });
	}

	OutCenter = BestCell.Center;
	OutRadius = BestCell.Distance;
	return OutRadius > 0;
}

void FGeometryUtils::PoissonDiskSampling(
	const TArray<FVector2D>& PolygonVertices, float Radius, int32 MaxPoints, FRandomStream& RandomStream, TArray<FVector2D>& OutPoints)
{
	OutPoints.Empty();

	if (PolygonVertices.Num() < 3 || Radius <= 0 || MaxPoints <= 0)
	{
		return;
	}

	// Get polygon bounds
	FVector2D MinBounds(FLT_MAX, FLT_MAX);
	FVector2D MaxBounds(-FLT_MAX, -FLT_MAX);

	for (const FVector2D& Vertex : PolygonVertices)
	{
		MinBounds.X = FMath::Min(MinBounds.X, Vertex.X);
		MinBounds.Y = FMath::Min(MinBounds.Y, Vertex.Y);
		MaxBounds.X = FMath::Max(MaxBounds.X, Vertex.X);
		MaxBounds.Y = FMath::Max(MaxBounds.Y, Vertex.Y);
	}

	// Grid for spatial acceleration
	const float CellSize = Radius / FMath::Sqrt(2.0f);
	const int32 GridWidth = FMath::CeilToInt((MaxBounds.X - MinBounds.X) / CellSize);
	const int32 GridHeight = FMath::CeilToInt((MaxBounds.Y - MinBounds.Y) / CellSize);

	// Grid to store point indices (-1 means empty)
	TArray<int32> Grid;
	Grid.Init(-1, GridWidth * GridHeight);

	auto GetGridIndex = [&](const FVector2D& Point) -> int32 {
		int32 X = FMath::FloorToInt((Point.X - MinBounds.X) / CellSize);
		int32 Y = FMath::FloorToInt((Point.Y - MinBounds.Y) / CellSize);
		X = FMath::Clamp(X, 0, GridWidth - 1);
		Y = FMath::Clamp(Y, 0, GridHeight - 1);
		return Y * GridWidth + X;
	};

	// Find initial point inside polygon
	FVector2D InitialPoint;
	bool	  bFoundInitial = false;

	for (int32 Attempts = 0; Attempts < 100 && !bFoundInitial; ++Attempts)
	{
		InitialPoint.X = RandomStream.FRandRange(MinBounds.X, MaxBounds.X);
		InitialPoint.Y = RandomStream.FRandRange(MinBounds.Y, MaxBounds.Y);

		if (PointInPolygon(PolygonVertices, InitialPoint))
		{
			bFoundInitial = true;
		}
	}

	if (!bFoundInitial)
	{
		return;
	}

	// Add initial point
	OutPoints.Add(InitialPoint);
	Grid[GetGridIndex(InitialPoint)] = 0;

	TArray<int32> ActivePoints;
	ActivePoints.Add(0);

	// Main Poisson disk sampling loop
	const int32 MaxCandidatesPerPoint = 30;

	while (ActivePoints.Num() > 0 && OutPoints.Num() < MaxPoints)
	{
		const int32		 ActiveIndex = RandomStream.RandRange(0, ActivePoints.Num() - 1);
		const int32		 PointIndex = ActivePoints[ActiveIndex];
		const FVector2D& ActivePoint = OutPoints[PointIndex];

		bool bFoundValidCandidate = false;

		for (int32 Candidate = 0; Candidate < MaxCandidatesPerPoint; ++Candidate)
		{
			// Generate candidate in annulus [Radius, 2*Radius]
			const float Angle = RandomStream.FRandRange(0, 2.0f * PI);
			const float Distance = RandomStream.FRandRange(Radius, 2.0f * Radius);

			const FVector2D CandidatePoint = ActivePoint + FVector2D(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance);

			// Check if candidate is inside polygon
			if (!PointInPolygon(PolygonVertices, CandidatePoint))
			{
				continue;
			}

			// Check distance to existing points in nearby grid cells
			bool		bTooClose = false;
			const int32 CandidateGridX = FMath::FloorToInt((CandidatePoint.X - MinBounds.X) / CellSize);
			const int32 CandidateGridY = FMath::FloorToInt((CandidatePoint.Y - MinBounds.Y) / CellSize);

			for (int32 dx = -2; dx <= 2 && !bTooClose; ++dx)
			{
				for (int32 dy = -2; dy <= 2 && !bTooClose; ++dy)
				{
					const int32 CheckX = CandidateGridX + dx;
					const int32 CheckY = CandidateGridY + dy;

					if (CheckX >= 0 && CheckX < GridWidth && CheckY >= 0 && CheckY < GridHeight)
					{
						const int32 CheckIndex = CheckY * GridWidth + CheckX;
						if (Grid[CheckIndex] >= 0)
						{
							const FVector2D& ExistingPoint = OutPoints[Grid[CheckIndex]];
							const float		 DistSq = FVector2D::DistSquared(CandidatePoint, ExistingPoint);
							if (DistSq < Radius * Radius)
							{
								bTooClose = true;
							}
						}
					}
				}
			}

			if (!bTooClose)
			{
				// Add valid candidate
				const int32 NewPointIndex = OutPoints.Add(CandidatePoint);
				Grid[GetGridIndex(CandidatePoint)] = NewPointIndex;
				ActivePoints.Add(NewPointIndex);
				bFoundValidCandidate = true;
				break;
			}
		}

		if (!bFoundValidCandidate)
		{
			// Remove this active point as it can't generate more candidates
			ActivePoints.RemoveAt(ActiveIndex);
		}
	}
}

// Helper functions

float FGeometryUtils::DistanceToLineSegment(const FVector2D& Point, const FVector2D& LineStart, const FVector2D& LineEnd)
{
	const FVector2D LineVector = LineEnd - LineStart;
	const FVector2D PointVector = Point - LineStart;

	const float LineLengthSq = LineVector.SizeSquared();
	if (LineLengthSq < UE_SMALL_NUMBER)
	{
		// Line is effectively a point
		return FVector2D::Distance(Point, LineStart);
	}

	const float		t = FMath::Clamp(FVector2D::DotProduct(PointVector, LineVector) / LineLengthSq, 0.0f, 1.0f);
	const FVector2D Projection = LineStart + t * LineVector;

	return FVector2D::Distance(Point, Projection);
}

FVector2D FGeometryUtils::GetPolygonCentroid(const TArray<FVector2D>& PolygonVertices)
{
	if (PolygonVertices.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : PolygonVertices)
	{
		Centroid += Vertex;
	}

	return Centroid / static_cast<float>(PolygonVertices.Num());
}
