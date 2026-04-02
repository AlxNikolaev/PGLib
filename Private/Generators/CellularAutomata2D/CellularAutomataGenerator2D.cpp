#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoguelikeGeometry, Log, All);

UCellularAutomataGenerator2D::UCellularAutomataGenerator2D()
{
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
	FillProbability = 0.45f;
	Iterations = 5;
	BirthRule = { 6, 7, 8 };
	SurvivalRule = { 3, 4, 5 };
	MinRegionSize = 20;
	bKeepCenterRegion = true;
	InitializeRandomStream();
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Super::SetBounds(InBounds);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetSeed(const FString& InSeed)
{
	Super::SetSeed(InSeed);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetGridSize(int32 InSize)
{
	Super::SetGridSize(InSize);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetCenter(const FVector2D& InCenter)
{
	Super::SetCenter(InCenter);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetFillProbability(float InProbability)
{
	FillProbability = FMath::Clamp(InProbability, 0.0f, 1.0f);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetIterations(int32 InIterations)
{
	Iterations = FMath::Max(0, InIterations);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetBirthRule(const TArray<int32>& InRule)
{
	BirthRule = InRule;
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetSurvivalRule(const TArray<int32>& InRule)
{
	SurvivalRule = InRule;
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetMinRegionSize(int32 InSize)
{
	MinRegionSize = FMath::Max(1, InSize);
	return this;
}

UCellularAutomataGenerator2D* UCellularAutomataGenerator2D::SetKeepCenterRegion(bool bKeep)
{
	bKeepCenterRegion = bKeep;
	return this;
}

uint16 UCellularAutomataGenerator2D::RuleToBitmask(const TArray<int32>& Rule)
{
	uint16 Mask = 0;
	for (const int32 Count : Rule)
	{
		if (Count >= 0 && Count <= 8)
		{
			Mask |= (1 << Count);
		}
	}
	return Mask;
}

int32 UCellularAutomataGenerator2D::CountWallNeighbors(const TArray<bool>& Grid, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const
{
	int32 WallCount = 0;
	for (int32 dy = -1; dy <= 1; ++dy)
	{
		for (int32 dx = -1; dx <= 1; ++dx)
		{
			if (dx == 0 && dy == 0)
			{
				continue;
			}

			const int32 NX = X + dx;
			const int32 NY = Y + dy;

			if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight)
			{
				++WallCount;
			}
			else if (!Grid[NY * GridWidth + NX])
			{
				++WallCount;
			}
		}
	}
	return WallCount;
}

FLayoutDiagram2D UCellularAutomataGenerator2D::Generate()
{
	const double StartTime = FPlatformTime::Seconds();

	UE_LOG(LogRoguelikeGeometry, Log,
		TEXT("[CA] Generate() — Bounds=(%.1f,%.1f)-(%.1f,%.1f) GridSize=%d Seed='%s' FillProb=%.2f Iterations=%d MinRegion=%d KeepCenter=%s"),
		Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y,
		GridSize, *Seed, FillProbability, Iterations, MinRegionSize, bKeepCenterRegion ? TEXT("true") : TEXT("false"));

	const float CellSize = static_cast<float>(GridSize);
	const float BoundsWidth = Bounds.Max.X - Bounds.Min.X;
	const float BoundsHeight = Bounds.Max.Y - Bounds.Min.Y;

	const int32 GridWidth = FMath::CeilToInt(BoundsWidth / CellSize);
	const int32 GridHeight = FMath::CeilToInt(BoundsHeight / CellSize);

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Grid dimensions: %dx%d (%d total cells)"), GridWidth, GridHeight, GridWidth * GridHeight);

	// OOM guard
	if (GridWidth <= 0 || GridHeight <= 0 || (int64)GridWidth * GridHeight > 4'194'304)
	{
		UE_LOG(LogRoguelikeGeometry, Error, TEXT("[CA] OOM guard triggered: %dx%d exceeds limit"), GridWidth, GridHeight);
		return FLayoutDiagram2D();
	}

	const int32 TotalCells = GridWidth * GridHeight;

	// Initialize grid: true = floor, false = wall
	TArray<bool> Grid;
	Grid.Init(false, TotalCells);

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			// Boundary cells are always walls
			if (X == 0 || X == GridWidth - 1 || Y == 0 || Y == GridHeight - 1)
			{
				continue;
			}
			// Floor if random value exceeds fill probability (fill = wall probability)
			Grid[Y * GridWidth + X] = (RandomStream.FRand() >= FillProbability);
		}
	}

	// CA iterations with double buffer
	const uint16 BirthMask = RuleToBitmask(BirthRule);
	const uint16 SurvivalMask = RuleToBitmask(SurvivalRule);

	TArray<bool> NewGrid;
	NewGrid.SetNum(TotalCells);

	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		for (int32 Y = 0; Y < GridHeight; ++Y)
		{
			for (int32 X = 0; X < GridWidth; ++X)
			{
				const int32 Index = Y * GridWidth + X;

				// Boundary cells stay walls
				if (X == 0 || X == GridWidth - 1 || Y == 0 || Y == GridHeight - 1)
				{
					NewGrid[Index] = false;
					continue;
				}

				const int32 WallNeighbors = CountWallNeighbors(Grid, X, Y, GridWidth, GridHeight);
				const bool bIsWall = !Grid[Index];

				if (bIsWall)
				{
					// Wall stays wall if survival rule matches
					NewGrid[Index] = ((SurvivalMask >> WallNeighbors) & 1) ? false : true;
				}
				else
				{
					// Floor becomes wall if birth rule matches
					NewGrid[Index] = ((BirthMask >> WallNeighbors) & 1) ? false : true;
				}
			}
		}

		Swap(Grid, NewGrid);
	}

	// Count floor cells after CA iterations
	{
		int32 FloorCount = 0;
		for (bool bIsFloor : Grid)
		{
			if (bIsFloor)
			{
				++FloorCount;
			}
		}
		UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] After %d iterations: %d floor cells (%.1f%% of grid)"),
			Iterations, FloorCount, 100.0f * FloorCount / TotalCells);
	}

	// Flood-fill: iterative BFS to identify connected floor regions
	TArray<int32> RegionIds;
	RegionIds.Init(-1, TotalCells);

	TArray<TArray<FIntPoint>> Regions;
	int32 CenterRegionId = -1;
	const int32 CenterX = GridWidth / 2;
	const int32 CenterY = GridHeight / 2;

	const int32 DX[] = { 1, -1, 0, 0 };
	const int32 DY[] = { 0, 0, 1, -1 };

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const int32 Index = Y * GridWidth + X;
			if (!Grid[Index] || RegionIds[Index] >= 0)
			{
				continue;
			}

			// Start new region with BFS
			const int32 RegionId = Regions.Num();
			TArray<FIntPoint>& Region = Regions.AddDefaulted_GetRef();

			TArray<FIntPoint> Queue;
			Queue.Add(FIntPoint(X, Y));
			RegionIds[Index] = RegionId;
			int32 Head = 0;

			while (Head < Queue.Num())
			{
				const FIntPoint Cell = Queue[Head++];
				Region.Add(Cell);

				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					const int32 NX = Cell.X + DX[Dir];
					const int32 NY = Cell.Y + DY[Dir];

					if (NX >= 0 && NX < GridWidth && NY >= 0 && NY < GridHeight)
					{
						const int32 NIndex = NY * GridWidth + NX;
						if (Grid[NIndex] && RegionIds[NIndex] < 0)
						{
							RegionIds[NIndex] = RegionId;
							Queue.Add(FIntPoint(NX, NY));
						}
					}
				}
			}

			// Check if this region contains the center cell
			if (CenterRegionId < 0)
			{
				const int32 CenterIndex = CenterY * GridWidth + CenterX;
				if (RegionIds[CenterIndex] == RegionId)
				{
					CenterRegionId = RegionId;
				}
			}
		}
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Flood-fill found %d regions, center region=%d"),
		Regions.Num(), CenterRegionId);

	// Cull small regions
	int32 CulledCount = 0;
	for (int32 RegionId = 0; RegionId < Regions.Num(); ++RegionId)
	{
		if (Regions[RegionId].Num() < MinRegionSize)
		{
			if (bKeepCenterRegion && RegionId == CenterRegionId)
			{
				continue;
			}

			for (const FIntPoint& Cell : Regions[RegionId])
			{
				Grid[Cell.Y * GridWidth + Cell.X] = false;
			}
			++CulledCount;
		}
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Culled %d regions below MinRegionSize=%d, %d surviving"),
		CulledCount, MinRegionSize, Regions.Num() - CulledCount);

	FLayoutDiagram2D Result = BuildDiagramFromRegions(Grid, RegionIds, Regions, CenterRegionId, GridWidth, GridHeight);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Generate() complete: %d cells in %.2fms"), Result.Cells.Num(), ElapsedMs);

	return Result;
}

FLayoutDiagram2D UCellularAutomataGenerator2D::BuildDiagramFromRegions(
	const TArray<bool>& Grid,
	const TArray<int32>& RegionIds,
	const TArray<TArray<FIntPoint>>& Regions,
	int32 CenterRegionId,
	int32 InGridWidth,
	int32 InGridHeight)
{
	const float CellSize = static_cast<float>(GridSize);

	// Stage 1: Identify surviving regions
	TMap<int32, int32> RegionToCellIndex;
	TArray<int32> SurvivingRegionIds;

	for (int32 RegionId = 0; RegionId < Regions.Num(); ++RegionId)
	{
		if (Regions[RegionId].Num() > 0 && Grid[Regions[RegionId][0].Y * InGridWidth + Regions[RegionId][0].X])
		{
			RegionToCellIndex.Add(RegionId, SurvivingRegionIds.Num());
			SurvivingRegionIds.Add(RegionId);
		}
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] BuildDiagramFromRegions: %d surviving regions"), SurvivingRegionIds.Num());

	if (SurvivingRegionIds.Num() == 0)
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[CA] No surviving regions — returning empty diagram"));
		return FLayoutDiagram2D();
	}

	// Stage 2: Trace boundaries and build cells
	FLayoutDiagram2D Diagram;
	Diagram.Bounds = Bounds;
	Diagram.Seed = Seed;
	Diagram.CenterPoint = CenterPoint;
	Diagram.CenterCellIndex = INDEX_NONE;

	for (int32 i = 0; i < SurvivingRegionIds.Num(); ++i)
	{
		const int32 RegionId = SurvivingRegionIds[i];
		const TArray<FIntPoint>& Region = Regions[RegionId];

		FLayoutCell2D Cell;
		Cell.CellIndex = i;
		Cell.Vertices = TraceBoundaryPolygon(Region, RegionIds, RegionId, InGridWidth, InGridHeight, CellSize);

		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[CA] Region %d: %d grid cells, %d boundary vertices, exterior=%s"),
			RegionId, Region.Num(), Cell.Vertices.Num(),
			(Cell.Vertices.Num() > 0) ? TEXT("pending") : TEXT("n/a"));

		// Compute center as arithmetic mean of constituent cell centers
		FVector2D CenterSum = FVector2D::ZeroVector;
		bool bTouchesBoundary = false;

		for (const FIntPoint& GridCell : Region)
		{
			CenterSum += FVector2D(
				Bounds.Min.X + (GridCell.X + 0.5f) * CellSize,
				Bounds.Min.Y + (GridCell.Y + 0.5f) * CellSize);

			if (GridCell.X == 0 || GridCell.X == InGridWidth - 1 || GridCell.Y == 0 || GridCell.Y == InGridHeight - 1)
			{
				bTouchesBoundary = true;
			}
		}

		Cell.Center = CenterSum / static_cast<float>(Region.Num());
		Cell.bIsExterior = bTouchesBoundary;

		Diagram.Cells.Add(Cell);

		if (RegionId == CenterRegionId)
		{
			Diagram.CenterCellIndex = Diagram.Cells.Num() - 1;
		}
	}

	// Stage 3: Compute region neighbors via wall cells
	const int32 DX[] = { 1, -1, 0, 0 };
	const int32 DY[] = { 0, 0, 1, -1 };

	for (int32 Y = 0; Y < InGridHeight; ++Y)
	{
		for (int32 X = 0; X < InGridWidth; ++X)
		{
			if (Grid[Y * InGridWidth + X])
			{
				continue;
			}

			TArray<int32, TInlineAllocator<4>> AdjacentCellIndices;

			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				const int32 NX = X + DX[Dir];
				const int32 NY = Y + DY[Dir];

				if (NX >= 0 && NX < InGridWidth && NY >= 0 && NY < InGridHeight && Grid[NY * InGridWidth + NX])
				{
					const int32 NRegionId = RegionIds[NY * InGridWidth + NX];
					const int32* CellIdx = RegionToCellIndex.Find(NRegionId);
					if (CellIdx)
					{
						AdjacentCellIndices.AddUnique(*CellIdx);
					}
				}
			}

			for (int32 a = 0; a < AdjacentCellIndices.Num(); ++a)
			{
				for (int32 b = a + 1; b < AdjacentCellIndices.Num(); ++b)
				{
					Diagram.Cells[AdjacentCellIndices[a]].Neighbors.AddUnique(AdjacentCellIndices[b]);
					Diagram.Cells[AdjacentCellIndices[b]].Neighbors.AddUnique(AdjacentCellIndices[a]);
				}
			}
		}
	}

	// Fallback: if center region was culled, find closest cell
	if (Diagram.CenterCellIndex == INDEX_NONE && Diagram.Cells.Num() > 0)
	{
		float BestDistSq = FLT_MAX;
		for (int32 CellIdx = 0; CellIdx < Diagram.Cells.Num(); ++CellIdx)
		{
			const float DistSq = FVector2D::DistSquared(Diagram.Cells[CellIdx].Center, CenterPoint);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Diagram.CenterCellIndex = CellIdx;
			}
		}
	}

	return Diagram;
}

TArray<FVector2D> UCellularAutomataGenerator2D::TraceBoundaryPolygon(
	const TArray<FIntPoint>& Region,
	const TArray<int32>& RegionIds,
	int32 RegionId,
	int32 InGridWidth,
	int32 InGridHeight,
	float InCellSize) const
{
	// Collect directed boundary edges in grid corner coordinates (CCW, interior on left)
	TMap<FIntPoint, FIntPoint> EdgeMap;

	auto IsOutsideRegion = [&](int32 NX, int32 NY) -> bool
	{
		if (NX < 0 || NX >= InGridWidth || NY < 0 || NY >= InGridHeight)
		{
			return true;
		}
		return RegionIds[NY * InGridWidth + NX] != RegionId;
	};

	auto TryAddEdge = [&](const FIntPoint& Start, const FIntPoint& End)
	{
		if (ensure(!EdgeMap.Contains(Start)))
		{
			EdgeMap.Add(Start, End);
		}
	};

	for (const FIntPoint& Cell : Region)
	{
		const int32 X = Cell.X;
		const int32 Y = Cell.Y;

		if (IsOutsideRegion(X + 1, Y))
		{
			TryAddEdge(FIntPoint(X + 1, Y), FIntPoint(X + 1, Y + 1));
		}
		if (IsOutsideRegion(X, Y + 1))
		{
			TryAddEdge(FIntPoint(X + 1, Y + 1), FIntPoint(X, Y + 1));
		}
		if (IsOutsideRegion(X - 1, Y))
		{
			TryAddEdge(FIntPoint(X, Y + 1), FIntPoint(X, Y));
		}
		if (IsOutsideRegion(X, Y - 1))
		{
			TryAddEdge(FIntPoint(X, Y), FIntPoint(X + 1, Y));
		}
	}

	if (EdgeMap.Num() == 0)
	{
		return TArray<FVector2D>();
	}

	// Chain edges into closed loops
	TArray<TArray<FIntPoint>> Loops;
	TSet<FIntPoint> Visited;

	for (const auto& Edge : EdgeMap)
	{
		const FIntPoint& Start = Edge.Key;
		if (Visited.Contains(Start))
		{
			continue;
		}

		TArray<FIntPoint> Loop;
		FIntPoint Current = Start;

		while (!Visited.Contains(Current))
		{
			Visited.Add(Current);
			Loop.Add(Current);

			const FIntPoint* Next = EdgeMap.Find(Current);
			if (!Next)
			{
				break;
			}
			Current = *Next;
		}

		if (Loop.Num() >= 3 && Current == Start)
		{
			Loops.Add(MoveTemp(Loop));
		}
	}

	if (Loops.Num() == 0)
	{
		return TArray<FVector2D>();
	}

	// Select outer boundary (largest absolute area by shoelace)
	int32 BestLoopIndex = 0;
	float BestArea = 0.0f;

	for (int32 i = 0; i < Loops.Num(); ++i)
	{
		const float Area = ComputePolygonArea(Loops[i]);
		if (Area > BestArea)
		{
			BestArea = Area;
			BestLoopIndex = i;
		}
	}

	return SimplifyAndConvert(Loops[BestLoopIndex], InCellSize);
}

float UCellularAutomataGenerator2D::ComputePolygonArea(const TArray<FIntPoint>& Loop)
{
	float Area = 0.0f;
	for (int32 i = 0; i < Loop.Num(); ++i)
	{
		const FIntPoint& V1 = Loop[i];
		const FIntPoint& V2 = Loop[(i + 1) % Loop.Num()];
		Area += static_cast<float>(V1.X * V2.Y - V2.X * V1.Y);
	}
	return FMath::Abs(Area) * 0.5f;
}

TArray<FVector2D> UCellularAutomataGenerator2D::SimplifyAndConvert(const TArray<FIntPoint>& Loop, float InCellSize) const
{
	TArray<FVector2D> Result;
	const int32 N = Loop.Num();

	for (int32 i = 0; i < N; ++i)
	{
		const FIntPoint& A = Loop[(i - 1 + N) % N];
		const FIntPoint& B = Loop[i];
		const FIntPoint& C = Loop[(i + 1) % N];

		const int32 Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);

		if (Cross != 0)
		{
			Result.Add(FVector2D(Bounds.Min.X + B.X * InCellSize, Bounds.Min.Y + B.Y * InCellSize));
		}
	}

	return Result;
}
