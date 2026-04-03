#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// Test 1: Default Generate() produces non-empty diagram with valid cells
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkDefaultGenerateTest, "ProceduralGeometry.DrunkardWalk.DefaultGenerate", DefaultTestFlags)

bool FDrunkardWalkDefaultGenerateTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetSeed(TEXT("DefaultTest"));

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestTrue("Diagram should have cells", Diagram.Cells.Num() > 0);

	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		const FLayoutCell2D& Cell = Diagram.Cells[i];

		// Each cell should have exactly 4 vertices (grid rectangle)
		TestEqual(FString::Printf(TEXT("Cell %d should have 4 vertices"), i), Cell.Vertices.Num(), 4);

		if (Cell.Vertices.Num() == 4)
		{
			// Verify CCW winding: signed area should be negative (CCW in screen coords)
			float SignedArea = 0.0f;
			for (int32 v = 0; v < Cell.Vertices.Num(); ++v)
			{
				const FVector2D& V1 = Cell.Vertices[v];
				const FVector2D& V2 = Cell.Vertices[(v + 1) % Cell.Vertices.Num()];
				SignedArea += (V2.X - V1.X) * (V2.Y + V1.Y);
			}
			TestTrue(FString::Printf(TEXT("Cell %d should be CCW (negative signed area)"), i), SignedArea < 0.0f);
		}

		TestEqual(FString::Printf(TEXT("Cell %d index should match"), i), Cell.CellIndex, i);
	}

	return true;
}

// Test 2: Determinism - same seed produces identical diagram
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkDeterminismTest, "ProceduralGeometry.DrunkardWalk.Determinism", DefaultTestFlags)

bool FDrunkardWalkDeterminismTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("DeterminismTest");

	UDrunkardWalkGenerator2D* Gen1 = NewObject<UDrunkardWalkGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed);
	FLayoutDiagram2D Diagram1 = Gen1->Generate();

	UDrunkardWalkGenerator2D* Gen2 = NewObject<UDrunkardWalkGenerator2D>();
	Gen2->SetBounds(TestBounds)->SetSeed(TestSeed);
	FLayoutDiagram2D Diagram2 = Gen2->Generate();

	TestEqual("Same cell count", Diagram1.Cells.Num(), Diagram2.Cells.Num());

	for (int32 i = 0; i < FMath::Min(Diagram1.Cells.Num(), Diagram2.Cells.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Cell %d vertex count"), i), Diagram1.Cells[i].Vertices.Num(), Diagram2.Cells[i].Vertices.Num());
		TestEqual(FString::Printf(TEXT("Cell %d neighbor count"), i), Diagram1.Cells[i].Neighbors.Num(), Diagram2.Cells[i].Neighbors.Num());

		TestEqual(FString::Printf(TEXT("Cell %d center X"), i),
			static_cast<float>(Diagram1.Cells[i].Center.X),
			static_cast<float>(Diagram2.Cells[i].Center.X),
			0.01f);
		TestEqual(FString::Printf(TEXT("Cell %d center Y"), i),
			static_cast<float>(Diagram1.Cells[i].Center.Y),
			static_cast<float>(Diagram2.Cells[i].Center.Y),
			0.01f);
	}

	return true;
}

// Test 3: Neighbor symmetry - if A neighbors B, B neighbors A
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkNeighborSymmetryTest, "ProceduralGeometry.DrunkardWalk.NeighborSymmetry", DefaultTestFlags)

bool FDrunkardWalkNeighborSymmetryTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetSeed(TEXT("SymmetryTest"));

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestTrue("Should have cells for symmetry check", Diagram.Cells.Num() > 0);

	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		const FLayoutCell2D& Cell = Diagram.Cells[i];
		for (int32 NeighborIdx : Cell.Neighbors)
		{
			TestTrue(FString::Printf(TEXT("Neighbor %d of cell %d should be valid index"), NeighborIdx, i),
				NeighborIdx >= 0 && NeighborIdx < Diagram.Cells.Num());

			if (NeighborIdx >= 0 && NeighborIdx < Diagram.Cells.Num())
			{
				TestTrue(FString::Printf(TEXT("Cell %d neighbors %d, so %d should neighbor %d"), i, NeighborIdx, NeighborIdx, i),
					Diagram.Cells[NeighborIdx].Neighbors.Contains(i));
			}
		}
	}

	return true;
}

// Test 4: WalkLength=1 produces at least 1 cell
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkMinimalWalkTest, "ProceduralGeometry.DrunkardWalk.MinimalWalk", DefaultTestFlags)

bool FDrunkardWalkMinimalWalkTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetSeed(TEXT("MinimalWalk"));
	Generator->SetWalkLength(1);

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestTrue("WalkLength=1 should produce at least 1 cell", Diagram.Cells.Num() >= 1);

	return true;
}

// Test 5: CorridorWidth>1 produces more cells than CorridorWidth=1
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkCorridorWidthTest, "ProceduralGeometry.DrunkardWalk.CorridorWidth", DefaultTestFlags)

bool FDrunkardWalkCorridorWidthTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("CorridorTest");

	UDrunkardWalkGenerator2D* GenNarrow = NewObject<UDrunkardWalkGenerator2D>();
	GenNarrow->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenNarrow->SetCorridorWidth(1);
	GenNarrow->SetWalkLength(200);
	FLayoutDiagram2D DiagramNarrow = GenNarrow->Generate();

	UDrunkardWalkGenerator2D* GenWide = NewObject<UDrunkardWalkGenerator2D>();
	GenWide->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenWide->SetCorridorWidth(3);
	GenWide->SetWalkLength(200);
	FLayoutDiagram2D DiagramWide = GenWide->Generate();

	TestTrue("Narrow corridor should have cells", DiagramNarrow.Cells.Num() > 0);
	TestTrue("Wide corridor should have cells", DiagramWide.Cells.Num() > 0);
	TestTrue("CorridorWidth=3 should produce more cells than CorridorWidth=1", DiagramWide.Cells.Num() > DiagramNarrow.Cells.Num());

	return true;
}

// Test 6: CenterCellIndex is valid
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkCenterCellTest, "ProceduralGeometry.DrunkardWalk.CenterCellIndex", DefaultTestFlags)

bool FDrunkardWalkCenterCellTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetSeed(TEXT("CenterTest"));

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestTrue("Diagram should have cells", Diagram.Cells.Num() > 0);
	TestTrue("CenterCellIndex should be valid", Diagram.CenterCellIndex >= 0 && Diagram.CenterCellIndex < Diagram.Cells.Num());

	return true;
}

// Test 7: OOM guard - huge bounds + small GridSize returns empty diagram
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkOOMGuardTest, "ProceduralGeometry.DrunkardWalk.OOMGuard", DefaultTestFlags)

bool FDrunkardWalkOOMGuardTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-100000, -100000), FVector2D(100000, 100000)));
	Generator->SetGridSize(10);
	Generator->SetSeed(TEXT("OOMTest"));

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestEqual("OOM guard should produce empty diagram", Diagram.Cells.Num(), 0);

	return true;
}

// Test 8: High momentum produces longer straight corridors
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkMomentumCorridorsTest, "ProceduralGeometry.DrunkardWalk.MomentumProducesLongerCorridors", DefaultTestFlags)

bool FDrunkardWalkMomentumCorridorsTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-1000, -1000), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("MomentumTest");
	const int32	  TestWalkLength = 800;

	// Grid A: no momentum
	UDrunkardWalkGenerator2D* GenA = NewObject<UDrunkardWalkGenerator2D>();
	GenA->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenA->SetWalkLength(TestWalkLength);
	FDrunkardWalkGridData DataA = GenA->GenerateWithGridData();

	// Grid B: high momentum
	UDrunkardWalkGenerator2D* GenB = NewObject<UDrunkardWalkGenerator2D>();
	GenB->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenB->SetWalkLength(TestWalkLength)->SetDirectionalMomentum(0.9f);
	FDrunkardWalkGridData DataB = GenB->GenerateWithGridData();

	// Compute straightness: count consecutive same-direction steps / total steps
	auto ComputeStraightness = [](const TArray<TArray<FIntPoint>>& WalkerPaths) -> float {
		int32 TotalSteps = 0;
		int32 SameDirectionSteps = 0;

		for (const TArray<FIntPoint>& Path : WalkerPaths)
		{
			for (int32 i = 2; i < Path.Num(); ++i)
			{
				const FIntPoint DirPrev = Path[i - 1] - Path[i - 2];
				const FIntPoint DirCurr = Path[i] - Path[i - 1];
				++TotalSteps;
				if (DirPrev == DirCurr)
				{
					++SameDirectionSteps;
				}
			}
		}

		return TotalSteps > 0 ? static_cast<float>(SameDirectionSteps) / static_cast<float>(TotalSteps) : 0.0f;
	};

	const float StraightnessA = ComputeStraightness(DataA.WalkerPaths);
	const float StraightnessB = ComputeStraightness(DataB.WalkerPaths);

	TestTrue(FString::Printf(TEXT("Momentum=0.9 straightness (%.3f) > Momentum=0.0 straightness (%.3f)"), StraightnessB, StraightnessA),
		StraightnessB > StraightnessA);

	return true;
}

// Test 9: High exploration bias carves more unique cells
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkExplorationCoverageTest, "ProceduralGeometry.DrunkardWalk.ExplorationBiasReducesOverlap", DefaultTestFlags)

bool FDrunkardWalkExplorationCoverageTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-1000, -1000), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("ExplorationTest");
	const int32	  TestWalkLength = 800;

	// Grid A: no exploration bias
	UDrunkardWalkGenerator2D* GenA = NewObject<UDrunkardWalkGenerator2D>();
	GenA->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenA->SetWalkLength(TestWalkLength);
	FDrunkardWalkGridData DataA = GenA->GenerateWithGridData();

	// Grid B: high exploration bias
	UDrunkardWalkGenerator2D* GenB = NewObject<UDrunkardWalkGenerator2D>();
	GenB->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenB->SetWalkLength(TestWalkLength)->SetExplorationBias(0.9f);
	FDrunkardWalkGridData DataB = GenB->GenerateWithGridData();

	// Count unique floor cells
	auto CountFloorCells = [](const TArray<bool>& Grid) -> int32 {
		int32 Count = 0;
		for (bool bIsFloor : Grid)
		{
			if (bIsFloor)
			{
				++Count;
			}
		}
		return Count;
	};

	const int32 FloorA = CountFloorCells(DataA.Grid);
	const int32 FloorB = CountFloorCells(DataB.Grid);

	TestTrue(FString::Printf(TEXT("ExplorationBias=0.9 floor cells (%d) > ExplorationBias=0.0 floor cells (%d)"), FloorB, FloorA), FloorB > FloorA);

	return true;
}

// Test 10: Momentum is deterministic
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkMomentumDeterminismTest, "ProceduralGeometry.DrunkardWalk.MomentumDeterminism", DefaultTestFlags)

bool FDrunkardWalkMomentumDeterminismTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("MomentumDeterminism");

	UDrunkardWalkGenerator2D* Gen1 = NewObject<UDrunkardWalkGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed);
	Gen1->SetDirectionalMomentum(0.7f);
	FDrunkardWalkGridData Data1 = Gen1->GenerateWithGridData();

	UDrunkardWalkGenerator2D* Gen2 = NewObject<UDrunkardWalkGenerator2D>();
	Gen2->SetBounds(TestBounds)->SetSeed(TestSeed);
	Gen2->SetDirectionalMomentum(0.7f);
	FDrunkardWalkGridData Data2 = Gen2->GenerateWithGridData();

	TestEqual("Grid sizes must match", Data1.Grid.Num(), Data2.Grid.Num());

	bool bIdentical = true;
	for (int32 i = 0; i < Data1.Grid.Num(); ++i)
	{
		if (Data1.Grid[i] != Data2.Grid[i])
		{
			bIdentical = false;
			break;
		}
	}
	TestTrue("Momentum grids should be cell-for-cell identical", bIdentical);

	return true;
}

// Test 11: Exploration bias is deterministic
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkExplorationDeterminismTest, "ProceduralGeometry.DrunkardWalk.ExplorationBiasDeterminism", DefaultTestFlags)

bool FDrunkardWalkExplorationDeterminismTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("ExplorationDeterminism");

	UDrunkardWalkGenerator2D* Gen1 = NewObject<UDrunkardWalkGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed);
	Gen1->SetExplorationBias(0.8f);
	FDrunkardWalkGridData Data1 = Gen1->GenerateWithGridData();

	UDrunkardWalkGenerator2D* Gen2 = NewObject<UDrunkardWalkGenerator2D>();
	Gen2->SetBounds(TestBounds)->SetSeed(TestSeed);
	Gen2->SetExplorationBias(0.8f);
	FDrunkardWalkGridData Data2 = Gen2->GenerateWithGridData();

	TestEqual("Grid sizes must match", Data1.Grid.Num(), Data2.Grid.Num());

	bool bIdentical = true;
	for (int32 i = 0; i < Data1.Grid.Num(); ++i)
	{
		if (Data1.Grid[i] != Data2.Grid[i])
		{
			bIdentical = false;
			break;
		}
	}
	TestTrue("Exploration bias grids should be cell-for-cell identical", bIdentical);

	return true;
}

// Test 12: Max momentum respects boundary — no floor cells in border ring
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkMaxMomentumBoundaryTest, "ProceduralGeometry.DrunkardWalk.MaxMomentumRespectsBoundary", DefaultTestFlags)

bool FDrunkardWalkMaxMomentumBoundaryTest::RunTest(const FString& Parameters)
{
	// Small grid to increase chance of hitting boundaries
	const FBox2D TestBounds(FVector2D(-50, -50), FVector2D(50, 50));

	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(TestBounds)->SetSeed(TEXT("MaxMomentumBoundary"));
	Generator->SetGridSize(10);
	Generator->SetWalkLength(500);
	Generator->SetDirectionalMomentum(1.0f);
	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	const int32 W = Data.GridWidth;
	const int32 H = Data.GridHeight;

	TestTrue("Grid should have non-zero dimensions", W > 0 && H > 0);

	bool bBorderClean = true;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			if (X == 0 || X == W - 1 || Y == 0 || Y == H - 1)
			{
				if (Data.Grid[Y * W + X])
				{
					AddError(FString::Printf(TEXT("Border cell (%d,%d) is floor — boundary violated"), X, Y));
					bBorderClean = false;
				}
			}
		}
	}
	TestTrue("No floor cells should exist in the border ring", bBorderClean);

	return true;
}

// Test 13: Max exploration bias still produces output (walker doesn't get permanently stuck)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkMaxExplorationOutputTest, "ProceduralGeometry.DrunkardWalk.MaxExplorationBiasStillProducesOutput", DefaultTestFlags)

bool FDrunkardWalkMaxExplorationOutputTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetSeed(TEXT("MaxExploration"));
	Generator->SetExplorationBias(1.0f);
	Generator->SetWalkLength(200);

	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	int32 FloorCount = 0;
	for (bool bIsFloor : Data.Grid)
	{
		if (bIsFloor)
		{
			++FloorCount;
		}
	}

	TestTrue(FString::Printf(TEXT("Max exploration bias should still produce floor cells (got %d)"), FloorCount), FloorCount > 0);

	return true;
}

// Test 14: MinRoomSpacing enforcement — all placed rooms are at least N Manhattan distance apart
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkMinRoomSpacingTest, "ProceduralGeometry.DrunkardWalk.MinRoomSpacingEnforcement", DefaultTestFlags)

bool FDrunkardWalkMinRoomSpacingTest::RunTest(const FString& Parameters)
{
	const int32 Spacing = 10;

	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000)))
		->SetSeed(TEXT("SpacingTest"))
		->SetWalkLength(1000)
		->SetRoomChance(1.0f)
		->SetRoomRadius(2)
		->SetMinRoomSpacing(Spacing);

	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	TestTrue("Should have placed at least one room", Data.RoomCenters.Num() > 0);

	for (int32 i = 0; i < Data.RoomCenters.Num(); ++i)
	{
		for (int32 j = i + 1; j < Data.RoomCenters.Num(); ++j)
		{
			const int32 Dist = FMath::Abs(Data.RoomCenters[i].X - Data.RoomCenters[j].X) + FMath::Abs(Data.RoomCenters[i].Y - Data.RoomCenters[j].Y);
			TestTrue(FString::Printf(TEXT("Room %d (%d,%d) and room %d (%d,%d) dist=%d should be >= %d"),
						 i,
						 Data.RoomCenters[i].X,
						 Data.RoomCenters[i].Y,
						 j,
						 Data.RoomCenters[j].X,
						 Data.RoomCenters[j].Y,
						 Dist,
						 Spacing),
				Dist >= Spacing);
		}
	}

	return true;
}

// Test 15: MaxRoomCount enforcement — at most N rooms placed
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkMaxRoomCountTest, "ProceduralGeometry.DrunkardWalk.MaxRoomCountEnforcement", DefaultTestFlags)

bool FDrunkardWalkMaxRoomCountTest::RunTest(const FString& Parameters)
{
	const int32 MaxRooms = 3;

	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000)))
		->SetSeed(TEXT("MaxRoomCountTest"))
		->SetWalkLength(1000)
		->SetRoomChance(1.0f)
		->SetRoomRadius(2)
		->SetMaxRoomCount(MaxRooms);

	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	TestTrue(FString::Printf(TEXT("RoomCenters.Num() (%d) should be <= %d"), Data.RoomCenters.Num(), MaxRooms), Data.RoomCenters.Num() <= MaxRooms);

	return true;
}

// Test 16: RoomRadiusRange variety — rooms are carved and RoomCenters populated
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkRoomRadiusRangeTest, "ProceduralGeometry.DrunkardWalk.RoomRadiusRangeVariety", DefaultTestFlags)

bool FDrunkardWalkRoomRadiusRangeTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000)))
		->SetSeed(TEXT("RadiusRangeTest"))
		->SetWalkLength(500)
		->SetRoomChance(1.0f)
		->SetRoomRadiusRange(2, 5);

	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	TestTrue("Should have placed rooms", Data.RoomCenters.Num() > 0);

	// Verify room-type cells exist
	bool bHasRoomCells = false;
	for (uint8 Type : Data.CellType)
	{
		if (Type == EDrunkardWalkCellType::Room)
		{
			bHasRoomCells = true;
			break;
		}
	}
	TestTrue("Should have room-type cells", bHasRoomCells);

	return true;
}

// Test 17: SetRoomRadius backward compat — identical output to SetRoomRadiusRange(R, R)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkSetRoomRadiusBackwardCompatTest, "ProceduralGeometry.DrunkardWalk.SetRoomRadiusBackwardCompat", DefaultTestFlags)

bool FDrunkardWalkSetRoomRadiusBackwardCompatTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("BackwardCompatTest");

	UDrunkardWalkGenerator2D* Gen1 = NewObject<UDrunkardWalkGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed)->SetWalkLength(300)->SetRoomChance(0.5f)->SetRoomRadius(3);
	FDrunkardWalkGridData Data1 = Gen1->GenerateWithGridData();

	UDrunkardWalkGenerator2D* Gen2 = NewObject<UDrunkardWalkGenerator2D>();
	Gen2->SetBounds(TestBounds)->SetSeed(TestSeed)->SetWalkLength(300)->SetRoomChance(0.5f)->SetRoomRadiusRange(3, 3);
	FDrunkardWalkGridData Data2 = Gen2->GenerateWithGridData();

	TestEqual("Grid sizes must match", Data1.Grid.Num(), Data2.Grid.Num());
	TestEqual("Room center counts must match", Data1.RoomCenters.Num(), Data2.RoomCenters.Num());

	bool bIdentical = true;
	for (int32 i = 0; i < Data1.Grid.Num(); ++i)
	{
		if (Data1.Grid[i] != Data2.Grid[i])
		{
			bIdentical = false;
			break;
		}
	}
	TestTrue("SetRoomRadius(3) and SetRoomRadiusRange(3,3) should produce identical grids", bIdentical);

	return true;
}

// Test 18: Spacing + Count combined — both constraints hold simultaneously
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkSpacingAndCountCombinedTest, "ProceduralGeometry.DrunkardWalk.SpacingAndCountCombined", DefaultTestFlags)

bool FDrunkardWalkSpacingAndCountCombinedTest::RunTest(const FString& Parameters)
{
	const int32 MaxRooms = 2;
	const int32 Spacing = 8;

	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000)))
		->SetSeed(TEXT("CombinedTest"))
		->SetWalkLength(1000)
		->SetRoomChance(1.0f)
		->SetRoomRadius(2)
		->SetMinRoomSpacing(Spacing)
		->SetMaxRoomCount(MaxRooms);

	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	// Count constraint
	TestTrue(FString::Printf(TEXT("RoomCenters.Num() (%d) should be <= %d"), Data.RoomCenters.Num(), MaxRooms), Data.RoomCenters.Num() <= MaxRooms);

	// Spacing constraint
	for (int32 i = 0; i < Data.RoomCenters.Num(); ++i)
	{
		for (int32 j = i + 1; j < Data.RoomCenters.Num(); ++j)
		{
			const int32 Dist = FMath::Abs(Data.RoomCenters[i].X - Data.RoomCenters[j].X) + FMath::Abs(Data.RoomCenters[i].Y - Data.RoomCenters[j].Y);
			TestTrue(FString::Printf(TEXT("Room %d and %d dist=%d should be >= %d"), i, j, Dist, Spacing), Dist >= Spacing);
		}
	}

	return true;
}

// Test 19: Defaults unchanged — no new setters called, RoomCenters empty (RoomChance=0)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkDefaultsUnchangedTest, "ProceduralGeometry.DrunkardWalk.DefaultsUnchanged", DefaultTestFlags)

bool FDrunkardWalkDefaultsUnchangedTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetSeed(TEXT("DefaultsTest"));

	FDrunkardWalkGridData Data = Generator->GenerateWithGridData();

	TestEqual("Default RoomChance=0 should produce no rooms", Data.RoomCenters.Num(), 0);

	return true;
}

// Test 20: Room placement determinism — same seed + params produces identical RoomCenters
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkRoomPlacementDeterminismTest, "ProceduralGeometry.DrunkardWalk.RoomPlacementDeterminism", DefaultTestFlags)

bool FDrunkardWalkRoomPlacementDeterminismTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("RoomDeterminism");

	auto CreateGenerator = [&]() {
		UDrunkardWalkGenerator2D* Gen = NewObject<UDrunkardWalkGenerator2D>();
		Gen->SetBounds(TestBounds)
			->SetSeed(TestSeed)
			->SetWalkLength(500)
			->SetRoomChance(0.5f)
			->SetRoomRadiusRange(2, 4)
			->SetMinRoomSpacing(6)
			->SetMaxRoomCount(5);
		return Gen;
	};

	FDrunkardWalkGridData Data1 = CreateGenerator()->GenerateWithGridData();
	FDrunkardWalkGridData Data2 = CreateGenerator()->GenerateWithGridData();

	TestEqual("Room center counts must match", Data1.RoomCenters.Num(), Data2.RoomCenters.Num());

	bool bIdentical = true;
	for (int32 i = 0; i < FMath::Min(Data1.RoomCenters.Num(), Data2.RoomCenters.Num()); ++i)
	{
		if (Data1.RoomCenters[i] != Data2.RoomCenters[i])
		{
			bIdentical = false;
			AddError(FString::Printf(TEXT("RoomCenter[%d]: (%d,%d) vs (%d,%d)"),
				i,
				Data1.RoomCenters[i].X,
				Data1.RoomCenters[i].Y,
				Data2.RoomCenters[i].X,
				Data2.RoomCenters[i].Y));
			break;
		}
	}
	TestTrue("RoomCenters arrays should be identical across runs with same seed", bIdentical);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
