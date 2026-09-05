#include "GeometryUtils/GeometryFunctionLibrary.h"

#include "GridBudget.h"
#include "ProceduralGeometry.h"

namespace
{
	/** Cells needed to span Extent at InCellSize, evaluated in double so an absurd extent cannot narrow negative. */
	int64 GeomLib_ProjectGridAxis(double Extent, double InCellSize)
	{
		const double Cells = FMath::CeilToDouble(Extent / InCellSize);
		if (!FMath::IsFinite(Cells) || Cells <= 0.0)
		{
			return 0;
		}
		return static_cast<int64>(FMath::Min(Cells, static_cast<double>(PGGrid::MaxGridCells)));
	}

	/**
	 * Doubles InOutCellSize until the projected grid fits CellBudget. A caller still over budget on return has an
	 * input no cell size can accommodate and must bail.
	 */
	void GeomLib_CoarsenToCellBudget(double ExtentX, double ExtentY, int64 CellBudget, double& InOutCellSize, int64& OutNumX, int64& OutNumY)
	{
		OutNumX = GeomLib_ProjectGridAxis(ExtentX, InOutCellSize);
		OutNumY = GeomLib_ProjectGridAxis(ExtentY, InOutCellSize);

		constexpr int32 MaxCoarsenPasses = 64;
		for (int32 Pass = 0; Pass < MaxCoarsenPasses && OutNumX > 0 && OutNumY > 0 && OutNumX * OutNumY > CellBudget; ++Pass)
		{
			InOutCellSize *= 2.0;
			OutNumX = GeomLib_ProjectGridAxis(ExtentX, InOutCellSize);
			OutNumY = GeomLib_ProjectGridAxis(ExtentY, InOutCellSize);
		}
	}
} // namespace

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

bool FGeometryUtils::ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal)
{
	TArray<FVector2D> Scratch;
	return ClipPolygonByHalfPlane(OutPolygon, Scratch, PlanePoint, PlaneNormal);
}

bool FGeometryUtils::ClipPolygonByHalfPlane(
	TArray<FVector2D>& OutPolygon, TArray<FVector2D>& Scratch, const FVector2D& PlanePoint, const FVector2D& PlaneNormal)
{
	if (OutPolygon.Num() == 0)
	{
		return false;
	}

	Scratch.Reset();

	FVector2D Prev = OutPolygon.Last();
	double	  PrevSide = FVector2D::DotProduct(Prev - PlanePoint, PlaneNormal);

	auto SafeAlpha = [](double InPrevSide, double InCurrSide) -> double {
		double Denominator = InPrevSide - InCurrSide;
		if (FMath::Abs(Denominator) < UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			return 0.5;
		}
		return InPrevSide / Denominator;
	};

	// A crossing that lands on an edge endpoint must be emitted once: that endpoint is already appended in its own
	// right, so the interpolated copy is a zero-length edge. The guard is on Alpha because Alpha is scale-free.
	constexpr double CoincidentAlpha = UE_DOUBLE_KINDA_SMALL_NUMBER;

	for (const FVector2D& Curr : OutPolygon)
	{
		double CurrSide = FVector2D::DotProduct(Curr - PlanePoint, PlaneNormal);

		if (PrevSide <= 0.0 && CurrSide <= 0.0)
		{
			Scratch.Add(Curr);
		}
		else if (PrevSide <= 0.0 && CurrSide > 0.0)
		{
			// Alpha lies in [0,1) here, so only the Prev end can coincide.
			double Alpha = SafeAlpha(PrevSide, CurrSide);
			if (Alpha > CoincidentAlpha)
			{
				Scratch.Add(FMath::Lerp(Prev, Curr, Alpha));
			}
		}
		else if (PrevSide > 0.0 && CurrSide <= 0.0)
		{
			// Alpha lies in (0,1] here, so only the Curr end can coincide.
			double Alpha = SafeAlpha(PrevSide, CurrSide);
			if (Alpha < 1.0 - CoincidentAlpha)
			{
				Scratch.Add(FMath::Lerp(Prev, Curr, Alpha));
			}
			Scratch.Add(Curr);
		}

		Prev = Curr;
		PrevSide = CurrSide;
	}

	Swap(OutPolygon, Scratch);
	return OutPolygon.Num() >= 3;
}

bool FGeometryUtils::PointInPolygon(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point)
{
	if (PolygonVertices.Num() < 3)
	{
		return false;
	}

	// Winding-number test.
	int32		WindingNumber = 0;
	const int32 NumVertices = PolygonVertices.Num();

	for (int32 i = 0; i < NumVertices; ++i)
	{
		const FVector2D& V1 = PolygonVertices[i];
		const FVector2D& V2 = PolygonVertices[(i + 1) % NumVertices];

		if (V1.Y <= Point.Y)
		{
			if (V2.Y > Point.Y)
			{
				// The cross product is taken in double to match the clipping pipeline on near-collinear edges.
				const double CrossProduct = (double(V2.X) - V1.X) * (double(Point.Y) - V1.Y) - (double(V2.Y) - V1.Y) * (double(Point.X) - V1.X);
				if (CrossProduct > 0.0)
				{
					++WindingNumber;
				}
			}
		}
		else
		{
			if (V2.Y <= Point.Y)
			{
				const double CrossProduct = (double(V2.X) - V1.X) * (double(Point.Y) - V1.Y) - (double(V2.Y) - V1.Y) * (double(Point.X) - V1.X);
				if (CrossProduct < 0.0)
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

	FVector2D MinBounds, MaxBounds;
	GetPolygonBounds(PolygonVertices, MinBounds, MaxBounds);

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

	TArray<FCell> CellQueue;

	const float GridSize = FMath::Min(MaxBounds.X - MinBounds.X, MaxBounds.Y - MinBounds.Y) / 4.0f;

	// A collinear polygon has zero extent on one axis and no inscribed circle of positive radius.
	if (!(GridSize > UE_KINDA_SMALL_NUMBER))
	{
		return false;
	}

	// Cell i sits at MinBounds + i * CellSize so cells stay distinct however far the polygon is from the origin; a
	// step below the representable spacing there makes every cell identical and cannot be rescued.
	const double MaxMagnitude =
		FMath::Max(FMath::Max(FMath::Abs(MinBounds.X), FMath::Abs(MaxBounds.X)), FMath::Max(FMath::Abs(MinBounds.Y), FMath::Abs(MaxBounds.Y)));
	double CellSizeD = GridSize;

	if (MaxMagnitude + CellSizeD == MaxMagnitude)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[Geometry] MaxInscribedCircle: cell size %g is below the representable step at coordinate magnitude %g — no usable grid"),
			CellSizeD,
			MaxMagnitude);
		return false;
	}

	// A sliver polygon projects a seed grid bounded only by its axis ratio, so coarsen until it fits. The budget sits
	// far below the GridBudget.h ceiling because a cell here is an FCell with an eager point-in-polygon distance.
	constexpr int64 SeedGridBudget = 65'536;

	int64 NumX = 0;
	int64 NumY = 0;
	GeomLib_CoarsenToCellBudget(MaxBounds.X - MinBounds.X, MaxBounds.Y - MinBounds.Y, SeedGridBudget, CellSizeD, NumX, NumY);

	if (NumX <= 0 || NumY <= 0 || NumX * NumY > SeedGridBudget)
	{
		return false;
	}

	FCell BestCell(GetPolygonCentroid(PolygonVertices), 0, PolygonVertices);

	// Max-heap by potential: the best candidate is always the top, so subdividing one cell costs a push per child
	// instead of re-sorting the whole queue.
	auto ByDescendingPotential = [](const FCell& A, const FCell& B) { return A.Potential > B.Potential; };

	const float HalfCell = static_cast<float>(CellSizeD * 0.5);
	CellQueue.Reserve(static_cast<int32>(NumX * NumY));

	for (int64 i = 0; i < NumX; ++i)
	{
		const double x = MinBounds.X + i * CellSizeD;
		for (int64 j = 0; j < NumY; ++j)
		{
			const double y = MinBounds.Y + j * CellSizeD;

			FCell NewCell(FVector2D(x + CellSizeD * 0.5, y + CellSizeD * 0.5), HalfCell, PolygonVertices);
			if (NewCell.Distance > BestCell.Distance)
			{
				BestCell = NewCell;
			}
			CellQueue.Add(NewCell);
		}
	}

	CellQueue.Heapify(ByDescendingPotential);

	while (CellQueue.Num() > 0 && CellQueue.HeapTop().Potential - BestCell.Distance > Epsilon)
	{
		FCell CurrentCell = CellQueue.HeapTop();
		CellQueue.HeapPopDiscard(ByDescendingPotential);

		if (CurrentCell.Potential - BestCell.Distance <= Epsilon)
		{
			continue;
		}

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
				CellQueue.HeapPush(SubCell, ByDescendingPotential);
			}
		}
	}

	OutCenter = BestCell.Center;
	OutRadius = BestCell.Distance;
	return OutRadius > 0;
}

void FGeometryUtils::PoissonDiskSampling(
	const TArray<FVector2D>& PolygonVertices, float Radius, int32 MaxPoints, FRandomStream& RandomStream, TArray<FVector2D>& OutPoints)
{
	OutPoints.Empty();

	if (PolygonVertices.Num() < 3 || Radius < UE_KINDA_SMALL_NUMBER || MaxPoints <= 0)
	{
		return;
	}

	FVector2D MinBounds, MaxBounds;
	GetPolygonBounds(PolygonVertices, MinBounds, MaxBounds);

	// The acceleration grid cell count follows the bounds alone, not MaxPoints, so a kilometre-scale bounds would
	// overflow an int32 product into TArray::SetNum; the bucket budget is tied to MaxPoints instead.
	const int64 BucketBudget = FMath::Clamp(static_cast<int64>(MaxPoints) * 64, static_cast<int64>(1024), PGGrid::MaxGridCells);

	// Computed at float width, matching the cell size every caller under the budget receives.
	const double RequestedCellSize = Radius / FMath::Sqrt(2.0f);
	double		 CellSizeD = RequestedCellSize;
	int64		 GridWidth64 = 0;
	int64		 GridHeight64 = 0;
	GeomLib_CoarsenToCellBudget(MaxBounds.X - MinBounds.X, MaxBounds.Y - MinBounds.Y, BucketBudget, CellSizeD, GridWidth64, GridHeight64);

	if (GridWidth64 <= 0 || GridHeight64 <= 0 || GridWidth64 * GridHeight64 > BucketBudget)
	{
		return;
	}

	if (CellSizeD > RequestedCellSize)
	{
		// NeighborRing is derived from the cell size, so the searched ring always spans Radius and the emitted points
		// are the same at any bucket size.
		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[Geometry] PoissonDiskSampling: acceleration grid coarsened from cell %.3f to %.3f to fit %lld buckets for MaxPoints=%d "
				 "(sampled points are unaffected)"),
			RequestedCellSize,
			CellSizeD,
			static_cast<long long>(BucketBudget),
			MaxPoints);
	}

	const float CellSize = static_cast<float>(CellSizeD);
	const int32 GridWidth = static_cast<int32>(GridWidth64);
	const int32 GridHeight = static_cast<int32>(GridHeight64);

	// Two points within Radius differ by at most this many buckets per axis; a coarser grid needs a narrower ring.
	const int32 NeighborRing = FMath::FloorToInt(Radius / CellSize) + 1;

	TArray<TArray<int32>> Grid;
	Grid.SetNum(GridWidth * GridHeight);

	auto GetGridIndex = [&](const FVector2D& Point) -> int32 {
		int32 X = FMath::FloorToInt((Point.X - MinBounds.X) / CellSize);
		int32 Y = FMath::FloorToInt((Point.Y - MinBounds.Y) / CellSize);
		X = FMath::Clamp(X, 0, GridWidth - 1);
		Y = FMath::Clamp(Y, 0, GridHeight - 1);
		return Y * GridWidth + X;
	};

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

	OutPoints.Add(InitialPoint);
	Grid[GetGridIndex(InitialPoint)].Add(0);

	TArray<int32> ActivePoints;
	ActivePoints.Add(0);

	const int32 MaxCandidatesPerPoint = 30;

	while (ActivePoints.Num() > 0 && OutPoints.Num() < MaxPoints)
	{
		const int32		 ActiveIndex = RandomStream.RandRange(0, ActivePoints.Num() - 1);
		const int32		 PointIndex = ActivePoints[ActiveIndex];
		const FVector2D& ActivePoint = OutPoints[PointIndex];

		bool bFoundValidCandidate = false;

		for (int32 Candidate = 0; Candidate < MaxCandidatesPerPoint; ++Candidate)
		{
			const float Angle = RandomStream.FRandRange(0, 2.0f * PI);
			const float Distance = RandomStream.FRandRange(Radius, 2.0f * Radius);

			const FVector2D CandidatePoint = ActivePoint + FVector2D(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance);

			if (!PointInPolygon(PolygonVertices, CandidatePoint))
			{
				continue;
			}

			bool		bTooClose = false;
			const int32 CandidateGridX = FMath::FloorToInt((CandidatePoint.X - MinBounds.X) / CellSize);
			const int32 CandidateGridY = FMath::FloorToInt((CandidatePoint.Y - MinBounds.Y) / CellSize);

			for (int32 dx = -NeighborRing; dx <= NeighborRing && !bTooClose; ++dx)
			{
				for (int32 dy = -NeighborRing; dy <= NeighborRing && !bTooClose; ++dy)
				{
					const int32 CheckX = CandidateGridX + dx;
					const int32 CheckY = CandidateGridY + dy;

					if (CheckX >= 0 && CheckX < GridWidth && CheckY >= 0 && CheckY < GridHeight)
					{
						const int32 CheckIndex = CheckY * GridWidth + CheckX;
						for (const int32 NeighborIndex : Grid[CheckIndex])
						{
							const FVector2D& ExistingPoint = OutPoints[NeighborIndex];
							const float		 DistSq = FVector2D::DistSquared(CandidatePoint, ExistingPoint);
							if (DistSq < Radius * Radius)
							{
								bTooClose = true;
								break;
							}
						}
					}
				}
			}

			if (!bTooClose)
			{
				const int32 NewPointIndex = OutPoints.Add(CandidatePoint);
				Grid[GetGridIndex(CandidatePoint)].Add(NewPointIndex);
				ActivePoints.Add(NewPointIndex);
				bFoundValidCandidate = true;
				break;
			}
		}

		if (!bFoundValidCandidate)
		{
			ActivePoints.RemoveAt(ActiveIndex);
		}
	}
}

void FGeometryUtils::ChaikinSubdivide(TArray<FVector2D>& Vertices, int32 Iterations)
{
	if (Vertices.Num() < 3 || Iterations <= 0)
	{
		return;
	}

	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		const int32		  N = Vertices.Num();
		TArray<FVector2D> Subdivided;
		Subdivided.Reserve(N * 2);

		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& P0 = Vertices[i];
			const FVector2D& P1 = Vertices[(i + 1) % N];

			Subdivided.Add(P0 * 0.75f + P1 * 0.25f);
			Subdivided.Add(P0 * 0.25f + P1 * 0.75f);
		}

		Vertices = MoveTemp(Subdivided);
	}
}

void FGeometryUtils::GetPolygonBounds(const TArray<FVector2D>& PolygonVertices, FVector2D& OutMin, FVector2D& OutMax)
{
	OutMin = FVector2D(FLT_MAX, FLT_MAX);
	OutMax = FVector2D(-FLT_MAX, -FLT_MAX);

	for (const FVector2D& Vertex : PolygonVertices)
	{
		OutMin.X = FMath::Min(OutMin.X, Vertex.X);
		OutMin.Y = FMath::Min(OutMin.Y, Vertex.Y);
		OutMax.X = FMath::Max(OutMax.X, Vertex.X);
		OutMax.Y = FMath::Max(OutMax.Y, Vertex.Y);
	}
}

float FGeometryUtils::DistanceToLineSegment(const FVector2D& Point, const FVector2D& LineStart, const FVector2D& LineEnd)
{
	const FVector2D LineVector = LineEnd - LineStart;
	const FVector2D PointVector = Point - LineStart;

	const float LineLengthSq = LineVector.SizeSquared();
	if (LineLengthSq < UE_SMALL_NUMBER)
	{
		return FVector2D::Distance(Point, LineStart);
	}

	const float		t = FMath::Clamp(FVector2D::DotProduct(PointVector, LineVector) / LineLengthSq, 0.0f, 1.0f);
	const FVector2D Projection = LineStart + t * LineVector;

	return FVector2D::Distance(Point, Projection);
}

FVector2D FGeometryUtils::GetPolygonCentroid(const TArray<FVector2D>& PolygonVertices)
{
	const int32 Num = PolygonVertices.Num();

	if (Num == 0)
	{
		return FVector2D::ZeroVector;
	}
	if (Num == 1)
	{
		return PolygonVertices[0];
	}
	if (Num == 2)
	{
		return (PolygonVertices[0] + PolygonVertices[1]) * 0.5f;
	}

	FVector2D Centroid = FVector2D::ZeroVector;
	float	  SignedArea = 0.0f;

	for (int32 i = 0; i < Num; ++i)
	{
		const FVector2D& V1 = PolygonVertices[i];
		const FVector2D& V2 = PolygonVertices[(i + 1) % Num];
		const float		 CrossProduct = V1.X * V2.Y - V2.X * V1.Y;
		SignedArea += CrossProduct;
		Centroid += (V1 + V2) * CrossProduct;
	}

	if (FMath::Abs(SignedArea) < UE_KINDA_SMALL_NUMBER)
	{
		Centroid = FVector2D::ZeroVector;
		for (const FVector2D& Vertex : PolygonVertices)
		{
			Centroid += Vertex;
		}
		return Centroid / static_cast<float>(Num);
	}

	return Centroid / (3.0f * SignedArea);
}

FVector2D FGeometryUtils::RotateVector(const FVector2D& V, const float RotationDeg)
{
	const float Rad = FMath::DegreesToRadians(RotationDeg);
	const float C = FMath::Cos(Rad);
	const float S = FMath::Sin(Rad);
	return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
}

void FGeometryUtils::RotatedRectCorners(const FVector2D& Center, const float RotationDeg, const FVector2D& Footprint, TArray<FVector2D>& OutCorners)
{
	const float		HX = Footprint.X * 0.5f;
	const float		HY = Footprint.Y * 0.5f;
	const FVector2D Local[4] = { FVector2D(-HX, -HY), FVector2D(HX, -HY), FVector2D(HX, HY), FVector2D(-HX, HY) };

	OutCorners.Reset(4);
	for (int32 i = 0; i < 4; ++i)
	{
		OutCorners.Add(Center + RotateVector(Local[i], RotationDeg));
	}
}

bool FGeometryUtils::ConvexPolygonsOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
{
	if (A.Num() < 3 || B.Num() < 3)
	{
		return false;
	}

	auto Project = [](const TArray<FVector2D>& Poly, const FVector2D& Axis, float& OutMin, float& OutMax) {
		OutMin = TNumericLimits<float>::Max();
		OutMax = TNumericLimits<float>::Lowest();
		for (const FVector2D& P : Poly)
		{
			const float D = FVector2D::DotProduct(P, Axis);
			OutMin = FMath::Min(OutMin, D);
			OutMax = FMath::Max(OutMax, D);
		}
	};

	const TArray<FVector2D>* Polys[2] = { &A, &B };
	for (int32 Pi = 0; Pi < 2; ++Pi)
	{
		const TArray<FVector2D>& Poly = *Polys[Pi];
		const int32				 N = Poly.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D Edge = Poly[(i + 1) % N] - Poly[i];
			FVector2D		Axis(-Edge.Y, Edge.X);
			const float		Len = Axis.Size();
			if (Len <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Axis /= Len;

			float MinA, MaxA, MinB, MaxB;
			Project(A, Axis, MinA, MaxA);
			Project(B, Axis, MinB, MaxB);

			// Strict gap on this axis => separated (touching counts as overlap).
			if (MaxA < MinB - KINDA_SMALL_NUMBER || MaxB < MinA - KINDA_SMALL_NUMBER)
			{
				return false;
			}
		}
	}
	return true;
}

void FGeometryUtils::SampleCatmullRom(const TArray<FVector2D>& ControlPoints, const int32 SamplesPerSegment, TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();
	if (ControlPoints.Num() < 3)
	{
		OutPoints = ControlPoints;
		return;
	}
	auto Pt = [&](int32 i) -> FVector2D { return ControlPoints[FMath::Clamp(i, 0, ControlPoints.Num() - 1)]; };
	for (int32 i = 0; i < ControlPoints.Num() - 1; ++i)
	{
		const FVector2D P0 = Pt(i - 1), P1 = Pt(i), P2 = Pt(i + 1), P3 = Pt(i + 2);
		for (int32 s = 0; s < SamplesPerSegment; ++s)
		{
			const float t = static_cast<float>(s) / static_cast<float>(SamplesPerSegment);
			const float t2 = t * t, t3 = t2 * t;
			OutPoints.Add(
				(P1 * 2.0f + (P2 - P0) * t + (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * t2 + (P1 * 3.0f - P0 - P2 * 3.0f + P3) * t3) * 0.5f);
		}
	}
	OutPoints.Add(ControlPoints.Last());
}

void FGeometryUtils::OffsetPolylineToRibbon(const TArray<FVector2D>& Polyline, const float Width, TArray<FVector2D>& OutRibbon)
{
	OutRibbon.Reset();
	const int32 N = Polyline.Num();
	if (N < 2)
	{
		return;
	}
	auto SegNormal = [&](int32 a, int32 b) -> FVector2D {
		const FVector2D D = (Polyline[b] - Polyline[a]).GetSafeNormal();
		return FVector2D(-D.Y, D.X);
	};
	TArray<FVector2D> Nrm;
	Nrm.SetNum(N);
	Nrm[0] = SegNormal(0, 1);
	Nrm[N - 1] = SegNormal(N - 2, N - 1);
	for (int32 i = 1; i < N - 1; ++i)
	{
		const FVector2D Averaged = (SegNormal(i - 1, i) + SegNormal(i, i + 1)).GetSafeNormal();
		// At an exact 180-degree reversal the segment normals cancel, so fall back to the incoming perpendicular.
		Nrm[i] = Averaged.IsNearlyZero() ? SegNormal(i - 1, i) : Averaged;
	}
	const float H = Width * 0.5f;
	for (int32 i = 0; i < N; ++i)
	{
		OutRibbon.Add(Polyline[i] + Nrm[i] * H);
	}
	for (int32 i = N - 1; i >= 0; --i)
	{
		OutRibbon.Add(Polyline[i] - Nrm[i] * H);
	}
}
