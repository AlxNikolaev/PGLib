// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"
#include "GridBudget.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkDefaultGenerateTest, "ProceduralGeometry.DrunkardWalk.DefaultGenerate", DefaultTestFlags)

bool FDrunkardWalkDefaultGenerateTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("DefaultTest"), /*RoomCount=*/3);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestTrue("DefaultGenerate: Diagram has cells", Data.Diagram.Cells.Num() > 0);
	return true;
}

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
		TestEqual(FString::Printf(TEXT("Determinism: PlacedRoom[%d].Width"), i), Data1.PlacedRooms[i].Width, Data2.PlacedRooms[i].Width);
		TestEqual(FString::Printf(TEXT("Determinism: PlacedRoom[%d].Height"), i), Data1.PlacedRooms[i].Height, Data2.PlacedRooms[i].Height);
	}

	// Counts alone would miss a layout change, so compare the geometry cell for cell.
	TestEqual("Determinism: Grid size matches", Data1.Grid.Num(), Data2.Grid.Num());
	TestEqual("Determinism: CellType size matches", Data1.CellType.Num(), Data2.CellType.Num());

	if (Data1.Grid.Num() == Data2.Grid.Num())
	{
		bool bGridMatches = true;
		for (int32 i = 0; i < Data1.Grid.Num() && bGridMatches; ++i)
		{
			if (Data1.Grid[i] != Data2.Grid[i])
			{
				bGridMatches = false;
				AddError(FString::Printf(TEXT("Determinism: Grid differs at cell %d"), i));
			}
		}
		TestTrue("Determinism: Grid matches cell-for-cell", bGridMatches);
	}

	if (Data1.CellType.Num() == Data2.CellType.Num())
	{
		bool bCellTypeMatches = true;
		for (int32 i = 0; i < Data1.CellType.Num() && bCellTypeMatches; ++i)
		{
			if (Data1.CellType[i] != Data2.CellType[i])
			{
				bCellTypeMatches = false;
				AddError(FString::Printf(TEXT("Determinism: CellType differs at cell %d"), i));
			}
		}
		TestTrue("Determinism: CellType matches cell-for-cell", bCellTypeMatches);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkParallelArraySizesTest, "ProceduralGeometry.DrunkardWalk.ParallelArraySizes", DefaultTestFlags)

bool FDrunkardWalkParallelArraySizesTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("ParallelArrayTest"), 3);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	// Seed and room count are fixed, so an empty grid is a regression rather than a case to skip.
	if (!TestTrue(TEXT("ParallelArraySizes: seeded generation produced a grid"), Data.GridWidth > 0 && Data.GridHeight > 0))
	{
		return false;
	}

	const int32 ExpectedGridSize = Data.GridWidth * Data.GridHeight;
	TestEqual("ParallelArraySizes: Grid.Num() == GridWidth * GridHeight", Data.Grid.Num(), ExpectedGridSize);
	TestEqual("ParallelArraySizes: CellType.Num() == Grid.Num()", Data.CellType.Num(), Data.Grid.Num());
	TestEqual("ParallelArraySizes: RegionIds.Num() == Grid.Num()", Data.RegionIds.Num(), Data.Grid.Num());

	TestEqual("ParallelArraySizes: WalkerPaths.Num() == CorridorSourceRoom.Num()", Data.WalkerPaths.Num(), Data.CorridorSourceRoom.Num());
	TestEqual("ParallelArraySizes: WalkerPaths.Num() == CorridorTargetRoom.Num()", Data.WalkerPaths.Num(), Data.CorridorTargetRoom.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkRoomCountMatchesPlacementTest, "ProceduralGeometry.DrunkardWalk.RoomCountMatchesPlacement", DefaultTestFlags)

bool FDrunkardWalkRoomCountMatchesPlacementTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("RoomCountTest"), 5);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	// A config asking for rooms that requests none is the regression here, so fail instead of skipping.
	if (!TestTrue(TEXT("RoomCountMatchesPlacement: the configured room types produced a room request"), Data.RequestedRoomCount > 0))
	{
		return false;
	}

	TestTrue("RoomCountMatchesPlacement: PlacedRooms.Num() >= 1", Data.PlacedRooms.Num() >= 1);
	TestTrue("RoomCountMatchesPlacement: PlacedRooms.Num() <= RequestedRoomCount", Data.PlacedRooms.Num() <= Data.RequestedRoomCount);

	return true;
}

// A 2500x2500 footprint rasters to ~6.26M cells, over PGGrid::MaxGridCells, so it downsamples instead of refusing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkOOMGuardTest, "ProceduralGeometry.DrunkardWalk.OOMGuard", DefaultTestFlags)

bool FDrunkardWalkOOMGuardTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("OOMTest"), 1, /*FootprintCells=*/2500);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestTrue("OOMGuard: degraded resolution flagged", Data.bDegradedResolution);
	TestTrue("OOMGuard: grid fits the cell budget", (int64)Data.GridWidth * Data.GridHeight <= PGGrid::MaxGridCells);
	TestTrue("OOMGuard: cell size enlarged beyond requested 100", Data.CellSize > 100.0f);
	TestTrue("OOMGuard: still produces cells", Data.Diagram.Cells.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkNoRoomTypesTest, "ProceduralGeometry.DrunkardWalk.NoRoomTypes_EmptyResult", DefaultTestFlags)

bool FDrunkardWalkNoRoomTypesTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
	Gen->SetSeed(TEXT("NoRoomTypes"));
	Gen->SetGridSize(100);

	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestEqual("NoRoomTypes_EmptyResult: Diagram.Cells.Num() == 0", Data.Diagram.Cells.Num(), 0);
	TestEqual("NoRoomTypes_EmptyResult: PlacedRooms.Num() == 0", Data.PlacedRooms.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkAllFloorCellsHaveValidRegionTest, "ProceduralGeometry.DrunkardWalk.AllFloorCellsHaveValidRegion", DefaultTestFlags)

bool FDrunkardWalkAllFloorCellsHaveValidRegionTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("RegionValidTest"), 4);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	// Fixed seed, fixed room count: an empty grid is a failure, not a reason to skip the per-cell checks.
	if (!TestTrue(TEXT("AllFloorCellsHaveValidRegion: seeded generation produced a grid"), Data.Grid.Num() > 0))
	{
		return false;
	}

	const int32 Total = Data.Grid.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		if (Data.Grid[i])
		{
			if (!TestTrue(FString::Printf(TEXT("FloorCell[%d] has valid RegionId"), i), Data.RegionIds[i] >= 0))
			{
				// First failure only, to keep the log readable.
				AddError(FString::Printf(TEXT("  First invalid cell at index %d"), i));
				break;
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkCellTypeConsistencyTest, "ProceduralGeometry.DrunkardWalk.CellTypeConsistency", DefaultTestFlags)

bool FDrunkardWalkCellTypeConsistencyTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("CellTypeTest"), 4);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	// Fixed seed, fixed room count: an empty grid is a failure, not a reason to skip the per-cell checks.
	if (!TestTrue(TEXT("CellTypeConsistency: seeded generation produced a grid"), Data.Grid.Num() > 0))
	{
		return false;
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

namespace
{
	/** Builds a config with room types of the given weights. */
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

	/** Sum of the resolved per-type counts. */
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalSumEqualWeightsTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_SumEqualWeights", DefaultTestFlags)

bool FDWConfigResolveForTotalSumEqualWeightsTest::RunTest(const FString& Parameters)
{
	// Prime total over equal weights forces a rounding remainder.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalWeightedTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_Weighted", DefaultTestFlags)

bool FDWConfigResolveForTotalWeightedTest::RunTest(const FString& Parameters)
{
	// Weights 3:1 over 8 rooms: Round(3/4*8)=6 for type 0, 2 for type 1.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalEndToEndTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_EndToEnd", DefaultTestFlags)

bool FDWConfigResolveForTotalEndToEndTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig				  Config = MakeDWConfig({ 2, 1 });
	constexpr int32					  Total = 6;
	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(Total);

	TestEqual("ResolveForTotal_EndToEnd: sum == 6", SumDWCounts(Params), Total);

	UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
	Gen->SetSeed(TEXT("ResolveForTotalE2E"));
	Gen->SetGridSize(100);
	Gen->ApplyResolvedParams(Params);

	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	TestTrue("ResolveForTotal_EndToEnd: diagram has cells", Data.Diagram.Cells.Num() > 0);
	TestTrue("ResolveForTotal_EndToEnd: rooms were placed", Data.PlacedRooms.Num() >= 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalMinRespectedTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_MinRespected", DefaultTestFlags)

bool FDWConfigResolveForTotalMinRespectedTest::RunTest(const FString& Parameters)
{
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigResolveForTotalMaxRespectedTest, "ProceduralGeometry.DrunkardWalk.Config.ResolveForTotal_MaxRespected", DefaultTestFlags)

bool FDWConfigResolveForTotalMaxRespectedTest::RunTest(const FString& Parameters)
{
	// Type 0 is capped at 4 despite the heavier weight; the uncapped type absorbs the rest of the budget of 10.
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

// The exit loop's band offset and TraceOne's band layout must share one perpendicular convention; disagreeing
// signs narrow the door on two of the four sides.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkDoorWidthTest, "ProceduralGeometry.DrunkardWalk.DoorWidthMatchesCorridorWidthOnEverySide", DefaultTestFlags)

bool FDrunkardWalkDoorWidthTest::RunTest(const FString& Parameters)
{
	// An even corridor width is what a perpendicular sign flip misplaces: the band is asymmetric around the rail.
	constexpr int32 CorridorWidth = 2;
	constexpr int32 RoomSide = 4;
	constexpr int32 SeedCount = 100;

	bool  bFaceObserved[4] = { false, false, false, false };
	int32 SampleCount = 0;

	for (int32 SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex)
	{
		UDrunkardWalkGenerator2D* Gen = MakeDrunkardGenerator(FString::Printf(TEXT("DoorWidth%d"), SeedIndex), /*RoomCount=*/2, RoomSide);
		Gen->SetCorridorWidth(CorridorWidth);
		Gen->SetCorridorTurnProbability(0.0f);
		Gen->SetCorridorBranchProbability(0.0f);
		Gen->SetBranchProbability(0.0f);

		const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();
		if (Data.WalkerPaths.Num() != 1 || Data.CorridorSourceRoom.Num() != 1 || Data.WalkerPaths[0].Num() == 0)
		{
			continue; // the walk backtracked into a different shape than this test inspects
		}
		const int32 SourceIndex = Data.CorridorSourceRoom[0];
		if (!Data.PlacedRooms.IsValidIndex(SourceIndex) || Data.GridWidth <= 0 || Data.GridHeight <= 0)
		{
			continue;
		}

		const FDrunkardWalkPlacedRoom& Room = Data.PlacedRooms[SourceIndex];
		const FIntPoint				   Start = Data.WalkerPaths[0][0];

		// The rail's first cell sits one cell outside exactly one face, which is what identifies it.
		int32 Face = INDEX_NONE;
		if (Start.X == Room.Min.X + Room.Width)
		{
			Face = 0;
		}
		else if (Start.X == Room.Min.X - 1)
		{
			Face = 1;
		}
		else if (Start.Y == Room.Min.Y + Room.Height)
		{
			Face = 2;
		}
		else if (Start.Y == Room.Min.Y - 1)
		{
			Face = 3;
		}
		if (Face == INDEX_NONE)
		{
			AddError(FString::Printf(TEXT("DoorWidth: seed %d - corridor start (%d,%d) is not adjacent to source room [%d,%d]+%dx%d"),
				SeedIndex,
				Start.X,
				Start.Y,
				Room.Min.X,
				Room.Min.Y,
				Room.Width,
				Room.Height));
			continue;
		}

		const bool	bVerticalFace = (Face <= 1); // the door line runs along Y for the -X/+X faces
		const int32 LineFixed = (Face == 0) ? Room.Min.X + Room.Width
			: (Face == 1)					? Room.Min.X - 1
			: (Face == 2)					? Room.Min.Y + Room.Height
											: Room.Min.Y - 1;
		const int32 InnerFixed = (Face == 0) ? Room.Min.X + Room.Width - 1
			: (Face == 1)					 ? Room.Min.X
			: (Face == 2)					 ? Room.Min.Y + Room.Height - 1
											 : Room.Min.Y;
		const int32 SpanMin = bVerticalFace ? Room.Min.Y : Room.Min.X;
		const int32 SpanMax = bVerticalFace ? Room.Min.Y + Room.Height - 1 : Room.Min.X + Room.Width - 1;
		const int32 SpanLimit = bVerticalFace ? Data.GridHeight : Data.GridWidth;

		auto CellTypeAt = [&Data](int32 X, int32 Y) -> uint8 {
			if (X < 0 || X >= Data.GridWidth || Y < 0 || Y >= Data.GridHeight)
			{
				return EDrunkardWalkCellType::Empty;
			}
			return Data.CellType[Y * Data.GridWidth + X];
		};

		int32 DoorCount = 0;
		int32 DoorMin = MAX_int32;
		int32 DoorMax = MIN_int32;
		int32 StrayCount = 0;

		for (int32 Along = 0; Along < SpanLimit; ++Along)
		{
			const int32 X = bVerticalFace ? LineFixed : Along;
			const int32 Y = bVerticalFace ? Along : LineFixed;
			if (CellTypeAt(X, Y) != EDrunkardWalkCellType::Corridor)
			{
				continue;
			}

			// A door cell is 4-adjacent to the source room's own floor; a diagonal-only touch is a nub off the corner.
			const int32 InnerX = bVerticalFace ? InnerFixed : Along;
			const int32 InnerY = bVerticalFace ? Along : InnerFixed;
			const bool	bInsideSpan = Along >= SpanMin && Along <= SpanMax;
			if (bInsideSpan && CellTypeAt(InnerX, InnerY) == EDrunkardWalkCellType::Room)
			{
				++DoorCount;
				DoorMin = FMath::Min(DoorMin, Along);
				DoorMax = FMath::Max(DoorMax, Along);
			}
			else
			{
				++StrayCount;
			}
		}

		const FString Context = FString::Printf(TEXT("DoorWidth: seed %d face %d"), SeedIndex, Face);
		TestEqual(*(Context + TEXT(" - door is as wide as the corridor")), DoorCount, CorridorWidth);
		TestEqual(*(Context + TEXT(" - no corridor cell hangs off the face")), StrayCount, 0);
		if (DoorCount > 0)
		{
			TestEqual(*(Context + TEXT(" - door cells are contiguous")), DoorMax - DoorMin + 1, DoorCount);
		}

		bFaceObserved[Face] = true;
		++SampleCount;
	}

	TestTrue(TEXT("DoorWidth: the seed sweep produced usable samples"), SampleCount > 0);
	for (int32 Face = 0; Face < 4; ++Face)
	{
		// Only two of the four sides are affected by a sign flip, so every face has to be exercised.
		TestTrue(*FString::Printf(TEXT("DoorWidth: exit face %d was exercised by the seed sweep"), Face), bFaceObserved[Face]);
	}

	return true;
}

// At margin 0 the clearance ring collapses onto the candidate cell, which pending cells are exempt from.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkRoomBorderMarginZeroTest, "ProceduralGeometry.DrunkardWalk.RoomBorderMarginZero_NoRoomOverlap", DefaultTestFlags)

bool FDrunkardWalkRoomBorderMarginZeroTest::RunTest(const FString& Parameters)
{
	constexpr int32 SeedCount = 80;
	constexpr int32 RoomCount = 10;

	int32 SampleCount = 0;
	int32 OverlapCount = 0;

	for (int32 SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex)
	{
		UDrunkardWalkGenerator2D* Gen = MakeDrunkardGenerator(FString::Printf(TEXT("MarginZero%d"), SeedIndex), RoomCount);
		Gen->SetRoomBorderMargin(0);
		Gen->SetCorridorTurnProbability(0.0f);
		Gen->SetCorridorBranchProbability(0.0f);
		// Growing from a random open room folds the layout back over itself, where candidates meet owned cells.
		Gen->SetBranchProbability(0.5f);

		const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();
		if (Data.bDegradedResolution)
		{
			continue; // downsampling merges coordinates, so two distinct footprints can legitimately fuse
		}

		++SampleCount;
		for (int32 A = 0; A < Data.PlacedRooms.Num(); ++A)
		{
			for (int32 B = A + 1; B < Data.PlacedRooms.Num(); ++B)
			{
				const FDrunkardWalkPlacedRoom& RA = Data.PlacedRooms[A];
				const FDrunkardWalkPlacedRoom& RB = Data.PlacedRooms[B];

				const bool bDisjoint = RA.Min.X + RA.Width <= RB.Min.X || RB.Min.X + RB.Width <= RA.Min.X || RA.Min.Y + RA.Height <= RB.Min.Y
					|| RB.Min.Y + RB.Height <= RA.Min.Y;
				if (bDisjoint)
				{
					continue;
				}

				++OverlapCount;
				if (OverlapCount == 1)
				{
					AddError(FString::Printf(TEXT("RoomBorderMarginZero: seed %d - room %d [%d,%d]+%dx%d overlaps room %d [%d,%d]+%dx%d"),
						SeedIndex,
						A,
						RA.Min.X,
						RA.Min.Y,
						RA.Width,
						RA.Height,
						B,
						RB.Min.X,
						RB.Min.Y,
						RB.Width,
						RB.Height));
				}
			}
		}
	}

	TestTrue(TEXT("RoomBorderMarginZero: the seed sweep produced usable samples"), SampleCount > 0);
	TestEqual(TEXT("RoomBorderMarginZero: no two placed rooms share a cell"), OverlapCount, 0);

	return true;
}

// A fork is seeded one cell off its parent's rail, so the parent band must be exempt from the fork's clearance ring.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkCorridorBranchProbabilityTest, "ProceduralGeometry.DrunkardWalk.CorridorBranchProbability_PlacesForks", DefaultTestFlags)

bool FDrunkardWalkCorridorBranchProbabilityTest::RunTest(const FString& Parameters)
{
	constexpr int32 SeedCount = 20;
	constexpr int32 RoomCount = 10;

	int32 ForksWhenBranching = 0;
	int32 ForksWhenStraight = 0;

	for (int32 SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex)
	{
		// Same seed and same room queue on both runs, so the fork probability is the only difference.
		const FString Seed = FString::Printf(TEXT("CorridorFork%d"), SeedIndex);

		UDrunkardWalkGenerator2D* Branching = MakeDrunkardGenerator(Seed, RoomCount);
		Branching->SetCorridorBranchProbability(1.0f);
		ForksWhenBranching += Branching->GenerateWithGridData().ForksPlaced;

		UDrunkardWalkGenerator2D* Straight = MakeDrunkardGenerator(Seed, RoomCount);
		Straight->SetCorridorBranchProbability(0.0f);
		ForksWhenStraight += Straight->GenerateWithGridData().ForksPlaced;
	}

	TestTrue(TEXT("CorridorBranchProbability: probability 1 places forks"), ForksWhenBranching > 0);
	TestEqual(TEXT("CorridorBranchProbability: probability 0 places none"), ForksWhenStraight, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkGridToDiagramTest, "ProceduralGeometry.DrunkardWalk.GridToDiagramMatchesGrid", DefaultTestFlags)

bool FDrunkardWalkGridToDiagramTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D*	Gen = MakeDrunkardGenerator(TEXT("GridToDiagram"), 6);
	const FDrunkardWalkGridData Data = Gen->GenerateWithGridData();

	const FLayoutDiagram2D& Diagram = Data.Diagram;
	const int32				GridWidth = Data.GridWidth;
	const int32				GridHeight = Data.GridHeight;
	const float				CellSize = Data.CellSize;

	// The conversion narrows the bounds origin to float, so the reference has to do its arithmetic at that width.
	const float MinX = static_cast<float>(Diagram.Bounds.Min.X);
	const float MinY = static_cast<float>(Diagram.Bounds.Min.Y);

	TestTrue(TEXT("Conversion produced cells"), Diagram.Cells.Num() > 0);

	// A degraded grid would rescale the cell size away from the pitch the conversion uses; this config is under budget.
	TestFalse(TEXT("This config does not degrade resolution"), Data.bDegradedResolution);

	// Rebuild the specified mapping: one CCW cell of one grid pitch per carved position, row-major, neighbours in
	// +X/-X/+Y/-Y order, center cell the first closest to CenterPoint.
	if (Data.Grid.Num() != GridWidth * GridHeight)
	{
		AddError(TEXT("Grid array size does not match the reported grid dimensions"));
		return false;
	}

	TArray<int32> ExpectedCellIndex;
	ExpectedCellIndex.Init(INDEX_NONE, GridWidth * GridHeight);
	int32 ExpectedCount = 0;
	for (int32 GridIndex = 0; GridIndex < Data.Grid.Num(); ++GridIndex)
	{
		if (Data.Grid[GridIndex])
		{
			ExpectedCellIndex[GridIndex] = ExpectedCount++;
		}
	}

	TestEqual(TEXT("One diagram cell per carved grid cell"), Diagram.Cells.Num(), ExpectedCount);

	static constexpr int32 DX[] = { 1, -1, 0, 0 };
	static constexpr int32 DY[] = { 0, 0, 1, -1 };

	// bIsExterior is left out of the comparison: ExteriorRingIsMarked owns that field against hardcoded positions.
	float ExpectedBestDistSq = FLT_MAX;
	int32 ExpectedCenterCell = INDEX_NONE;
	int32 Mismatches = 0;

	for (int32 Y = 0; Y < GridHeight && Mismatches == 0; ++Y)
	{
		for (int32 X = 0; X < GridWidth && Mismatches == 0; ++X)
		{
			const int32 GridIndex = Y * GridWidth + X;
			const int32 CellIndex = ExpectedCellIndex[GridIndex];
			if (CellIndex == INDEX_NONE || !Diagram.Cells.IsValidIndex(CellIndex))
			{
				continue;
			}

			const FLayoutCell2D& Cell = Diagram.Cells[CellIndex];

			const float X0 = MinX + X * CellSize;
			const float Y0 = MinY + Y * CellSize;
			const float X1 = MinX + (X + 1) * CellSize;
			const float Y1 = MinY + (Y + 1) * CellSize;

			const TArray<FVector2D> ExpectedVertices = { FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) };
			const FVector2D			ExpectedCenter(MinX + (X + 0.5f) * CellSize, MinY + (Y + 0.5f) * CellSize);

			TArray<int32> ExpectedNeighbors;
			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				const int32 NX = X + DX[Dir];
				const int32 NY = Y + DY[Dir];
				if (NX >= 0 && NX < GridWidth && NY >= 0 && NY < GridHeight && ExpectedCellIndex[NY * GridWidth + NX] != INDEX_NONE)
				{
					ExpectedNeighbors.Add(ExpectedCellIndex[NY * GridWidth + NX]);
				}
			}

			const float DistSq = FVector2D::DistSquared(ExpectedCenter, Diagram.CenterPoint);
			if (DistSq < ExpectedBestDistSq)
			{
				ExpectedBestDistSq = DistSq;
				ExpectedCenterCell = CellIndex;
			}

			if (Cell.CellIndex != CellIndex || Cell.Vertices != ExpectedVertices || Cell.Center != ExpectedCenter
				|| Cell.Neighbors != ExpectedNeighbors)
			{
				++Mismatches;
				AddError(FString::Printf(TEXT("Diagram cell %d (grid %d,%d) does not match the expected conversion"), CellIndex, X, Y));
			}
		}
	}

	TestEqual(TEXT("Every diagram cell matches the expected conversion"), Mismatches, 0);
	TestEqual(TEXT("CenterCellIndex is the first cell closest to CenterPoint"), Diagram.CenterCellIndex, ExpectedCenterCell);

	return true;
}

// The raster is padded, so exterior means on the raster edge or orthogonally against a wall that is; without it
// FVoronoiGridDiagram::ExteriorCells stays empty for every raster cluster.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkExteriorRingTest, "ProceduralGeometry.DrunkardWalk.ExteriorRingIsMarked", DefaultTestFlags)

bool FDrunkardWalkExteriorRingTest::RunTest(const FString& Parameters)
{
	// 7x7 raster: a 5x5 floor block inset by one wall cell, with an interior wall at (3,3).
	static constexpr int32 GridWidth = 7;
	static constexpr int32 GridHeight = 7;

	TArray<bool> Grid;
	Grid.Init(false, GridWidth * GridHeight);
	for (int32 Y = 1; Y <= 5; ++Y)
	{
		for (int32 X = 1; X <= 5; ++X)
		{
			Grid[Y * GridWidth + X] = !(X == 3 && Y == 3);
		}
	}

	UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
	Gen->SetSeed(TEXT("ExteriorRing"));
	Gen->SetGridSize(100);
	Gen->SetBounds(FBox2D(FVector2D(0.0f, 0.0f), FVector2D(static_cast<float>(GridWidth * 100), static_cast<float>(GridHeight * 100))));

	const FLayoutDiagram2D Diagram = Gen->ConvertGridToDiagramForTests(Grid, GridWidth, GridHeight);

	// Cells are emitted one per carved position in row-major order, so the same scan recovers the mapping.
	TArray<int32> CellIndexOf;
	CellIndexOf.Init(INDEX_NONE, GridWidth * GridHeight);
	int32 Emitted = 0;
	for (int32 GridIndex = 0; GridIndex < Grid.Num(); ++GridIndex)
	{
		if (Grid[GridIndex])
		{
			CellIndexOf[GridIndex] = Emitted++;
		}
	}

	if (!TestEqual(TEXT("One diagram cell per carved position"), Diagram.Cells.Num(), Emitted))
	{
		return false;
	}

	auto ExteriorAt = [&](int32 X, int32 Y) -> bool {
		const int32 CellIndex = CellIndexOf[Y * GridWidth + X];
		if (!Diagram.Cells.IsValidIndex(CellIndex))
		{
			AddError(FString::Printf(TEXT("Grid position (%d,%d) was expected to be a carved cell"), X, Y));
			return false;
		}
		return Diagram.Cells[CellIndex].bIsExterior;
	};

	TestTrue(TEXT("A floor cell against a raster-edge wall is exterior (1,1)"), ExteriorAt(1, 1));
	TestTrue(TEXT("A floor cell against a raster-edge wall is exterior (5,5)"), ExteriorAt(5, 5));
	TestTrue(TEXT("A floor cell against a raster-edge wall is exterior (3,1)"), ExteriorAt(3, 1));
	TestFalse(TEXT("A floor cell surrounded by floor is not exterior (2,2)"), ExteriorAt(2, 2));
	TestFalse(TEXT("A floor cell beside an interior wall is not exterior (2,3)"), ExteriorAt(2, 3));
	TestFalse(TEXT("A floor cell beside an interior wall is not exterior (3,2)"), ExteriorAt(3, 2));

	return true;
}

// An unseeded run records the seed it invented on the diagram, so its layout can be reproduced.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkSubstitutedSeedTest, "ProceduralGeometry.DrunkardWalk.SubstitutedSeedIsRecorded", DefaultTestFlags)

bool FDrunkardWalkSubstitutedSeedTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(
		TEXT("was substituted and is carried on the diagram"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

	FRoomTypeConfig RoomType;
	RoomType.Tag = FName(TEXT("Test"));
	RoomType.FootprintWidthCells = 4;
	RoomType.FootprintHeightCells = 4;
	RoomType.Weight = 3;

	UDrunkardWalkGenerator2D* Unseeded = NewObject<UDrunkardWalkGenerator2D>();
	Unseeded->SetGridSize(100);
	Unseeded->SetRoomTypes({ RoomType });

	const FDrunkardWalkGridData Data = Unseeded->GenerateWithGridData();
	const FString				Substituted = Data.Diagram.Seed;

	if (!TestFalse(TEXT("An unseeded run records the seed it invented"), Substituted.IsEmpty()))
	{
		return false;
	}

	UDrunkardWalkGenerator2D* Replay = NewObject<UDrunkardWalkGenerator2D>();
	Replay->SetSeed(Substituted);
	Replay->SetGridSize(100);
	Replay->SetRoomTypes({ RoomType });

	const FDrunkardWalkGridData Replayed = Replay->GenerateWithGridData();

	TestEqual(TEXT("Replaying the substituted seed reproduces the cell count"), Replayed.Diagram.Cells.Num(), Data.Diagram.Cells.Num());

	const int32 NumCells = FMath::Min(Replayed.Diagram.Cells.Num(), Data.Diagram.Cells.Num());
	int32		CentreMismatches = 0;
	for (int32 Index = 0; Index < NumCells; ++Index)
	{
		if (!Replayed.Diagram.Cells[Index].Center.Equals(Data.Diagram.Cells[Index].Center, 0.01))
		{
			++CentreMismatches;
		}
	}
	TestEqual(TEXT("Replaying the substituted seed reproduces every cell centre"), CentreMismatches, 0);

	return true;
}

// A weightless type with a positive Min is a mandatory-count entry and receives exactly its Min.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigMinOnlyRoomTypeTest, "ProceduralGeometry.DrunkardWalk.Config.MinOnlyRoomTypeSurvivesResolve", DefaultTestFlags)

bool FDWConfigMinOnlyRoomTypeTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig Config;
	{
		FRoomTypeConfig Mandatory;
		Mandatory.Tag = FName(TEXT("Boss"));
		Mandatory.FootprintWidthCells = 4;
		Mandatory.FootprintHeightCells = 4;
		Mandatory.Weight = 0;
		Mandatory.Min = 2;
		Config.RoomTypes.Add(Mandatory);

		FRoomTypeConfig Filler;
		Filler.Tag = FName(TEXT("Normal"));
		Filler.FootprintWidthCells = 4;
		Filler.FootprintHeightCells = 4;
		Filler.Weight = 5;
		Config.RoomTypes.Add(Filler);
	}

	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(10);

	if (!TestEqual(TEXT("MinOnlyRoomType: the weightless type survives Resolve"), Params.RoomTypes.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("MinOnlyRoomType: the mandatory type receives exactly its Min"), Params.RoomTypes[0].Weight, 2);
	TestEqual(TEXT("MinOnlyRoomType: the weighted type absorbs the rest"), Params.RoomTypes[1].Weight, 8);
	TestEqual(TEXT("MinOnlyRoomType: sum == 10"), SumDWCounts(Params), 10);

	return true;
}

// With only Min-only types nothing claims the leftover budget, so it stays unspent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDWConfigAllMinOnlyRoomTypesTest, "ProceduralGeometry.DrunkardWalk.Config.AllMinOnlyTypesPlaceOnlyTheirMinimums", DefaultTestFlags)

bool FDWConfigAllMinOnlyRoomTypesTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig Config;
	{
		FRoomTypeConfig Boss;
		Boss.Tag = FName(TEXT("Boss"));
		Boss.FootprintWidthCells = 4;
		Boss.FootprintHeightCells = 4;
		Boss.Weight = 0;
		Boss.Min = 1;
		Config.RoomTypes.Add(Boss);

		FRoomTypeConfig Vault;
		Vault.Tag = FName(TEXT("Vault"));
		Vault.FootprintWidthCells = 4;
		Vault.FootprintHeightCells = 4;
		Vault.Weight = 0;
		Vault.Min = 2;
		Config.RoomTypes.Add(Vault);
	}

	const FDrunkardWalkResolvedParams Params = Config.ResolveForTotal(10);

	if (!TestEqual(TEXT("AllMinOnly: both weightless types survive Resolve"), Params.RoomTypes.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("AllMinOnly: Boss receives exactly its Min"), Params.RoomTypes[0].Weight, 1);
	TestEqual(TEXT("AllMinOnly: Vault receives exactly its Min"), Params.RoomTypes[1].Weight, 2);
	TestEqual(TEXT("AllMinOnly: the unclaimed budget is not distributed"), SumDWCounts(Params), 3);

	// Minimums that overrun the budget still have to fit inside it.
	const FDrunkardWalkResolvedParams Squeezed = Config.ResolveForTotal(2);
	TestEqual(TEXT("AllMinOnly: minimums over budget are scaled into it"), SumDWCounts(Squeezed), 2);

	return true;
}

#endif
