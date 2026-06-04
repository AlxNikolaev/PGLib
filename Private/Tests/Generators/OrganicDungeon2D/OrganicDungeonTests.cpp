// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

	// Use per-file macro to avoid anonymous-namespace symbol collisions under Unity Build.
	#define ORGANICDUNGEON_TEST_FLAGS                                                                                    \
		(EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter \
			| EAutomationTestFlags::MediumPriority)

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
		RoomType.DoorwaysPerEdge = 1;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonDefaultGenerateTest, "ProceduralGeometry.OrganicDungeon.DefaultGenerate", ORGANICDUNGEON_TEST_FLAGS)

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonDeterminismTest, "ProceduralGeometry.OrganicDungeon.Determinism", ORGANICDUNGEON_TEST_FLAGS)

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonParallelArraySizesTest, "ProceduralGeometry.OrganicDungeon.ParallelArraySizes", ORGANICDUNGEON_TEST_FLAGS)

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonRoomCountBoundsTest, "ProceduralGeometry.OrganicDungeon.RoomCountBounds", ORGANICDUNGEON_TEST_FLAGS)

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrganicDungeonOOMGuardTest, "ProceduralGeometry.OrganicDungeon.OOMGuard", ORGANICDUNGEON_TEST_FLAGS)

bool FOrganicDungeonOOMGuardTest::RunTest(const FString& Parameters)
{
	// Register the expected Error log so the automation runner does not auto-fail this test.
	AddExpectedError(TEXT("OOM guard triggered"), EAutomationExpectedErrorFlags::Contains);

	FOrganicResolvedRoomType BigRoom;
	BigRoom.FootprintWidth = 21000.0f; // at GridSize=10: 2100 cells/axis + padding > 4M total
	BigRoom.FootprintHeight = 21000.0f;
	BigRoom.Count = 1;
	BigRoom.DoorwaysPerEdge = 1;

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonNoRoomTypesTest, "ProceduralGeometry.OrganicDungeon.NoRoomTypes_EmptyResult", ORGANICDUNGEON_TEST_FLAGS)

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
// Test 7: StartRoomIndex and EndRoomIndex are valid when rooms exist.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonStartEndRoomValidTest, "ProceduralGeometry.OrganicDungeon.StartEndRoomValid", ORGANICDUNGEON_TEST_FLAGS)

bool FOrganicDungeonStartEndRoomValidTest::RunTest(const FString& Parameters)
{
	UOrganicDungeonGenerator2D*	  Gen = MakeOrganicGenerator(TEXT("StartEndTest"), 4);
	const FOrganicDungeonGridData Data = Gen->GenerateWithGridData();

	if (Data.Rooms.Num() == 0)
	{
		AddWarning(TEXT("StartEndRoomValid: no rooms placed, skipping"));
		return true;
	}

	const int32 RoomCount = Data.Rooms.Num();

	TestTrue("StartEndRoomValid: StartRoomIndex is valid", Data.StartRoomIndex >= 0 && Data.StartRoomIndex < RoomCount);
	TestTrue("StartEndRoomValid: EndRoomIndex is valid", Data.EndRoomIndex >= 0 && Data.EndRoomIndex < RoomCount);

	return true;
}

// ============================================================
// Test 8: Cell type consistency — floor cells are Room or Corridor;
//         non-floor cells are Wall or Empty.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOrganicDungeonCellTypeConsistencyTest, "ProceduralGeometry.OrganicDungeon.CellTypeConsistency", ORGANICDUNGEON_TEST_FLAGS)

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
	FOrganicDungeonCorridorRadiiParallelTest, "ProceduralGeometry.OrganicDungeon.CorridorRadiiParallel", ORGANICDUNGEON_TEST_FLAGS)

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
		T.DoorwaysPerEdge = 1;
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalZeroTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_Zero", ORGANICDUNGEON_TEST_FLAGS)

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
	FODConfigResolveForTotalSingleTypeTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_SingleType", ORGANICDUNGEON_TEST_FLAGS)

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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FODConfigResolveForTotalSumEqualWeightsTest,
	"ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_SumEqualWeights",
	ORGANICDUNGEON_TEST_FLAGS)

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
	FODConfigResolveForTotalWeightedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_Weighted", ORGANICDUNGEON_TEST_FLAGS)

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
	FODConfigResolveForTotalOvercountGuardTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_OvercountGuard", ORGANICDUNGEON_TEST_FLAGS)

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
// Test 15: OD ResolveForTotal — start/end rooms are unaffected; only regular types
//          are redistributed.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FODConfigResolveForTotalStartEndUnaffectedTest,
	"ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_StartEndUnaffected",
	ORGANICDUNGEON_TEST_FLAGS)

bool FODConfigResolveForTotalStartEndUnaffectedTest::RunTest(const FString& Parameters)
{
	FOrganicDungeonConfig Config;
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.RoomTypes.Add(MakeODRoomType(1));
	Config.StartRoom = MakeODRoomType(1);
	Config.EndRoom = MakeODRoomType(1);

	const FOrganicDungeonResolvedParams Params = Config.ResolveForTotal(6);

	// Regular types should sum to 6.
	TestEqual("OD_ResolveForTotal_StartEndUnaffected: regular type sum == 6", SumODCounts(Params), 6);

	// Start/end present and have their footprint resolved but Count is irrelevant.
	TestTrue("OD_ResolveForTotal_StartEndUnaffected: bHasStartRoom", Params.bHasStartRoom);
	TestTrue("OD_ResolveForTotal_StartEndUnaffected: bHasEndRoom", Params.bHasEndRoom);

	return true;
}

// ============================================================
// Test 16: OD ResolveForTotal end-to-end — params from ResolveForTotal feed the
//          generator and produce a non-empty result.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FODConfigResolveForTotalEndToEndTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_EndToEnd", ORGANICDUNGEON_TEST_FLAGS)

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
	FODConfigResolveForTotalMinRespectedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_MinRespected", ORGANICDUNGEON_TEST_FLAGS)

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
		T.DoorwaysPerEdge = 1;
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
	FODConfigResolveForTotalMaxRespectedTest, "ProceduralGeometry.OrganicDungeon.Config.ResolveForTotal_MaxRespected", ORGANICDUNGEON_TEST_FLAGS)

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
		T0.DoorwaysPerEdge = 1;
		Config.RoomTypes.Add(T0);

		FOrganicRoomType T1;
		T1.Weight = 1;
		T1.FootprintOverride = FVector2D(600.0f, 600.0f);
		T1.DoorwaysPerEdge = 1;
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

	#undef ORGANICDUNGEON_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
