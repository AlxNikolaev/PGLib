#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoguelikeGeometry, Log, All);

UDrunkardWalkGenerator2D::UDrunkardWalkGenerator2D()
{
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
	WalkLength = 500;
	NumWalkers = 1;
	BranchProbability = 0.0f;
	CorridorWidth = 1;
	RoomChance = 0.0f;
	RoomRadius = 1;
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
	RoomRadius = FMath::Max(1, InRadius);
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
		TEXT(
			"[DW] Generate() — Bounds=(%.1f,%.1f)-(%.1f,%.1f) GridSize=%d Seed='%s' WalkLength=%d Walkers=%d BranchProb=%.2f CorridorWidth=%d RoomChance=%.2f RoomRadius=%d"),
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
		RoomRadius);

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
	int32 CarvedCount = 0;
	for (int32 Step = 0; Step < WalkLength && Walkers.Num() > 0; ++Step)
	{
		// Pick one random active walker
		const int32 WalkerIndex = RandomStream.RandRange(0, Walkers.Num() - 1);
		FWalker&	Walker = Walkers[WalkerIndex];

		// Pick random cardinal direction and move (reject out-of-bounds, retry up to 4 times)
		bool bValidMove = false;
		for (int32 Retry = 0; Retry < 4; ++Retry)
		{
			const int32 Dir = RandomStream.RandRange(0, 3);
			const int32 NewX = Walker.X + DX[Dir];
			const int32 NewY = Walker.Y + DY[Dir];
			if (NewX >= 1 && NewX <= GWidth - 2 && NewY >= 1 && NewY <= GHeight - 2)
			{
				Walker.X = NewX;
				Walker.Y = NewY;
				bValidMove = true;
				break;
			}
		}

		// If all 4 directions were rejected (walker cornered on tiny grid), skip this step
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
			CarveRoom(Grid, CellType, Walker.X, Walker.Y, GWidth, GHeight);
			UE_LOG(LogRoguelikeGeometry, VeryVerbose, TEXT("[DW] Step %d: carved room at (%d,%d)"), Step, Walker.X, Walker.Y);
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

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Walk complete: %d steps, %d floor cells carved"), CarvedCount, FloorCells);

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
	TArray<bool>& Grid, TArray<uint8>& InCellType, int32 CenterX, int32 CenterY, int32 GridWidth, int32 GridHeight) const
{
	for (int32 dy = -RoomRadius; dy <= RoomRadius; ++dy)
	{
		for (int32 dx = -RoomRadius; dx <= RoomRadius; ++dx)
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
