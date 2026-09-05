#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"
#include "Generators/CellularAutomata2D/CellularAutomataConfig.h"
#include "GridBudget.h"
#include "../../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

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

// Test 8: Cell budget — huge bounds + small GridSize degrades resolution instead of returning empty.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataOOMGuardTest, "ProceduralGeometry.CellularAutomata.OOMGuard", DefaultTestFlags)

bool FCellularAutomataOOMGuardTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-100000, -100000), FVector2D(100000, 100000)));
	Generator->SetGridSize(10);
	Generator->SetSeed(TEXT("OOMTest"));

	const FCellularAutomataGridData Data = Generator->GenerateWithGridData();

	TestTrue("Cell budget should flag degraded resolution", Data.bDegradedResolution);
	TestTrue("Degraded grid should fit the cell budget", (int64)Data.GridWidth * Data.GridHeight <= PGGrid::MaxGridCells);
	TestTrue("Degraded cell size should exceed the requested 10", Data.CellSize > 10.0f);
	TestTrue("Degraded generation should still produce cells", Data.Diagram.Cells.Num() > 0);

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

// Test 13: CorridorWidth changes how much a corridor carves
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsWidthTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsWidthChangesCarve", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsWidthTest::RunTest(const FString& Parameters)
{
	// Two hand-built floor blobs that share no boundary: the carve is forced regardless of CA randomness,
	// so CorridorWidth is the only variable between the two runs below.
	constexpr int32 GridW = 21;
	constexpr int32 GridH = 21;

	auto MakeTwoRegionGrid = []() {
		FCellularAutomataGridData Data;
		Data.GridWidth = GridW;
		Data.GridHeight = GridH;
		Data.CellSize = 100.0f;
		Data.CenterRegionId = -1;
		Data.Grid.Init(false, GridW * GridH);
		Data.RegionIds.Init(-1, GridW * GridH);
		Data.Regions.SetNum(2);

		auto FillBlob = [&Data](int32 MinX, int32 MaxX, int32 RegionId) {
			for (int32 Y = 8; Y <= 12; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const int32 Index = Y * GridW + X;
					Data.Grid[Index] = true;
					Data.RegionIds[Index] = RegionId;
					Data.Regions[RegionId].Add(FIntPoint(X, Y));
				}
			}
		};
		FillBlob(2, 5, 0);
		FillBlob(15, 18, 1);

		Data.SurvivingRegions.Init(true, 2);

		// CarveCorridors reads region adjacency off the diagram; two cells with no neighbors is exactly the
		// disconnected pair it exists to bridge.
		Data.Diagram.Cells.SetNum(2);
		Data.Diagram.Cells[0].CellIndex = 0;
		Data.Diagram.Cells[1].CellIndex = 1;

		return Data;
	};

	auto CountFloor = [](const FCellularAutomataGridData& Data) {
		int32 Count = 0;
		for (bool bIsFloor : Data.Grid)
		{
			if (bIsFloor)
			{
				++Count;
			}
		}
		return Count;
	};

	FCellularAutomataGridData NarrowData = MakeTwoRegionGrid();
	const int32				  FloorBefore = CountFloor(NarrowData);

	FRandomStream NarrowStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(NarrowData, 1.0f, 1, NarrowStream);
	const int32 NarrowFloor = CountFloor(NarrowData);

	FCellularAutomataGridData WideData = MakeTwoRegionGrid();
	FRandomStream			  WideStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(WideData, 1.0f, 3, WideStream);
	const int32 WideFloor = CountFloor(WideData);

	TestTrue("Width 1 carves a corridor between the two regions", NarrowFloor > FloorBefore);
	TestTrue("Width 3 carves strictly more floor than width 1", WideFloor > NarrowFloor);

	return true;
}

// ============================================================
// CarveCorridors nearest-pair equivalence
// ============================================================

namespace
{
	/**
	 * Reference CarveCorridors: the plain double loop over every cell of both regions, kept so the indexed search in
	 * the generator can be proven to pick the same pair. Every other step — pair ordering, the adjacency skip, the
	 * probability draw, the Bresenham walk and the carve band — is a literal copy, so the two runs consume the random
	 * stream identically and any divergence in the output grid is a divergence in the chosen pair.
	 */
	void CACorridorRef_CarveCorridorsBruteForce(FCellularAutomataGridData& GridData, float Probability, int32 Width, FRandomStream& InRandomStream)
	{
		if (Probability <= 0.0f)
		{
			return;
		}

		Width = FMath::Max(1, Width);

		TArray<int32> SurvivingIds;
		for (int32 i = 0; i < GridData.SurvivingRegions.Num(); ++i)
		{
			if (GridData.SurvivingRegions[i])
			{
				SurvivingIds.Add(i);
			}
		}

		if (SurvivingIds.Num() < 2)
		{
			return;
		}

		TMap<int32, int32> RegionToDiagramCell;
		for (int32 CellIdx = 0; CellIdx < GridData.Diagram.Cells.Num(); ++CellIdx)
		{
			if (CellIdx < SurvivingIds.Num())
			{
				RegionToDiagramCell.Add(SurvivingIds[CellIdx], CellIdx);
			}
		}

		TSet<TPair<int32, int32>> ConnectedPairs;
		for (int32 CellIdx = 0; CellIdx < GridData.Diagram.Cells.Num(); ++CellIdx)
		{
			for (int32 NeighborIdx : GridData.Diagram.Cells[CellIdx].Neighbors)
			{
				ConnectedPairs.Add(TPair<int32, int32>(FMath::Min(CellIdx, NeighborIdx), FMath::Max(CellIdx, NeighborIdx)));
			}
		}

		for (int32 a = 0; a < SurvivingIds.Num(); ++a)
		{
			for (int32 b = a + 1; b < SurvivingIds.Num(); ++b)
			{
				const int32* CellA = RegionToDiagramCell.Find(SurvivingIds[a]);
				const int32* CellB = RegionToDiagramCell.Find(SurvivingIds[b]);

				if (!CellA || !CellB)
				{
					continue;
				}

				if (ConnectedPairs.Contains(TPair<int32, int32>(FMath::Min(*CellA, *CellB), FMath::Max(*CellA, *CellB))))
				{
					continue;
				}

				if (InRandomStream.FRand() > Probability)
				{
					continue;
				}

				const int32				 RegionIdA = SurvivingIds[a];
				const int32				 RegionIdB = SurvivingIds[b];
				const TArray<FIntPoint>& RegionCellsA = GridData.Regions[RegionIdA];
				const TArray<FIntPoint>& RegionCellsB = GridData.Regions[RegionIdB];

				float	  BestDistSq = FLT_MAX;
				FIntPoint BestA(0, 0);
				FIntPoint BestB(0, 0);

				for (const FIntPoint& CellPtA : RegionCellsA)
				{
					for (const FIntPoint& CellPtB : RegionCellsB)
					{
						const float DistSq = static_cast<float>(FMath::Square(CellPtA.X - CellPtB.X) + FMath::Square(CellPtA.Y - CellPtB.Y));
						if (DistSq < BestDistSq)
						{
							BestDistSq = DistSq;
							BestA = CellPtA;
							BestB = CellPtB;
						}
					}
				}

				const int32 HalfWidth = Width / 2;

				int32		X0 = BestA.X, Y0 = BestA.Y;
				const int32 X1 = BestB.X, Y1 = BestB.Y;
				const int32 DX = FMath::Abs(X1 - X0);
				const int32 DY = -FMath::Abs(Y1 - Y0);
				const int32 SX = X0 < X1 ? 1 : -1;
				const int32 SY = Y0 < Y1 ? 1 : -1;
				int32		Err = DX + DY;

				while (true)
				{
					for (int32 OffY = -HalfWidth; OffY <= HalfWidth; ++OffY)
					{
						for (int32 OffX = -HalfWidth; OffX <= HalfWidth; ++OffX)
						{
							const int32 CX = X0 + OffX;
							const int32 CY = Y0 + OffY;

							if (CX > 0 && CX < GridData.GridWidth - 1 && CY > 0 && CY < GridData.GridHeight - 1)
							{
								const int32 Idx = CY * GridData.GridWidth + CX;
								if (!GridData.Grid[Idx])
								{
									GridData.Grid[Idx] = true;
									GridData.RegionIds[Idx] = RegionIdA;
								}
							}
						}
					}

					if (X0 == X1 && Y0 == Y1)
					{
						break;
					}

					const int32 E2 = 2 * Err;
					if (E2 >= DY)
					{
						Err += DY;
						X0 += SX;
					}
					if (E2 <= DX)
					{
						Err += DX;
						Y0 += SY;
					}
				}
			}
		}
	}

	/** Hand-built grid of solid blobs separated by walls, so a carve is guaranteed regardless of CA randomness. */
	FCellularAutomataGridData CACorridorRef_MakeBlobGrid(int32 GridW, int32 GridH, const TArray<FIntRect>& Blobs)
	{
		FCellularAutomataGridData Data;
		Data.GridWidth = GridW;
		Data.GridHeight = GridH;
		Data.CellSize = 100.0f;
		Data.CenterRegionId = -1;
		Data.Grid.Init(false, GridW * GridH);
		Data.RegionIds.Init(-1, GridW * GridH);
		Data.Regions.SetNum(Blobs.Num());

		for (int32 RegionId = 0; RegionId < Blobs.Num(); ++RegionId)
		{
			const FIntRect& Blob = Blobs[RegionId];
			for (int32 Y = Blob.Min.Y; Y <= Blob.Max.Y; ++Y)
			{
				for (int32 X = Blob.Min.X; X <= Blob.Max.X; ++X)
				{
					const int32 Index = Y * GridW + X;
					Data.Grid[Index] = true;
					Data.RegionIds[Index] = RegionId;
					Data.Regions[RegionId].Add(FIntPoint(X, Y));
				}
			}
		}

		Data.SurvivingRegions.Init(true, Blobs.Num());

		// CarveCorridors reads adjacency off the diagram; neighbourless cells are exactly the disconnected pairs.
		Data.Diagram.Cells.SetNum(Blobs.Num());
		for (int32 CellIdx = 0; CellIdx < Blobs.Num(); ++CellIdx)
		{
			Data.Diagram.Cells[CellIdx].CellIndex = CellIdx;
		}

		return Data;
	}

	/**
	 * Lattice of irregular pockets, one per slot, each grown by a seeded walk confined to its slot interior so a
	 * one-cell wall always separates neighbouring slots. Emergent CA substrates were tried first and rejected: their
	 * surviving regions are usually diagram-adjacent (or collapse to one region), which makes CarveCorridors skip
	 * every pair and turns the comparison vacuous. The pockets are concave and ragged, which is what the boundary
	 * pruning in the indexed search has to survive; convex rectangles would not exercise it.
	 */
	FCellularAutomataGridData CACorridorRef_MakePocketGrid(int32 SlotsX, int32 SlotsY, int32 SlotPitch, int32 WalkSteps, int32 Seed)
	{
		const int32 GridW = SlotsX * SlotPitch + 1;
		const int32 GridH = SlotsY * SlotPitch + 1;

		FCellularAutomataGridData Data;
		Data.GridWidth = GridW;
		Data.GridHeight = GridH;
		Data.CellSize = 100.0f;
		Data.CenterRegionId = -1;
		Data.Grid.Init(false, GridW * GridH);
		Data.RegionIds.Init(-1, GridW * GridH);
		Data.Regions.SetNum(SlotsX * SlotsY);

		FRandomStream Stream(Seed);

		for (int32 SlotY = 0; SlotY < SlotsY; ++SlotY)
		{
			for (int32 SlotX = 0; SlotX < SlotsX; ++SlotX)
			{
				const int32 RegionId = SlotY * SlotsX + SlotX;

				// The slot's writable interior; the +1 margin on each side is the wall that keeps pockets apart.
				const int32 MinX = SlotX * SlotPitch + 1;
				const int32 MinY = SlotY * SlotPitch + 1;
				const int32 MaxX = MinX + SlotPitch - 2;
				const int32 MaxY = MinY + SlotPitch - 2;

				int32 X = (MinX + MaxX) / 2;
				int32 Y = (MinY + MaxY) / 2;

				for (int32 Step = 0; Step < WalkSteps; ++Step)
				{
					const int32 Index = Y * GridW + X;
					if (!Data.Grid[Index])
					{
						Data.Grid[Index] = true;
						Data.RegionIds[Index] = RegionId;
						Data.Regions[RegionId].Add(FIntPoint(X, Y));
					}

					switch (Stream.RandRange(0, 3))
					{
						case 0:
							X = FMath::Min(X + 1, MaxX);
							break;
						case 1:
							X = FMath::Max(X - 1, MinX);
							break;
						case 2:
							Y = FMath::Min(Y + 1, MaxY);
							break;
						default:
							Y = FMath::Max(Y - 1, MinY);
							break;
					}
				}
			}
		}

		Data.SurvivingRegions.Init(true, Data.Regions.Num());

		// CarveCorridors reads adjacency off the diagram; neighbourless cells are exactly the disconnected pairs.
		Data.Diagram.Cells.SetNum(Data.Regions.Num());
		for (int32 CellIdx = 0; CellIdx < Data.Regions.Num(); ++CellIdx)
		{
			Data.Diagram.Cells[CellIdx].CellIndex = CellIdx;
		}

		return Data;
	}
} // namespace

// Test 14: the indexed nearest-pair search must carve exactly what the full double loop carves
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataCarveCorridorsNearestPairTest,
	"ProceduralGeometry.CellularAutomata.CarveCorridors.NearestPairMatchesBruteForce",
	DefaultTestFlags)

bool FCellularAutomataCarveCorridorsNearestPairTest::RunTest(const FString& Parameters)
{
	// Each case asserts its own carve count: a shared total would let the two hand-built blob cases, which carve by
	// construction, mask a SwissCheese case that silently stopped producing disconnected pairs.
	auto CompareRun = [this](const FString& CaseName, const FCellularAutomataGridData& Source, int32 CorridorWidth) {
		FCellularAutomataGridData Indexed = Source;
		FRandomStream			  IndexedStream(4242);
		UCellularAutomataGenerator2D::CarveCorridors(Indexed, 1.0f, CorridorWidth, IndexedStream);

		FCellularAutomataGridData Reference = Source;
		FRandomStream			  ReferenceStream(4242);
		CACorridorRef_CarveCorridorsBruteForce(Reference, 1.0f, CorridorWidth, ReferenceStream);

		int32 Mismatches = 0;
		int32 Carved = 0;
		for (int32 Index = 0; Index < Source.Grid.Num(); ++Index)
		{
			if (Indexed.Grid[Index] != Reference.Grid[Index] || Indexed.RegionIds[Index] != Reference.RegionIds[Index])
			{
				++Mismatches;
			}
			if (Indexed.Grid[Index] && !Source.Grid[Index])
			{
				++Carved;
			}
		}

		int32 SurvivingCount = 0;
		for (const bool bSurvives : Source.SurvivingRegions)
		{
			SurvivingCount += bSurvives ? 1 : 0;
		}

		TestEqual(FString::Printf(TEXT("%s: indexed carve matches the brute-force carve cell for cell"), *CaseName), Mismatches, 0);
		TestTrue(FString::Printf(TEXT("%s: substrate has at least two surviving regions to connect (%d)"), *CaseName, SurvivingCount),
			SurvivingCount >= 2);
		TestTrue(FString::Printf(TEXT("%s: at least one cell was carved, so the comparison is not vacuous"), *CaseName), Carved > 0);
	};

	// Hand-built blobs: four separated rectangles guarantee carving, and the asymmetric layout makes a wrong pair
	// choice move the corridor by a visible number of cells.
	{
		const TArray<FIntRect> Blobs = { FIntRect(FIntPoint(2, 2), FIntPoint(9, 9)),
			FIntRect(FIntPoint(20, 3), FIntPoint(28, 12)),
			FIntRect(FIntPoint(4, 20), FIntPoint(14, 27)),
			FIntRect(FIntPoint(22, 22), FIntPoint(27, 26)) };

		const FCellularAutomataGridData Blobbed = CACorridorRef_MakeBlobGrid(31, 31, Blobs);
		CompareRun(TEXT("FourBlobs width 1"), Blobbed, 1);
		CompareRun(TEXT("FourBlobs width 3"), Blobbed, 3);
	}

	// Many small irregular pockets: the many-region case the index exists for, and the shape class that makes the
	// boundary pruning non-trivial.
	const TArray<int32> PocketSeeds = { 101, 202, 303, 404 };
	for (const int32 PocketSeed : PocketSeeds)
	{
		const FCellularAutomataGridData Pockets = CACorridorRef_MakePocketGrid(5, 5, 7, 24, PocketSeed);
		CompareRun(FString::Printf(TEXT("Pockets seed %d"), PocketSeed), Pockets, 2);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
