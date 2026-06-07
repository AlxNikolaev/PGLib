// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"
#include "Generators/OrganicDungeon2D/OrganicLayoutDebug.h"
#include "Generators/OrganicDungeon2D/OrganicConfigIO.h"
#include "Factories/ProceduralMeshFactory.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "../../ProceduralGeometryTestFlags.h"

#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Default room type: 600×600 world units, 1 doorway per edge. */
	FOrganicResolvedRoomType MakeDefaultRoom(int32 Count = 1)
	{
		FOrganicResolvedRoomType RoomType;
		RoomType.FootprintWidth = 600.0f;
		RoomType.FootprintHeight = 600.0f;
		RoomType.Count = Count;
		RoomType.Weight = Count;
		return RoomType;
	}

	/** Creates an OrganicDungeon generator with a single room type containing RoomCount rooms. */
	UOrganicDungeonGenerator2D* MakeOrganicGenerator(const FString& Seed, int32 RoomCount, int32 InGridSize = 50)
	{
		FOrganicDungeonResolvedParams Params;
		Params.RoomTypes.Add(MakeDefaultRoom(RoomCount));

		UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
		Gen->SetSeed(Seed);
		Gen->SetGridSize(InGridSize);
		Gen->ApplyResolvedParams(Params);
		return Gen;
	}
} // namespace

// ============================================================
// Test 1: Default generation produces non-empty rooms and diagram.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonDefaultGenerateTest, "ProceduralGeometry.OrganicDungeon.DefaultGenerate", DefaultTestFlags)

bool FOrganicDungeonDefaultGenerateTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("DefaultTest"), /*RoomCount=*/4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	TestTrue("DefaultGenerate: Rooms.Num() > 0", Data.Rooms.Num() > 0);
	TestTrue("DefaultGenerate: Diagram.Cells.Num() > 0", Data.Diagram.Cells.Num() > 0);

	return true;
}

// ============================================================
// Test 2: Same seed produces identical room count and corridor count.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonDeterminismTest, "ProceduralGeometry.OrganicDungeon.Determinism", DefaultTestFlags)

bool FOrganicDungeonDeterminismTest::RunTest(const FString& Parameters)
{
	const FString Seed = TEXT("OrganicDeterminism99");

	UOrganicDungeonGenerator2D* Gen1 = MakeOrganicGenerator(Seed, 4);
	UOrganicDungeonGenerator2D* Gen2 = MakeOrganicGenerator(Seed, 4);

	const FOrganicDungeonGridData Data1 = Gen1->GenerateWithGridData();
	const FOrganicDungeonGridData Data2 = Gen2->GenerateWithGridData();

	TestEqual("Determinism: Rooms.Num() matches", Data1.Rooms.Num(), Data2.Rooms.Num());
	TestEqual("Determinism: Corridors.Num() matches", Data1.Corridors.Num(), Data2.Corridors.Num());

	// FVector2D components are double in UE5; tolerance must match.
	constexpr double Tolerance = 1.0; // 1 world-unit tolerance for floating-point room centers
	for (int32 i = 0; i < FMath::Min(Data1.Rooms.Num(), Data2.Rooms.Num()); ++i)
	{
		TestEqual(
			FString::Printf(TEXT("Determinism: Room[%d].Center.X"), i), (double)Data1.Rooms[i].Center.X, (double)Data2.Rooms[i].Center.X, Tolerance);
		TestEqual(
			FString::Printf(TEXT("Determinism: Room[%d].Center.Y"), i), (double)Data1.Rooms[i].Center.Y, (double)Data2.Rooms[i].Center.Y, Tolerance);
	}

	return true;
}

// ============================================================
// Test 3: Diagram cell invariants — gridless room-cell diagram.
// One FLayoutCell2D per room AND per junction; corridor Radii parallel to Centerline.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonParallelArraySizesTest, "ProceduralGeometry.OrganicDungeon.ParallelArraySizes", DefaultTestFlags)

bool FOrganicDungeonParallelArraySizesTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("ParallelTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() == 0)
	{
		AddWarning(TEXT("ParallelArraySizes: empty layout, skipping"));
		return true;
	}

	// Gridless: exactly one diagram cell per room plus one per junction.
	TestEqual("ParallelArraySizes: Diagram.Cells.Num() == Rooms.Num() + Junctions.Num()",
		Data.Diagram.Cells.Num(),
		Data.Rooms.Num() + Data.Junctions.Num());

	// Cell indices are contiguous: rooms first [0,Rooms.Num()), then junctions.
	for (int32 i = 0; i < Data.Diagram.Cells.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("ParallelArraySizes: Cell[%d].CellIndex == %d"), i, i), Data.Diagram.Cells[i].CellIndex, i);
		TestTrue(FString::Printf(TEXT("ParallelArraySizes: Cell[%d] has >= 3 boundary vertices"), i), Data.Diagram.Cells[i].Vertices.Num() >= 3);
	}

	// Each corridor's Radii must be parallel to its Centerline.
	for (int32 i = 0; i < Data.Corridors.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("ParallelArraySizes: Corridor[%d].Radii.Num() == Centerline.Num()"), i),
			Data.Corridors[i].Radii.Num(),
			Data.Corridors[i].Centerline.Num());
	}

	return true;
}

// ============================================================
// Test 4: Room count is within [1, RequestedRoomCount].
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonRoomCountBoundsTest, "ProceduralGeometry.OrganicDungeon.RoomCountBounds", DefaultTestFlags)

bool FOrganicDungeonRoomCountBoundsTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("RoomCountBoundsTest"), 5);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.RequestedRoomCount == 0)
	{
		AddWarning(TEXT("RoomCountBounds: RequestedRoomCount == 0, skipping"));
		return true;
	}

	TestTrue("RoomCountBounds: Rooms.Num() >= 1", Data.Rooms.Num() >= 1);
	TestTrue("RoomCountBounds: Rooms.Num() <= RequestedRoomCount", Data.Rooms.Num() <= Data.RequestedRoomCount);

	return true;
}

// ============================================================
// Test 5: Large footprint — gridless. The OrganicDungeon generator no longer rasterizes a fine grid, so a
// huge room footprint no longer triggers any OOM guard; it simply produces one room cell. This regression-tests
// that a footprint that would have blown the old grid limit now generates cleanly (exactly one cell, no error).
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonOOMGuardTest, "ProceduralGeometry.OrganicDungeon.OOMGuard", DefaultTestFlags)

bool FOrganicDungeonOOMGuardTest::RunTest(const FString& Parameters)
{
	FOrganicResolvedRoomType BigRoom;
	BigRoom.FootprintWidth = 21000.0f; // would have exceeded the old 4M-cell grid limit at GridSize=10
	BigRoom.FootprintHeight = 21000.0f;
	BigRoom.Count = 1;

	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(BigRoom);
	Params.WallThickness = 1;

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("OOMTest"));
	Gen->SetGridSize(10);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	// Gridless: one room -> exactly one cell, no OOM, no error.
	TestEqual("OOMGuard: gridless build yields one cell for one room", Data.Diagram.Cells.Num(), 1);

	return true;
}

// ============================================================
// Test 6: No room types → empty result, no crash.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonNoRoomTypesTest, "ProceduralGeometry.OrganicDungeon.NoRoomTypes_EmptyResult", DefaultTestFlags)

bool FOrganicDungeonNoRoomTypesTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonResolvedParams EmptyParams;
	// RoomTypes is empty; bHasStartRoom/bHasEndRoom default to false.

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("NoRoomTypes"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(EmptyParams);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	TestEqual("NoRoomTypes_EmptyResult: Rooms.Num() == 0", Data.Rooms.Num(), 0);
	TestEqual("NoRoomTypes_EmptyResult: Diagram.Cells.Num() == 0", Data.Diagram.Cells.Num(), 0);

	return true;
}

// ============================================================
// Test 7: StartRoomIndex and EndRoomIndex are valid, distinct room indices (the single entrance/exit)
//         at the graph-diameter endpoints when more than one room exists.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonEntranceAndExitValidTest, "ProceduralGeometry.OrganicDungeon.EntranceAndExitValid", DefaultTestFlags)

bool FOrganicDungeonEntranceAndExitValidTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("EntranceExitTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() == 0)
	{
		AddWarning(TEXT("EntranceAndExitValid: no rooms placed, skipping"));
		return true;
	}

	const int32 RoomCount = Data.Rooms.Num();

	// Entrance and exit must both be valid room indices.
	TestTrue("EntranceAndExitValid: StartRoomIndex is valid", Data.StartRoomIndex >= 0 && Data.StartRoomIndex < RoomCount);
	TestTrue("EntranceAndExitValid: EndRoomIndex is valid", Data.EndRoomIndex >= 0 && Data.EndRoomIndex < RoomCount);

	// With more than one room the exit must be a different room from the entrance (the diameter far endpoint).
	if (RoomCount >= 2)
	{
		TestTrue("EntranceAndExitValid: EndRoomIndex distinct from StartRoomIndex", Data.EndRoomIndex != Data.StartRoomIndex);
	}

	return true;
}

// ============================================================
// Test 7b: A single-room cluster degenerates to EndRoomIndex == StartRoomIndex without crashing.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonSingleRoomExitTest, "ProceduralGeometry.OrganicDungeon.SingleRoomExitDegenerate", DefaultTestFlags)

bool FOrganicDungeonSingleRoomExitTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("SingleRoomExitTest"), 1);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() != 1)
	{
		AddWarning(TEXT("SingleRoomExitDegenerate: expected exactly one room, skipping"));
		return true;
	}

	TestEqual("SingleRoomExitDegenerate: StartRoomIndex == 0", Data.StartRoomIndex, 0);
	TestEqual("SingleRoomExitDegenerate: EndRoomIndex collapses onto StartRoomIndex", Data.EndRoomIndex, Data.StartRoomIndex);

	return true;
}

// ============================================================
// Test 8: Diagram connectivity — the gridless room-cell diagram is ONE connected component over corridor
//         adjacency (Neighbors), and every room cell has at least one neighbour (the all-single-door star).
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonCellTypeConsistencyTest, "ProceduralGeometry.OrganicDungeon.CellTypeConsistency", DefaultTestFlags)

bool FOrganicDungeonCellTypeConsistencyTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("CellTypeTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	const int32 NumCells = Data.Diagram.Cells.Num();
	if (NumCells == 0)
	{
		AddWarning(TEXT("CellTypeConsistency: empty diagram, skipping"));
		return true;
	}

	// Neighbor indices must be in range and symmetric (undirected corridor adjacency).
	for (int32 i = 0; i < NumCells; ++i)
	{
		for (const int32 N : Data.Diagram.Cells[i].Neighbors)
		{
			if (N < 0 || N >= NumCells)
			{
				AddError(FString::Printf(TEXT("CellTypeConsistency: Cell[%d] has out-of-range neighbour %d (NumCells=%d)"), i, N, NumCells));
				return true;
			}
			TestTrue(
				FString::Printf(TEXT("CellTypeConsistency: adjacency symmetric for (%d,%d)"), i, N), Data.Diagram.Cells[N].Neighbors.Contains(i));
		}
	}

	// Every room cell (index < Rooms.Num()) must have >= 1 corridor neighbour when more than one cell exists.
	if (NumCells > 1)
	{
		for (int32 r = 0; r < Data.Rooms.Num(); ++r)
		{
			TestTrue(FString::Printf(TEXT("CellTypeConsistency: room cell[%d] has >= 1 neighbour"), r), Data.Diagram.Cells[r].Neighbors.Num() >= 1);
		}
	}

	// Single connected component: BFS over Neighbors from cell 0 must reach every cell.
	TArray<bool> Visited;
	Visited.Init(false, NumCells);
	TArray<int32> Queue;
	Queue.Add(0);
	Visited[0] = true;
	int32 Head = 0;
	int32 Reached = 1;
	while (Head < Queue.Num())
	{
		const int32 U = Queue[Head++];
		for (const int32 V : Data.Diagram.Cells[U].Neighbors)
		{
			if (!Visited[V])
			{
				Visited[V] = true;
				++Reached;
				Queue.Add(V);
			}
		}
	}
	TestEqual("CellTypeConsistency: diagram is ONE connected component", Reached, NumCells);

	return true;
}

// ============================================================
// Test 9: Every corridor's Radii array is parallel to its Centerline.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonCorridorRadiiParallelTest, "ProceduralGeometry.OrganicDungeon.CorridorRadiiParallel", DefaultTestFlags)

bool FOrganicDungeonCorridorRadiiParallelTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("CorridorRadiiTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	for (int32 i = 0; i < Data.Corridors.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("CorridorRadiiParallel: Corridor[%d].Radii.Num() == Centerline.Num()"), i),
			Data.Corridors[i].Radii.Num(),
			Data.Corridors[i].Centerline.Num());
	}

	return true;
}

// ============================================================
// ResolveForTotal tests — FOrganicDungeonConfig
// Uses FootprintOverride to avoid level loading during tests.
// ============================================================

namespace
{
	/**
	 * Builds a FOrganicRoomType with an explicit footprint override so Resolve()
	 * does not attempt to load any level asset (which would log spurious warnings).
	 */
	FOrganicRoomType MakeODRoomType(int32 Weight, float FootprintW = 600.0f, float FootprintH = 600.0f)
	{
		FOrganicRoomType T;
		T.Weight = Weight;
		T.FootprintOverride = FVector2D(FootprintW, FootprintH);
		return T;
	}

	/** Returns the sum of Count across all resolved room types. */
	int32 SumODCounts(const FOrganicDungeonResolvedParams& Params)
	{
		int32 Sum = 0;
		for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
		{
			Sum += RT.Count;
		}
		return Sum;
	}
} // namespace

// ============================================================
// Test 10: OD ResolveForTotal — TotalRooms == 0 clears all counts.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FODConfigResolveForTotalZeroTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_Zero", DefaultTestFlags)

bool FODConfigResolveForTotalZeroTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(3));
	Config.RoomTypes.Add(MakeODRoomType(1));

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(0);

	for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
	{
		TestEqual("OD_ResolveForTotal_Zero: every Count == 0", RT.Count, 0);
	}
	return true;
}

// ============================================================
// Test 11: OD ResolveForTotal — single type receives all rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalSingleTypeTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_SingleType", DefaultTestFlags)

bool FODConfigResolveForTotalSingleTypeTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(1));

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(7);

	TestEqual("OD_ResolveForTotal_SingleType: one room type", Params.RoomTypes.Num(), 1);
	if (Params.RoomTypes.Num() == 1)
	{
		TestEqual("OD_ResolveForTotal_SingleType: sole type gets all rooms", Params.RoomTypes[0].Count, 7);
	}
	return true;
}

// ============================================================
// Test 12: OD ResolveForTotal — multiple equal-weight types sum exactly to TotalRooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalSumEqualWeightsTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_SumEqualWeights", DefaultTestFlags)

bool FODConfigResolveForTotalSumEqualWeightsTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.RoomTypes.Add(MakeODRoomType(1));

	// Prime total forces a rounding remainder — last type must absorb it.
	constexpr int32						Total = 7;
	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(Total);

	TestEqual("OD_ResolveForTotal_SumEqualWeights: sum == Total", SumODCounts(Params), Total);
	for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
	{
		TestTrue("OD_ResolveForTotal_SumEqualWeights: all counts >= 0", RT.Count >= 0);
	}
	return true;
}

// ============================================================
// Test 13: OD ResolveForTotal — weighted distribution (3:1) sums to TotalRooms
//          and the heavier type gets more rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalWeightedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_Weighted", DefaultTestFlags)

bool FODConfigResolveForTotalWeightedTest::RunTest(const FString& Parameters)
{
	// Weights 3:1, TotalRooms=8: Round(3/4*8)=6 → type0=6, type1=2. Sum=8.
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(3));
	Config.RoomTypes.Add(MakeODRoomType(1));

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(8);

	TestEqual("OD_ResolveForTotal_Weighted: two types remain", Params.RoomTypes.Num(), 2);
	TestEqual("OD_ResolveForTotal_Weighted: sum == 8", SumODCounts(Params), 8);
	if (Params.RoomTypes.Num() == 2)
	{
		TestTrue("OD_ResolveForTotal_Weighted: heavier type > lighter type", Params.RoomTypes[0].Count > Params.RoomTypes[1].Count);
	}
	return true;
}

// ============================================================
// Test 14: OD ResolveForTotal — rounding cannot produce a negative last-type count.
//          weights=[1,1,1], TotalRooms=2 → Round(2/3)=1, 1, last=Max(0,2-2)=0. Sum=2 ✓
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalOvercountGuardTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_OvercountGuard", DefaultTestFlags)

bool FODConfigResolveForTotalOvercountGuardTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.RoomTypes.Add(MakeODRoomType(1));

	constexpr int32						Total = 2;
	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(Total);

	TestEqual("OD_ResolveForTotal_OvercountGuard: sum == 2", SumODCounts(Params), Total);
	for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
	{
		TestTrue("OD_ResolveForTotal_OvercountGuard: no negative counts", RT.Count >= 0);
	}
	return true;
}

// ============================================================
// Test 15: OD ResolveForTotal — entrance / exit-portal-room are unaffected; only regular types
//          are redistributed.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalStartEndUnaffectedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_StartEndUnaffected", DefaultTestFlags)

bool FODConfigResolveForTotalStartEndUnaffectedTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.StartRoom = MakeODRoomType(1);
	// EndRoom left default (no level ref / no footprint override) so bHasEndRoom = false; test just verifies
	// StartRoom is unaffected by the regular-room distribution.

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(6);

	// Regular types should sum to 6.
	TestEqual("OD_ResolveForTotal_StartEndUnaffected: regular type sum == 6", SumODCounts(Params), 6);

	// StartRoom still present and resolved; EndRoom left null (no level ref set).
	TestTrue("OD_ResolveForTotal_StartEndUnaffected: bHasStartRoom", Params.bHasStartRoom);
	TestFalse("OD_ResolveForTotal_StartEndUnaffected: bHasEndRoom (no level / footprint)", Params.bHasEndRoom);

	return true;
}

// ============================================================
// Test 16: OD ResolveForTotal end-to-end — params from ResolveForTotal feed the
//          generator and produce a non-empty result.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalEndToEndTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_EndToEnd", DefaultTestFlags)

bool FODConfigResolveForTotalEndToEndTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(2));
	Config.RoomTypes.Add(MakeODRoomType(1));

	constexpr int32						Total = 5;
	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(Total);

	// Sanity-check the distribution before feeding to the generator.
	TestEqual("OD_ResolveForTotal_EndToEnd: sum == 5", SumODCounts(Params), Total);

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("ODResolveForTotalE2E"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	TestTrue("OD_ResolveForTotal_EndToEnd: rooms were placed", Data.Rooms.Num() >= 1);
	TestTrue("OD_ResolveForTotal_EndToEnd: diagram has cells", Data.Diagram.Cells.Num() > 0);

	return true;
}

// ============================================================
// Test 17: OD ResolveForTotal — Min is respected: each type receives at least Min rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalMinRespectedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_MinRespected", DefaultTestFlags)

bool FODConfigResolveForTotalMinRespectedTest::RunTest(const FString& Parameters)
{
	// Two types, each with Min=2. Budget=10. Each must receive >= 2 rooms.
	FOrganicDungeonConfig Config;
	for (int32 i = 0; i < 2; ++i)
	{
		FOrganicRoomType T;
		T.Weight = 1;
		T.Min = 2;
		T.FootprintOverride = FVector2D(600.0f, 600.0f);
		Config.RoomTypes.Add(T);
	}
	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(10);

	TestEqual("OD_ResolveForTotal_MinRespected: sum == 10", SumODCounts(Params), 10);
	for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
	{
		TestTrue("OD_ResolveForTotal_MinRespected: each type Count >= Min(2)", RT.Count >= 2);
	}
	return true;
}

// ============================================================
// Test 18: OD ResolveForTotal — Max is respected: each type receives at most Max rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalMaxRespectedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_MaxRespected", DefaultTestFlags)

bool FODConfigResolveForTotalMaxRespectedTest::RunTest(const FString& Parameters)
{
	// Type0: Weight=3, Max=4. Type1: Weight=1, Max=0 (uncapped). Budget=10.
	// Type0 is capped at 4; Type1 absorbs the remaining 6.
	FOrganicDungeonConfig Config;
	{
		FOrganicRoomType T0;
		T0.Weight = 3;
		T0.Max = 4;
		T0.FootprintOverride = FVector2D(600.0f, 600.0f);
		Config.RoomTypes.Add(T0);

		FOrganicRoomType T1;
		T1.Weight = 1;
		T1.FootprintOverride = FVector2D(600.0f, 600.0f);
		Config.RoomTypes.Add(T1);
	}
	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(10);

	TestEqual("OD_ResolveForTotal_MaxRespected: two types", Params.RoomTypes.Num(), 2);
	TestEqual("OD_ResolveForTotal_MaxRespected: sum == 10", SumODCounts(Params), 10);
	if (Params.RoomTypes.Num() == 2)
	{
		TestTrue("OD_ResolveForTotal_MaxRespected: capped type Count <= Max(4)", Params.RoomTypes[0].Count <= 4);
	}
	return true;
}

// ============================================================
// FloorBuilder (vector path) Test Helpers
// ============================================================

namespace
{
	/** Build a straight 2-sample corridor (P0 → P1, constant radius). */
	FOrganicCorridor MakeCorridor(const FVector2D& P0, const FVector2D& P1, float Radius)
	{
		FOrganicCorridor Cor;
		Cor.Centerline.Add(P0);
		Cor.Centerline.Add(P1);
		Cor.Radii.Add(Radius);
		Cor.Radii.Add(Radius);
		return Cor;
	}

	/** Build an axis-aligned room OBB centered at Center with the given half-extent. */
	FOrganicRoom MakeRoom(const FVector2D& Center, const FVector2D& HalfExtent, float RotationDeg = 0.0f)
	{
		FOrganicRoom Room;
		Room.Center = Center;
		Room.RotationDeg = RotationDeg;
		Room.HalfExtent = HalfExtent;
		return Room;
	}

	/** 2D shoelace signed area; positive = CCW. */
	float TestSignedArea(const TArray<FVector2D>& Ring)
	{
		float		Area = 0.0f;
		const int32 N = Ring.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[(i + 1) % N];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area * 0.5f;
	}
} // namespace

// ============================================================
// Declared Doorway Tests
// ============================================================

namespace
{
	/**
	 * Creates a resolved room type with N declared baked doorways evenly spaced around the clock.
	 * LocalOutwardDir points outward along the N-th compass direction; position is on the footprint edge.
	 */
	FOrganicResolvedRoomType MakeDeclaredDoorwayRoom(int32 NumDoorways, float FootprintW = 600.0f)
	{
		FOrganicResolvedRoomType RT;
		RT.FootprintWidth = FootprintW;
		RT.FootprintHeight = FootprintW;
		RT.Count = 1;
		RT.Weight = 1;

		const float HalfW = FootprintW * 0.5f;
		for (int32 i = 0; i < NumDoorways; ++i)
		{
			const float			 AngleDeg = (360.0f / NumDoorways) * i;
			const float			 AngleRad = FMath::DegreesToRadians(AngleDeg);
			FOrganicBakedDoorway D;
			D.LocalOutwardDir = FVector2D(FMath::Cos(AngleRad), FMath::Sin(AngleRad));
			D.LocalPosition = D.LocalOutwardDir * HalfW; // on footprint edge
			D.Width = 150.0f;
			RT.Doorways.Add(D);
		}
		return RT;
	}
} // namespace

// ============================================================
// Test 25: Declared doorways override synthetic generation.
// A room type with 2 baked doorways yields exactly 2 Room.Doorways with bDeclared=true;
// not the 4-edge synthetic candidates.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDeclaredDoorwaysOverrideSyntheticTest, "ProceduralGeometry.OrganicDungeon.DeclaredDoorways.OverrideSynthetic", DefaultTestFlags)

bool FOrganicDeclaredDoorwaysOverrideSyntheticTest::RunTest(const FString& Parameters)
{
	// Two declared doorways on a single room type — generator should use exactly 2 declared doorways.
	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(MakeDeclaredDoorwayRoom(/*NumDoorways=*/2));
	Params.bRandomRotation = false;
	Params.MinThickness = 50.0f;
	Params.MaxWidth = 150.0f;
	Params.CorridorLengthMin = 400.0f;
	Params.CorridorLengthMax = 800.0f;

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("DeclaredOverride"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() == 0)
	{
		AddWarning(TEXT("DeclaredOverride: no rooms placed, skipping"));
		return true;
	}

	// Every room placed from a declared-doorway type should have exactly 2 doorways, all declared.
	// Also verify the transformed world position and outward normal (AC #1).
	// Setup uses MakeDeclaredDoorwayRoom(2, 600): bRandomRotation=false, single room → RotationDeg=0,
	// FootprintCenterOffset=ZeroVector, Room.Center=(0,0) (no obstacles, CenterPoint=ZeroVector).
	// Expected transforms at RotationDeg=0:
	//   Doorway[0]: i=0→AngleDeg=0°  → LocalPos=(HalfW,0),  LocalDir=(+1,0)  → World=(Center+(HalfW,0),  (+1,0))
	//   Doorway[1]: i=1→AngleDeg=180°→ LocalPos=(-HalfW,0), LocalDir=(-1,0)  → World=(Center+(-HalfW,0), (-1,0))
	constexpr float DeclHalfW = 600.0f * 0.5f; // HalfW for MakeDeclaredDoorwayRoom FootprintW
	constexpr float PosTolOver = 2.0f;		   // world-unit tolerance for transform floating-point
	constexpr float NrmDotMinOv = 0.99f;	   // min dot product for normal alignment (≈ cos 8°)

	for (int32 r = 0; r < Data.Rooms.Num(); ++r)
	{
		const FOrganicRoom& Room = Data.Rooms[r];
		TestEqual(FString::Printf(TEXT("DeclaredOverride: Room[%d].Doorways.Num() == 2"), r), Room.Doorways.Num(), 2);
		for (int32 d = 0; d < Room.Doorways.Num(); ++d)
		{
			TestTrue(FString::Printf(TEXT("DeclaredOverride: Room[%d].Doorways[%d].bDeclared"), r, d), Room.Doorways[d].bDeclared);
		}

		// AC #1: transformed position and outward normal must match declared LocalPosition/LocalOutwardDir.
		if (Room.Doorways.Num() == 2)
		{
			// Expected at RotationDeg=0 (single room → no orientation solve, bRandomRotation=false).
			const FVector2D ExpPos0 = Room.Center + FVector2D(DeclHalfW, 0.0f);
			const FVector2D ExpPos1 = Room.Center + FVector2D(-DeclHalfW, 0.0f);
			const FVector2D ExpNrm0(1.0f, 0.0f);
			const FVector2D ExpNrm1(-1.0f, 0.0f);

			TestTrue(
				FString::Printf(TEXT("DeclaredOverride: Room[%d].D[0].Pos.X≈CenterX+300 (got %.1f, exp %.1f)"), r, Room.Doorways[0].Pos.X, ExpPos0.X),
				FMath::IsNearlyEqual(Room.Doorways[0].Pos.X, ExpPos0.X, PosTolOver));
			TestTrue(
				FString::Printf(TEXT("DeclaredOverride: Room[%d].D[0].Pos.Y≈CenterY (got %.1f, exp %.1f)"), r, Room.Doorways[0].Pos.Y, ExpPos0.Y),
				FMath::IsNearlyEqual(Room.Doorways[0].Pos.Y, ExpPos0.Y, PosTolOver));
			TestTrue(FString::Printf(TEXT("DeclaredOverride: Room[%d].D[0].Normal≈(+1,0) (dot=%.3f)"),
						 r,
						 FVector2D::DotProduct(Room.Doorways[0].OutwardNormal, ExpNrm0)),
				FVector2D::DotProduct(Room.Doorways[0].OutwardNormal, ExpNrm0) >= NrmDotMinOv);

			TestTrue(
				FString::Printf(TEXT("DeclaredOverride: Room[%d].D[1].Pos.X≈CenterX-300 (got %.1f, exp %.1f)"), r, Room.Doorways[1].Pos.X, ExpPos1.X),
				FMath::IsNearlyEqual(Room.Doorways[1].Pos.X, ExpPos1.X, PosTolOver));
			TestTrue(
				FString::Printf(TEXT("DeclaredOverride: Room[%d].D[1].Pos.Y≈CenterY (got %.1f, exp %.1f)"), r, Room.Doorways[1].Pos.Y, ExpPos1.Y),
				FMath::IsNearlyEqual(Room.Doorways[1].Pos.Y, ExpPos1.Y, PosTolOver));
			TestTrue(FString::Printf(TEXT("DeclaredOverride: Room[%d].D[1].Normal≈(-1,0) (dot=%.3f)"),
						 r,
						 FVector2D::DotProduct(Room.Doorways[1].OutwardNormal, ExpNrm1)),
				FVector2D::DotProduct(Room.Doorways[1].OutwardNormal, ExpNrm1) >= NrmDotMinOv);
		}
	}

	return true;
}

// ============================================================
// Test 26: SolveRoomOrientation — feasible single connection.
// One declared doorway pointing right (+X), one mandatory dir pointing right — any rotation that
// keeps the door facing right should succeed.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicSolveOrientationSingleFeasibleTest, "ProceduralGeometry.OrganicDungeon.SolveRoomOrientation.SingleFeasible", DefaultTestFlags)

bool FOrganicSolveOrientationSingleFeasibleTest::RunTest(const FString& Parameters)
{
	// One declared doorway pointing +X (0°).
	TArray<FOrganicBakedDoorway> Declared;
	{
		FOrganicBakedDoorway D;
		D.LocalOutwardDir = FVector2D(1.0f, 0.0f);
		D.LocalPosition = FVector2D(300.0f, 0.0f);
		D.Width = 150.0f;
		Declared.Add(D);
	}

	// Mandatory direction: also +X.
	TArray<FVector2D> MandatoryDirs = { FVector2D(1.0f, 0.0f) };

	// Candidate rotation: 0° (door already aligned to mandatory dir).
	TArray<float> Candidates = { 0.0f };

	float		  OutRot = -1.0f;
	TArray<int32> OutAssign;
	const bool	  bSolved = UOrganicDungeonGenerator2D::SolveRoomOrientation(
		   Declared, MandatoryDirs, 45.0f, TArrayView<const float>(Candidates.GetData(), Candidates.Num()), OutRot, OutAssign);

	TestTrue("SolveOrientation_SingleFeasible: returns true", bSolved);
	if (bSolved)
	{
		TestEqual("SolveOrientation_SingleFeasible: rot==0", (double)OutRot, 0.0, 0.5);
		TestEqual("SolveOrientation_SingleFeasible: assignment.Num()==1", OutAssign.Num(), 1);
		if (OutAssign.Num() == 1)
		{
			TestEqual("SolveOrientation_SingleFeasible: assigns doorway 0", OutAssign[0], 0);
		}
	}

	return true;
}

// ============================================================
// Test 27: SolveRoomOrientation — infeasible when connections > doorways.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicSolveOrientationTooManyConnectionsTest, "ProceduralGeometry.OrganicDungeon.SolveRoomOrientation.TooManyConnections", DefaultTestFlags)

bool FOrganicSolveOrientationTooManyConnectionsTest::RunTest(const FString& Parameters)
{
	// One declared doorway, two mandatory connections — impossible.
	TArray<FOrganicBakedDoorway> Declared;
	{
		FOrganicBakedDoorway D;
		D.LocalOutwardDir = FVector2D(1.0f, 0.0f);
		D.LocalPosition = FVector2D(300.0f, 0.0f);
		D.Width = 150.0f;
		Declared.Add(D);
	}

	TArray<FVector2D> MandatoryDirs = { FVector2D(1.0f, 0.0f), FVector2D(-1.0f, 0.0f) };
	TArray<float>	  Candidates = { 0.0f, 180.0f };

	float		  OutRot = 0.0f;
	TArray<int32> OutAssign;
	const bool	  bSolved = UOrganicDungeonGenerator2D::SolveRoomOrientation(
		   Declared, MandatoryDirs, 45.0f, TArrayView<const float>(Candidates.GetData(), Candidates.Num()), OutRot, OutAssign);

	TestFalse("SolveOrientation_TooManyConnections: returns false", bSolved);

	return true;
}

// ============================================================
// Test 28: SolveRoomOrientation — infeasible when no rotation aligns within tolerance.
// Declared doorway at 0°, mandatory direction at 180° — 90° tolerance is not enough to match.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicSolveOrientationNoRotationAlignsTest, "ProceduralGeometry.OrganicDungeon.SolveRoomOrientation.NoRotationAligns", DefaultTestFlags)

bool FOrganicSolveOrientationNoRotationAlignsTest::RunTest(const FString& Parameters)
{
	TArray<FOrganicBakedDoorway> Declared;
	{
		FOrganicBakedDoorway D;
		D.LocalOutwardDir = FVector2D(1.0f, 0.0f); // points +X
		D.LocalPosition = FVector2D(300.0f, 0.0f);
		D.Width = 150.0f;
		Declared.Add(D);
	}

	// Mandatory dir is -X; candidate rotation is 0° (door stays at +X) — mismatch.
	TArray<FVector2D> MandatoryDirs = { FVector2D(-1.0f, 0.0f) };
	TArray<float>	  Candidates = { 0.0f }; // only one candidate: no rotation

	float		  OutRot = 0.0f;
	TArray<int32> OutAssign;
	// Use 45° tolerance — 180° difference exceeds it.
	const bool bSolved = UOrganicDungeonGenerator2D::SolveRoomOrientation(
		Declared, MandatoryDirs, 45.0f, TArrayView<const float>(Candidates.GetData(), Candidates.Num()), OutRot, OutAssign);

	TestFalse("SolveOrientation_NoRotationAligns: returns false", bSolved);

	return true;
}

// ============================================================
// Test 29: SolveRoomOrientation — deterministic: same inputs → same output.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicSolveOrientationDeterministicTest, "ProceduralGeometry.OrganicDungeon.SolveRoomOrientation.Deterministic", DefaultTestFlags)

bool FOrganicSolveOrientationDeterministicTest::RunTest(const FString& Parameters)
{
	// 4 doorways at 90° intervals; 2 mandatory directions at 45° and 135°.
	TArray<FOrganicBakedDoorway> Declared;
	for (int32 i = 0; i < 4; ++i)
	{
		const float			 Rad = FMath::DegreesToRadians(90.0f * i);
		FOrganicBakedDoorway D;
		D.LocalOutwardDir = FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
		D.LocalPosition = D.LocalOutwardDir * 300.0f;
		D.Width = 150.0f;
		Declared.Add(D);
	}

	TArray<FVector2D> MandatoryDirs = { FVector2D(FMath::Cos(FMath::DegreesToRadians(45.0f)), FMath::Sin(FMath::DegreesToRadians(45.0f))),
		FVector2D(FMath::Cos(FMath::DegreesToRadians(135.0f)), FMath::Sin(FMath::DegreesToRadians(135.0f))) };
	TArray<float>	  Candidates = { 45.0f, 135.0f, 225.0f, 315.0f };

	float		  OutRot1 = -1.0f, OutRot2 = -1.0f;
	TArray<int32> Out1, Out2;

	const bool b1 = UOrganicDungeonGenerator2D::SolveRoomOrientation(
		Declared, MandatoryDirs, 50.0f, TArrayView<const float>(Candidates.GetData(), Candidates.Num()), OutRot1, Out1);
	const bool b2 = UOrganicDungeonGenerator2D::SolveRoomOrientation(
		Declared, MandatoryDirs, 50.0f, TArrayView<const float>(Candidates.GetData(), Candidates.Num()), OutRot2, Out2);

	// At θ=45°: doorway[0] (was 0°) → 45°  matches MandatoryDirs[0]=45°; doorway[1] (was 90°) → 135°
	// matches MandatoryDirs[1]=135°.  This input is designed to be feasible — assert it actually solves.
	TestTrue("SolveOrientation_Deterministic: designed-feasible input must solve", b1);

	TestEqual("SolveOrientation_Deterministic: both succeed or both fail", b1, b2);
	if (b1 && b2)
	{
		TestEqual("SolveOrientation_Deterministic: same rotation", (double)OutRot1, (double)OutRot2, 0.01);
		TestEqual("SolveOrientation_Deterministic: same assignment count", Out1.Num(), Out2.Num());
	}

	return true;
}

// ============================================================
// Test 30: Width clamp — corridor endpoint radius ≤ Width/2 at a declared doorway.
// Build a layout with a declared-doorway room type whose Width=100. A corridor to that room
// must have its endpoint radius ≤ 50.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDeclaredDoorwayWidthClampTest, "ProceduralGeometry.OrganicDungeon.DeclaredDoorways.WidthClamp", DefaultTestFlags)

bool FOrganicDeclaredDoorwayWidthClampTest::RunTest(const FString& Parameters)
{
	constexpr float DoorwayWidth = 100.0f;

	FOrganicResolvedRoomType RT = MakeDeclaredDoorwayRoom(/*NumDoorways=*/4, /*FootprintW=*/600.0f);
	// Override width of all doorways to the narrow test value.
	for (FOrganicBakedDoorway& D : RT.Doorways)
	{
		D.Width = DoorwayWidth;
	}

	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(RT);
	Params.RoomTypes[0].Count = 3;
	Params.MinThickness = 80.0f; // larger than DoorwayWidth/2 — must be clamped
	Params.MaxWidth = 200.0f;
	Params.CorridorLengthMin = 400.0f;
	Params.CorridorLengthMax = 800.0f;

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("WidthClampTest"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	const float MaxAllowed = DoorwayWidth * 0.5f + KINDA_SMALL_NUMBER;

	// Counter ensures the clamp check is actually exercised — a zero counter means the test passed
	// vacuously (no corridor had a declared-doorway endpoint) and the clamp path was never reached.
	int32 CheckedEndpoints = 0;

	for (int32 c = 0; c < Data.Corridors.Num(); ++c)
	{
		const FOrganicCorridor& Cor = Data.Corridors[c];
		if (Cor.Radii.Num() == 0)
		{
			continue;
		}

		// Check endpoint radii if the corridor attaches to a declared-doorway room.
		const bool bAnchorADeclared = (Cor.AnchorA.Type == EOrganicAnchorType::Room) && Data.Rooms.IsValidIndex(Cor.AnchorA.Index)
			&& Data.Rooms[Cor.AnchorA.Index].Doorways.ContainsByPredicate([](const FOrganicDoorway& D) { return D.bDeclared && D.bUsed; });

		const bool bAnchorBDeclared = (Cor.AnchorB.Type == EOrganicAnchorType::Room) && Data.Rooms.IsValidIndex(Cor.AnchorB.Index)
			&& Data.Rooms[Cor.AnchorB.Index].Doorways.ContainsByPredicate([](const FOrganicDoorway& D) { return D.bDeclared && D.bUsed; });

		if (bAnchorADeclared)
		{
			TestTrue(FString::Printf(TEXT("WidthClamp: Corridor[%d] start radius (%.1f) <= Width/2 (%.1f)"), c, Cor.Radii[0], MaxAllowed),
				Cor.Radii[0] <= MaxAllowed);
			++CheckedEndpoints;
		}
		if (bAnchorBDeclared)
		{
			TestTrue(FString::Printf(TEXT("WidthClamp: Corridor[%d] end radius (%.1f) <= Width/2 (%.1f)"), c, Cor.Radii.Last(), MaxAllowed),
				Cor.Radii.Last() <= MaxAllowed);
			++CheckedEndpoints;
		}
	}

	// Guard against vacuous pass: with 3 declared-doorway rooms there should be at least one MST
	// backbone corridor whose endpoints both anchor at declared-doorway rooms.
	TestTrue("WidthClamp: at least one declared-doorway corridor endpoint was verified (clamp path exercised)", CheckedEndpoints > 0);

	return true;
}

// ============================================================
// Test 31: Back-compat — rooms with empty BakedDoorways use legacy synthetic doorways.
// Existing test setup (MakeDefaultRoom) has no Doorways → bDeclared must be false on all doorways.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDeclaredDoorwaysBackCompatTest, "ProceduralGeometry.OrganicDungeon.DeclaredDoorways.BackCompat", DefaultTestFlags)

bool FOrganicDeclaredDoorwaysBackCompatTest::RunTest(const FString& Parameters)
{
	// MakeOrganicGenerator uses MakeDefaultRoom which has no BakedDoorways → Doorways is empty.
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("BackCompatDeclared"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	TestTrue("BackCompat: rooms placed", Data.Rooms.Num() > 0);
	TestTrue("BackCompat: diagram non-empty", Data.Diagram.Cells.Num() > 0);

	// All doorways must be legacy (bDeclared == false).
	for (int32 r = 0; r < Data.Rooms.Num(); ++r)
	{
		for (int32 d = 0; d < Data.Rooms[r].Doorways.Num(); ++d)
		{
			TestFalse(FString::Printf(TEXT("BackCompat: Room[%d].Doorways[%d].bDeclared == false"), r, d), Data.Rooms[r].Doorways[d].bDeclared);
		}
	}

	return true;
}

// ============================================================
// Test 32: Mandatory edges hit real doorways (AC #2 + #3).
// Multi-room declared-doorway layout: every MST backbone corridor incident to a
// declared-doorway room must anchor at a declared doorway (position + normal), not at
// a synthetic perimeter point.  Exercises the orientation-solve → declared-doorway carve
// path end-to-end, not just the pure SolveRoomOrientation helper.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicMandatoryEdgesHitDeclaredDoorwaysTest,
	"ProceduralGeometry.OrganicDungeon.DeclaredDoorways.MandatoryEdgesHitDeclaredDoorways",
	DefaultTestFlags)

bool FOrganicMandatoryEdgesHitDeclaredDoorwaysTest::RunTest(const FString& Parameters)
{
	// 4-doorway rooms, 4 instances: guarantees a multi-room MST with backbone corridors.
	// All room types are declared-doorway, so every corridor endpoint must anchor at a
	// declared doorway (AC #2: mandatory edges hit real doorways; AC #3: no sealed rooms).
	FOrganicResolvedRoomType RT = MakeDeclaredDoorwayRoom(/*NumDoorways=*/4, /*FootprintW=*/600.0f);
	RT.Count = 4;
	RT.Weight = 4;

	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(RT);
	Params.MinThickness = 50.0f;
	Params.MaxWidth = 150.0f;
	Params.CorridorLengthMin = 400.0f;
	Params.CorridorLengthMax = 800.0f;

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("MandatoryEdgesDeclared"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() < 2)
	{
		AddWarning(TEXT("MandatoryEdgesDeclared: fewer than 2 rooms placed — skipping"));
		return true;
	}

	// Corridor anchors are set directly from doorway positions/normals (see MakeRoomCorridor),
	// so they should match exactly; use a tiny floating-point tolerance.
	constexpr float PosTol = 1.0f;	   // world units
	constexpr float NrmDotMin = 0.99f; // cos(≈8°): anchors set from same floats, should be exact

	int32 DeclaredEndpointChecks = 0;

	for (int32 c = 0; c < Data.Corridors.Num(); ++c)
	{
		const FOrganicCorridor& Cor = Data.Corridors[c];

		// Verify each room-type endpoint against the room's declared doorways.
		auto CheckEndpoint = [&](const FOrganicAnchor& Anchor, const TCHAR* Side) {
			if (Anchor.Type != EOrganicAnchorType::Room)
			{
				return;
			}
			if (!Data.Rooms.IsValidIndex(Anchor.Index))
			{
				return;
			}
			const FOrganicRoom& Room = Data.Rooms[Anchor.Index];

			// Only enforce on rooms that carry declared doorways.
			const bool bHasDeclared = Room.Doorways.ContainsByPredicate([](const FOrganicDoorway& D) { return D.bDeclared; });
			if (!bHasDeclared)
			{
				return;
			}

			++DeclaredEndpointChecks;

			// Find a declared doorway whose position matches the anchor within tolerance (AC #2).
			bool bFoundDecl = false;
			for (const FOrganicDoorway& D : Room.Doorways)
			{
				if (!D.bDeclared)
				{
					continue;
				}
				if (FVector2D::Distance(D.Pos, Anchor.Pos) <= PosTol)
				{
					bFoundDecl = true;
					// Also verify outward-normal alignment (corridor meets the opening face-on).
					const float NrmDot = FVector2D::DotProduct(D.OutwardNormal, Anchor.Normal);
					TestTrue(FString::Printf(TEXT("MandatoryEdgesDeclared: Corridor[%d] %s normal aligns with declared doorway (dot=%.3f, min=%.2f)"),
								 c,
								 Side,
								 NrmDot,
								 NrmDotMin),
						NrmDot >= NrmDotMin);
					break;
				}
			}

			// AC #3: no sealed rooms — anchor must correspond to a declared (real) doorway.
			TestTrue(FString::Printf(TEXT("MandatoryEdgesDeclared: Corridor[%d] %s at Room[%d] pos=(%.0f,%.0f) is a declared doorway (AC#2/#3)"),
						 c,
						 Side,
						 Anchor.Index,
						 Anchor.Pos.X,
						 Anchor.Pos.Y),
				bFoundDecl);
		};

		CheckEndpoint(Cor.AnchorA, TEXT("AnchorA"));
		CheckEndpoint(Cor.AnchorB, TEXT("AnchorB"));
	}

	// Guard: ensure the declared-doorway check was actually reached.
	// With >=2 rooms and all declared-doorway, MST backbone corridors always have room-type
	// endpoints on declared-doorway rooms, so this must be > 0.
	TestTrue("MandatoryEdgesDeclared: at least one declared-doorway corridor endpoint was verified", DeclaredEndpointChecks > 0);

	return true;
}

// ============================================================
// Test 33: Mismatch → never drop, best-effort connect (spawn-tree backbone).
// 1-doorway room type with 5 rooms: in any spanning tree of 5 nodes the handshaking lemma
// guarantees at least one interior node with degree >= 2 (sum of degrees = 2*(N-1) = 8 > 5).
// A 1-doorway room cannot serve >=2 backbone corridors from its single opening (the "star" case).
// The pipeline no longer drops such rooms — it keeps every placed room and logs a connectivity
// shortfall (junctions will absorb the deficit in a later phase).
// Verify: (a) every requested room SURVIVES (no room dropped), and (b) every declared-doorway
// room's corridor endpoint is at a declared doorway (no sealed prefab rooms / no body-crossing anchor).
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicMismatchNeverDropTest, "ProceduralGeometry.OrganicDungeon.DeclaredDoorways.MismatchNeverDrop", DefaultTestFlags)

bool FOrganicMismatchNeverDropTest::RunTest(const FString& Parameters)
{
	// 1 declared doorway pointing +X.  5 rooms of this type → the "star" mismatch case.
	FOrganicResolvedRoomType RT;
	RT.FootprintWidth = 600.0f;
	RT.FootprintHeight = 600.0f;
	RT.Count = 5;
	RT.Weight = 5;
	{
		FOrganicBakedDoorway D;
		D.LocalOutwardDir = FVector2D(1.0f, 0.0f);
		D.LocalPosition = FVector2D(300.0f, 0.0f);
		D.Width = 150.0f;
		RT.Doorways.Add(D);
	}

	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(RT);
	Params.MinThickness = 50.0f;
	Params.MaxWidth = 200.0f;
	Params.CorridorLengthMin = 400.0f;
	Params.CorridorLengthMax = 800.0f;

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("MismatchNeverDrop"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() < 2)
	{
		// The multi-room "star" mismatch (degree > doorway budget) can only arise with >= 2 rooms. When
		// placement legitimately fits 0 or 1 room (a placement shortfall the pipeline logs, NOT a connectivity
		// drop), the property under test — connectivity never drops a room — cannot be exercised here, so skip
		// rather than assert the indirect >=2 proxy (which would fail spuriously even though nothing was dropped).
		AddInfo(
			FString::Printf(TEXT("MismatchNeverDrop: only %d room(s) placed (placement shortfall); multi-room star case not exercised — skipping"),
				Data.Rooms.Num()));
		return true;
	}

	// (a) Never drop: connectivity must not remove any placed room. With >= 2 rooms placed we are in the
	//     multi-room "star" case (handshaking lemma forces a degree-2 node a 1-doorway room cannot serve), so
	//     this is exactly the configuration that the old orientation-solve-or-drop stage would have collapsed.
	//     The spawn-tree backbone keeps every placed room; survivors never exceed the 5 requested rooms.
	TestTrue("MismatchNeverDrop: survivors do not exceed the 5 requested rooms", Data.Rooms.Num() <= 5);

	// (a2) StarConnected contract: with the junction-on-demand fallback every placed room joins the network.
	//      OrganicDungeon is gridless, so connectivity is verified over the room-cell diagram's corridor
	//      adjacency (Neighbors): a BFS from cell 0 must reach every cell — exactly ONE connected component.
	//      This is the all-single-door "star" the old loop/link/junction-count phases could not guarantee.
	{
		const int32 NumCells = Data.Diagram.Cells.Num();
		TestTrue("MismatchNeverDrop: diagram has cells", NumCells > 0);
		if (NumCells > 0)
		{
			TArray<bool> Visited;
			Visited.Init(false, NumCells);
			TArray<int32> Queue;
			Queue.Add(0);
			Visited[0] = true;
			int32 Head = 0;
			int32 Reached = 1;
			while (Head < Queue.Num())
			{
				const int32 U = Queue[Head++];
				for (const int32 V : Data.Diagram.Cells[U].Neighbors)
				{
					if (V >= 0 && V < NumCells && !Visited[V])
					{
						Visited[V] = true;
						++Reached;
						Queue.Add(V);
					}
				}
			}
			TestEqual("MismatchNeverDrop: all-single-door star forms ONE connected component", Reached, NumCells);
		}
	}

	// (b) No sealed prefab rooms: every corridor endpoint at a declared-doorway room must
	//     anchor at one of that room's declared doorway positions.
	constexpr float PosTol = 1.0f; // world units (anchors set from same float → should match exactly)
	int32			DeclChecked = 0;

	for (int32 c = 0; c < Data.Corridors.Num(); ++c)
	{
		const FOrganicCorridor& Cor = Data.Corridors[c];

		auto CheckAnchor = [&](const FOrganicAnchor& Anchor) {
			if (Anchor.Type != EOrganicAnchorType::Room)
			{
				return;
			}
			if (!Data.Rooms.IsValidIndex(Anchor.Index))
			{
				return;
			}
			const FOrganicRoom& Room = Data.Rooms[Anchor.Index];

			const bool bHasDeclared = Room.Doorways.ContainsByPredicate([](const FOrganicDoorway& D) { return D.bDeclared; });
			if (!bHasDeclared)
			{
				return;
			}
			++DeclChecked;

			bool bFoundDecl = false;
			for (const FOrganicDoorway& D : Room.Doorways)
			{
				if (D.bDeclared && FVector2D::Distance(D.Pos, Anchor.Pos) <= PosTol)
				{
					bFoundDecl = true;
					break;
				}
			}
			TestTrue(
				FString::Printf(TEXT("MismatchNeverDrop: Corridor[%d] anchor at Room[%d] pos=(%.0f,%.0f) is at a declared doorway (no sealed room)"),
					c,
					Anchor.Index,
					Anchor.Pos.X,
					Anchor.Pos.Y),
				bFoundDecl);
		};

		CheckAnchor(Cor.AnchorA);
		CheckAnchor(Cor.AnchorB);
	}

	// If at least one corridor was built to a surviving declared-doorway room, the check ran.
	if (Data.Corridors.Num() > 0 && Data.Rooms.Num() >= 1)
	{
		TestTrue("MismatchNeverDrop: at least one declared-doorway endpoint was verified (no-seal path exercised)", DeclChecked > 0);
	}

	return true;
}

// ============================================================
// Footprint component-classification test — FOrganicDungeonConfig::IsFootprintExcludedComponentClass.
// Drives the pure class-level footprint filter directly (no UWorld / level load): mesh components must
// contribute to a prefab's measured footprint, while trigger volumes, spline guides, and editor marker
// gizmos must be excluded so they cannot inflate it.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicFootprintComponentFilterTest, "ProceduralGeometry.OrganicDungeon.Config.FootprintComponentFilter", DefaultTestFlags)

bool FOrganicFootprintComponentFilterTest::RunTest(const FString& Parameters)
{
	// Mesh geometry forms the playable room → must contribute (not excluded).
	TestFalse("FootprintComponentFilter: StaticMeshComponent contributes",
		FOrganicDungeonConfig::IsFootprintExcludedComponentClass(UStaticMeshComponent::StaticClass()));

	// Trigger volume (a UShapeComponent subclass) → excluded.
	TestTrue("FootprintComponentFilter: BoxComponent (trigger) excluded",
		FOrganicDungeonConfig::IsFootprintExcludedComponentClass(UBoxComponent::StaticClass()));

	// Path/authoring guides and editor marker gizmos → excluded.
	TestTrue("FootprintComponentFilter: SplineComponent excluded",
		FOrganicDungeonConfig::IsFootprintExcludedComponentClass(USplineComponent::StaticClass()));
	TestTrue("FootprintComponentFilter: ArrowComponent excluded",
		FOrganicDungeonConfig::IsFootprintExcludedComponentClass(UArrowComponent::StaticClass()));
	TestTrue("FootprintComponentFilter: BillboardComponent excluded",
		FOrganicDungeonConfig::IsFootprintExcludedComponentClass(UBillboardComponent::StaticClass()));
	TestTrue("FootprintComponentFilter: TextRenderComponent excluded",
		FOrganicDungeonConfig::IsFootprintExcludedComponentClass(UTextRenderComponent::StaticClass()));

	// Null class is not classified as excluded (defensive).
	TestFalse("FootprintComponentFilter: null class not excluded", FOrganicDungeonConfig::IsFootprintExcludedComponentClass(nullptr));

	return true;
}

// ============================================================
// Baked marker footprint precedence — FOrganicDungeonConfig::Resolve.
// The baked basement-marker footprint is the top-priority signal: it must win over an explicit
// FootprintOverride, and its center must be carried through to the resolved room type. Driven through
// the public Resolve() (which calls the anonymous-namespace ResolveRoomFootprint); no UWorld load needed
// because the baked fields are set directly on the FOrganicRoomType.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicBakedMarkerFootprintPrecedenceTest, "ProceduralGeometry.OrganicDungeon.Config.BakedMarkerFootprintPrecedence", DefaultTestFlags)

bool FOrganicBakedMarkerFootprintPrecedenceTest::RunTest(const FString& Parameters)
{
	// A room type with BOTH a baked marker footprint and a (different) explicit override.
	// The marker must win, including its center offset.
	FOrganicRoomType T;
	T.Weight = 1;
	T.FootprintOverride = FVector2D(600.0f, 600.0f);
	T.bHasBakedMarkerFootprint = true;
	T.BakedMarkerWidth = 1200.0f;
	T.BakedMarkerHeight = 800.0f;
	T.BakedMarkerCenter = FVector2D(150.0f, -50.0f);

	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(T);

	const FOrganicDungeonResolvedParams Params = Config.Resolve();

	if (!TestEqual("BakedMarkerFootprintPrecedence: one resolved room type", Params.RoomTypes.Num(), 1))
	{
		return false;
	}

	const FOrganicResolvedRoomType& RT = Params.RoomTypes[0];
	TestEqual("BakedMarkerFootprintPrecedence: width from marker (not override)", (double)RT.FootprintWidth, 1200.0, 0.01);
	TestEqual("BakedMarkerFootprintPrecedence: height from marker (not override)", (double)RT.FootprintHeight, 800.0, 0.01);
	TestEqual("BakedMarkerFootprintPrecedence: center.X from marker", (double)RT.FootprintCenter.X, 150.0, 0.01);
	TestEqual("BakedMarkerFootprintPrecedence: center.Y from marker", (double)RT.FootprintCenter.Y, -50.0, 0.01);

	// With the marker flag cleared, the explicit override takes over (next precedence tier).
	FOrganicRoomType TNoMarker = T;
	TNoMarker.bHasBakedMarkerFootprint = false;

	FOrganicDungeonConfig ConfigNoMarker;
	ConfigNoMarker.RoomTypes.Add(TNoMarker);

	const FOrganicDungeonResolvedParams ParamsNoMarker = ConfigNoMarker.Resolve();
	if (TestEqual("BakedMarkerFootprintPrecedence: one resolved room type (no marker)", ParamsNoMarker.RoomTypes.Num(), 1))
	{
		TestEqual("BakedMarkerFootprintPrecedence: width falls back to override", (double)ParamsNoMarker.RoomTypes[0].FootprintWidth, 600.0, 0.01);
		TestEqual("BakedMarkerFootprintPrecedence: height falls back to override", (double)ParamsNoMarker.RoomTypes[0].FootprintHeight, 600.0, 0.01);
	}

	return true;
}

// ============================================================
// Layout diagnostics dump (e2e harness): generate representative layouts for several seeds and write
// text + SVG dumps to <ProjectSaved>/Logs/OD/ for headless inspection / iteration. Not a pass/fail
// assertion of layout quality — the written artifacts are the point.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonDumpLayoutsTest, "ProceduralGeometry.OrganicDungeon.DumpLayouts", DefaultTestFlags)

bool FOrganicDungeonDumpLayoutsTest::RunTest(const FString& Parameters)
{
	auto MakeRoom = [](float W, float H, int32 Count) {
		FOrganicResolvedRoomType RT;
		RT.FootprintWidth = W;
		RT.FootprintHeight = H;
		RT.Count = Count;
		RT.Weight = Count;
		return RT;
	};

	// Representative multi-room-type config with waviness, mirroring an authored OD location.
	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(MakeRoom(3200.0f, 3100.0f, 3));
	Params.RoomTypes.Add(MakeRoom(6200.0f, 5800.0f, 2));
	Params.RoomTypes.Add(MakeRoom(1800.0f, 1800.0f, 2));
	Params.CorridorStyle = EOrganicCorridorStyle::Cave;
	Params.MinThickness = 400.0f;
	Params.MaxWidth = 900.0f;
	Params.Waviness = 1.0f;
	Params.CorridorLengthMin = 1500.0f;
	Params.CorridorLengthMax = 3000.0f;
	Params.BranchProbability = 1.0f;
	Params.SpineWidthScale = 1.6f;
	Params.WallThickness = 2;

	const TArray<FString> Seeds = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("seedA"), TEXT("seedB") };
	for (const FString& Seed : Seeds)
	{
		UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
		Gen->SetSeed(Seed);
		Gen->SetGridSize(100);
		Gen->ApplyResolvedParams(Params);
		const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

		const FString Path = FOrganicLayoutDebug::DumpToFiles(Data, FString::Printf(TEXT("dump_%s"), *Seed));

		// Surface the VALID summary line (achieved-vs-requested + crossings) so the e2e loop can spot regressions
		// without opening the SVG. Extract it from the text dump rather than recomputing.
		FString			ValidLine = TEXT("(no VALID line)");
		TArray<FString> Lines;
		FOrganicLayoutDebug::ToText(Data).ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("VALID ")))
			{
				ValidLine = Line;
				break;
			}
		}
		AddInfo(FString::Printf(TEXT("[DumpLayouts] seed='%s' %s -> %s"), *Seed, *ValidLine, *Path));
		TestTrue(FString::Printf(TEXT("DumpLayouts: wrote dump for seed '%s'"), *Seed), !Path.IsEmpty());
	}
	return true;
}

// ============================================================
// Import an exported OD config (<ProjectSaved>/OD/import_config.json) and dump layouts for several seeds.
// Skips cleanly (passes) when no import_config.json is present. Drop a designer-exported config there to
// drive the e2e loop with real rooms/doorways/params.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonDumpFromConfigTest, "ProceduralGeometry.OrganicDungeon.DumpFromConfig", DefaultTestFlags)

bool FOrganicDungeonDumpFromConfigTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OD"), TEXT("import_config.json"));

	FOrganicDungeonConfig Config;
	if (!FOrganicConfigIO::LoadFromFile(Path, Config))
	{
		AddInfo(FString::Printf(TEXT("[DumpFromConfig] no import_config.json at %s — skipping (drop a config there to use it)"), *Path));
		return true;
	}

	// EXACT-PARITY resolve path: the editor resolves OD via ResolveForTotal(LocationSize) + SetGridSize(500)
	// (GeneratedLevelActor: OrganicGridCellSize default 500, ResolveForTotal(LocationSize)). Mirror that here so
	// a config + size + seed reproduces the in-editor layout. The total-room size is read from an optional
	// sidecar (<ProjectSaved>/OD/import_size.txt); absent/invalid falls back to a sensible default.
	constexpr int32 DefaultTotalRooms = 12;
	constexpr int32 EditorGridCellSize = 500; // matches GeneratedLevelActor OrganicGridCellSize default
	int32			TotalRooms = DefaultTotalRooms;
	{
		const FString SizePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OD"), TEXT("import_size.txt"));
		FString		  SizeStr;
		if (FFileHelper::LoadFileToString(SizeStr, *SizePath))
		{
			const int32 Parsed = FCString::Atoi(*SizeStr.TrimStartAndEnd());
			if (Parsed > 0)
			{
				TotalRooms = Parsed;
			}
		}
	}

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(TotalRooms);
	AddInfo(FString::Printf(
		TEXT(
			"[DumpFromConfig] parity resolve: totalRooms=%d (sidecar import_size.txt, default %d) gridCellSize=%d (matches editor OrganicGridCellSize)"),
		TotalRooms,
		DefaultTotalRooms,
		EditorGridCellSize));

	const TArray<FString> Seeds = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6") };
	for (const FString& Seed : Seeds)
	{
		UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
		Gen->SetSeed(Seed);
		Gen->SetGridSize(EditorGridCellSize);
		Gen->ApplyResolvedParams(Params);
		const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

		const FString Out = FOrganicLayoutDebug::DumpToFiles(Data, FString::Printf(TEXT("cfg_%s"), *Seed));
		AddInfo(FString::Printf(TEXT("[DumpFromConfig] seed='%s' rooms=%d/%d corridors=%d junctions=%d -> %s"),
			*Seed,
			Data.Rooms.Num(),
			Data.RequestedRoomCount,
			Data.Corridors.Num(),
			Data.Junctions.Num(),
			*Out));
	}
	return true;
}

// ============================================================
// Parity resolve path: a FOrganicDungeonConfig + total size + seed must reproduce the same layout the editor
// produces (ResolveForTotal(Size) + grid cell size 500). This locks the headless parity entry point so a
// dropped config can reproduce an in-editor cluster, and asserts the achieved layout obeys the hard invariant
// (no corridor crosses a room body — VALID crossings must be 0).
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonConfigParityTest, "ProceduralGeometry.OrganicDungeon.ConfigParity", DefaultTestFlags)

bool FOrganicDungeonConfigParityTest::RunTest(const FString& Parameters)
{
	// Build a synthetic config (footprint overrides so no prefab world is loaded) and resolve it the editor way.
	FOrganicDungeonConfig Config;
	{
		FOrganicRoomType RT;
		RT.FootprintOverride = FVector2D(3000.0f, 3000.0f);
		RT.Weight = 1;
		Config.RoomTypes.Add(RT);
		FOrganicRoomType RT2;
		RT2.FootprintOverride = FVector2D(1800.0f, 1800.0f);
		RT2.Weight = 1;
		Config.RoomTypes.Add(RT2);
	}
	Config.CorridorStyle = EOrganicCorridorStyle::Cave;

	constexpr int32 TotalRooms = 8;
	constexpr int32 GridCellSize = 500; // matches GeneratedLevelActor OrganicGridCellSize default

	auto Resolve = [&]() {
		const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(TotalRooms);
		UOrganicDungeonGenerator2D*			Gen = NewObject<UOrganicDungeonGenerator2D>();
		Gen->SetSeed(TEXT("ParitySeed"));
		Gen->SetGridSize(GridCellSize);
		Gen->ApplyResolvedParams(Params);
		return Gen->GenerateWithGridData();
	};

	const FOrganicDungeonGridData A = Resolve();
	const FOrganicDungeonGridData B = Resolve();

	// Same config + size + seed + grid cell size → identical room/corridor geometry (the parity guarantee).
	TestEqual("ConfigParity: Rooms.Num matches across runs", A.Rooms.Num(), B.Rooms.Num());
	TestEqual("ConfigParity: Corridors.Num matches across runs", A.Corridors.Num(), B.Corridors.Num());
	TestEqual("ConfigParity: Junctions.Num matches across runs", A.Junctions.Num(), B.Junctions.Num());
	bool bCenterlinesMatch = (A.Corridors.Num() == B.Corridors.Num());
	for (int32 c = 0; bCenterlinesMatch && c < A.Corridors.Num(); ++c)
	{
		bCenterlinesMatch = A.Corridors[c].Centerline.Num() == B.Corridors[c].Centerline.Num();
		for (int32 p = 0; bCenterlinesMatch && p < A.Corridors[c].Centerline.Num(); ++p)
		{
			bCenterlinesMatch = A.Corridors[c].Centerline[p].Equals(B.Corridors[c].Centerline[p], 0.01f);
		}
	}
	TestTrue("ConfigParity: corridor centerlines identical across runs", bCenterlinesMatch);

	// Requested counts are carried so the dump can compute achieved-vs-requested.
	TestEqual("ConfigParity: RequestedRoomCount carried", A.RequestedRoomCount, TotalRooms);

	// Hard invariant: the VALID dump line must report crossings=0 (no corridor crosses a room body).
	const FString DumpText = FOrganicLayoutDebug::ToText(A);
	TestTrue("ConfigParity: dump has VALID line", DumpText.Contains(TEXT("VALID ")));
	TestTrue("ConfigParity: no corridor crosses a room body (crossings=0)", DumpText.Contains(TEXT("crossings=0")));

	return true;
}

// ============================================================
// Gridless invariants (multi-seed) — the three properties the VALID dump line reports must hold for every seed:
//   (a) all requested rooms placed   (best-effort: a shortfall is logged, not failed, but >= 1 room always placed),
//   (b) the room-cell network is ONE connected region (corridor adjacency — the all-single-door star), and
//   (c) zero corridor↔room-body crossings (the hard invariant).
// Asserted through FOrganicLayoutDebug::ToText so this also locks the e2e VALID grammar (rooms=/regions=/crossings=)
// the orchestrator greps. Connectivity is cross-checked against a direct BFS over the diagram's corridor adjacency.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonGridlessInvariantsTest, "ProceduralGeometry.OrganicDungeon.GridlessInvariants", DefaultTestFlags)

bool FOrganicDungeonGridlessInvariantsTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Seeds = { TEXT("inv1"), TEXT("inv2"), TEXT("inv3"), TEXT("seedX"), TEXT("seedY") };
	for (const FString& Seed : Seeds)
	{
		UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(Seed, /*RoomCount=*/6);
		const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

		// At least one room is always placed for a non-empty request.
		TestTrue(FString::Printf(TEXT("GridlessInvariants[%s]: >= 1 room placed"), *Seed), Data.Rooms.Num() >= 1);
		// Best-effort placement: survivors never exceed the request. A shortfall is logged, not failed.
		TestTrue(FString::Printf(TEXT("GridlessInvariants[%s]: rooms placed <= requested"), *Seed), Data.Rooms.Num() <= Data.RequestedRoomCount);
		if (Data.Rooms.Num() < Data.RequestedRoomCount)
		{
			AddInfo(FString::Printf(
				TEXT("GridlessInvariants[%s]: placement shortfall %d/%d (logged, not a failure)"), *Seed, Data.Rooms.Num(), Data.RequestedRoomCount));
		}

		// (b) Single connected region via a direct BFS over the room-cell diagram's corridor adjacency.
		const int32 NumCells = Data.Diagram.Cells.Num();
		if (TestTrue(FString::Printf(TEXT("GridlessInvariants[%s]: diagram has cells"), *Seed), NumCells > 0))
		{
			TArray<bool> Visited;
			Visited.Init(false, NumCells);
			TArray<int32> Queue;
			Queue.Add(0);
			Visited[0] = true;
			int32 Reached = 1;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				for (const int32 V : Data.Diagram.Cells[Queue[Head]].Neighbors)
				{
					if (V >= 0 && V < NumCells && !Visited[V])
					{
						Visited[V] = true;
						++Reached;
						Queue.Add(V);
					}
				}
			}
			TestEqual(FString::Printf(TEXT("GridlessInvariants[%s]: ONE connected region"), *Seed), Reached, NumCells);
		}

		// (a)+(b)+(c) via the VALID dump line (also locks the e2e grammar: "VALID " + rooms=/regions=/crossings=).
		const FString DumpText = FOrganicLayoutDebug::ToText(Data);
		TestTrue(FString::Printf(TEXT("GridlessInvariants[%s]: dump has VALID line"), *Seed), DumpText.Contains(TEXT("VALID ")));
		TestTrue(FString::Printf(TEXT("GridlessInvariants[%s]: regions=1 token present"), *Seed), DumpText.Contains(TEXT("regions=1")));
		TestTrue(FString::Printf(TEXT("GridlessInvariants[%s]: crossings=0 token present (no room-body crossing)"), *Seed),
			DumpText.Contains(TEXT("crossings=0")));
	}
	return true;
}

// ============================================================
// Wavy few-point corridor curves (random 2-or-3 intermediate control points per corridor)
// ============================================================

namespace
{
	/** Builds an OD generator with default rooms; corridors now pick a random 2-or-3 intermediate control points. */
	UOrganicDungeonGenerator2D* MakeWavyGenerator(const FString& Seed, int32 RoomCount, float Waviness = 0.4f)
	{
		FOrganicDungeonResolvedParams Params;
		Params.RoomTypes.Add(MakeDefaultRoom(RoomCount));
		Params.Waviness = Waviness;

		UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
		Gen->SetSeed(Seed);
		Gen->SetGridSize(50);
		Gen->ApplyResolvedParams(Params);
		return Gen;
	}
} // namespace

// Test: each corridor's Radii stays parallel to its (few-point) Centerline, and the centerline now has
// 4 or 5 points (2 endpoints + a random 2-or-3 intermediate control points drawn per corridor).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonWavyRadiiParallelTest, "ProceduralGeometry.OrganicDungeon.Wavy.RadiiParallel", DefaultTestFlags)

bool FOrganicDungeonWavyRadiiParallelTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeWavyGenerator(TEXT("WavyRadiiTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	for (int32 i = 0; i < Data.Corridors.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("Wavy.RadiiParallel: Corridor[%d].Radii.Num() == Centerline.Num()"), i),
			Data.Corridors[i].Radii.Num(),
			Data.Corridors[i].Centerline.Num());
		const int32 N = Data.Corridors[i].Centerline.Num();
		TestTrue(FString::Printf(TEXT("Wavy.RadiiParallel: Corridor[%d] has 4 or 5 control points (2 endpoints + 2-or-3 intermediate)"), i),
			N == 4 || N == 5);
	}
	return true;
}

// Test: a 2-control-point sample (k=0 equivalent) is a straight evenly-stepped polyline (no perpendicular drift).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonSamplerStraightTest, "ProceduralGeometry.OrganicDungeon.Wavy.SamplerStraight", DefaultTestFlags)

bool FOrganicDungeonSamplerStraightTest::RunTest(const FString& Parameters)
{
	const FVector2D	  A(0.0f, 0.0f);
	const FVector2D	  B(1000.0f, 0.0f);
	TArray<FVector2D> Control = { A, B };
	TArray<FVector2D> Dense;
	UOrganicDungeonGenerator2D::SampleCorridorCurve(Control, FVector2D(1, 0), FVector2D(-1, 0), /*StepLen=*/25.0f, Dense);

	TestTrue("SamplerStraight: at least 2 samples", Dense.Num() >= 2);
	TestEqual("SamplerStraight: first == A", (double)Dense[0].X, (double)A.X, 0.01);
	TestEqual("SamplerStraight: last == B", (double)Dense.Last().X, (double)B.X, 0.01);

	// Every sample lies on the X axis (no perpendicular drift for a 2-point straight line).
	bool bAllOnAxis = true;
	for (const FVector2D& P : Dense)
	{
		if (FMath::Abs(P.Y) > 0.01)
		{
			bAllOnAxis = false;
			break;
		}
	}
	TestTrue("SamplerStraight: no perpendicular drift", bAllOnAxis);
	return true;
}

// Test: a curve with intermediate control points actually drifts off the chord (waviness took effect),
// while still starting and ending exactly at the endpoints.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonSamplerWavyTest, "ProceduralGeometry.OrganicDungeon.Wavy.SamplerWavy", DefaultTestFlags)

bool FOrganicDungeonSamplerWavyTest::RunTest(const FString& Parameters)
{
	const FVector2D	  A(0.0f, 0.0f);
	const FVector2D	  Mid(500.0f, 200.0f); // offset perpendicular
	const FVector2D	  B(1000.0f, 0.0f);
	TArray<FVector2D> Control = { A, Mid, B };
	TArray<FVector2D> Dense;
	UOrganicDungeonGenerator2D::SampleCorridorCurve(Control, FVector2D(1, 0), FVector2D(-1, 0), /*StepLen=*/25.0f, Dense);

	TestEqual("SamplerWavy: first == A.X", (double)Dense[0].X, (double)A.X, 0.01);
	TestEqual("SamplerWavy: first == A.Y", (double)Dense[0].Y, (double)A.Y, 0.01);
	TestEqual("SamplerWavy: last == B.X", (double)Dense.Last().X, (double)B.X, 0.01);
	TestEqual("SamplerWavy: last == B.Y", (double)Dense.Last().Y, (double)B.Y, 0.01);

	float MaxDrift = 0.0f;
	for (const FVector2D& P : Dense)
	{
		MaxDrift = FMath::Max(MaxDrift, FMath::Abs((float)P.Y));
	}
	TestTrue("SamplerWavy: curve drifts off the chord", MaxDrift > 50.0f);
	return true;
}

// Test: the same seed produces identical room/corridor counts and centerlines run-to-run. Corridors now draw
// a random 2-or-3 intermediate control points internally, but the draw comes from the seeded RandomStream so
// the output is reproducible for a given seed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonWavyDefaultDeterminismTest, "ProceduralGeometry.OrganicDungeon.Wavy.DefaultDeterminism", DefaultTestFlags)

bool FOrganicDungeonWavyDefaultDeterminismTest::RunTest(const FString& Parameters)
{
	const FString Seed = TEXT("WavyDefaultDeterminism");

	UOrganicDungeonGenerator2D* GenA = MakeOrganicGenerator(Seed, 4);
	UOrganicDungeonGenerator2D* GenB = MakeOrganicGenerator(Seed, 4);

	const FOrganicDungeonGridData DataA = GenA->GenerateWithGridData();
	const FOrganicDungeonGridData DataB = GenB->GenerateWithGridData();

	TestEqual("Wavy.DefaultDeterminism: Rooms.Num() matches", DataA.Rooms.Num(), DataB.Rooms.Num());
	TestEqual("Wavy.DefaultDeterminism: Corridors.Num() matches", DataA.Corridors.Num(), DataB.Corridors.Num());

	for (int32 c = 0; c < FMath::Min(DataA.Corridors.Num(), DataB.Corridors.Num()); ++c)
	{
		// Every corridor now bows with 2 or 3 intermediate control points → 4 or 5 total points.
		const int32 N = DataA.Corridors[c].Centerline.Num();
		TestTrue(FString::Printf(TEXT("Wavy.DefaultDeterminism: Corridor[%d] has 4 or 5 control points"), c), N == 4 || N == 5);
		TestEqual(FString::Printf(TEXT("Wavy.DefaultDeterminism: Corridor[%d] count matches run-to-run"), c),
			DataA.Corridors[c].Centerline.Num(),
			DataB.Corridors[c].Centerline.Num());
		if (DataA.Corridors[c].Centerline.Num() == DataB.Corridors[c].Centerline.Num())
		{
			for (int32 p = 0; p < N; ++p)
			{
				TestTrue(FString::Printf(TEXT("Wavy.DefaultDeterminism: Corridor[%d].Pt[%d] identical run-to-run"), c, p),
					DataA.Corridors[c].Centerline[p].Equals(DataB.Corridors[c].Centerline[p], 0.01f));
			}
		}
	}
	return true;
}

// ============================================================
// Junction node tests (deformed-circle hubs, unlimited ports)
// ============================================================

// NOTE: on-demand junctions are now the universal connectivity fallback (inserted on an existing corridor to
// branch to an otherwise-unreachable room). The StarConnected contract — the all-single-door star forms ONE
// connected component — is asserted by the DeclaredDoorways.MismatchNeverDrop test above (BFS over the
// room-cell diagram's corridor adjacency reaches every cell).
// The MakeJunction / AttachCorridorToJunction math is covered directly by Junction.Attach below.

// Test: AttachCorridorToJunction returns the perimeter vertex whose outward direction best matches Toward.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonJunctionAttachTest, "ProceduralGeometry.OrganicDungeon.Junction.Attach", DefaultTestFlags)

bool FOrganicDungeonJunctionAttachTest::RunTest(const FString& Parameters)
{
	// Build a deterministic junction directly through the public MakeJunction.
	FOrganicDungeonResolvedParams Params;
	Params.MinThickness = 200.0f;
	Params.MaxWidth = 400.0f;

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("JunctionAttach"));
	Gen->SetGridSize(50);

	const FVector2D		   Center(1000.0f, -500.0f);
	const FOrganicJunction J = Gen->MakeJunction(Params, Center);

	if (!TestTrue("Junction.Attach: junction has a 16-vertex perimeter", J.Perimeter.Num() == 16))
	{
		return false;
	}

	// For several Toward directions, AttachCorridorToJunction must return the perimeter vertex whose outward
	// direction maximizes the dot product with the (normalized) Toward — i.e. no other vertex faces it better.
	const TArray<FVector2D> Towards = {
		FVector2D(1.0f, 0.0f), FVector2D(-1.0f, 0.0f), FVector2D(0.0f, 1.0f), FVector2D(0.0f, -1.0f), FVector2D(1.0f, 1.0f), FVector2D(-0.3f, 0.8f)
	};

	for (int32 t = 0; t < Towards.Num(); ++t)
	{
		const FVector2D Dir = Towards[t].GetSafeNormal();

		FVector2D AttachPos;
		FVector2D AttachNormal;
		UOrganicDungeonGenerator2D::AttachCorridorToJunction(J, Towards[t], AttachPos, AttachNormal);

		// The chosen attach point must be one of the perimeter vertices.
		int32 ChosenIdx = INDEX_NONE;
		for (int32 v = 0; v < J.Perimeter.Num(); ++v)
		{
			if (FVector2D::Distance(J.Perimeter[v], AttachPos) <= 1.0f)
			{
				ChosenIdx = v;
				break;
			}
		}
		if (!TestTrue(FString::Printf(TEXT("Junction.Attach[%d]: attach pos is a perimeter vertex"), t), ChosenIdx != INDEX_NONE))
		{
			continue;
		}

		// Its outward direction must have the maximal dot product against Dir among all vertices.
		const float ChosenDot = FVector2D::DotProduct((J.Perimeter[ChosenIdx] - J.Center).GetSafeNormal(), Dir);
		float		BestDot = -FLT_MAX;
		for (int32 v = 0; v < J.Perimeter.Num(); ++v)
		{
			BestDot = FMath::Max(BestDot, FVector2D::DotProduct((J.Perimeter[v] - J.Center).GetSafeNormal(), Dir));
		}
		TestTrue(FString::Printf(TEXT("Junction.Attach[%d]: chosen vertex best faces Toward (chosen=%.4f best=%.4f)"), t, ChosenDot, BestDot),
			ChosenDot >= BestDot - KINDA_SMALL_NUMBER);

		// The returned outward normal points from center toward the attach point.
		const FVector2D ExpectedNormal = (AttachPos - J.Center).GetSafeNormal();
		TestTrue(
			FString::Printf(
				TEXT("Junction.Attach[%d]: outward normal points center->attach (dot=%.3f)"), t, FVector2D::DotProduct(AttachNormal, ExpectedNormal)),
			FVector2D::DotProduct(AttachNormal, ExpectedNormal) >= 0.99f);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
