// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"
#include "../../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Creates a DrunkardWalk generator pre-configured with N identical rooms. */
	UDrunkardWalkGenerator2D* MakeDrunkardGenerator(const FString& Seed, int32 RoomCount, int32 FootprintCells = 4)
	{
		UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
		Gen->SetSeed(Seed);
		Gen->SetGridSize(100);

		FRoomTypeConfig RoomType;
		RoomType.Tag = FName(TEXT("Test"));
		RoomType.FootprintWidthCells = FootprintCells;
		RoomType.FootprintHeightCells = FootprintCells;
		RoomType.Weight = RoomCount;

		Gen->SetRoomTypes({ RoomType });
		return Gen;
	}
} // namespace

// ============================================================
// Test 1: Default generation produces a non-empty diagram.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkDefaultGenerateTest, "ProceduralGeometry.DrunkardWalk.DefaultGenerate", DefaultTestFlags)

bool FDrunkardWalkDefaultGenerateTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("DefaultTest"), /*RoomCount=*/3);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestTrue("DefaultGenerate: Diagram has cells", Data.Diagram.Cells.Num() > 0);
	return true;
}

// ============================================================
// Test 2: Same seed and config produce identical results.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkDeterminismTest, "ProceduralGeometry.DrunkardWalk.Determinism", DefaultTestFlags)

bool FDrunkardWalkDeterminismTest::RunTest(const FString& Parameters)
{
	const FString Seed = TEXT("DeterminismSeed42");

	UDrunkardWalkGenerator2D* Gen1 = MakeDrunkardGenerator(Seed, 4);
	UDrunkardWalkGenerator2D* Gen2 = MakeDrunkardGenerator(Seed, 4);

	const FDrunkardWalkGridData Data1 = Gen1->GenerateWithGridData();
	const FDrunkardWalkGridData Data2 = Gen2->GenerateWithGridData();

	TestEqual("Determinism: PlacedRooms count matches", Data1.PlacedRooms.Num(), Data2.PlacedRooms.Num());
	TestEqual("Determinism: CorridorSourceRoom count matches", Data1.CorridorSourceRoom.Num(), Data2.CorridorSourceRoom.Num());

	for (int32 i = 0; i < FMath::Min(Data1.PlacedRooms.Num(), Data2.PlacedRooms.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Determinism: PlacedRoom[%d].Min.X"), i), Data1.PlacedRooms[i].Min.X, Data2.PlacedRooms[i].Min.X);
		TestEqual(FString::Printf(TEXT("Determinism: PlacedRoom[%d].Min.Y"), i), Data1.PlacedRooms[i].Min.Y, Data2.PlacedRooms[i].Min.Y);
	}

	return true;
}

// ============================================================
// Test 3: Parallel array size invariants.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkParallelArraySizesTest, "ProceduralGeometry.DrunkardWalk.ParallelArraySizes", DefaultTestFlags)

bool FDrunkardWalkParallelArraySizesTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("ParallelArrayTest"), 3);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	if (Data.GridWidth == 0 || Data.GridHeight == 0)
	{
		// Generation returned empty — nothing to check.
		AddWarning(TEXT("ParallelArraySizes: grid is empty, skipping checks"));
		return true;
	}

	// Grid flat-array size must equal GridWidth * GridHeight.
	const int32 ExpectedGridSize = Data.GridWidth * Data.GridHeight;
	TestEqual("ParallelArraySizes: Grid.Num() == GridWidth * GridHeight", Data.Grid.Num(), ExpectedGridSize);
	TestEqual("ParallelArraySizes: CellType.Num() == Grid.Num()", Data.CellType.Num(), Data.Grid.Num());
	TestEqual("ParallelArraySizes: RegionIds.Num() == Grid.Num()", Data.RegionIds.Num(), Data.Grid.Num());

	// Corridor parallel arrays must have equal size.
	TestEqual("ParallelArraySizes: WalkerPaths.Num() == CorridorSourceRoom.Num()", Data.WalkerPaths.Num(), Data.CorridorSourceRoom.Num());
	TestEqual("ParallelArraySizes: WalkerPaths.Num() == CorridorTargetRoom.Num()", Data.WalkerPaths.Num(), Data.CorridorTargetRoom.Num());

	return true;
}

// ============================================================
// Test 4: PlacedRooms count is within [1, RequestedRoomCount].
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkRoomCountMatchesPlacementTest, "ProceduralGeometry.DrunkardWalk.RoomCountMatchesPlacement", DefaultTestFlags)

bool FDrunkardWalkRoomCountMatchesPlacementTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("RoomCountTest"), 5);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	if (Data.RequestedRoomCount == 0)
	{
		AddWarning(TEXT("RoomCountMatchesPlacement: RequestedRoomCount is 0, skipping"));
		return true;
	}

	TestTrue("RoomCountMatchesPlacement: PlacedRooms.Num() >= 1", Data.PlacedRooms.Num() >= 1);
	TestTrue("RoomCountMatchesPlacement: PlacedRooms.Num() <= RequestedRoomCount", Data.PlacedRooms.Num() <= Data.RequestedRoomCount);

	return true;
}

// ============================================================
// Test 5: OOM guard — large footprint triggers empty result.
// A single room with 2500×2500 cell footprint produces a raster grid of
// roughly 2502×2502 = 6.26M cells, which exceeds the 4,194,304-cell limit.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkOOMGuardTest, "ProceduralGeometry.DrunkardWalk.OOMGuard", DefaultTestFlags)

bool FDrunkardWalkOOMGuardTest::RunTest(const FString& Parameters)
{
	// Large footprint: 2500×2500 grid cells. First room is always placed at origin
	// regardless of bounds, so grid extent ≈ 2500×2500 ≫ 4M-cell limit.
	// The generator logs an Error when the guard fires; register it as expected so the
	// automation framework does not auto-fail the test on the Error-level log.
	AddExpectedError(TEXT("OOM guard triggered"), EAutomationExpectedErrorFlags::Contains);

	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("OOMTest"), 1, /*FootprintCells=*/2500);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestEqual("OOMGuard: Diagram.Cells.Num() == 0", Data.Diagram.Cells.Num(), 0);
	TestEqual("OOMGuard: GridWidth == 0", Data.GridWidth, 0);
	TestEqual("OOMGuard: GridHeight == 0", Data.GridHeight, 0);

	return true;
}

// ============================================================
// Test 6: No room types → empty result, no crash.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkNoRoomTypesTest, "ProceduralGeometry.DrunkardWalk.NoRoomTypes_EmptyResult", DefaultTestFlags)

bool FDrunkardWalkNoRoomTypesTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
	Gen->SetSeed(TEXT("NoRoomTypes"));
	Gen->SetGridSize(100);
	// No room types set.

	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestEqual("NoRoomTypes_EmptyResult: Diagram.Cells.Num() == 0", Data.Diagram.Cells.Num(), 0);
	TestEqual("NoRoomTypes_EmptyResult: PlacedRooms.Num() == 0", Data.PlacedRooms.Num(), 0);

	return true;
}

// ============================================================
// Test 7: Every floor cell has a valid (>= 0) RegionId.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkAllFloorCellsHaveValidRegionTest, "ProceduralGeometry.DrunkardWalk.AllFloorCellsHaveValidRegion", DefaultTestFlags)

bool FDrunkardWalkAllFloorCellsHaveValidRegionTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("RegionValidTest"), 4);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	if (Data.Grid.Num() == 0)
	{
		AddWarning(TEXT("AllFloorCellsHaveValidRegion: empty grid, skipping"));
		return true;
	}

	const int32 Total = Data.Grid.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		if (Data.Grid[i])
		{
			if (!TestTrue(FString::Printf(TEXT("FloorCell[%d] has valid RegionId"), i), Data.RegionIds[i] >= 0))
			{
				// Report first failure only to avoid flooding the log.
				AddError(FString::Printf(TEXT("  First invalid cell at index %d"), i));
				break;
			}
		}
	}

	return true;
}

// ============================================================
// Test 8: Cell type consistency — floor cells are Corridor or Room;
//         non-floor cells are Wall or Empty.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkCellTypeConsistencyTest, "ProceduralGeometry.DrunkardWalk.CellTypeConsistency", DefaultTestFlags)

bool FDrunkardWalkCellTypeConsistencyTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("CellTypeTest"), 4);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

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
			const bool bValidFloorType = (Type == EDrunkardWalkCellType::Corridor || Type == EDrunkardWalkCellType::Room);
			if (!bValidFloorType)
			{
				AddError(FString::Printf(TEXT("CellTypeConsistency: floor cell[%d] has non-floor type %d"), i, (int32)Type));
				break;
			}
		}
		else
		{
			const bool bValidNonFloorType = (Type == EDrunkardWalkCellType::Wall || Type == EDrunkardWalkCellType::Empty);
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
// Test 9: Corridor graph indices are valid PlacedRooms indices.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkCorridorGraphIndicesValidTest, "ProceduralGeometry.DrunkardWalk.CorridorGraphIndicesValid", DefaultTestFlags)

bool FDrunkardWalkCorridorGraphIndicesValidTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("CorridorIndexTest"), 4);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	const int32 RoomCount = Data.PlacedRooms.Num();

	for (int32 i = 0; i < Data.CorridorSourceRoom.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("CorridorSourceRoom[%d] in [0, PlacedRooms.Num())"), i),
			Data.CorridorSourceRoom[i] >= 0 && Data.CorridorSourceRoom[i] < RoomCount);
	}

	for (int32 i = 0; i < Data.CorridorTargetRoom.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("CorridorTargetRoom[%d] in [0, PlacedRooms.Num())"), i),
			Data.CorridorTargetRoom[i] >= 0 && Data.CorridorTargetRoom[i] < RoomCount);
	}

	return true;
}

// ============================================================
// ResolveForTotal tests — FDrunkardWalkConfig
// ============================================================

namespace
{
	/** Helper: builds a FDrunkardWalkConfig with room types of given weights. */
	FDrunkardWalkConfig MakeDWConfig(const TArray<int32>& Weights)
	{
		FDrunkardWalkConfig Config;
		for (int32 i = 0; i < Weights.Num(); ++i)
		{
			FRoomTypeConfig T;
			T.Tag = FName(*FString::Printf(TEXT("Type%d"), i));
			T.FootprintWidthCells = 4;
			T.FootprintHeightCells = 4;
			T.Weight = Weights[i];
			Config.RoomTypes.Add(T);
		}
		return Config;
	}

	/** Returns the sum of Count across all room types in resolved params. */
	int32 SumDWCounts(const FDrunkardWalkResolvedParams& Params)
	{
		int32 Sum = 0;
		for (const FRoomTypeConfig& RT : Params.RoomTypes)
		{
			Sum += RT.Weight;
		}
		return Sum;
	}
} // namespace

// ============================================================
// Test 10: ResolveForTotal — TotalRooms == 0 clears all counts.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWConfigResolveForTotalZeroTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_Zero", DefaultTestFlags)

bool FDWConfigResolveForTotalZeroTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 3, 1 });
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(0);

	for (const FRoomTypeConfig& RT : Params.RoomTypes)
	{
		TestEqual("ResolveForTotal_Zero: every Weight == 0", RT.Weight, 0);
	}
	return true;
}

// ============================================================
// Test 11: ResolveForTotal — single type receives all rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalSingleTypeTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_SingleType", DefaultTestFlags)

bool FDWConfigResolveForTotalSingleTypeTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 1 });
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(7);

	TestEqual("ResolveForTotal_SingleType: one room type", Params.RoomTypes.Num(), 1);
	if (Params.RoomTypes.Num() == 1)
	{
		TestEqual("ResolveForTotal_SingleType: sole type gets all rooms", Params.RoomTypes[0].Weight, 7);
	}
	return true;
}

// ============================================================
// Test 12: ResolveForTotal — multiple equal-weight types sum exactly to TotalRooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalSumEqualWeightsTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_SumEqualWeights", DefaultTestFlags)

bool FDWConfigResolveForTotalSumEqualWeightsTest::RunTest(const FString& Parameters)
{
	// 3 equal-weight types, prime total — forces a rounding remainder.
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 1, 1, 1 });
	constexpr int32					  Total = 7;
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(Total);

	TestEqual("ResolveForTotal_SumEqualWeights: sum == Total", SumDWCounts(Params), Total);
	for (const FRoomTypeConfig& RT : Params.RoomTypes)
	{
		TestTrue("ResolveForTotal_SumEqualWeights: all counts >= 0", RT.Weight >= 0);
	}
	return true;
}

// ============================================================
// Test 13: ResolveForTotal — weighted distribution (3:1) sums to TotalRooms and
//          the heavier type receives more rooms than the lighter type.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalWeightedTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_Weighted", DefaultTestFlags)

bool FDWConfigResolveForTotalWeightedTest::RunTest(const FString& Parameters)
{
	// Weights 3:1 → type 0 should get 75 % of rooms.
	// TotalRooms=8: Round(3/4*8)=6 → type0=6, type1=2. Sum=8.
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 3, 1 });
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(8);

	TestEqual("ResolveForTotal_Weighted: two types remain", Params.RoomTypes.Num(), 2);
	TestEqual("ResolveForTotal_Weighted: sum == 8", SumDWCounts(Params), 8);
	if (Params.RoomTypes.Num() == 2)
	{
		TestTrue("ResolveForTotal_Weighted: heavier type > lighter type", Params.RoomTypes[0].Weight > Params.RoomTypes[1].Weight);
	}
	return true;
}

// ============================================================
// Test 14: ResolveForTotal — rounding cannot produce a negative last-type count.
//          weights=[1,1,1], TotalRooms=2 → Round(2/3)=1, 1, last=Max(0,2-2)=0. Sum=2 ✓
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalOvercountGuardTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_OvercountGuard", DefaultTestFlags)

bool FDWConfigResolveForTotalOvercountGuardTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 1, 1, 1 });
	constexpr int32					  Total = 2;
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(Total);

	TestEqual("ResolveForTotal_OvercountGuard: sum == 2", SumDWCounts(Params), Total);
	for (const FRoomTypeConfig& RT : Params.RoomTypes)
	{
		TestTrue("ResolveForTotal_OvercountGuard: no negative counts", RT.Weight >= 0);
	}
	return true;
}

// ============================================================
// Test 15: ResolveForTotal end-to-end — params from ResolveForTotal feed the
//          generator and produce a non-empty result.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalEndToEndTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_EndToEnd", DefaultTestFlags)

bool FDWConfigResolveForTotalEndToEndTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 2, 1 });
	constexpr int32					  Total = 6;
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(Total);

	// Verify the total is correct before feeding to the generator.
	TestEqual("ResolveForTotal_EndToEnd: sum == 6", SumDWCounts(Params), Total);

	// Apply the resolved params to the generator and run generation.
	UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
	Gen->SetSeed(TEXT("ResolveForTotalE2E"));
	Gen->SetGridSize(100);
	Gen->ApplyResolvedParams(Params);

	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestTrue("ResolveForTotal_EndToEnd: diagram has cells", Data.Diagram.Cells.Num() > 0);
	TestTrue("ResolveForTotal_EndToEnd: rooms were placed", Data.PlacedRooms.Num() >= 1);

	return true;
}

// ============================================================
// Test 16: ResolveForTotal — Min is respected: each type receives at least Min rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalMinRespectedTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_MinRespected", DefaultTestFlags)

bool FDWConfigResolveForTotalMinRespectedTest::RunTest(const FString& Parameters)
{
	// Two types, each with Min=2. Budget=10. Each type must receive >= 2 rooms.
	FDrunkardWalkConfig Config;
	for (int32 i = 0; i < 2; ++i)
	{
		FRoomTypeConfig T;
		T.Tag = FName(*FString::Printf(TEXT("Type%d"), i));
		T.FootprintWidthCells = 4;
		T.FootprintHeightCells = 4;
		T.Weight = 1;
		T.Min = 2;
		Config.RoomTypes.Add(T);
	}
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(10);

	TestEqual("ResolveForTotal_MinRespected: sum == 10", SumDWCounts(Params), 10);
	for (const FRoomTypeConfig& RT : Params.RoomTypes)
	{
		TestTrue("ResolveForTotal_MinRespected: each type >= Min(2)", RT.Weight >= 2);
	}
	return true;
}

// ============================================================
// Test 17: ResolveForTotal — Max is respected: each type receives at most Max rooms.
// ============================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalMaxRespectedTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_MaxRespected", DefaultTestFlags)

bool FDWConfigResolveForTotalMaxRespectedTest::RunTest(const FString& Parameters)
{
	// Type0: Weight=3, Max=4. Type1: Weight=1, Max=0 (uncapped). Budget=10.
	// Type0 would normally get ~7-8 rooms but is capped at 4. Type1 absorbs the rest.
	FDrunkardWalkConfig Config;
	{
		FRoomTypeConfig T0;
		T0.Tag = FName(TEXT("Capped"));
		T0.FootprintWidthCells = 4;
		T0.FootprintHeightCells = 4;
		T0.Weight = 3;
		T0.Max = 4;
		Config.RoomTypes.Add(T0);

		FRoomTypeConfig T1;
		T1.Tag = FName(TEXT("Uncapped"));
		T1.FootprintWidthCells = 4;
		T1.FootprintHeightCells = 4;
		T1.Weight = 1;
		Config.RoomTypes.Add(T1);
	}
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(10);

	TestEqual("ResolveForTotal_MaxRespected: two types", Params.RoomTypes.Num(), 2);
	TestEqual("ResolveForTotal_MaxRespected: sum == 10", SumDWCounts(Params), 10);
	if (Params.RoomTypes.Num() == 2)
	{
		TestTrue("ResolveForTotal_MaxRespected: capped type <= Max(4)", Params.RoomTypes[0].Weight <= 4);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
