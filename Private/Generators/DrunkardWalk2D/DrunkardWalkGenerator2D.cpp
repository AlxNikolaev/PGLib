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
	const double StartTime = FPlatformTime::Seconds();

	UE_LOG(LogRoguelikeGeometry, Log,
		TEXT("[DW] Generate() — Bounds=(%.1f,%.1f)-(%.1f,%.1f) GridSize=%d Seed='%s' WalkLength=%d Walkers=%d BranchProb=%.2f CorridorWidth=%d RoomChance=%.2f RoomRadius=%d"),
		Bounds.Min.X, Bounds.Min.Y, Bounds.Max.X, Bounds.Max.Y,
		GridSize, *Seed, WalkLength, NumWalkers, BranchProbability, CorridorWidth, RoomChance, RoomRadius);

	const float CellSize = static_cast<float>(GridSize);
	const float BoundsWidth = Bounds.Max.X - Bounds.Min.X;
	const float BoundsHeight = Bounds.Max.Y - Bounds.Min.Y;

	const int32 GridWidth = FMath::CeilToInt(BoundsWidth / CellSize);
	const int32 GridHeight = FMath::CeilToInt(BoundsHeight / CellSize);

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Grid dimensions: %dx%d (%d total cells)"), GridWidth, GridHeight, GridWidth * GridHeight);

	// OOM guard
	if (GridWidth <= 0 || GridHeight <= 0 || (int64)GridWidth * GridHeight > 4'194'304)
	{
		UE_LOG(LogRoguelikeGeometry, Error, TEXT("[DW] OOM guard triggered: %dx%d exceeds limit"), GridWidth, GridHeight);
		return FLayoutDiagram2D();
	}

	// Initialize grid (all walls)
	TArray<bool> Grid;
	Grid.Init(false, GridWidth * GridHeight);

	// Place walkers at grid center
	const int32 CenterX = GridWidth / 2;
	const int32 CenterY = GridHeight / 2;

	struct FWalker
	{
		int32 X;
		int32 Y;
	};

	TArray<FWalker> Walkers;
	Walkers.Reserve(NumWalkers);
	for (int32 i = 0; i < NumWalkers; ++i)
	{
		Walkers.Add({ CenterX, CenterY });
	}

	// Carve initial position
	CarveCell(Grid, CenterX, CenterY, GridWidth, GridHeight);

	// Cardinal directions: right, left, up, down
	const int32 DX[] = { 1, -1, 0, 0 };
	const int32 DY[] = { 0, 0, 1, -1 };

	// Walk loop
	int32 CarvedCount = 0;
	for (int32 Step = 0; Step < WalkLength && Walkers.Num() > 0; ++Step)
	{
		// Pick one random active walker
		const int32 WalkerIndex = RandomStream.RandRange(0, Walkers.Num() - 1);
		FWalker& Walker = Walkers[WalkerIndex];

		// Pick random cardinal direction and move
		const int32 Dir = RandomStream.RandRange(0, 3);
		Walker.X = FMath::Clamp(Walker.X + DX[Dir], 0, GridWidth - 1);
		Walker.Y = FMath::Clamp(Walker.Y + DY[Dir], 0, GridHeight - 1);

		// Carve corridor or room
		if (RoomChance > 0.0f && RandomStream.FRand() < RoomChance)
		{
			CarveRoom(Grid, Walker.X, Walker.Y, GridWidth, GridHeight);
			UE_LOG(LogRoguelikeGeometry, VeryVerbose, TEXT("[DW] Step %d: carved room at (%d,%d)"), Step, Walker.X, Walker.Y);
		}
		else
		{
			CarveCell(Grid, Walker.X, Walker.Y, GridWidth, GridHeight);
		}
		++CarvedCount;

		// Branch: spawn new walker at current position
		if (BranchProbability > 0.0f && RandomStream.FRand() < BranchProbability)
		{
			Walkers.Add({ Walker.X, Walker.Y });
			UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[DW] Step %d: branched new walker at (%d,%d), total walkers=%d"), Step, Walker.X, Walker.Y, Walkers.Num());
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

	FLayoutDiagram2D Result = ConvertGridToDiagram(Grid, GridWidth, GridHeight);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Generate() complete: %d cells in %.2fms"), Result.Cells.Num(), ElapsedMs);

	return Result;
}

void UDrunkardWalkGenerator2D::CarveCell(TArray<bool>& Grid, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const
{
	const int32 HalfWidth = (CorridorWidth - 1) / 2;
	for (int32 dy = -HalfWidth; dy <= HalfWidth; ++dy)
	{
		for (int32 dx = -HalfWidth; dx <= HalfWidth; ++dx)
		{
			const int32 CX = FMath::Clamp(X + dx, 0, GridWidth - 1);
			const int32 CY = FMath::Clamp(Y + dy, 0, GridHeight - 1);
			Grid[CY * GridWidth + CX] = true;
		}
	}
}

void UDrunkardWalkGenerator2D::CarveRoom(TArray<bool>& Grid, int32 CenterX, int32 CenterY, int32 GridWidth, int32 GridHeight) const
{
	for (int32 dy = -RoomRadius; dy <= RoomRadius; ++dy)
	{
		for (int32 dx = -RoomRadius; dx <= RoomRadius; ++dx)
		{
			const int32 CX = FMath::Clamp(CenterX + dx, 0, GridWidth - 1);
			const int32 CY = FMath::Clamp(CenterY + dy, 0, GridHeight - 1);
			Grid[CY * GridWidth + CX] = true;
		}
	}
}
