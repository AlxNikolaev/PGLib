#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"
#include "Generators/CellularAutomata2D/CellularAutomataConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// Test 1: Default Generate() produces non-empty diagram
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataDefaultGenerateTest, "ProceduralGeometry.CellularAutomata.DefaultGenerate", DefaultTestFlags)

bool FCellularAutomataDefaultGenerateTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetSeed(TEXT("DefaultTest"));

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestTrue("Default generation should produce cells", Diagram.Cells.Num() > 0);

	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Cell %d should have >= 3 vertices"), i), Diagram.Cells[i].Vertices.Num() >= 3);
		TestEqual(FString::Printf(TEXT("Cell %d index should match"), i), Diagram.Cells[i].CellIndex, i);
	}

	return true;
}

// Test 2: Determinism - same seed produces identical diagram
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataDeterminismTest, "ProceduralGeometry.CellularAutomata.Determinism", DefaultTestFlags)

bool FCellularAutomataDeterminismTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("DeterminismTest");

	UCellularAutomataGenerator2D* Gen1 = NewObject<UCellularAutomataGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed);
	FLayoutDiagram2D Diagram1 = Gen1->Generate();

	UCellularAutomataGenerator2D* Gen2 = NewObject<UCellularAutomataGenerator2D>();
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataNeighborSymmetryTest, "ProceduralGeometry.CellularAutomata.NeighborSymmetry", DefaultTestFlags)

bool FCellularAutomataNeighborSymmetryTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
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

// Test 4: FillProbability=0.0 carves most interior cells (low fill = mostly floor)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataLowFillTest, "ProceduralGeometry.CellularAutomata.LowFillProbability", DefaultTestFlags)

bool FCellularAutomataLowFillTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetGridSize(100);
	Generator->SetSeed(TEXT("LowFillTest"));
	Generator->SetFillProbability(0.0f);
	Generator->SetIterations(0); // No CA smoothing, pure initial fill

	FLayoutDiagram2D Diagram = Generator->Generate();

	// With FillProbability=0.0 and 0 iterations, all 64 interior floor cells
	// form one connected region that merges into a single cell
	TestEqual("FillProbability=0 with 0 iterations should produce 1 merged region", Diagram.Cells.Num(), 1);

	// The boundary of an 8x8 rectangular interior block simplifies to 4 vertices
	if (Diagram.Cells.Num() > 0)
	{
		TestEqual("Single region should have 4 vertices (rectangle)", Diagram.Cells[0].Vertices.Num(), 4);
	}

	return true;
}

// Test 5: FillProbability=1.0 produces empty or near-empty diagram
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataHighFillTest, "ProceduralGeometry.CellularAutomata.HighFillProbability", DefaultTestFlags)

bool FCellularAutomataHighFillTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetGridSize(100);
	Generator->SetSeed(TEXT("HighFillTest"));
	Generator->SetFillProbability(1.0f);
	Generator->SetIterations(0); // No CA smoothing, pure initial fill

	FLayoutDiagram2D Diagram = Generator->Generate();

	// With FillProbability=1.0, FRand() >= 1.0 is almost never true, so nearly all cells are wall
	TestTrue("FillProbability=1.0 should produce very few cells", Diagram.Cells.Num() <= 2);

	return true;
}

// Test 6: MinRegionSize filter removes small regions
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataMinRegionSizeTest, "ProceduralGeometry.CellularAutomata.MinRegionSize", DefaultTestFlags)

bool FCellularAutomataMinRegionSizeTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("RegionSizeTest");

	// Generate with MinRegionSize=1 (keep all regions)
	UCellularAutomataGenerator2D* GenKeepAll = NewObject<UCellularAutomataGenerator2D>();
	GenKeepAll->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenKeepAll->SetMinRegionSize(1);
	GenKeepAll->SetKeepCenterRegion(false);
	FLayoutDiagram2D DiagramAll = GenKeepAll->Generate();

	// Generate with high MinRegionSize (cull small regions)
	UCellularAutomataGenerator2D* GenCulled = NewObject<UCellularAutomataGenerator2D>();
	GenCulled->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenCulled->SetMinRegionSize(50);
	GenCulled->SetKeepCenterRegion(false);
	FLayoutDiagram2D DiagramCulled = GenCulled->Generate();

	TestTrue("Keeping all regions should have cells", DiagramAll.Cells.Num() > 0);

	// Culling small regions should produce fewer or equal cells
	TestTrue("High MinRegionSize should produce fewer or equal cells", DiagramCulled.Cells.Num() <= DiagramAll.Cells.Num());

	return true;
}

// Test 7: bKeepCenterRegion=true preserves center region even when below MinRegionSize
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataKeepCenterTest, "ProceduralGeometry.CellularAutomata.KeepCenterRegion", DefaultTestFlags)

bool FCellularAutomataKeepCenterTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("CenterRegionTest");

	// Generate with bKeepCenterRegion=true and high MinRegionSize
	UCellularAutomataGenerator2D* GenKeepCenter = NewObject<UCellularAutomataGenerator2D>();
	GenKeepCenter->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenKeepCenter->SetMinRegionSize(9999);
	GenKeepCenter->SetKeepCenterRegion(true);
	FLayoutDiagram2D DiagramKeep = GenKeepCenter->Generate();

	// Generate with bKeepCenterRegion=false and same high MinRegionSize
	UCellularAutomataGenerator2D* GenCullCenter = NewObject<UCellularAutomataGenerator2D>();
	GenCullCenter->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenCullCenter->SetMinRegionSize(9999);
	GenCullCenter->SetKeepCenterRegion(false);
	FLayoutDiagram2D DiagramCull = GenCullCenter->Generate();

	// With bKeepCenterRegion=true, the center region is preserved
	TestTrue("KeepCenterRegion=true should preserve cells", DiagramKeep.Cells.Num() > 0);
	TestTrue("KeepCenterRegion=true should have valid CenterCellIndex",
		DiagramKeep.CenterCellIndex >= 0 && DiagramKeep.CenterCellIndex < DiagramKeep.Cells.Num());

	// With bKeepCenterRegion=false and very high MinRegionSize, all regions may be culled
	TestTrue("KeepCenterRegion=true should produce >= cells compared to false", DiagramKeep.Cells.Num() >= DiagramCull.Cells.Num());

	return true;
}

// Test 8: OOM guard - huge bounds + small GridSize returns empty diagram
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataOOMGuardTest, "ProceduralGeometry.CellularAutomata.OOMGuard", DefaultTestFlags)

bool FCellularAutomataOOMGuardTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-100000, -100000), FVector2D(100000, 100000)));
	Generator->SetGridSize(10);
	Generator->SetSeed(TEXT("OOMTest"));

	FLayoutDiagram2D Diagram = Generator->Generate();

	TestEqual("OOM guard should produce empty diagram", Diagram.Cells.Num(), 0);

	return true;
}

// Test 9: Region merging produces correct region-level cells
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataRegionMergingTest, "ProceduralGeometry.CellularAutomata.RegionMerging", DefaultTestFlags)

bool FCellularAutomataRegionMergingTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetGridSize(100);
	Generator->SetSeed(TEXT("RegionMergeTest"));
	Generator->SetFillProbability(0.45f);
	Generator->SetIterations(3);
	Generator->SetMinRegionSize(1);
	Generator->SetKeepCenterRegion(true);

	FLayoutDiagram2D Diagram = Generator->Generate();

	// Region merging should produce fewer cells than total possible interior cells
	TestTrue("Should have cells", Diagram.Cells.Num() > 0);
	TestTrue("Merged regions should be fewer than raw grid cells", Diagram.Cells.Num() < 8 * 8);

	// Each cell should have a valid polygon
	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Cell %d should have >= 3 vertices"), i), Diagram.Cells[i].Vertices.Num() >= 3);
		TestEqual(FString::Printf(TEXT("Cell %d index matches"), i), Diagram.Cells[i].CellIndex, i);
	}

	// Neighbor symmetry at region level
	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		for (int32 NeighborIdx : Diagram.Cells[i].Neighbors)
		{
			TestTrue(FString::Printf(TEXT("Neighbor %d of cell %d is valid"), NeighborIdx, i), NeighborIdx >= 0 && NeighborIdx < Diagram.Cells.Num());

			if (NeighborIdx >= 0 && NeighborIdx < Diagram.Cells.Num())
			{
				TestTrue(FString::Printf(TEXT("Cell %d neighbors %d, so %d should neighbor %d"), i, NeighborIdx, NeighborIdx, i),
					Diagram.Cells[NeighborIdx].Neighbors.Contains(i));
			}
		}
	}

	// CenterCellIndex should be valid
	TestTrue("CenterCellIndex should be valid", Diagram.CenterCellIndex >= 0 && Diagram.CenterCellIndex < Diagram.Cells.Num());

	return true;
}

// Test 10: CarveCorridors connects disconnected regions
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsConnectsDisconnected", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsTest::RunTest(const FString& Parameters)
{
	// SwissCheese config produces many small isolated pockets — good for testing corridor carving
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)))->SetGridSize(20)->SetSeed(TEXT("corridor_test_disconnect"));
	Generator->SetFillProbability(0.45f);
	Generator->SetIterations(5);
	Generator->SetBirthRule({ 6, 7, 8 });
	Generator->SetSurvivalRule({ 3, 4, 5 });
	Generator->SetMinRegionSize(5);
	Generator->SetKeepCenterRegion(true);

	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();

	// Count surviving regions
	int32 SurvivingCount = 0;
	for (bool bSurvived : GridData.SurvivingRegions)
	{
		if (bSurvived)
		{
			++SurvivingCount;
		}
	}

	TestTrue("At least 2 surviving regions", SurvivingCount >= 2);

	if (SurvivingCount < 2)
	{
		AddWarning(TEXT("Grid randomness produced fewer than 2 surviving regions — corridor carving assertions skipped"));
		return true;
	}

	// Check for disconnected pairs
	TSet<TPair<int32, int32>> ConnectedPairs;
	for (int32 CellIdx = 0; CellIdx < GridData.Diagram.Cells.Num(); ++CellIdx)
	{
		for (int32 NeighborIdx : GridData.Diagram.Cells[CellIdx].Neighbors)
		{
			int32 MinIdx = FMath::Min(CellIdx, NeighborIdx);
			int32 MaxIdx = FMath::Max(CellIdx, NeighborIdx);
			ConnectedPairs.Add(TPair<int32, int32>(MinIdx, MaxIdx));
		}
	}

	// Count total possible pairs vs connected pairs
	const int32 TotalPossiblePairs = GridData.Diagram.Cells.Num() * (GridData.Diagram.Cells.Num() - 1) / 2;
	const bool	bHasDisconnectedPairs = ConnectedPairs.Num() < TotalPossiblePairs;

	if (!bHasDisconnectedPairs)
	{
		AddWarning(TEXT("All surviving regions are already connected — corridor carving connectivity assertions skipped"));
		return true;
	}

	// Snapshot floor count before carving
	int32 FloorBefore = 0;
	for (bool bIsFloor : GridData.Grid)
	{
		if (bIsFloor)
		{
			++FloorBefore;
		}
	}

	// Carve with probability 1.0 and width 2
	FRandomStream CorridorStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(GridData, 1.0f, 2, CorridorStream);

	int32 FloorAfter = 0;
	for (bool bIsFloor : GridData.Grid)
	{
		if (bIsFloor)
		{
			++FloorAfter;
		}
	}

	TestTrue("Carving added floor cells", FloorAfter > FloorBefore);

	// Verify carved cells have valid region assignments
	for (int32 i = 0; i < GridData.Grid.Num(); ++i)
	{
		if (GridData.Grid[i])
		{
			TestTrue(FString::Printf(TEXT("Floor cell %d has valid RegionId"), i), GridData.RegionIds[i] >= 0);
		}
	}

	// Rebuild diagram and verify
	Generator->RebuildDiagram(GridData);
	TestTrue("Rebuilt diagram has cells", GridData.Diagram.Cells.Num() > 0);

	return true;
}

// Test 11: CarveCorridors with probability 0 is a no-op
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsNoOpTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsProbabilityZeroNoOp", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsNoOpTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)))->SetGridSize(20)->SetSeed(TEXT("corridor_test_disconnect"));
	Generator->SetFillProbability(0.45f);
	Generator->SetIterations(5);
	Generator->SetBirthRule({ 6, 7, 8 });
	Generator->SetSurvivalRule({ 3, 4, 5 });
	Generator->SetMinRegionSize(5);
	Generator->SetKeepCenterRegion(true);

	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();

	// Deep copy grid
	TArray<bool> GridCopy = GridData.Grid;

	// Carve with probability 0 — should be a no-op
	FRandomStream CorridorStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(GridData, 0.0f, 2, CorridorStream);

	TestTrue("Grid unchanged with probability 0", GridData.Grid == GridCopy);

	return true;
}

// Test 12: CarveCorridors with no disconnected regions is a no-op
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsAllConnectedTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsAllConnectedNoOp", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsAllConnectedTest::RunTest(const FString& Parameters)
{
	// OpenChambers style with large cells — tends to produce one large connected region
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)))->SetGridSize(50)->SetSeed(TEXT("corridor_test_connected"));
	Generator->SetFillProbability(0.40f);
	Generator->SetIterations(10);
	Generator->SetBirthRule({ 5, 6, 7, 8 });
	Generator->SetSurvivalRule({ 4, 5, 6, 7, 8 });
	Generator->SetMinRegionSize(1);
	Generator->SetKeepCenterRegion(true);

	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();

	// Deep copy grid
	TArray<bool> GridCopy = GridData.Grid;

	// Check if all surviving regions are connected
	TSet<TPair<int32, int32>> ConnectedPairs;
	for (int32 CellIdx = 0; CellIdx < GridData.Diagram.Cells.Num(); ++CellIdx)
	{
		for (int32 NeighborIdx : GridData.Diagram.Cells[CellIdx].Neighbors)
		{
			int32 MinIdx = FMath::Min(CellIdx, NeighborIdx);
			int32 MaxIdx = FMath::Max(CellIdx, NeighborIdx);
			ConnectedPairs.Add(TPair<int32, int32>(MinIdx, MaxIdx));
		}
	}

	const int32 TotalPossiblePairs = GridData.Diagram.Cells.Num() * (GridData.Diagram.Cells.Num() - 1) / 2;
	const bool	bAllConnected = (ConnectedPairs.Num() >= TotalPossiblePairs) || (GridData.Diagram.Cells.Num() <= 1);

	// Carve with probability 1.0 — if all connected, should be a no-op
	FRandomStream CorridorStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(GridData, 1.0f, 2, CorridorStream);

	if (bAllConnected)
	{
		TestTrue("Grid unchanged when all regions already connected", GridData.Grid == GridCopy);
	}
	else
	{
		// Edge case: this config still produced disconnected regions — carving is valid
		AddWarning(TEXT("Config produced disconnected regions — grid may have changed, which is acceptable"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
