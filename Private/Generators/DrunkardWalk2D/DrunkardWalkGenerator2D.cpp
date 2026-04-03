#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

#include "ProceduralGeometry.h"

UDrunkardWalkGenerator2D::UDrunkardWalkGenerator2D()
{
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
	WalkLength = 500;
	NumWalkers = 1;
	BranchProbability = 0.0f;
	CorridorWidth = 1;
	RoomChance = 0.0f;
	RoomRadiusMin = 1;
	RoomRadiusMax = 1;
	MinRoomSpacing = 0;
	MaxRoomCount = 0;
	DirectionalMomentum = 0.0f;
	ExplorationBias = 0.0f;
	InitializeRandomStream();
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Super::SetBounds(InBounds);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetSeed(const FString& InSeed)
{
	Super::SetSeed(InSeed);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetGridSize(int32 InSize)
{
	Super::SetGridSize(InSize);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCenter(const FVector2D& InCenter)
{
	Super::SetCenter(InCenter);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetWalkLength(int32 InWalkLength)
{
	WalkLength = FMath::Max(1, InWalkLength);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetNumWalkers(int32 InNumWalkers)
{
	NumWalkers = FMath::Max(1, InNumWalkers);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetBranchProbability(float InProbability)
{
	BranchProbability = FMath::Clamp(InProbability, 0.0f, 1.0f);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCorridorWidth(int32 InWidth)
{
	CorridorWidth = FMath::Max(1, InWidth);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetRoomChance(float InChance)
{
	RoomChance = FMath::Clamp(InChance, 0.0f, 1.0f);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetRoomRadius(int32 InRadius)
{
	RoomRadiusMin = FMath::Max(1, InRadius);
	RoomRadiusMax = RoomRadiusMin;
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetRoomRadiusRange(int32 InMin, int32 InMax)
{
	RoomRadiusMin = FMath::Max(1, InMin);
	RoomRadiusMax = FMath::Max(RoomRadiusMin, InMax);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetMinRoomSpacing(int32 InSpacing)
{
	MinRoomSpacing = FMath::Max(0, InSpacing);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetMaxRoomCount(int32 InCount)
{
	MaxRoomCount = FMath::Max(0, InCount);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetDirectionalMomentum(float InMomentum)
{
	DirectionalMomentum = FMath::Clamp(InMomentum, 0.0f, 1.0f);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetExplorationBias(float InBias)
{
	ExplorationBias = FMath::Clamp(InBias, 0.0f, 1.0f);
	return this;
}

FLayoutDiagram2D UDrunkardWalkGenerator2D::Generate()
{
	return GenerateInternal().Diagram;
}

FDrunkardWalkGridData UDrunkardWalkGenerator2D::GenerateWithGridData()
{
	return GenerateInternal();
}

FDrunkardWalkGridData UDrunkardWalkGenerator2D::GenerateInternal()
{
	const double StartTime = FPlatformTime::Seconds();

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[DW] Generate() — Bounds=(%.1f,%.1f)-(%.1f,%.1f) GridSize=%d Seed='%s' WalkLength=%d Walkers=%d BranchProb=%.2f "
			 "CorridorWidth=%d RoomChance=%.2f RoomRadius=[%d,%d] MinRoomSpacing=%d MaxRoomCount=%d Momentum=%.2f ExplorationBias=%.2f"),
		Bounds.Min.X,
		Bounds.Min.Y,
		Bounds.Max.X,
		Bounds.Max.Y,
		GridSize,
		*Seed,
		WalkLength,
		NumWalkers,
		BranchProbability,
		CorridorWidth,
		RoomChance,
		RoomRadiusMin,
		RoomRadiusMax,
		MinRoomSpacing,
		MaxRoomCount,
		DirectionalMomentum,
		ExplorationBias);

	const float CellSizeVal = static_cast<float>(GridSize);
	const float BoundsWidth = Bounds.Max.X - Bounds.Min.X;
	const float BoundsHeight = Bounds.Max.Y - Bounds.Min.Y;

	const int32 GWidth = FMath::CeilToInt(BoundsWidth / CellSizeVal);
	const int32 GHeight = FMath::CeilToInt(BoundsHeight / CellSizeVal);

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Grid dimensions: %dx%d (%d total cells)"), GWidth, GHeight, GWidth * GHeight);

	// OOM guard
	if (GWidth <= 0 || GHeight <= 0 || (int64)GWidth * GHeight > 4'194'304)
	{
		UE_LOG(LogRoguelikeGeometry, Error, TEXT("[DW] OOM guard triggered: %dx%d exceeds limit"), GWidth, GHeight);
		FDrunkardWalkGridData EmptyResult;
		EmptyResult.CenterRegionId = -1;
		EmptyResult.GridWidth = 0;
		EmptyResult.GridHeight = 0;
		EmptyResult.CellSize = CellSizeVal;
		return EmptyResult;
	}

	const int32 TotalCells = GWidth * GHeight;

	// Initialize grid (all walls) and cell type array
	TArray<bool> Grid;
	Grid.Init(false, TotalCells);

	TArray<uint8> CellType;
	CellType.Init(EDrunkardWalkCellType::Wall, TotalCells);

	// Place walkers at grid center
	const int32 CenterX = GWidth / 2;
	const int32 CenterY = GHeight / 2;

	struct FWalker
	{
		int32			  X;
		int32			  Y;
		int32			  LastDirection = -1; // -1 = no previous direction (first step or branched)
		TArray<FIntPoint> Path;
	};

	TArray<FWalker> Walkers;
	Walkers.Reserve(NumWalkers);
	for (int32 i = 0; i < NumWalkers; ++i)
	{
		FWalker NewWalker;
		NewWalker.X = CenterX;
		NewWalker.Y = CenterY;
		NewWalker.Path.Reserve(WalkLength);
		NewWalker.Path.Add(FIntPoint(CenterX, CenterY));
		Walkers.Add(MoveTemp(NewWalker));
	}

	// Carve initial position
	CarveCell(Grid, CellType, CenterX, CenterY, GWidth, GHeight);

	// Cardinal directions: right, left, up, down
	const int32 DX[] = { 1, -1, 0, 0 };
	const int32 DY[] = { 0, 0, 1, -1 };

	// Walk loop
	int32			  CarvedCount = 0;
	TArray<FIntPoint> PlacedRoomCenters;
	for (int32 Step = 0; Step < WalkLength && Walkers.Num() > 0; ++Step)
	{
		// Pick one random active walker
		const int32 WalkerIndex = RandomStream.RandRange(0, Walkers.Num() - 1);
		FWalker&	Walker = Walkers[WalkerIndex];

		// Direction selection — either original retry loop (at defaults) or weighted selection
		bool bValidMove = false;

		const bool bUseWeightedSelection = (DirectionalMomentum > 0.0f || ExplorationBias > 0.0f);

		if (!bUseWeightedSelection)
		{
			// Original retry loop: preserves exact seed-to-layout mapping at defaults
			for (int32 Retry = 0; Retry < 4; ++Retry)
			{
				const int32 Dir = RandomStream.RandRange(0, 3);
				const int32 NewX = Walker.X + DX[Dir];
				const int32 NewY = Walker.Y + DY[Dir];
				if (NewX >= 1 && NewX <= GWidth - 2 && NewY >= 1 && NewY <= GHeight - 2)
				{
					Walker.X = NewX;
					Walker.Y = NewY;
					Walker.LastDirection = Dir;
					bValidMove = true;
					break;
				}
			}
		}
		else
		{
			// Step 1: Momentum check — attempt to continue in last direction
			if (DirectionalMomentum > 0.0f && Walker.LastDirection >= 0)
			{
				const float Roll = RandomStream.FRand();
				if (Roll < DirectionalMomentum)
				{
					const int32 NewX = Walker.X + DX[Walker.LastDirection];
					const int32 NewY = Walker.Y + DY[Walker.LastDirection];
					if (NewX >= 1 && NewX <= GWidth - 2 && NewY >= 1 && NewY <= GHeight - 2)
					{
						Walker.X = NewX;
						Walker.Y = NewY;
						// LastDirection stays the same
						bValidMove = true;
					}
					// else: momentum direction blocked by boundary, fall through to weighted selection
				}
			}

			// Step 2: Weighted direction selection (if momentum didn't resolve)
			if (!bValidMove)
			{
				float Weights[4];
				float TotalWeight = 0.0f;

				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					const int32 NewX = Walker.X + DX[Dir];
					const int32 NewY = Walker.Y + DY[Dir];

					if (NewX < 1 || NewX > GWidth - 2 || NewY < 1 || NewY > GHeight - 2)
					{
						Weights[Dir] = 0.0f;
					}
					else if (!Grid[NewY * GWidth + NewX])
					{
						// Uncarved cell — full weight
						Weights[Dir] = 1.0f;
					}
					else
					{
						// Already carved — reduced weight based on exploration bias
						Weights[Dir] = 1.0f - ExplorationBias;
					}

					TotalWeight += Weights[Dir];
				}

				if (TotalWeight <= 0.0f)
				{
					// Degenerate case: all directions blocked or all carved at max bias
					// Fall back to uniform random among in-bounds directions
					int32 InBoundsDirs[4];
					int32 NumInBounds = 0;
					for (int32 Dir = 0; Dir < 4; ++Dir)
					{
						const int32 NewX = Walker.X + DX[Dir];
						const int32 NewY = Walker.Y + DY[Dir];
						if (NewX >= 1 && NewX <= GWidth - 2 && NewY >= 1 && NewY <= GHeight - 2)
						{
							InBoundsDirs[NumInBounds++] = Dir;
						}
					}

					if (NumInBounds > 0)
					{
						const int32 PickedDir = InBoundsDirs[RandomStream.RandRange(0, NumInBounds - 1)];
						Walker.X += DX[PickedDir];
						Walker.Y += DY[PickedDir];
						Walker.LastDirection = PickedDir;
						bValidMove = true;
					}
				}
				else
				{
					// Weighted random pick
					float Pick = RandomStream.FRandRange(0.0f, TotalWeight);
					for (int32 Dir = 0; Dir < 4; ++Dir)
					{
						Pick -= Weights[Dir];
						if (Pick <= 0.0f)
						{
							Walker.X += DX[Dir];
							Walker.Y += DY[Dir];
							Walker.LastDirection = Dir;
							bValidMove = true;
							break;
						}
					}
				}
			}
		}

		// If no valid direction found (walker cornered on tiny grid), skip this step
		if (!bValidMove)
		{
			UE_LOG(LogRoguelikeGeometry,
				Verbose,
				TEXT("[DW] Step %d: walker %d cornered at (%d,%d), skipping step"),
				Step,
				WalkerIndex,
				Walker.X,
				Walker.Y);
			continue;
		}

		// Record position in walker path
		Walker.Path.Add(FIntPoint(Walker.X, Walker.Y));

		// Carve corridor or room
		if (RoomChance > 0.0f && RandomStream.FRand() < RoomChance)
		{
			bool bCanPlace = true;

			// Count gate
			if (MaxRoomCount > 0 && PlacedRoomCenters.Num() >= MaxRoomCount)
			{
				bCanPlace = false;
				UE_LOG(LogRoguelikeGeometry,
					Verbose,
					TEXT("[DW] Step %d: room skipped at (%d,%d) — count cap reached (%d/%d)"),
					Step,
					Walker.X,
					Walker.Y,
					PlacedRoomCenters.Num(),
					MaxRoomCount);
			}

			// Spacing gate
			if (bCanPlace && MinRoomSpacing > 0)
			{
				for (const FIntPoint& Existing : PlacedRoomCenters)
				{
					const int32 Dist = FMath::Abs(Walker.X - Existing.X) + FMath::Abs(Walker.Y - Existing.Y);
					if (Dist < MinRoomSpacing)
					{
						bCanPlace = false;
						UE_LOG(LogRoguelikeGeometry,
							Verbose,
							TEXT("[DW] Step %d: room skipped at (%d,%d) — too close to existing room at (%d,%d), dist=%d < spacing=%d"),
							Step,
							Walker.X,
							Walker.Y,
							Existing.X,
							Existing.Y,
							Dist,
							MinRoomSpacing);
						break;
					}
				}
			}

			if (bCanPlace)
			{
				const int32 Radius = RandomStream.RandRange(RoomRadiusMin, RoomRadiusMax);
				CarveRoom(Grid, CellType, Walker.X, Walker.Y, Radius, GWidth, GHeight);
				PlacedRoomCenters.Add(FIntPoint(Walker.X, Walker.Y));
				UE_LOG(LogRoguelikeGeometry,
					Verbose,
					TEXT("[DW] Step %d: carved room at (%d,%d) radius=%d (total rooms=%d)"),
					Step,
					Walker.X,
					Walker.Y,
					Radius,
					PlacedRoomCenters.Num());
			}
			else
			{
				CarveCell(Grid, CellType, Walker.X, Walker.Y, GWidth, GHeight);
			}
		}
		else
		{
			CarveCell(Grid, CellType, Walker.X, Walker.Y, GWidth, GHeight);
		}
		++CarvedCount;

		// Branch: spawn new walker at current position
		if (BranchProbability > 0.0f && RandomStream.FRand() < BranchProbability)
		{
			FWalker BranchedWalker;
			BranchedWalker.X = Walker.X;
			BranchedWalker.Y = Walker.Y;
			BranchedWalker.Path.Reserve(WalkLength - Step);
			BranchedWalker.Path.Add(FIntPoint(Walker.X, Walker.Y));
			Walkers.Add(MoveTemp(BranchedWalker));
			UE_LOG(LogRoguelikeGeometry,
				Verbose,
				TEXT("[DW] Step %d: branched new walker at (%d,%d), total walkers=%d"),
				Step,
				Walker.X,
				Walker.Y,
				Walkers.Num());
		}
	}

	// Count floor cells
	int32 FloorCells = 0;
	for (bool bIsFloor : Grid)
	{
		if (bIsFloor)
		{
			++FloorCells;
		}
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[DW] Walk complete: %d steps, %d floor cells carved, %d rooms placed"),
		CarvedCount,
		FloorCells,
		PlacedRoomCenters.Num());

	// Collect walker paths before walkers go out of scope
	TArray<TArray<FIntPoint>> WalkerPaths;
	WalkerPaths.Reserve(Walkers.Num());
	for (FWalker& Walker : Walkers)
	{
		WalkerPaths.Add(MoveTemp(Walker.Path));
	}

	// Flood-fill: iterative BFS to identify connected floor regions (same algorithm as CA)
	TArray<int32> RegionIds;
	RegionIds.Init(-1, TotalCells);

	TArray<TArray<FIntPoint>> Regions;
	int32					  CenterRegionId = -1;

	for (int32 Y = 0; Y < GHeight; ++Y)
	{
		for (int32 X = 0; X < GWidth; ++X)
		{
			const int32 Index = Y * GWidth + X;
			if (!Grid[Index] || RegionIds[Index] >= 0)
			{
				continue;
			}

			// Start new region with BFS
			const int32		   RegionId = Regions.Num();
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

					if (NX >= 0 && NX < GWidth && NY >= 0 && NY < GHeight)
					{
						const int32 NIndex = NY * GWidth + NX;
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
				const int32 CenterIndex = CenterY * GWidth + CenterX;
				if (RegionIds[CenterIndex] == RegionId)
				{
					CenterRegionId = RegionId;
				}
			}
		}
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Flood-fill found %d regions, center region=%d"), Regions.Num(), CenterRegionId);

	FLayoutDiagram2D Diagram = ConvertGridToDiagram(Grid, GWidth, GHeight);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Generate() complete: %d cells in %.2fms"), Diagram.Cells.Num(), ElapsedMs);

	FDrunkardWalkGridData Result;
	Result.Grid = MoveTemp(Grid);
	Result.CellType = MoveTemp(CellType);
	Result.RegionIds = MoveTemp(RegionIds);
	Result.Regions = MoveTemp(Regions);
	Result.CenterRegionId = CenterRegionId;
	Result.WalkerPaths = MoveTemp(WalkerPaths);
	Result.RoomCenters = MoveTemp(PlacedRoomCenters);
	Result.GridWidth = GWidth;
	Result.GridHeight = GHeight;
	Result.CellSize = CellSizeVal;
	Result.Diagram = MoveTemp(Diagram);

	return Result;
}

void UDrunkardWalkGenerator2D::CarveCell(TArray<bool>& Grid, TArray<uint8>& InCellType, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const
{
	const int32 HalfWidth = (CorridorWidth - 1) / 2;
	for (int32 dy = -HalfWidth; dy <= HalfWidth; ++dy)
	{
		for (int32 dx = -HalfWidth; dx <= HalfWidth; ++dx)
		{
			const int32 CX = X + dx;
			const int32 CY = Y + dy;
			// Skip cells in the border ring to preserve wall border
			if (CX < 1 || CX > GridWidth - 2 || CY < 1 || CY > GridHeight - 2)
			{
				continue;
			}
			const int32 Index = CY * GridWidth + CX;
			Grid[Index] = true;
			// Corridor only if currently wall — room wins over corridor
			if (InCellType[Index] == EDrunkardWalkCellType::Wall)
			{
				InCellType[Index] = EDrunkardWalkCellType::Corridor;
			}
		}
	}
}

void UDrunkardWalkGenerator2D::CarveRoom(
	TArray<bool>& Grid, TArray<uint8>& InCellType, int32 CenterX, int32 CenterY, int32 Radius, int32 GridWidth, int32 GridHeight) const
{
	for (int32 dy = -Radius; dy <= Radius; ++dy)
	{
		for (int32 dx = -Radius; dx <= Radius; ++dx)
		{
			const int32 CX = CenterX + dx;
			const int32 CY = CenterY + dy;
			// Skip cells in the border ring to preserve wall border
			if (CX < 1 || CX > GridWidth - 2 || CY < 1 || CY > GridHeight - 2)
			{
				continue;
			}
			const int32 Index = CY * GridWidth + CX;
			Grid[Index] = true;
			// Room always wins unconditionally
			InCellType[Index] = EDrunkardWalkCellType::Room;
		}
	}
}
