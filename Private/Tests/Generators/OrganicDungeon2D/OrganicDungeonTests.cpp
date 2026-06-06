// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"
#include "Generators/OrganicDungeon2D/OrganicFloorBuilder.h"
#include "Factories/ProceduralMeshFactory.h"
#include "../../ProceduralGeometryTestFlags.h"

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
// Test 3: Parallel array size invariants.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonParallelArraySizesTest, "ProceduralGeometry.OrganicDungeon.ParallelArraySizes", DefaultTestFlags)

bool FOrganicDungeonParallelArraySizesTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("ParallelTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.GridWidth == 0 || Data.GridHeight == 0)
	{
		AddWarning(TEXT("ParallelArraySizes: empty grid, skipping"));
		return true;
	}

	const int32 ExpectedGridSize = Data.GridWidth * Data.GridHeight;
	TestEqual("ParallelArraySizes: Grid.Num() == GridWidth * GridHeight", Data.Grid.Num(), ExpectedGridSize);
	TestEqual("ParallelArraySizes: CellType.Num() == Grid.Num()", Data.CellType.Num(), Data.Grid.Num());
	TestEqual("ParallelArraySizes: RegionIds.Num() == Grid.Num()", Data.RegionIds.Num(), Data.Grid.Num());

	// RoomFootprintCells is parallel to Rooms.
	TestEqual("ParallelArraySizes: RoomFootprintCells.Num() == Rooms.Num()", Data.RoomFootprintCells.Num(), Data.Rooms.Num());

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
// Test 5: OOM guard — GridSize=10 (the minimum after clamping) with a 21000×21000
// world-unit footprint. At GridSize=10, the rasterized grid is roughly:
//   GWidth  = ceil(21000/10) + 2*Pad = 2100 + 4 = 2104   (Pad = max(1,WallThickness)+1 = 2)
//   GHeight = 2104
//   Total   = 2104 × 2104 = 4,426,816  >  4,194,304 (OOM limit) ✓
// The generator logs an Error when the guard fires; register it as expected so the
// automation framework does not auto-fail the test on the Error-level log.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonOOMGuardTest, "ProceduralGeometry.OrganicDungeon.OOMGuard", DefaultTestFlags)

bool FOrganicDungeonOOMGuardTest::RunTest(const FString& Parameters)
{
	// Register the expected Error log so the automation runner does not auto-fail this test.
	AddExpectedError(TEXT("OOM guard triggered"), EAutomationExpectedErrorFlags::Contains);

	FOrganicResolvedRoomType BigRoom;
	BigRoom.FootprintWidth = 21000.0f; // at GridSize=10: 2100 cells/axis + padding > 4M total
	BigRoom.FootprintHeight = 21000.0f;
	BigRoom.Count = 1;

	FOrganicDungeonResolvedParams Params;
	Params.RoomTypes.Add(BigRoom);
	Params.WallThickness = 1; // explicit: Pad = max(1,1)+1 = 2 → GWidth = 2104

	UOrganicDungeonGenerator2D* Gen = NewObject<UOrganicDungeonGenerator2D>();
	Gen->SetSeed(TEXT("OOMTest"));
	Gen->SetGridSize(10); // minimum after clamp — sets CellSizeVal = 10
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	TestEqual("OOMGuard: Diagram.Cells.Num() == 0", Data.Diagram.Cells.Num(), 0);

	return true;
}

// ============================================================
// Test 6: No room types → empty result, no crash.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonNoRoomTypesTest, "ProceduralGeometry.OrganicDungeon.NoRoomTypes_EmptyResult", DefaultTestFlags)

bool FOrganicDungeonNoRoomTypesTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonResolvedParams EmptyParams;
	// RoomTypes is empty; bHasStartRoom/bHasExitPortalRoom default to false.

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
// Test 7: StartRoomIndex is valid and ExitAnchors are non-empty when rooms exist.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonEntranceAndAnchorsValidTest, "ProceduralGeometry.OrganicDungeon.EntranceAndAnchorsValid", DefaultTestFlags)

bool FOrganicDungeonEntranceAndAnchorsValidTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("EntranceAnchorTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() == 0)
	{
		AddWarning(TEXT("EntranceAndAnchorsValid: no rooms placed, skipping"));
		return true;
	}

	const int32 RoomCount = Data.Rooms.Num();

	// Entrance must be a valid room index.
	TestTrue("EntranceAndAnchorsValid: StartRoomIndex is valid", Data.StartRoomIndex >= 0 && Data.StartRoomIndex < RoomCount);

	// At least one exit anchor must be produced (default RequiredExitAnchors = 1).
	TestTrue("EntranceAndAnchorsValid: ExitAnchors is non-empty", Data.ExitAnchors.Num() > 0);

	// All anchor rooms must be valid indices and distinct from the entrance.
	for (int32 i = 0; i < Data.ExitAnchors.Num(); ++i)
	{
		const FOrganicExitAnchor& A = Data.ExitAnchors[i];
		TestTrue(FString::Printf(TEXT("EntranceAndAnchorsValid: anchor[%d] RoomIndex valid"), i), A.RoomIndex >= 0 && A.RoomIndex < RoomCount);
		TestTrue(FString::Printf(TEXT("EntranceAndAnchorsValid: anchor[%d] not at entrance"), i), A.RoomIndex != Data.StartRoomIndex);
		TestTrue(FString::Printf(TEXT("EntranceAndAnchorsValid: anchor[%d] GraphDistance > 0"), i), A.GraphDistance > 0);
	}

	return true;
}

// ============================================================
// Test 8: Cell type consistency — floor cells are Room or Corridor;
//         non-floor cells are Wall or Empty.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonCellTypeConsistencyTest, "ProceduralGeometry.OrganicDungeon.CellTypeConsistency", DefaultTestFlags)

bool FOrganicDungeonCellTypeConsistencyTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("CellTypeTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Grid.Num() == 0)
	{
		AddWarning(TEXT("CellTypeConsistency: empty grid, skipping"));
		return true;
	}

	const int32 Total = Data.Grid.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		const bool	bFloor = Data.Grid[i];
		const uint8 Type = Data.CellType[i];

		if (bFloor)
		{
			const bool bValidFloorType = (Type == EOrganicCellType::Corridor || Type == EOrganicCellType::Room);
			if (!bValidFloorType)
			{
				AddError(FString::Printf(TEXT("CellTypeConsistency: floor cell[%d] has non-floor type %d"), i, (int32)Type));
				break;
			}
		}
		else
		{
			const bool bValidNonFloorType = (Type == EOrganicCellType::Wall || Type == EOrganicCellType::Empty);
			if (!bValidNonFloorType)
			{
				AddError(FString::Printf(TEXT("CellTypeConsistency: non-floor cell[%d] has unexpected type %d"), i, (int32)Type));
				break;
			}
		}
	}

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
	// ExitPortalRoom has no level ref here so bHasExitPortalRoom = false; test just verifies StartRoom is unaffected.

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(6);

	// Regular types should sum to 6.
	TestEqual("OD_ResolveForTotal_StartEndUnaffected: regular type sum == 6", SumODCounts(Params), 6);

	// StartRoom still present and resolved; ExitPortalRoom left null (no level ref set).
	TestTrue("OD_ResolveForTotal_StartEndUnaffected: bHasStartRoom", Params.bHasStartRoom);
	TestFalse("OD_ResolveForTotal_StartEndUnaffected: bHasExitPortalRoom (null level ref)", Params.bHasExitPortalRoom);

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
// FloorBuilder Test Helpers
// ============================================================

namespace
{
	/**
	 * Build a minimal FOrganicDungeonGridData with a rectangular floor blob
	 * covering cells (StartX..EndX, StartY..EndY) inclusive.
	 * CellSize=100, GridOriginWorld=(0,0).
	 */
	FOrganicDungeonGridData MakeRectGrid(int32 GridW, int32 GridH, int32 StartX, int32 EndX, int32 StartY, int32 EndY, float CellSz = 100.0f)
	{
		FOrganicDungeonGridData Data;
		Data.GridWidth = GridW;
		Data.GridHeight = GridH;
		Data.CellSize = CellSz;
		Data.GridOriginWorld = FVector2D::ZeroVector;
		Data.Grid.Init(false, GridW * GridH);
		Data.CellType.Init(EOrganicCellType::Empty, GridW * GridH);

		for (int32 cy = StartY; cy <= EndY && cy < GridH; ++cy)
		{
			for (int32 cx = StartX; cx <= EndX && cx < GridW; ++cx)
			{
				const int32 Idx = cy * GridW + cx;
				Data.Grid[Idx] = true;
				Data.CellType[Idx] = EOrganicCellType::Corridor;
			}
		}
		return Data;
	}

	/** Build a 2-sample corridor (P0 → P1, constant radius) plus one room. */
	FOrganicDungeonGridData MakeRibbonGrid(float Radius = 50.0f)
	{
		FOrganicDungeonGridData Data;
		Data.bUseGridContourFloor = false;
		Data.GridWidth = 0;
		Data.GridHeight = 0;
		Data.CellSize = 100.0f;
		Data.GridOriginWorld = FVector2D::ZeroVector;

		FOrganicCorridor Cor;
		Cor.Centerline.Add(FVector2D(0.0f, 0.0f));
		Cor.Centerline.Add(FVector2D(500.0f, 0.0f));
		Cor.Radii.Add(Radius);
		Cor.Radii.Add(Radius);
		Data.Corridors.Add(Cor);

		FOrganicRoom Room;
		Room.Center = FVector2D(700.0f, 0.0f);
		Room.RotationDeg = 0.0f;
		Room.HalfExtent = FVector2D(100.0f, 100.0f);
		Data.Rooms.Add(Room);

		return Data;
	}
} // namespace

// ============================================================
// Test 19: ComputeWalkableContour — single rectangular blob → one CCW outer polygon.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicFloorContourRectTest, "ProceduralGeometry.OrganicFloor.ComputeWalkableContour_RectBlob", DefaultTestFlags)

bool FOrganicFloorContourRectTest::RunTest(const FString& /*Parameters*/)
{
	// 5×4 grid with a filled 3×2 rectangle at (1,1)–(3,2).
	FOrganicDungeonGridData Data = MakeRectGrid(/*W=*/5, /*H=*/4, 1, 3, 1, 2, /*CellSz=*/100.0f);

	FWalkableRegionContour Contour;
	FOrganicFloorBuilder::ComputeWalkableContour(Data, Contour);

	if (!TestEqual("RectBlob: exactly one outer polygon", Contour.Polygons.Num(), 1))
		return false;

	const FWalkablePolygon& Poly = Contour.Polygons[0];
	TestTrue("RectBlob: outer ring has >= 4 vertices", Poly.Outer.Num() >= 4);
	TestTrue("RectBlob: no holes", Poly.Holes.IsEmpty());

	// Verify world-coordinate extents match the expected rectangle.
	// The floor rectangle occupies cells (1,1)–(3,2) with CellSize=100.
	// Vertex coords should span [1*100, 4*100] × [1*100, 3*100] = [100,400] × [100,300].
	FVector2D MinPt(MAX_FLT, MAX_FLT), MaxPt(-MAX_FLT, -MAX_FLT);
	for (const FVector2D& V : Poly.Outer)
	{
		MinPt.X = FMath::Min(MinPt.X, V.X);
		MinPt.Y = FMath::Min(MinPt.Y, V.Y);
		MaxPt.X = FMath::Max(MaxPt.X, V.X);
		MaxPt.Y = FMath::Max(MaxPt.Y, V.Y);
	}
	TestTrue("RectBlob: outer.MinX == 100", FMath::IsNearlyEqual(MinPt.X, 100.0f, 1.0f));
	TestTrue("RectBlob: outer.MinY == 100", FMath::IsNearlyEqual(MinPt.Y, 100.0f, 1.0f));
	TestTrue("RectBlob: outer.MaxX == 400", FMath::IsNearlyEqual(MaxPt.X, 400.0f, 1.0f));
	TestTrue("RectBlob: outer.MaxY == 300", FMath::IsNearlyEqual(MaxPt.Y, 300.0f, 1.0f));

	return true;
}

// ============================================================
// Test 20: ComputeWalkableContour — corridor + overlapping room → exactly one outer polygon,
//          zero holes (union correctness).
//
// Grid layout (10×6, CellSize=100):
//   Corridor cells: rows 2-3, cols 1-7  (horizontal strip)
//   Room cells:     rows 1-4, cols 5-9  (vertical block)
//   Overlap:        rows 2-3, cols 5-7  (shared cells)
//
// The corridor and room form ONE connected region after union — the contour must yield
// exactly one outer polygon with no holes and no spurious duplicate loops.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicFloorContourUnionTest, "ProceduralGeometry.OrganicFloor.ComputeWalkableContour_CorridorRoomUnion", DefaultTestFlags)

bool FOrganicFloorContourUnionTest::RunTest(const FString& /*Parameters*/)
{
	// Hand-built deterministic grid: 10 wide × 6 tall.
	FOrganicDungeonGridData Data;
	Data.GridWidth = 10;
	Data.GridHeight = 6;
	Data.CellSize = 100.0f;
	Data.GridOriginWorld = FVector2D::ZeroVector;
	Data.Grid.Init(false, 10 * 6);
	Data.CellType.Init(EOrganicCellType::Empty, 10 * 6);

	auto SetCell = [&](int32 cx, int32 cy, uint8 Type) {
		const int32 Idx = cy * 10 + cx;
		Data.Grid[Idx] = true;
		Data.CellType[Idx] = Type;
	};

	// Corridor: rows 2-3, cols 1-7
	for (int32 cy = 2; cy <= 3; ++cy)
		for (int32 cx = 1; cx <= 7; ++cx)
			SetCell(cx, cy, EOrganicCellType::Corridor);

	// Room: rows 1-4, cols 5-9 (overlaps corridor at rows 2-3, cols 5-7)
	for (int32 cy = 1; cy <= 4; ++cy)
		for (int32 cx = 5; cx <= 9; ++cx)
			SetCell(cx, cy, EOrganicCellType::Room);

	FWalkableRegionContour Contour;
	FOrganicFloorBuilder::ComputeWalkableContour(Data, Contour);

	// The union of corridor + overlapping room is ONE connected region.
	// Union correctness: exactly one outer polygon (no duplicate loops).
	if (!TestEqual("UnionTest: exactly one outer polygon", Contour.Polygons.Num(), 1))
		return false;

	const FWalkablePolygon& Poly = Contour.Polygons[0];
	TestTrue("UnionTest: outer ring has >= 4 vertices", Poly.Outer.Num() >= 4);
	TestTrue("UnionTest: no holes (connected L/T-shape, no enclosed pockets)", Poly.Holes.IsEmpty());

	return true;
}

// ============================================================
// Test 21: ComputeWalkableContour — floor with an interior pocket → one outer + one hole.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicFloorContourHoleTest, "ProceduralGeometry.OrganicFloor.ComputeWalkableContour_InteriorHole", DefaultTestFlags)

bool FOrganicFloorContourHoleTest::RunTest(const FString& /*Parameters*/)
{
	// 7×7 grid: ring of floor cells around a 3×3 hollow interior.
	// Floor ring: all cells except the 3×3 center (2..4, 2..4).
	FOrganicDungeonGridData Data;
	Data.GridWidth = 7;
	Data.GridHeight = 7;
	Data.CellSize = 100.0f;
	Data.GridOriginWorld = FVector2D::ZeroVector;
	Data.Grid.Init(false, 49);
	Data.CellType.Init(EOrganicCellType::Empty, 49);

	for (int32 cy = 1; cy <= 5; ++cy)
	{
		for (int32 cx = 1; cx <= 5; ++cx)
		{
			// Leave (2,2)–(4,4) empty (the interior hole).
			if (cx >= 2 && cx <= 4 && cy >= 2 && cy <= 4)
				continue;
			const int32 Idx = cy * 7 + cx;
			Data.Grid[Idx] = true;
			Data.CellType[Idx] = EOrganicCellType::Corridor;
		}
	}

	FWalkableRegionContour Contour;
	FOrganicFloorBuilder::ComputeWalkableContour(Data, Contour);

	if (!TestTrue("InteriorHole: at least one polygon", Contour.Polygons.Num() >= 1))
		return false;

	// The ring should produce exactly one outer polygon with exactly one hole.
	int32 TotalHoles = 0;
	for (const FWalkablePolygon& Poly : Contour.Polygons)
	{
		TotalHoles += Poly.Holes.Num();
	}
	TestEqual("InteriorHole: exactly one hole", TotalHoles, 1);

	return true;
}

// ============================================================
// Test 22: BuildCorridorFloorMesh ribbon path — 2-sample corridor + 1 room.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicFloorMeshRibbonTest, "ProceduralGeometry.OrganicFloor.BuildCorridorFloorMesh_Ribbon", DefaultTestFlags)

bool FOrganicFloorMeshRibbonTest::RunTest(const FString& /*Parameters*/)
{
	FOrganicDungeonGridData Data = MakeRibbonGrid();
	// bUseGridContourFloor is already false from MakeRibbonGrid.

	constexpr float FloorH = 100.0f;
	FMeshData		Mesh;
	const bool		bBuilt = FOrganicFloorBuilder::BuildCorridorFloorMesh(Data, FloorH, Mesh);

	TestTrue("Ribbon: BuildCorridorFloorMesh returns true", bBuilt);
	TestTrue("Ribbon: non-empty vertices", Mesh.Vertices.Num() > 0);
	TestTrue("Ribbon: non-empty triangles", Mesh.Triangles.Num() > 0);

	// A 2-sample corridor (1 segment → 1 quad = 4 verts, 2 tris) + 1 room (1 quad = 4 verts, 2 tris)
	// → 8 vertices, 4 triangles (12 triangle indices).
	TestEqual("Ribbon: 8 vertices (1 corridor quad + 1 room quad)", Mesh.Vertices.Num(), 8);
	TestEqual("Ribbon: 4 triangles", Mesh.Triangles.Num() / 3, 4);

	// All top vertices must be at Z == FloorHeight.
	for (const FVector& V : Mesh.Vertices)
	{
		TestTrue("Ribbon: vertex Z == FloorHeight", FMath::IsNearlyEqual(V.Z, FloorH, KINDA_SMALL_NUMBER));
	}

	// Verify corridor ribbon width == 2*radius (acceptance criterion #7: floor width = corridor thickness).
	// The corridor runs from (0,0) to (500,0) with constant radius=50 (see MakeRibbonGrid).
	// Corridor ribbon vertices have X in [0,500]; the Y extent must span exactly [-50, +50].
	// (The room rect at (700,0) with HalfExtent=(100,100) has vertices outside X=500 so does not
	// contaminate the corridor-region Y measurement.)
	float CorridorMinY = MAX_FLT, CorridorMaxY = -MAX_FLT;
	for (const FVector& V : Mesh.Vertices)
	{
		if (V.X <= 500.0f + 1.0f) // corridor region
		{
			CorridorMinY = FMath::Min(CorridorMinY, V.Y);
			CorridorMaxY = FMath::Max(CorridorMaxY, V.Y);
		}
	}
	TestTrue("Ribbon: corridor Y-min == -radius (-50.0)", FMath::IsNearlyEqual(CorridorMinY, -50.0f, 1.0f));
	TestTrue("Ribbon: corridor Y-max == +radius (+50.0)", FMath::IsNearlyEqual(CorridorMaxY, 50.0f, 1.0f));

	return true;
}

// ============================================================
// Test 23: BuildCorridorFloorMesh grid-contour path — filled grid → fewer tris than per-cell.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicFloorMeshContourTest, "ProceduralGeometry.OrganicFloor.BuildCorridorFloorMesh_GridContour", DefaultTestFlags)

bool FOrganicFloorMeshContourTest::RunTest(const FString& /*Parameters*/)
{
	// 6×6 fully-filled grid of floor cells (36 cells → 72 per-cell-quad triangles).
	FOrganicDungeonGridData Data = MakeRectGrid(6, 6, 0, 5, 0, 5, 100.0f);
	Data.bUseGridContourFloor = true;
	FOrganicFloorBuilder::ComputeWalkableContour(Data, Data.WalkableContour);

	constexpr float FloorH = 50.0f;
	FMeshData		Mesh;
	const bool		bBuilt = FOrganicFloorBuilder::BuildCorridorFloorMesh(Data, FloorH, Mesh);

	TestTrue("Contour: BuildCorridorFloorMesh returns true", bBuilt);
	TestTrue("Contour: non-empty mesh", Mesh.Vertices.Num() > 0);

	// Per-cell-quad count would be 36×2 = 72 triangles. The contour path should produce far fewer.
	const int32 PerCellTris = Data.GridWidth * Data.GridHeight * 2; // 72
	const int32 ContourTris = Mesh.Triangles.Num() / 3;
	TestTrue("Contour: fewer triangles than per-cell quads", ContourTris < PerCellTris);

	// Verify the XY extent of the mesh matches the contour bounding box.
	if (!Data.WalkableContour.Polygons.IsEmpty())
	{
		FVector2D ContourMin(MAX_FLT, MAX_FLT), ContourMax(-MAX_FLT, -MAX_FLT);
		for (const FVector2D& V : Data.WalkableContour.Polygons[0].Outer)
		{
			ContourMin.X = FMath::Min(ContourMin.X, V.X);
			ContourMin.Y = FMath::Min(ContourMin.Y, V.Y);
			ContourMax.X = FMath::Max(ContourMax.X, V.X);
			ContourMax.Y = FMath::Max(ContourMax.Y, V.Y);
		}

		FVector MeshMin(MAX_FLT, MAX_FLT, MAX_FLT), MeshMax(-MAX_FLT, -MAX_FLT, -MAX_FLT);
		for (const FVector& V : Mesh.Vertices)
		{
			MeshMin.X = FMath::Min(MeshMin.X, V.X);
			MeshMin.Y = FMath::Min(MeshMin.Y, V.Y);
			MeshMax.X = FMath::Max(MeshMax.X, V.X);
			MeshMax.Y = FMath::Max(MeshMax.Y, V.Y);
		}

		TestTrue("Contour: mesh XMin matches contour", FMath::IsNearlyEqual(MeshMin.X, ContourMin.X, 1.0f));
		TestTrue("Contour: mesh YMin matches contour", FMath::IsNearlyEqual(MeshMin.Y, ContourMin.Y, 1.0f));
		TestTrue("Contour: mesh XMax matches contour", FMath::IsNearlyEqual(MeshMax.X, ContourMax.X, 1.0f));
		TestTrue("Contour: mesh YMax matches contour", FMath::IsNearlyEqual(MeshMax.Y, ContourMax.Y, 1.0f));
	}

	return true;
}

// ============================================================
// Test 24: BuildCorridorFloorMesh — empty/no-floor grid → returns false, empty mesh.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicFloorMeshEmptyTest, "ProceduralGeometry.OrganicFloor.BuildCorridorFloorMesh_EmptyGrid", DefaultTestFlags)

bool FOrganicFloorMeshEmptyTest::RunTest(const FString& /*Parameters*/)
{
	// Empty grid — no corridors, no rooms, no floor cells.
	FOrganicDungeonGridData Data;
	Data.GridWidth = 5;
	Data.GridHeight = 5;
	Data.CellSize = 100.0f;
	Data.GridOriginWorld = FVector2D::ZeroVector;
	Data.Grid.Init(false, 25);
	Data.CellType.Init(EOrganicCellType::Empty, 25);
	Data.bUseGridContourFloor = false;

	FMeshData  Mesh;
	const bool bBuilt = FOrganicFloorBuilder::BuildCorridorFloorMesh(Data, 100.0f, Mesh);

	TestFalse("EmptyGrid: BuildCorridorFloorMesh returns false", bBuilt);
	TestTrue("EmptyGrid: vertices empty", Mesh.Vertices.IsEmpty());
	TestTrue("EmptyGrid: triangles empty", Mesh.Triangles.IsEmpty());

	// Grid-contour path with empty contour should also return false.
	Data.bUseGridContourFloor = true;
	FMeshData  Mesh2;
	const bool bBuilt2 = FOrganicFloorBuilder::BuildCorridorFloorMesh(Data, 100.0f, Mesh2);
	TestFalse("EmptyGrid(contour): returns false", bBuilt2);

	return true;
}

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
// Test 33: Mismatch → drop/re-roll, never seal (AC #4).
// 1-doorway room type with 5 rooms: in any MST of 5 nodes (tree) the handshaking lemma
// guarantees at least one interior node with degree ≥ 2 (sum of degrees = 2*(N-1) = 8 > 5).
// That node cannot be satisfied by 1 declared doorway, so it is dropped.
// Verify: (a) output room count < 5 demonstrating the drop path ran, and (b) every surviving
// declared-doorway room's corridor endpoint is at a declared doorway (no sealed prefab rooms).
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicMismatchDropRerollTest, "ProceduralGeometry.OrganicDungeon.DeclaredDoorways.MismatchDropReroll", DefaultTestFlags)

bool FOrganicMismatchDropRerollTest::RunTest(const FString& Parameters)
{
	// 1 declared doorway pointing +X.  5 rooms of this type → at least 1 drop guaranteed.
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
	Gen->SetSeed(TEXT("MismatchDropReroll"));
	Gen->SetGridSize(50);
	Gen->ApplyResolvedParams(Params);

	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() == 0)
	{
		AddWarning(TEXT("MismatchDropReroll: no rooms placed — skipping"));
		return true;
	}

	// (a) At least one drop must have occurred: any tree of 5 nodes has at least one node
	//     with degree >= 2 (handshaking: sum degrees = 8, only 5 nodes), which with 1 declared
	//     doorway is infeasible and must be dropped.  Even accounting for placement shortfall,
	//     the drop path is exercised here.
	TestTrue("MismatchDropReroll: output room count < 5 (drop occurred — no infeasible room carved sealed)", Data.Rooms.Num() < 5);

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
				FString::Printf(TEXT("MismatchDropReroll: Corridor[%d] anchor at Room[%d] pos=(%.0f,%.0f) is at a declared doorway (no sealed room)"),
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
		TestTrue("MismatchDropReroll: at least one declared-doorway endpoint was verified (no-seal path exercised)", DeclChecked > 0);
	}

	return true;
}

// ============================================================
// SelectExitAnchors tests — pure, no world needed.
// Build hand-crafted room arrays + MST adjacency lists and verify
// anchor selection invariants without running the full generator.
// ============================================================

namespace
{
	/** Builds a minimal FOrganicRoom at (X, Y) with one synthetic doorway facing outward (+X). */
	static FOrganicRoom MakeAnchorTestRoom(float X, float Y, int32 TypeIndex = 0)
	{
		FOrganicRoom R;
		R.Center = FVector2D(X, Y);
		R.HalfExtent = FVector2D(300.0f, 300.0f);
		R.TypeIndex = TypeIndex;

		// One outward (+X) synthetic doorway.
		FOrganicDoorway D;
		D.Pos = FVector2D(X + 300.0f, Y);
		D.OutwardNormal = FVector2D(1.0f, 0.0f);
		D.bUsed = false;
		R.Doorways.Add(D);

		return R;
	}
} // namespace

// ============================================================
// SelectExitAnchors_Distinctness: all returned anchors use distinct rooms/doorways.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODSelectExitAnchors_DistinctnessTest, "ProceduralGeometry.OrganicDungeon.SelectExitAnchors.Distinctness", DefaultTestFlags)

bool FODSelectExitAnchors_DistinctnessTest::RunTest(const FString& Parameters)
{
	// Linear chain: 0-1-2-3-4. Entrance = 2 (center). Leaves = 0 and 4.
	// Request 2 anchors — should get rooms 0 and 4 (the two leaves).
	TArray<FOrganicRoom> Rooms;
	for (int32 i = 0; i < 5; ++i)
	{
		Rooms.Add(MakeAnchorTestRoom(static_cast<float>(i) * 1200.0f, 0.0f));
	}

	TArray<TArray<int32>> MstAdj;
	MstAdj.SetNum(5);
	for (int32 i = 0; i + 1 < 5; ++i)
	{
		MstAdj[i].Add(i + 1);
		MstAdj[i + 1].Add(i);
	}

	TArray<FOrganicExitAnchor> Anchors;
	int32					   Shortfall = -1;
	UOrganicDungeonGenerator2D::SelectExitAnchors(Rooms, MstAdj, 2, 2, EOrganicTerminusForm::PortalStub, Anchors, Shortfall);

	TestEqual("Distinctness: 2 anchors produced", Anchors.Num(), 2);
	TestEqual("Distinctness: no shortfall", Shortfall, 0);

	// All anchor rooms must be distinct.
	TSet<int32> UsedRooms;
	for (int32 i = 0; i < Anchors.Num(); ++i)
	{
		TestFalse(FString::Printf(TEXT("Distinctness: anchor[%d] room not duplicated"), i), UsedRooms.Contains(Anchors[i].RoomIndex));
		UsedRooms.Add(Anchors[i].RoomIndex);
		TestTrue(FString::Printf(TEXT("Distinctness: anchor[%d] not the entrance"), i), Anchors[i].RoomIndex != 2);
		TestTrue(FString::Printf(TEXT("Distinctness: anchor[%d] GraphDistance > 0"), i), Anchors[i].GraphDistance > 0);
	}

	return true;
}

// ============================================================
// SelectExitAnchors_FarFromEntrance: every anchor is farther than immediate entrance neighbours.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODSelectExitAnchors_FarFromEntranceTest, "ProceduralGeometry.OrganicDungeon.SelectExitAnchors.FarFromEntrance", DefaultTestFlags)

bool FODSelectExitAnchors_FarFromEntranceTest::RunTest(const FString& Parameters)
{
	// Star topology: entrance = 0, spokes = 1,2,3 (each at distance 1 from entrance),
	// leaves = 4,5,6 (each at distance 2, connected to spoke 1,2,3 respectively).
	// Requesting 1 anchor — should select one of the far leaves (dist=2).
	TArray<FOrganicRoom> Rooms;
	Rooms.Add(MakeAnchorTestRoom(0.0f, 0.0f));	   // 0 = entrance
	Rooms.Add(MakeAnchorTestRoom(1200.0f, 0.0f));  // 1
	Rooms.Add(MakeAnchorTestRoom(0.0f, 1200.0f));  // 2
	Rooms.Add(MakeAnchorTestRoom(-1200.0f, 0.0f)); // 3
	Rooms.Add(MakeAnchorTestRoom(2400.0f, 0.0f));  // 4 = leaf behind 1
	Rooms.Add(MakeAnchorTestRoom(0.0f, 2400.0f));  // 5 = leaf behind 2
	Rooms.Add(MakeAnchorTestRoom(-2400.0f, 0.0f)); // 6 = leaf behind 3

	TArray<TArray<int32>> MstAdj;
	MstAdj.SetNum(7);
	auto AddEdge = [&](int32 A, int32 B) {
		MstAdj[A].Add(B);
		MstAdj[B].Add(A);
	};
	AddEdge(0, 1);
	AddEdge(0, 2);
	AddEdge(0, 3);
	AddEdge(1, 4);
	AddEdge(2, 5);
	AddEdge(3, 6);

	TArray<FOrganicExitAnchor> Anchors;
	int32					   Shortfall = -1;
	UOrganicDungeonGenerator2D::SelectExitAnchors(Rooms, MstAdj, 0, 1, EOrganicTerminusForm::PortalStub, Anchors, Shortfall);

	TestEqual("FarFromEntrance: 1 anchor produced", Anchors.Num(), 1);
	TestEqual("FarFromEntrance: no shortfall", Shortfall, 0);
	if (Anchors.Num() == 1)
	{
		TestTrue("FarFromEntrance: anchor is a far leaf (dist=2)", Anchors[0].GraphDistance == 2);
		TestTrue("FarFromEntrance: anchor not at entrance", Anchors[0].RoomIndex != 0);
	}

	return true;
}

// ============================================================
// SelectExitAnchors_FallbackOnShortfall: graceful degradation when leaves < required.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FODSelectExitAnchors_FallbackTest, "ProceduralGeometry.OrganicDungeon.SelectExitAnchors.Fallback", DefaultTestFlags)

bool FODSelectExitAnchors_FallbackTest::RunTest(const FString& Parameters)
{
	// Single chain: 0-1-2. Entrance=0. Only one leaf = 2.
	// Request 3 anchors — supply is 1 leaf; expect 1 regular anchor + fallback(s); shortfall = 3 - produced.
	// Room 1 (non-leaf, dist=1) should be used as fallback.
	TArray<FOrganicRoom> Rooms;
	Rooms.Add(MakeAnchorTestRoom(0.0f, 0.0f));	  // 0 = entrance
	Rooms.Add(MakeAnchorTestRoom(1200.0f, 0.0f)); // 1 = non-leaf
	Rooms.Add(MakeAnchorTestRoom(2400.0f, 0.0f)); // 2 = leaf

	TArray<TArray<int32>> MstAdj;
	MstAdj.SetNum(3);
	MstAdj[0].Add(1);
	MstAdj[1].Add(0);
	MstAdj[1].Add(2);
	MstAdj[2].Add(1);

	TArray<FOrganicExitAnchor> Anchors;
	int32					   Shortfall = -1;
	UOrganicDungeonGenerator2D::SelectExitAnchors(Rooms, MstAdj, 0, 3, EOrganicTerminusForm::PortalStub, Anchors, Shortfall);

	// Should produce at most 2 anchors (rooms 1 and 2 — entrance excluded), with shortfall = max(0, 3-2)=1.
	TestTrue("Fallback: produced > 0 anchors", Anchors.Num() > 0);
	TestTrue("Fallback: Shortfall reported correctly", Shortfall == FMath::Max(0, 3 - Anchors.Num()));

	// All rooms in anchors must be distinct and not the entrance.
	TSet<int32> Seen;
	for (int32 i = 0; i < Anchors.Num(); ++i)
	{
		TestFalse(FString::Printf(TEXT("Fallback: anchor[%d] not duplicated"), i), Seen.Contains(Anchors[i].RoomIndex));
		TestFalse(FString::Printf(TEXT("Fallback: anchor[%d] not at entrance"), i), Anchors[i].RoomIndex == 0);
		Seen.Add(Anchors[i].RoomIndex);
	}

	return true;
}

// ============================================================
// SelectExitAnchors_Determinism: same inputs always produce the same anchor set/order.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODSelectExitAnchors_DeterminismTest, "ProceduralGeometry.OrganicDungeon.SelectExitAnchors.Determinism", DefaultTestFlags)

bool FODSelectExitAnchors_DeterminismTest::RunTest(const FString& Parameters)
{
	// Linear chain 0-1-2-3-4. Entrance=1. Request 2 anchors.
	auto MakeRooms = []() {
		TArray<FOrganicRoom> Rooms;
		for (int32 i = 0; i < 5; ++i)
		{
			Rooms.Add(MakeAnchorTestRoom(static_cast<float>(i) * 1200.0f, 0.0f));
		}
		return Rooms;
	};

	TArray<TArray<int32>> MstAdj;
	MstAdj.SetNum(5);
	for (int32 i = 0; i + 1 < 5; ++i)
	{
		MstAdj[i].Add(i + 1);
		MstAdj[i + 1].Add(i);
	}

	TArray<FOrganicExitAnchor> Anchors1, Anchors2;
	int32					   Shortfall1 = 0, Shortfall2 = 0;

	TArray<FOrganicRoom> Rooms1 = MakeRooms();
	UOrganicDungeonGenerator2D::SelectExitAnchors(Rooms1, MstAdj, 1, 2, EOrganicTerminusForm::PortalStub, Anchors1, Shortfall1);

	TArray<FOrganicRoom> Rooms2 = MakeRooms();
	UOrganicDungeonGenerator2D::SelectExitAnchors(Rooms2, MstAdj, 1, 2, EOrganicTerminusForm::PortalStub, Anchors2, Shortfall2);

	TestEqual("Determinism: same anchor count", Anchors1.Num(), Anchors2.Num());
	TestEqual("Determinism: same shortfall", Shortfall1, Shortfall2);
	for (int32 i = 0; i < FMath::Min(Anchors1.Num(), Anchors2.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Determinism: anchor[%d] same room"), i), Anchors1[i].RoomIndex, Anchors2[i].RoomIndex);
		TestEqual(FString::Printf(TEXT("Determinism: anchor[%d] same GraphDistance"), i), Anchors1[i].GraphDistance, Anchors2[i].GraphDistance);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
