#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"

#include "GridBudget.h"
#include "ProceduralGeometry.h"

namespace
{
	/**
	 * Boundary cells of one region plus a uniform bucket grid over them (built on the first query), used to answer
	 * "which cell of this region is nearest to point P" without scanning the region.
	 *
	 * Only boundary cells can win: from a cell whose four orthogonal neighbours all belong to the same region, the
	 * single-axis step toward any cell of another region stays inside the region and strictly shortens the distance,
	 * so an interior cell is never a minimiser. Ties resolve to the lowest position in the region's own cell array,
	 * and the boundary subset keeps that array's order, so the pair reported here is the same pair a full double
	 * loop over both regions would settle on.
	 */
	struct FCACorridorBoundaryIndex
	{
		/** Boundary cells in the source region's original order. */
		TArray<FIntPoint> Cells;

		void Build(const TArray<FIntPoint>& RegionCells, const TArray<int32>& RegionIds, int32 RegionId, int32 GridWidth, int32 GridHeight)
		{
			const bool			   bRegionIdsUsable = RegionIds.Num() == GridWidth * GridHeight;
			static constexpr int32 DX[] = { 1, -1, 0, 0 };
			static constexpr int32 DY[] = { 0, 0, 1, -1 };

			Cells.Reset();
			for (const FIntPoint& Cell : RegionCells)
			{
				bool bIsBoundary = !bRegionIdsUsable;

				for (int32 Dir = 0; Dir < 4 && !bIsBoundary; ++Dir)
				{
					const int32 NX = Cell.X + DX[Dir];
					const int32 NY = Cell.Y + DY[Dir];

					// Off the grid counts as "not this region", so grid-edge cells are always boundary.
					bIsBoundary = (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight) || RegionIds[NY * GridWidth + NX] != RegionId;
				}

				if (bIsBoundary)
				{
					Cells.Add(Cell);
				}
			}

			if (Cells.IsEmpty())
			{
				Cells = RegionCells;
			}

			bBucketsBuilt = false;
		}

		/** Nearest cell to From, ties to the lowest index in Cells. Returns false when the index holds no cells. */
		bool FindNearest(const FIntPoint& From, float& OutBestDistSq, int32& OutBestIndex)
		{
			OutBestDistSq = FLT_MAX;
			OutBestIndex = INDEX_NONE;

			if (Cells.IsEmpty())
			{
				return false;
			}

			// The boundary set has to be collected while RegionIds is still pre-carve, but the buckets over it are a
			// pure function of that set, so most substrates (every pair already diagram-adjacent, or the probability
			// roll rejecting every pair) never pay for them.
			if (!bBucketsBuilt)
			{
				BuildBuckets();
				bBucketsBuilt = true;
			}

			const int32 FromBucketX = FMath::FloorToInt(static_cast<double>(From.X - MinX) / BucketSize);
			const int32 FromBucketY = FMath::FloorToInt(static_cast<double>(From.Y - MinY) / BucketSize);

			const int32 MaxRing = FMath::Max(FMath::Max(FMath::Abs(FromBucketX), FMath::Abs(NumBucketsX - 1 - FromBucketX)),
				FMath::Max(FMath::Abs(FromBucketY), FMath::Abs(NumBucketsY - 1 - FromBucketY)));

			// From is usually outside this region's bucket grid entirely; the rings below that first ring hold no
			// buckets at all, so skipping straight to it keeps a distant query from sweeping empty rings.
			const int32 GapX = FMath::Max(0, FMath::Max(-FromBucketX, FromBucketX - (NumBucketsX - 1)));
			const int32 GapY = FMath::Max(0, FMath::Max(-FromBucketY, FromBucketY - (NumBucketsY - 1)));

			for (int32 Ring = FMath::Max(GapX, GapY); Ring <= MaxRing; ++Ring)
			{
				// Every cell in a bucket Ring steps away sits at least (Ring - 1) * BucketSize from From, so once that
				// bound passes the best distance found no further ring can beat it — or tie it, which matters because
				// a tie at a lower index would win. The slack keeps float rounding of a squared integer distance from
				// making a tie look one ulp farther than the bound.
				if (OutBestIndex != INDEX_NONE && Ring > 0)
				{
					const double RingFloor = static_cast<double>(Ring - 1) * BucketSize;
					if (RingFloor * RingFloor > static_cast<double>(OutBestDistSq) * 1.000001 + 1.0)
					{
						break;
					}
				}

				VisitRing(From, FromBucketX, FromBucketY, Ring, OutBestDistSq, OutBestIndex);
			}

			return OutBestIndex != INDEX_NONE;
		}

	private:
		/** Compressed bucket rows: BucketStart[b]..BucketStart[b+1] indexes BucketItems, which holds Cells indices. */
		TArray<int32> BucketStart;
		TArray<int32> BucketItems;
		int32		  MinX = 0;
		int32		  MinY = 0;
		int32		  BucketSize = 1;
		int32		  NumBucketsX = 1;
		int32		  NumBucketsY = 1;
		bool		  bBucketsBuilt = false;

		void BuildBuckets()
		{
			BucketStart.Reset();
			BucketItems.Reset();

			if (Cells.IsEmpty())
			{
				return;
			}

			int32 MaxX = Cells[0].X;
			int32 MaxY = Cells[0].Y;
			MinX = Cells[0].X;
			MinY = Cells[0].Y;
			for (const FIntPoint& Cell : Cells)
			{
				MinX = FMath::Min(MinX, Cell.X);
				MinY = FMath::Min(MinY, Cell.Y);
				MaxX = FMath::Max(MaxX, Cell.X);
				MaxY = FMath::Max(MaxY, Cell.Y);
			}

			// Aim for roughly one cell per bucket, then coarsen until the bucket array cannot outgrow the cell list
			// it indexes (a sparse ring of cells around a large empty middle would otherwise allocate by area).
			const int64 SpanX = static_cast<int64>(MaxX - MinX) + 1;
			const int64 SpanY = static_cast<int64>(MaxY - MinY) + 1;
			BucketSize = FMath::Max(1, FMath::FloorToInt(FMath::Sqrt(static_cast<double>(SpanX * SpanY) / Cells.Num())));

			const int64 BucketBudget = static_cast<int64>(Cells.Num()) * 4 + 64;
			NumBucketsX = static_cast<int32>(FMath::DivideAndRoundUp(SpanX, static_cast<int64>(BucketSize)));
			NumBucketsY = static_cast<int32>(FMath::DivideAndRoundUp(SpanY, static_cast<int64>(BucketSize)));

			while (static_cast<int64>(NumBucketsX) * NumBucketsY > BucketBudget)
			{
				BucketSize *= 2;
				NumBucketsX = static_cast<int32>(FMath::DivideAndRoundUp(SpanX, static_cast<int64>(BucketSize)));
				NumBucketsY = static_cast<int32>(FMath::DivideAndRoundUp(SpanY, static_cast<int64>(BucketSize)));
			}

			const int32 NumBuckets = NumBucketsX * NumBucketsY;
			BucketStart.Init(0, NumBuckets + 1);

			for (const FIntPoint& Cell : Cells)
			{
				++BucketStart[BucketIndexOf(Cell) + 1];
			}
			for (int32 Bucket = 0; Bucket < NumBuckets; ++Bucket)
			{
				BucketStart[Bucket + 1] += BucketStart[Bucket];
			}

			TArray<int32> FillCursor = BucketStart;
			BucketItems.SetNumUninitialized(Cells.Num());
			for (int32 Index = 0; Index < Cells.Num(); ++Index)
			{
				BucketItems[FillCursor[BucketIndexOf(Cells[Index])]++] = Index;
			}
		}

		int32 BucketIndexOf(const FIntPoint& Cell) const
		{
			const int32 BucketX = FMath::Clamp((Cell.X - MinX) / BucketSize, 0, NumBucketsX - 1);
			const int32 BucketY = FMath::Clamp((Cell.Y - MinY) / BucketSize, 0, NumBucketsY - 1);
			return BucketY * NumBucketsX + BucketX;
		}

		void VisitBucket(const FIntPoint& From, int32 BucketX, int32 BucketY, float& InOutBestDistSq, int32& InOutBestIndex) const
		{
			if (BucketX < 0 || BucketX >= NumBucketsX || BucketY < 0 || BucketY >= NumBucketsY)
			{
				return;
			}

			const int32 Bucket = BucketY * NumBucketsX + BucketX;
			for (int32 Slot = BucketStart[Bucket]; Slot < BucketStart[Bucket + 1]; ++Slot)
			{
				const int32		 Index = BucketItems[Slot];
				const FIntPoint& Cell = Cells[Index];

				// Same arithmetic and same float rounding as a plain double loop, so equal distances compare equal.
				const float DistSq = static_cast<float>(FMath::Square(From.X - Cell.X) + FMath::Square(From.Y - Cell.Y));
				if (DistSq < InOutBestDistSq || (DistSq == InOutBestDistSq && Index < InOutBestIndex))
				{
					InOutBestDistSq = DistSq;
					InOutBestIndex = Index;
				}
			}
		}

		void VisitRing(const FIntPoint& From, int32 CenterX, int32 CenterY, int32 Ring, float& InOutBestDistSq, int32& InOutBestIndex) const
		{
			if (Ring == 0)
			{
				VisitBucket(From, CenterX, CenterY, InOutBestDistSq, InOutBestIndex);
				return;
			}

			for (int32 BucketX = CenterX - Ring; BucketX <= CenterX + Ring; ++BucketX)
			{
				VisitBucket(From, BucketX, CenterY - Ring, InOutBestDistSq, InOutBestIndex);
				VisitBucket(From, BucketX, CenterY + Ring, InOutBestDistSq, InOutBestIndex);
			}
			for (int32 BucketY = CenterY - Ring + 1; BucketY <= CenterY + Ring - 1; ++BucketY)
			{
				VisitBucket(From, CenterX - Ring, BucketY, InOutBestDistSq, InOutBestIndex);
				VisitBucket(From, CenterX + Ring, BucketY, InOutBestDistSq, InOutBestIndex);
			}
		}
	};
} // namespace

UCellularAutomataGenerator2D::UCellularAutomataGenerator2D()
{
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
	FillProbability = 0.45f;
	Iterations = 5;
	BirthRule = { 6, 7, 8 };
	SurvivalRule = { 3, 4, 5 };
	MinRegionSize = 20;
	bKeepCenterRegion = true;
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
	return GenerateInternal().Diagram;
}

FCellularAutomataGridData UCellularAutomataGenerator2D::GenerateWithGridData()
{
	return GenerateInternal();
}

FCellularAutomataGridData UCellularAutomataGenerator2D::GenerateInternal()
{
	const double StartTime = FPlatformTime::Seconds();

	// Re-seed from the configured seed so a fixed seed yields identical output regardless of any prior
	// Generate() call on this instance (idempotent reuse).
	InitializeRandomStream();

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[CA] Generate() — Bounds=(%.1f,%.1f)-(%.1f,%.1f) GridSize=%d Seed='%s' FillProb=%.2f Iterations=%d MinRegion=%d KeepCenter=%s"),
		Bounds.Min.X,
		Bounds.Min.Y,
		Bounds.Max.X,
		Bounds.Max.Y,
		GridSize,
		*Seed,
		FillProbability,
		Iterations,
		MinRegionSize,
		bKeepCenterRegion ? TEXT("true") : TEXT("false"));

	const float BoundsWidth = Bounds.Max.X - Bounds.Min.X;
	const float BoundsHeight = Bounds.Max.Y - Bounds.Min.Y;

	if (BoundsWidth <= 0.0f || BoundsHeight <= 0.0f)
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[CA] Invalid bounds (%.1fx%.1f) — nothing to generate."), BoundsWidth, BoundsHeight);
		FCellularAutomataGridData EmptyResult;
		EmptyResult.CenterRegionId = -1;
		EmptyResult.GridWidth = 0;
		EmptyResult.GridHeight = 0;
		EmptyResult.CellSize = static_cast<float>(GridSize);
		return EmptyResult;
	}

	bool bDegradedResolution = false;

	// The pitch this run generates at. It is deliberately a local: coarsening is a property of this call's
	// bounds, and writing it back to the GridSize UPROPERTY would silently coarsen every later run on the
	// same instance, including ones whose bounds fit the authored pitch.
	int32 EffectiveGridSize = GridSize;

	// Enlarge the cell size so the grid fits the cell budget rather than refusing to generate. Solving
	// (W/c)(H/c) <= MaxGridCells for c gives c >= sqrt(W*H / MaxGridCells).
	if (static_cast<int64>(FMath::CeilToInt(BoundsWidth / static_cast<float>(EffectiveGridSize)))
			* static_cast<int64>(FMath::CeilToInt(BoundsHeight / static_cast<float>(EffectiveGridSize)))
		> PGGrid::MaxGridCells)
	{
		const double MinCellSize =
			FMath::Sqrt(static_cast<double>(BoundsWidth) * static_cast<double>(BoundsHeight) / static_cast<double>(PGGrid::MaxGridCells));
		EffectiveGridSize = FMath::Max(EffectiveGridSize, FMath::CeilToInt(MinCellSize));
		bDegradedResolution = true;

		const int32 DegradedWidth = FMath::CeilToInt(BoundsWidth / static_cast<float>(EffectiveGridSize));
		const int32 DegradedHeight = FMath::CeilToInt(BoundsHeight / static_cast<float>(EffectiveGridSize));
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[CA] Cell budget exceeded: GridSize %d would produce >%lld cells; degrading to GridSize %d (%dx%d)."),
			GridSize,
			PGGrid::MaxGridCells,
			EffectiveGridSize,
			DegradedWidth,
			DegradedHeight);
	}

	const float CellSizeVal = static_cast<float>(EffectiveGridSize);

	const int32 GWidth = FMath::CeilToInt(BoundsWidth / CellSizeVal);
	const int32 GHeight = FMath::CeilToInt(BoundsHeight / CellSizeVal);

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Grid dimensions: %dx%d (%d total cells)"), GWidth, GHeight, GWidth * GHeight);

	const int32 TotalCells = GWidth * GHeight;

	TArray<bool> Grid;
	Grid.Init(false, TotalCells);

	for (int32 Y = 0; Y < GHeight; ++Y)
	{
		for (int32 X = 0; X < GWidth; ++X)
		{
			if (X == 0 || X == GWidth - 1 || Y == 0 || Y == GHeight - 1)
			{
				continue;
			}
			Grid[Y * GWidth + X] = (RandomStream.FRand() >= FillProbability);
		}
	}

	const uint16 BirthMask = RuleToBitmask(BirthRule);
	const uint16 SurvivalMask = RuleToBitmask(SurvivalRule);

	TArray<bool> NewGrid;
	NewGrid.SetNum(TotalCells);

	for (int32 Iter = 0; Iter < Iterations; ++Iter)
	{
		for (int32 Y = 0; Y < GHeight; ++Y)
		{
			for (int32 X = 0; X < GWidth; ++X)
			{
				const int32 Index = Y * GWidth + X;

				if (X == 0 || X == GWidth - 1 || Y == 0 || Y == GHeight - 1)
				{
					NewGrid[Index] = false;
					continue;
				}

				const int32 WallNeighbors = CountWallNeighbors(Grid, X, Y, GWidth, GHeight);
				const bool	bIsWall = !Grid[Index];

				if (bIsWall)
				{
					NewGrid[Index] = ((SurvivalMask >> WallNeighbors) & 1) ? false : true;
				}
				else
				{
					NewGrid[Index] = ((BirthMask >> WallNeighbors) & 1) ? false : true;
				}
			}
		}

		Swap(Grid, NewGrid);
	}

	{
		int32 FloorCount = 0;
		for (bool bIsFloor : Grid)
		{
			if (bIsFloor)
			{
				++FloorCount;
			}
		}
		UE_LOG(LogRoguelikeGeometry,
			Log,
			TEXT("[CA] After %d iterations: %d floor cells (%.1f%% of grid)"),
			Iterations,
			FloorCount,
			100.0f * FloorCount / TotalCells);
	}

	TArray<int32>			  RegionIds;
	TArray<TArray<FIntPoint>> Regions;
	int32					  CenterRegionId = -1;
	const int32				  CenterX = GWidth / 2;
	const int32				  CenterY = GHeight / 2;

	FloodFillRegions(Grid, GWidth, GHeight, CenterX, CenterY, RegionIds, Regions, CenterRegionId);

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Flood-fill found %d regions, center region=%d"), Regions.Num(), CenterRegionId);

	// When bKeepCenterRegion is set but the exact center cell is a wall, fall back to the region
	// nearest the center so culling always has a target to preserve.
	if (bKeepCenterRegion && CenterRegionId < 0 && Regions.Num() > 0)
	{
		const FVector2D CenterWorld(Bounds.Min.X + (CenterX + 0.5f) * CellSizeVal, Bounds.Min.Y + (CenterY + 0.5f) * CellSizeVal);
		float			BestDistSq = FLT_MAX;
		int32			BestRegion = 0;
		for (int32 RegionId = 0; RegionId < Regions.Num(); ++RegionId)
		{
			for (const FIntPoint& Cell : Regions[RegionId])
			{
				const FVector2D CellWorld(Bounds.Min.X + (Cell.X + 0.5f) * CellSizeVal, Bounds.Min.Y + (Cell.Y + 0.5f) * CellSizeVal);
				const float		DistSq = FVector2D::DistSquared(CellWorld, CenterWorld);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestRegion = RegionId;
				}
			}
		}
		CenterRegionId = BestRegion;
		UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Center cell is wall — KeepCenterRegion fallback to nearest region %d"), CenterRegionId);
	}

	TArray<bool> SurvivingRegions;
	SurvivingRegions.Init(true, Regions.Num());

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
				Grid[Cell.Y * GWidth + Cell.X] = false;
			}
			SurvivingRegions[RegionId] = false;
			++CulledCount;
		}
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[CA] Culled %d regions below MinRegionSize=%d, %d surviving"),
		CulledCount,
		MinRegionSize,
		Regions.Num() - CulledCount);

	FLayoutDiagram2D Diagram = BuildDiagramFromRegions(Grid, RegionIds, Regions, CenterRegionId, GWidth, GHeight, CellSizeVal);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] Generate() complete: %d cells in %.2fms"), Diagram.Cells.Num(), ElapsedMs);

	FCellularAutomataGridData Result;
	Result.Grid = MoveTemp(Grid);
	Result.RegionIds = MoveTemp(RegionIds);
	Result.Regions = MoveTemp(Regions);
	Result.SurvivingRegions = MoveTemp(SurvivingRegions);
	Result.CenterRegionId = CenterRegionId;
	Result.GridWidth = GWidth;
	Result.GridHeight = GHeight;
	Result.CellSize = CellSizeVal;
	Result.bDegradedResolution = bDegradedResolution;
	Result.Diagram = MoveTemp(Diagram);

	return Result;
}

void UCellularAutomataGenerator2D::CarveCorridors(FCellularAutomataGridData& GridData, float Probability, int32 Width, FRandomStream& InRandomStream)
{
	if (Probability <= 0.0f)
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[CA] CarveCorridors: probability=0, skipping"));
		return;
	}

	Width = FMath::Max(1, Width);

	// Identify surviving region indices
	TArray<int32> SurvivingIds;
	for (int32 i = 0; i < GridData.SurvivingRegions.Num(); ++i)
	{
		if (GridData.SurvivingRegions[i])
		{
			SurvivingIds.Add(i);
		}
	}

	if (SurvivingIds.Num() < 2)
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[CA] CarveCorridors: fewer than 2 surviving regions, nothing to connect"));
		return;
	}

	// Built while RegionIds still describes the pre-carve layout, which is also what GridData.Regions holds: the
	// carve below rewrites RegionIds for the cells it opens but never touches Regions, so the pair search must keep
	// reading the original membership.
	TArray<FCACorridorBoundaryIndex> BoundaryIndexes;
	BoundaryIndexes.SetNum(SurvivingIds.Num());
	for (int32 Slot = 0; Slot < SurvivingIds.Num(); ++Slot)
	{
		const int32 RegionId = SurvivingIds[Slot];
		if (GridData.Regions.IsValidIndex(RegionId))
		{
			BoundaryIndexes[Slot].Build(GridData.Regions[RegionId], GridData.RegionIds, RegionId, GridData.GridWidth, GridData.GridHeight);
		}
	}

	// Build neighbor set from current diagram for surviving regions
	TMap<int32, int32> RegionToDiagramCell;
	for (int32 CellIdx = 0; CellIdx < GridData.Diagram.Cells.Num(); ++CellIdx)
	{
		// Map by finding which surviving region index this diagram cell corresponds to
		// The diagram cells are ordered by surviving region (same order as BuildDiagramFromRegions)
		if (CellIdx < SurvivingIds.Num())
		{
			RegionToDiagramCell.Add(SurvivingIds[CellIdx], CellIdx);
		}
	}

	// Find disconnected pairs (surviving regions that are NOT neighbors in the diagram)
	TSet<TPair<int32, int32>> ConnectedPairs;
	for (int32 CellIdx = 0; CellIdx < GridData.Diagram.Cells.Num(); ++CellIdx)
	{
		for (int32 NeighborIdx : GridData.Diagram.Cells[CellIdx].Neighbors)
		{
			int32 MinIdx = FMath::Min(CellIdx, NeighborIdx);
			int32 MaxIdx = FMath::Max(CellIdx, NeighborIdx);
			ConnectedPairs.Add(TPair<int32, int32>(MinIdx, MaxIdx));
		}
	}

	int32 CorridorsCarved = 0;

	for (int32 a = 0; a < SurvivingIds.Num(); ++a)
	{
		for (int32 b = a + 1; b < SurvivingIds.Num(); ++b)
		{
			const int32* CellA = RegionToDiagramCell.Find(SurvivingIds[a]);
			const int32* CellB = RegionToDiagramCell.Find(SurvivingIds[b]);

			if (!CellA || !CellB)
			{
				continue;
			}

			int32 MinCell = FMath::Min(*CellA, *CellB);
			int32 MaxCell = FMath::Max(*CellA, *CellB);

			if (ConnectedPairs.Contains(TPair<int32, int32>(MinCell, MaxCell)))
			{
				// Already connected — skip
				continue;
			}

			// Probabilistic check
			if (InRandomStream.FRand() > Probability)
			{
				continue;
			}

			// Find nearest cells between the two regions
			const int32						RegionIdA = SurvivingIds[a];
			const FCACorridorBoundaryIndex& IndexA = BoundaryIndexes[a];
			FCACorridorBoundaryIndex&		IndexB = BoundaryIndexes[b];

			// First strictly-closest pair in region order: each A cell contributes its own nearest B cell (ties to
			// the lowest B position), and only a strict improvement replaces the running best.
			float	  BestDistSq = FLT_MAX;
			FIntPoint BestA(0, 0);
			FIntPoint BestB(0, 0);

			for (const FIntPoint& CellPtA : IndexA.Cells)
			{
				float CandidateDistSq = FLT_MAX;
				int32 CandidateIndex = INDEX_NONE;

				if (IndexB.FindNearest(CellPtA, CandidateDistSq, CandidateIndex) && CandidateDistSq < BestDistSq)
				{
					BestDistSq = CandidateDistSq;
					BestA = CellPtA;
					BestB = IndexB.Cells[CandidateIndex];
				}
			}

			// Carve a straight-line corridor from BestA to BestB
			const int32 HalfWidth = Width / 2;

			// Bresenham-like line from BestA to BestB
			int32		X0 = BestA.X, Y0 = BestA.Y;
			int32		X1 = BestB.X, Y1 = BestB.Y;
			const int32 DX = FMath::Abs(X1 - X0);
			const int32 DY = -FMath::Abs(Y1 - Y0);
			const int32 SX = X0 < X1 ? 1 : -1;
			const int32 SY = Y0 < Y1 ? 1 : -1;
			int32		Err = DX + DY;

			while (true)
			{
				// Carve a band of width cells centered on (X0, Y0)
				for (int32 OffY = -HalfWidth; OffY <= HalfWidth; ++OffY)
				{
					for (int32 OffX = -HalfWidth; OffX <= HalfWidth; ++OffX)
					{
						const int32 CX = X0 + OffX;
						const int32 CY = Y0 + OffY;

						// Stay within bounds, keep boundary walls intact
						if (CX > 0 && CX < GridData.GridWidth - 1 && CY > 0 && CY < GridData.GridHeight - 1)
						{
							const int32 Idx = CY * GridData.GridWidth + CX;
							if (!GridData.Grid[Idx])
							{
								GridData.Grid[Idx] = true;
								GridData.RegionIds[Idx] = RegionIdA;
							}
						}
					}
				}

				if (X0 == X1 && Y0 == Y1)
				{
					break;
				}

				const int32 E2 = 2 * Err;
				if (E2 >= DY)
				{
					Err += DY;
					X0 += SX;
				}
				if (E2 <= DX)
				{
					Err += DX;
					Y0 += SY;
				}
			}

			++CorridorsCarved;
		}
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[CA] CarveCorridors: carved %d corridors (probability=%.2f, width=%d)"),
		CorridorsCarved,
		Probability,
		Width);
}

void UCellularAutomataGenerator2D::RebuildDiagram(FCellularAutomataGridData& GridData)
{
	UE_LOG(LogRoguelikeGeometry,
		Verbose,
		TEXT("[CA] RebuildDiagram: rebuilding diagram from modified grid (%dx%d)"),
		GridData.GridWidth,
		GridData.GridHeight);

	const int32 CenterX = GridData.GridWidth / 2;
	const int32 CenterY = GridData.GridHeight / 2;

	int32 NewCenterRegionId = -1;
	FloodFillRegions(
		GridData.Grid, GridData.GridWidth, GridData.GridHeight, CenterX, CenterY, GridData.RegionIds, GridData.Regions, NewCenterRegionId);

	GridData.CenterRegionId = NewCenterRegionId;

	// GridData.CellSize, not GridSize: a grid that came back coarsened would otherwise be rebuilt at the authored
	// pitch and its polygons would no longer line up with the cells they were traced from.
	GridData.Diagram = BuildDiagramFromRegions(
		GridData.Grid, GridData.RegionIds, GridData.Regions, GridData.CenterRegionId, GridData.GridWidth, GridData.GridHeight, GridData.CellSize);

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[CA] RebuildDiagram: produced %d cells"), GridData.Diagram.Cells.Num());
}

FLayoutDiagram2D UCellularAutomataGenerator2D::BuildDiagramFromRegions(const TArray<bool>& Grid,
	const TArray<int32>&																   RegionIds,
	const TArray<TArray<FIntPoint>>&													   Regions,
	int32																				   CenterRegionId,
	int32																				   InGridWidth,
	int32																				   InGridHeight,
	float																				   InCellSize)
{
	WarnIfSeedSubstituted();

	const float CellSize = InCellSize;

	// Stage 1: Identify surviving regions
	TMap<int32, int32> RegionToCellIndex;
	TArray<int32>	   SurvivingRegionIds;

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
		const int32				 RegionId = SurvivingRegionIds[i];
		const TArray<FIntPoint>& Region = Regions[RegionId];

		FLayoutCell2D Cell;
		Cell.CellIndex = i;
		Cell.Vertices = TraceBoundaryPolygon(Region, RegionIds, RegionId, InGridWidth, InGridHeight, CellSize);

		UE_LOG(
			LogRoguelikeGeometry, Verbose, TEXT("[CA] Region %d: %d grid cells, %d boundary vertices"), RegionId, Region.Num(), Cell.Vertices.Num());

		// Compute center as arithmetic mean of constituent cell centers
		FVector2D CenterSum = FVector2D::ZeroVector;

		for (const FIntPoint& GridCell : Region)
		{
			CenterSum += FVector2D(Bounds.Min.X + (GridCell.X + 0.5f) * CellSize, Bounds.Min.Y + (GridCell.Y + 0.5f) * CellSize);
		}

		Cell.Center = CenterSum / static_cast<float>(Region.Num());

		// One layout cell is a whole cave lobe here, and the CA forces its outermost ring to wall at seed, at every
		// iteration and while carving corridors, so no lobe can reach the raster edge. Exterior is a per-grid-cell
		// concept on the raster path and a bounds-contact concept on the Voronoi path; on this one it is always false.
		Cell.bIsExterior = false;

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
					const int32	 NRegionId = RegionIds[NY * InGridWidth + NX];
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
	const TArray<FIntPoint>& Region, const TArray<int32>& RegionIds, int32 RegionId, int32 InGridWidth, int32 InGridHeight, float InCellSize) const
{
	// Collect directed boundary edges in grid corner coordinates (CCW, interior on left).
	// A region can pinch to a single corner, emitting two outgoing edges from it, so allow multiple
	// outgoing edges per start corner (multimap) and disambiguate during chaining.
	TMultiMap<FIntPoint, FIntPoint> EdgeMap;

	auto IsOutsideRegion = [&](int32 NX, int32 NY) -> bool {
		if (NX < 0 || NX >= InGridWidth || NY < 0 || NY >= InGridHeight)
		{
			return true;
		}
		return RegionIds[NY * InGridWidth + NX] != RegionId;
	};

	auto TryAddEdge = [&](const FIntPoint& Start, const FIntPoint& End) {
		// Keep both edges from a pinched corner; only skip an exact duplicate of the same directed edge.
		TArray<FIntPoint> Existing;
		EdgeMap.MultiFind(Start, Existing);
		if (!Existing.Contains(End))
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

	// Chaining reads a lexicographically sorted edge list rather than the multimap: which loop a corner ends up in
	// must not depend on hash-bucket order.
	TArray<TPair<FIntPoint, FIntPoint>> Edges;
	Edges.Reserve(EdgeMap.Num());
	for (const TPair<FIntPoint, FIntPoint>& Edge : EdgeMap)
	{
		Edges.Add(Edge);
	}
	Edges.Sort([](const TPair<FIntPoint, FIntPoint>& A, const TPair<FIntPoint, FIntPoint>& B) {
		if (A.Key.X != B.Key.X)
		{
			return A.Key.X < B.Key.X;
		}
		if (A.Key.Y != B.Key.Y)
		{
			return A.Key.Y < B.Key.Y;
		}
		if (A.Value.X != B.Value.X)
		{
			return A.Value.X < B.Value.X;
		}
		return A.Value.Y < B.Value.Y;
	});

	TMap<FIntPoint, TArray<int32, TInlineAllocator<2>>> OutgoingByCorner;
	for (int32 EdgeIdx = 0; EdgeIdx < Edges.Num(); ++EdgeIdx)
	{
		OutgoingByCorner.FindOrAdd(Edges[EdgeIdx].Key).Add(EdgeIdx);
	}

	// Every boundary edge is one unit axis step, so a direction is one of four; numbering them counter-clockwise
	// turns "which way does this edge leave the corner" into arithmetic.
	auto DirectionIndex = [](const FIntPoint& Step) -> int32 {
		if (Step.X > 0)
		{
			return 0; // +X
		}
		if (Step.Y > 0)
		{
			return 1; // +Y
		}
		if (Step.X < 0)
		{
			return 2; // -X
		}
		return 3; // -Y
	};

	TArray<TArray<FIntPoint>> Loops;
	TArray<bool>			  bEdgeUsed;
	bEdgeUsed.Init(false, Edges.Num());

	for (int32 SeedEdge = 0; SeedEdge < Edges.Num(); ++SeedEdge)
	{
		if (bEdgeUsed[SeedEdge])
		{
			continue;
		}

		const FIntPoint	  Start = Edges[SeedEdge].Key;
		TArray<FIntPoint> Loop;
		int32			  CurrentEdge = SeedEdge;
		bool			  bClosed = false;

		while (true)
		{
			bEdgeUsed[CurrentEdge] = true;
			Loop.Add(Edges[CurrentEdge].Key);

			const FIntPoint Corner = Edges[CurrentEdge].Value;
			if (Corner == Start)
			{
				bClosed = true;
				break;
			}

			const TArray<int32, TInlineAllocator<2>>* Candidates = OutgoingByCorner.Find(Corner);
			if (!Candidates)
			{
				break;
			}

			// Two cells of one region can meet at a single corner while the void outside and a void enclosed by the
			// region meet at the same point; the corner then carries two outgoing edges and pairing them the wrong
			// way chains the outer boundary and the hole into one keyhole loop. Standard planar face traversal picks
			// the pairing: from the reversed incoming direction, the first outgoing edge counter-clockwise is the
			// sharpest turn that keeps the region's interior on the left, which is the winding these edges are
			// emitted with. A U-turn back along the incoming edge sorts last so a chain is never stranded.
			const FIntPoint Incoming = Corner - Edges[CurrentEdge].Key;
			const int32		ReverseDir = DirectionIndex(FIntPoint(-Incoming.X, -Incoming.Y));

			int32 NextEdge = INDEX_NONE;
			int32 BestRank = MAX_int32;
			for (const int32 CandidateIdx : *Candidates)
			{
				if (bEdgeUsed[CandidateIdx])
				{
					continue;
				}

				const FIntPoint Step = Edges[CandidateIdx].Value - Edges[CandidateIdx].Key;
				const int32		Offset = (DirectionIndex(Step) - ReverseDir + 4) % 4;
				const int32		Rank = (Offset == 0) ? 4 : Offset;
				if (Rank < BestRank)
				{
					BestRank = Rank;
					NextEdge = CandidateIdx;
				}
			}

			if (NextEdge == INDEX_NONE)
			{
				break;
			}

			CurrentEdge = NextEdge;
		}

		if (bClosed && Loop.Num() >= 3)
		{
			Loops.Add(MoveTemp(Loop));
		}
	}

	TArray<FVector2D> Result;

	if (Loops.Num() > 0)
	{
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

		Result = SimplifyAndConvert(Loops[BestLoopIndex], InCellSize);
	}

	if (Result.Num() < 3)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[CA] Region %d traced a degenerate boundary (%d vertices); it keeps its diagram cell but contributes no footprint polygon."),
			RegionId,
			Result.Num());
		return TArray<FVector2D>();
	}

	return Result;
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
	const int32		  N = Loop.Num();

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
