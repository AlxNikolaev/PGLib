// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/CellDungeon2D/CellDungeonGenerator2D.h"
#include "Generators/CellDungeon2D/CellDungeonConfig.h"
#include "Generators/CellDungeon2D/CellDungeonDebug.h"
#include "../../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Generous world bounds so the COARSE layout (rooms spaced by diagonal + a corridor-cell gap) has
	// enough cells to grow a blob of TargetRoomCount rooms, and the fine substrate has routing room.
	const FBox2D TestBounds(FVector2D(-5000.0f, -5000.0f), FVector2D(5000.0f, 5000.0f));

	/**
	 * Builds a square room type with one doorway per edge (4 doorways), each at the edge
	 * midpoint with the outward normal pointing away from the room. This is the "normal"
	 * multi-doorway pool used by most tests.
	 */
	FCellRoomType MakeFourDoorRoom(float Width = 300.0f, int32 Weight = 1, int32 Min = 0, int32 Max = 0)
	{
		FCellRoomType RoomType;
		RoomType.Footprint = FVector2D(Width, Width);
		RoomType.Weight = Weight;
		RoomType.Min = Min;
		RoomType.Max = Max;

		const float HalfW = Width * 0.5f;

		// +X edge midpoint.
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(1.0f, 0.0f);
			D.LocalPosition = FVector2D(HalfW, 0.0f);
			RoomType.Doorways.Add(D);
		}
		// -X edge midpoint.
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(-1.0f, 0.0f);
			D.LocalPosition = FVector2D(-HalfW, 0.0f);
			RoomType.Doorways.Add(D);
		}
		// +Y edge midpoint.
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(0.0f, 1.0f);
			D.LocalPosition = FVector2D(0.0f, HalfW);
			RoomType.Doorways.Add(D);
		}
		// -Y edge midpoint.
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(0.0f, -1.0f);
			D.LocalPosition = FVector2D(0.0f, -HalfW);
			RoomType.Doorways.Add(D);
		}

		return RoomType;
	}

	/**
	 * Builds a square room type with EXACTLY ONE doorway on its +X edge midpoint.
	 * Used for the headline single-door-star hypothesis: every room has one doorway,
	 * yet corridors branching through empty/corridor cells still produce one region.
	 */
	FCellRoomType MakeSingleDoorRoom(float Width = 300.0f, int32 Weight = 1)
	{
		FCellRoomType RoomType;
		RoomType.Footprint = FVector2D(Width, Width);
		RoomType.Weight = Weight;

		const float HalfW = Width * 0.5f;

		FCellDoorway D;
		D.LocalOutwardDir = FVector2D(1.0f, 0.0f);
		D.LocalPosition = FVector2D(HalfW, 0.0f);
		RoomType.Doorways.Add(D);

		return RoomType;
	}

	/** Build a W x H room with NumDoors doorways on distinct random edges (deterministic via R). */
	FCellRoomType MakeRoom(float W, float H, int32 NumDoors, FRandomStream& R)
	{
		FCellRoomType RoomType;
		RoomType.Footprint = FVector2D(W, H);
		const float HX = W * 0.5f;
		const float HY = H * 0.5f;

		TArray<FCellDoorway> AllEdges;
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(1.0f, 0.0f);
			D.LocalPosition = FVector2D(HX, 0.0f);
			AllEdges.Add(D);
		}
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(-1.0f, 0.0f);
			D.LocalPosition = FVector2D(-HX, 0.0f);
			AllEdges.Add(D);
		}
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(0.0f, 1.0f);
			D.LocalPosition = FVector2D(0.0f, HY);
			AllEdges.Add(D);
		}
		{
			FCellDoorway D;
			D.LocalOutwardDir = FVector2D(0.0f, -1.0f);
			D.LocalPosition = FVector2D(0.0f, -HY);
			AllEdges.Add(D);
		}

		// Deterministic Fisher-Yates, then take the first NumDoors distinct edges.
		TArray<int32> Order = { 0, 1, 2, 3 };
		for (int32 i = Order.Num() - 1; i > 0; --i)
		{
			Order.Swap(i, R.RandRange(0, i));
		}
		const int32 Count = FMath::Clamp(NumDoors, 1, 4);
		for (int32 i = 0; i < Count; ++i)
		{
			RoomType.Doorways.Add(AllEdges[Order[i]]);
		}
		return RoomType;
	}

	/**
	 * Realistic project-scale config: corridor width 1000; three middle room types
	 * (5x 2500x2500, 2x 5000x5000, 1x 6000x7000) each with 1-2 random doorways;
	 * start 3000x4000 (1 door), end 1500x1500 (1 door). 10 rooms total.
	 */
	FCellDungeonConfig MakeRealisticConfig()
	{
		FRandomStream R(12345);

		FCellDungeonConfig Config;
		Config.CorridorSize = 1000.0f;
		Config.TargetRoomCount = 10; // start + end + (5+2+1) middles

		Config.StartRoom = MakeRoom(3000.0f, 4000.0f, 1, R);
		Config.EndRoom = MakeRoom(1500.0f, 1500.0f, 1, R);

		FCellRoomType T0 = MakeRoom(2500.0f, 2500.0f, R.RandRange(1, 2), R);
		T0.Min = 5;
		T0.Max = 5;
		T0.Weight = 5;
		FCellRoomType T1 = MakeRoom(5000.0f, 5000.0f, R.RandRange(1, 2), R);
		T1.Min = 2;
		T1.Max = 2;
		T1.Weight = 2;
		FCellRoomType T2 = MakeRoom(6000.0f, 7000.0f, R.RandRange(1, 2), R);
		T2.Min = 1;
		T2.Max = 1;
		T2.Weight = 1;

		Config.MiddleRooms.Add(T0);
		Config.MiddleRooms.Add(T1);
		Config.MiddleRooms.Add(T2);
		return Config;
	}

	/** A normal multi-doorway config: start/end/middle all 4-door rooms. */
	FCellDungeonConfig MakeMultiDoorConfig(int32 TargetRoomCount = 8, float CellSize = 300.0f)
	{
		FCellDungeonConfig Config;
		Config.CorridorSize = CellSize;
		Config.TargetRoomCount = TargetRoomCount;
		Config.StartRoom = MakeFourDoorRoom();
		Config.EndRoom = MakeFourDoorRoom();
		Config.MiddleRooms.Add(MakeFourDoorRoom());
		return Config;
	}

	/** Single-door-star config: every room type has exactly one doorway. */
	FCellDungeonConfig MakeSingleDoorConfig(int32 TargetRoomCount = 6, float CellSize = 300.0f)
	{
		FCellDungeonConfig Config;
		Config.CorridorSize = CellSize;
		Config.TargetRoomCount = TargetRoomCount;
		Config.StartRoom = MakeSingleDoorRoom();
		Config.EndRoom = MakeSingleDoorRoom();
		Config.MiddleRooms.Add(MakeSingleDoorRoom());
		return Config;
	}

	/** Creates and configures a generator, then runs it. */
	FCellDungeonResult RunGenerator(const FString& Seed, const FCellDungeonConfig& Config, const FBox2D& Bounds = TestBounds)
	{
		UCellDungeonGenerator2D* Gen = NewObject<UCellDungeonGenerator2D>();
		Gen->SetSeed(Seed)->SetBounds(Bounds)->SetConfig(Config);
		return Gen->Generate();
	}

	/**
	 * BFS over the cell graph (Diagram.Cells[i].Neighbors) restricted to cells whose state is in
	 * AllowedStates, starting from SeedCell. Fills Visited (parallel to cells) with reached cells.
	 * Returns the number of cells reached.
	 */
	int32 BfsConnectedComponent(const FCellDungeonResult& R, int32 SeedCell, const TArray<ECellState>& AllowedStates, TArray<bool>& OutVisited)
	{
		const int32 NumCells = R.Diagram.Cells.Num();
		OutVisited.Init(false, NumCells);

		auto IsAllowed = [&](int32 CellIdx) -> bool {
			if (!R.CellState.IsValidIndex(CellIdx))
			{
				return false;
			}
			const ECellState S = R.CellState[CellIdx];
			for (const ECellState A : AllowedStates)
			{
				if (S == A)
				{
					return true;
				}
			}
			return false;
		};

		if (!R.Diagram.Cells.IsValidIndex(SeedCell) || !IsAllowed(SeedCell))
		{
			return 0;
		}

		TArray<int32> Queue;
		Queue.Add(SeedCell);
		OutVisited[SeedCell] = true;
		int32 Head = 0;
		int32 Reached = 1;
		while (Head < Queue.Num())
		{
			const int32 U = Queue[Head++];
			for (const int32 V : R.Diagram.Cells[U].Neighbors)
			{
				if (R.Diagram.Cells.IsValidIndex(V) && !OutVisited[V] && IsAllowed(V))
				{
					OutVisited[V] = true;
					++Reached;
					Queue.Add(V);
				}
			}
		}
		return Reached;
	}
} // namespace

// ============================================================
// Test 1: AllRoomsPlaced — a normal multi-doorway pool places exactly RequestedRoomCount rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonAllRoomsPlacedTest, "ProceduralGeometry.CellDungeon.AllRoomsPlaced", DefaultTestFlags)

bool FCellDungeonAllRoomsPlacedTest::RunTest(const FString& Parameters)
{
	const FCellDungeonConfig Config = MakeMultiDoorConfig(/*TargetRoomCount=*/8);
	const FCellDungeonResult Result = RunGenerator(TEXT("CellAllRoomsPlaced"), Config);

	FCellDungeonDebug::DumpToFiles(Result, TEXT("test_AllRoomsPlaced"));

	if (!Result.bValid)
	{
		// Shortfall is tolerated by the contract — surface it loudly rather than hard-failing.
		AddWarning(
			FString::Printf(TEXT("AllRoomsPlaced: shortfall — placed %d of %d requested rooms"), Result.Rooms.Num(), Result.RequestedRoomCount));
	}

	TestEqual("AllRoomsPlaced: RequestedRoomCount matches config", Result.RequestedRoomCount, Config.TargetRoomCount);
	TestTrue("AllRoomsPlaced: at least start + end placed", Result.Rooms.Num() >= 2);
	TestEqual("AllRoomsPlaced: Rooms.Num() == RequestedRoomCount", Result.Rooms.Num(), Result.RequestedRoomCount);

	return true;
}

// ============================================================
// Test 2: OneConnectedRegion — BFS over {RoomOccupied UNION Corridor} reaches every RoomOccupied cell.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonOneConnectedRegionTest, "ProceduralGeometry.CellDungeon.OneConnectedRegion", DefaultTestFlags)

bool FCellDungeonOneConnectedRegionTest::RunTest(const FString& Parameters)
{
	const FCellDungeonConfig Config = MakeMultiDoorConfig(/*TargetRoomCount=*/8);
	const FCellDungeonResult Result = RunGenerator(TEXT("CellOneConnectedRegion"), Config);

	FCellDungeonDebug::DumpToFiles(Result, TEXT("test_OneConnectedRegion"));

	if (Result.Rooms.Num() == 0)
	{
		AddWarning(TEXT("OneConnectedRegion: no rooms placed, skipping"));
		return true;
	}

	// Find a seed cell: the first RoomOccupied cell.
	int32 SeedCell = INDEX_NONE;
	for (int32 i = 0; i < Result.CellState.Num(); ++i)
	{
		if (Result.CellState[i] == ECellState::RoomOccupied)
		{
			SeedCell = i;
			break;
		}
	}

	if (SeedCell == INDEX_NONE)
	{
		AddError(TEXT("OneConnectedRegion: no RoomOccupied cell found despite rooms being placed"));
		return true;
	}

	// BFS over the room+corridor network.
	const TArray<ECellState> RoomAndCorridor = { ECellState::RoomOccupied, ECellState::Corridor };
	TArray<bool>			 Visited;
	BfsConnectedComponent(Result, SeedCell, RoomAndCorridor, Visited);

	// Every RoomOccupied cell must have been reached => exactly one connected region of rooms.
	int32 UnreachedRoomCells = 0;
	for (int32 i = 0; i < Result.CellState.Num(); ++i)
	{
		if (Result.CellState[i] == ECellState::RoomOccupied && !Visited[i])
		{
			++UnreachedRoomCells;
		}
	}

	TestEqual("OneConnectedRegion: all RoomOccupied cells reachable via room+corridor BFS", UnreachedRoomCells, 0);

	return true;
}

// ============================================================
// Test 3: NoOverlap — no cell is double-owned; corridor cells own no room; no two rooms share a cell.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonNoOverlapTest, "ProceduralGeometry.CellDungeon.NoOverlap", DefaultTestFlags)

bool FCellDungeonNoOverlapTest::RunTest(const FString& Parameters)
{
	const FCellDungeonConfig Config = MakeMultiDoorConfig(/*TargetRoomCount=*/8);
	const FCellDungeonResult Result = RunGenerator(TEXT("CellNoOverlap"), Config);

	FCellDungeonDebug::DumpToFiles(Result, TEXT("test_NoOverlap"));

	const int32 NumCells = Result.CellState.Num();
	TestEqual("NoOverlap: CellRoomIndex is parallel to CellState", Result.CellRoomIndex.Num(), NumCells);

	// (a) CellRoomIndex consistency: RoomOccupied cells carry a valid room index; Empty/Corridor/Blocked carry INDEX_NONE.
	for (int32 i = 0; i < NumCells; ++i)
	{
		const ECellState S = Result.CellState[i];
		const int32		 RoomIdx = Result.CellRoomIndex.IsValidIndex(i) ? Result.CellRoomIndex[i] : INDEX_NONE;
		if (S == ECellState::RoomOccupied)
		{
			TestTrue(FString::Printf(TEXT("NoOverlap: RoomOccupied cell[%d] has valid CellRoomIndex"), i), Result.Rooms.IsValidIndex(RoomIdx));
		}
		else
		{
			// (b) no Corridor (or any non-room) cell may claim a room.
			TestEqual(FString::Printf(TEXT("NoOverlap: non-room cell[%d] has INDEX_NONE CellRoomIndex"), i), RoomIdx, (int32)INDEX_NONE);
		}
	}

	// (c) No two rooms share an OccupiedCell — track ownership across all rooms.
	TArray<int32> CellOwner;
	CellOwner.Init(INDEX_NONE, NumCells);
	for (int32 r = 0; r < Result.Rooms.Num(); ++r)
	{
		for (const int32 Cell : Result.Rooms[r].OccupiedCells)
		{
			if (!CellOwner.IsValidIndex(Cell))
			{
				AddError(FString::Printf(TEXT("NoOverlap: Room[%d] references out-of-range cell %d"), r, Cell));
				continue;
			}
			TestEqual(FString::Printf(TEXT("NoOverlap: cell %d not already owned (room %d vs %d)"), Cell, CellOwner[Cell], r),
				CellOwner[Cell],
				(int32)INDEX_NONE);
			CellOwner[Cell] = r;

			// And the room's occupied cell must actually be flagged RoomOccupied with matching CellRoomIndex.
			TestEqual(FString::Printf(TEXT("NoOverlap: Room[%d] OccupiedCell %d is RoomOccupied"), r, Cell),
				(int32)Result.CellState[Cell],
				(int32)ECellState::RoomOccupied);
			TestEqual(FString::Printf(TEXT("NoOverlap: Room[%d] OccupiedCell %d CellRoomIndex matches"), r, Cell), Result.CellRoomIndex[Cell], r);
		}
	}

	return true;
}

// ============================================================
// Test 4: SingleDoorStar — every room has EXACTLY ONE doorway; still ONE connected region.
// This is the headline hypothesis of the generator.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonSingleDoorStarTest, "ProceduralGeometry.CellDungeon.SingleDoorStar", DefaultTestFlags)

bool FCellDungeonSingleDoorStarTest::RunTest(const FString& Parameters)
{
	const FCellDungeonConfig Config = MakeSingleDoorConfig(/*TargetRoomCount=*/6);
	const FCellDungeonResult Result = RunGenerator(TEXT("CellSingleDoorStar"), Config);

	FCellDungeonDebug::DumpToFiles(Result, TEXT("test_SingleDoorStar"));

	// Guard the premise: every placed room genuinely has exactly one doorway.
	for (int32 r = 0; r < Result.Rooms.Num(); ++r)
	{
		TestEqual(FString::Printf(TEXT("SingleDoorStar: Room[%d] has exactly one doorway"), r), Result.Rooms[r].DoorwayPos.Num(), 1);
	}

	// The headline hypothesis: single-doorway rooms must NOT stall the frontier. All requested rooms
	// must place — corridors branch in empty/corridor cells, so a consumed door is not a dead end.
	TestEqual("SingleDoorStar: all requested rooms placed (single doors do not stall growth)", Result.Rooms.Num(), Result.RequestedRoomCount);

	if (Result.Rooms.Num() < 2)
	{
		AddError(TEXT("SingleDoorStar: fewer than 2 rooms placed — connectivity check is meaningless"));
		return false;
	}

	// Seed BFS from the first RoomOccupied cell over room+corridor cells.
	int32 SeedCell = INDEX_NONE;
	for (int32 i = 0; i < Result.CellState.Num(); ++i)
	{
		if (Result.CellState[i] == ECellState::RoomOccupied)
		{
			SeedCell = i;
			break;
		}
	}

	if (SeedCell == INDEX_NONE)
	{
		AddError(TEXT("SingleDoorStar: no RoomOccupied cell found"));
		return true;
	}

	const TArray<ECellState> RoomAndCorridor = { ECellState::RoomOccupied, ECellState::Corridor };
	TArray<bool>			 Visited;
	BfsConnectedComponent(Result, SeedCell, RoomAndCorridor, Visited);

	int32 UnreachedRoomCells = 0;
	for (int32 i = 0; i < Result.CellState.Num(); ++i)
	{
		if (Result.CellState[i] == ECellState::RoomOccupied && !Visited[i])
		{
			++UnreachedRoomCells;
		}
	}

	TestEqual("SingleDoorStar: ONE connected region even with single-doorway rooms", UnreachedRoomCells, 0);

	return true;
}

// ============================================================
// Test 5: Determinism — same seed twice yields identical rooms and cell states.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonDeterminismTest, "ProceduralGeometry.CellDungeon.Determinism", DefaultTestFlags)

bool FCellDungeonDeterminismTest::RunTest(const FString& Parameters)
{
	const FString			 Seed = TEXT("CellDeterminism-42");
	const FCellDungeonConfig Config = MakeMultiDoorConfig(/*TargetRoomCount=*/8);

	const FCellDungeonResult A = RunGenerator(Seed, Config);
	const FCellDungeonResult B = RunGenerator(Seed, Config);

	FCellDungeonDebug::DumpToFiles(A, TEXT("test_Determinism"));

	// Room count identical.
	TestEqual("Determinism: Rooms.Num() matches", A.Rooms.Num(), B.Rooms.Num());

	// Room centers identical (exact seed => exact placement).
	constexpr double Tolerance = 0.001;
	for (int32 i = 0; i < FMath::Min(A.Rooms.Num(), B.Rooms.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Determinism: Room[%d].Center.X"), i), (double)A.Rooms[i].Center.X, (double)B.Rooms[i].Center.X, Tolerance);
		TestEqual(FString::Printf(TEXT("Determinism: Room[%d].Center.Y"), i), (double)A.Rooms[i].Center.Y, (double)B.Rooms[i].Center.Y, Tolerance);
		TestEqual(
			FString::Printf(TEXT("Determinism: Room[%d].RotationDeg"), i), (double)A.Rooms[i].RotationDeg, (double)B.Rooms[i].RotationDeg, Tolerance);
		TestEqual(FString::Printf(TEXT("Determinism: Room[%d].TypeIndex"), i), A.Rooms[i].TypeIndex, B.Rooms[i].TypeIndex);
	}

	// CellState arrays identical (same substrate + same carving).
	TestEqual("Determinism: CellState.Num() matches", A.CellState.Num(), B.CellState.Num());
	if (A.CellState.Num() == B.CellState.Num())
	{
		int32 MismatchedCells = 0;
		for (int32 i = 0; i < A.CellState.Num(); ++i)
		{
			if (A.CellState[i] != B.CellState[i])
			{
				++MismatchedCells;
			}
		}
		TestEqual("Determinism: every CellState entry matches", MismatchedCells, 0);
	}

	// Start/end indices identical.
	TestEqual("Determinism: StartRoomIndex matches", A.StartRoomIndex, B.StartRoomIndex);
	TestEqual("Determinism: EndRoomIndex matches", A.EndRoomIndex, B.EndRoomIndex);

	return true;
}

// ============================================================
// Test 6: CorridorRoomGap — corridors keep a 1-cell gap from rooms; the only
// corridor cells allowed to touch a room are the per-room seed cells.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonCorridorRoomGapTest, "ProceduralGeometry.CellDungeon.CorridorRoomGap", DefaultTestFlags)

bool FCellDungeonCorridorRoomGapTest::RunTest(const FString& Parameters)
{
	const FCellDungeonConfig Config = MakeMultiDoorConfig(/*TargetRoomCount=*/8);
	const FCellDungeonResult Result = RunGenerator(TEXT("CellCorridorRoomGap"), Config);

	FCellDungeonDebug::DumpToFiles(Result, TEXT("test_CorridorRoomGap"));

	const TSet<int32> SeedSet(Result.CorridorSeeds);

	int32 OffendingCells = 0;
	for (int32 i = 0; i < Result.CellState.Num(); ++i)
	{
		if (Result.CellState[i] != ECellState::Corridor || SeedSet.Contains(i))
		{
			continue; // only non-seed corridor cells must keep their distance
		}
		for (const int32 Nb : Result.Diagram.Cells[i].Neighbors)
		{
			if (Result.CellState.IsValidIndex(Nb) && Result.CellState[Nb] == ECellState::RoomOccupied)
			{
				++OffendingCells;
				break;
			}
		}
	}

	TestEqual("CorridorRoomGap: no non-seed corridor cell touches a room", OffendingCells, 0);
	TestTrue("CorridorRoomGap: every room contributed a seed", Result.CorridorSeeds.Num() >= Result.Rooms.Num() - 1);

	return true;
}

// ============================================================
// Test 7: RealisticShowcase — project-scale config (corridor 1000; mixed large
// rooms with 1-2 random doors). Dumps for visual inspection; asserts invariants.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellDungeonRealisticShowcaseTest, "ProceduralGeometry.CellDungeon.RealisticShowcase", DefaultTestFlags)

bool FCellDungeonRealisticShowcaseTest::RunTest(const FString& Parameters)
{
	const FCellDungeonConfig Config = MakeRealisticConfig();
	const FBox2D			 Bounds(FVector2D(-45000.0f, -45000.0f), FVector2D(45000.0f, 45000.0f));
	const FCellDungeonResult Result = RunGenerator(TEXT("CellRealistic"), Config, Bounds);

	FCellDungeonDebug::DumpToFiles(Result, TEXT("test_RealisticShowcase"));

	AddInfo(FString::Printf(TEXT("RealisticShowcase: placed %d/%d rooms, %d corridors, %d seeds"),
		Result.Rooms.Num(),
		Result.RequestedRoomCount,
		Result.CorridorPaths.Num(),
		Result.CorridorSeeds.Num()));

	TestTrue("RealisticShowcase: all (or all-but-one) rooms placed", Result.Rooms.Num() >= Result.RequestedRoomCount - 1);

	// One connected region over room+corridor cells.
	int32 SeedCell = INDEX_NONE;
	for (int32 i = 0; i < Result.CellState.Num(); ++i)
	{
		if (Result.CellState[i] == ECellState::RoomOccupied)
		{
			SeedCell = i;
			break;
		}
	}
	if (SeedCell != INDEX_NONE)
	{
		const TArray<ECellState> RoomAndCorridor = { ECellState::RoomOccupied, ECellState::Corridor };
		TArray<bool>			 Visited;
		BfsConnectedComponent(Result, SeedCell, RoomAndCorridor, Visited);
		int32 Unreached = 0;
		for (int32 i = 0; i < Result.CellState.Num(); ++i)
		{
			if (Result.CellState[i] == ECellState::RoomOccupied && !Visited[i])
			{
				++Unreached;
			}
		}
		TestEqual("RealisticShowcase: one connected region", Unreached, 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
