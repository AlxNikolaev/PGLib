#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"
#include "Generators/CellularAutomata2D/CellularAutomataConfig.h"
#include "GridBudget.h"
#include "../../PGStructuralHash.h"
#include "../../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * Hand-built grid of solid blobs separated by walls. The corridor tests need a substrate whose disconnected pairs
	 * are a property of the input rather than of CA chance: an emergent substrate can come out already connected, or
	 * with a single surviving region, and then the assertions the test exists for have nothing to run on.
	 *
	 * Names carry the CACorridorRef_ prefix because adaptive unity builds can merge these translation units.
	 */
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

	/** The two-blob substrate the corridor tests share: one carve, across the middle of a 21x21 grid. */
	FCellularAutomataGridData CACorridorRef_MakeTwoBlobGrid()
	{
		return CACorridorRef_MakeBlobGrid(21, 21, { FIntRect(FIntPoint(2, 8), FIntPoint(5, 12)), FIntRect(FIntPoint(15, 8), FIntPoint(18, 12)) });
	}

	int32 CACorridorRef_CountFloor(const FCellularAutomataGridData& Data)
	{
		int32 Count = 0;
		for (const bool bIsFloor : Data.Grid)
		{
			Count += bIsFloor ? 1 : 0;
		}
		return Count;
	}
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

	// Counts and centres are blind to the drift that matters: an adjacency or contour pass that started reading a
	// TSet in hash order leaves every cell with the same number of vertices, the same number of neighbours and the
	// same centre while moving the vertices and reshuffling the neighbour lists. The hash covers all of it.
	if (!TestTrue(TEXT("Deterministic seed produced a non-empty diagram to compare"), Diagram1.Cells.Num() > 0))
	{
		return false;
	}

	TestEqual(
		TEXT("Same-seed diagrams are structurally identical"), PGTestHash::HashLayoutDiagram2D(Diagram1), PGTestHash::HashLayoutDiagram2D(Diagram2));

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

// Test 10: CarveCorridors bridges a pair of regions that share no boundary
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsConnectsDisconnected", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsTest::RunTest(const FString& Parameters)
{
	// Two hand-built blobs with no shared boundary and no diagram adjacency: the disconnected pair CarveCorridors
	// exists to bridge, present by construction. An emergent CA substrate can come out already connected or collapsed
	// to one region, and then this test has nothing to assert on.
	FCellularAutomataGridData GridData = CACorridorRef_MakeTwoBlobGrid();

	const int32 FloorBefore = CACorridorRef_CountFloor(GridData);

	FRandomStream CorridorStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(GridData, 1.0f, 2, CorridorStream);

	TestTrue(TEXT("Carving added floor cells"), CACorridorRef_CountFloor(GridData) > FloorBefore);

	for (int32 Index = 0; Index < GridData.Grid.Num(); ++Index)
	{
		if (GridData.Grid[Index] && GridData.RegionIds[Index] < 0)
		{
			AddError(FString::Printf(TEXT("Floor cell %d has no region after carving"), Index));
			break;
		}
	}

	// The carve has to leave the two blobs reachable from one another, not merely add floor somewhere. Re-flooding
	// the modified grid through the production path answers exactly that: one region means one connected component.
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	// Bounds and cell size only place the traced polygons in world space; they are set so the rebuild reads a
	// well-defined origin rather than a default-constructed box.
	Generator->SetBounds(FBox2D(FVector2D::ZeroVector, FVector2D(GridData.GridWidth * GridData.CellSize, GridData.GridHeight * GridData.CellSize)));
	Generator->SetGridSize(static_cast<int32>(GridData.CellSize));
	Generator->RebuildDiagram(GridData);
	TestEqual(TEXT("The two regions are one connected region after carving"), GridData.Diagram.Cells.Num(), 1);

	return true;
}

// Test 11: CarveCorridors with probability 0 is a no-op
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsNoOpTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsProbabilityZeroNoOp", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsNoOpTest::RunTest(const FString& Parameters)
{
	const FCellularAutomataGridData Source = CACorridorRef_MakeTwoBlobGrid();

	FCellularAutomataGridData ZeroProbability = Source;
	FRandomStream			  ZeroStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(ZeroProbability, 0.0f, 2, ZeroStream);

	TestTrue(TEXT("Grid unchanged with probability 0"), ZeroProbability.Grid == Source.Grid);

	// Control: the same substrate at probability 1 must change, or "unchanged" above would be a statement about the
	// input having nothing to carve rather than about the probability gate.
	FCellularAutomataGridData FullProbability = Source;
	FRandomStream			  FullStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(FullProbability, 1.0f, 2, FullStream);

	TestTrue(TEXT("The same substrate does carve at probability 1"), FullProbability.Grid != Source.Grid);

	return true;
}

// Test 12: CarveCorridors leaves an already-connected pair alone
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsAllConnectedTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsAllConnectedNoOp", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsAllConnectedTest::RunTest(const FString& Parameters)
{
	// Same two blobs, but recorded as diagram neighbours: CarveCorridors reads connectivity off the diagram, so this
	// is the "already connected" case stated as an input instead of hoped for from a CA configuration.
	FCellularAutomataGridData Connected = CACorridorRef_MakeTwoBlobGrid();
	Connected.Diagram.Cells[0].Neighbors = { 1 };
	Connected.Diagram.Cells[1].Neighbors = { 0 };

	const TArray<bool> GridBeforeCarve = Connected.Grid;

	FRandomStream ConnectedStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(Connected, 1.0f, 2, ConnectedStream);

	TestTrue(TEXT("Grid unchanged when the pair is already connected"), Connected.Grid == GridBeforeCarve);

	// Control: drop the adjacency and the identical grid does carve, so the no-op above is the adjacency skip and not
	// a substrate that could never be carved.
	FCellularAutomataGridData Disconnected = CACorridorRef_MakeTwoBlobGrid();
	FRandomStream			  DisconnectedStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(Disconnected, 1.0f, 2, DisconnectedStream);

	TestTrue(TEXT("The same grid without the adjacency does carve"), Disconnected.Grid != GridBeforeCarve);

	return true;
}

// Test 13: CorridorWidth changes how much a corridor carves
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataCarveCorridorsWidthTest, "ProceduralGeometry.CellularAutomata.CarveCorridorsWidthChangesCarve", DefaultTestFlags)

bool FCellularAutomataCarveCorridorsWidthTest::RunTest(const FString& Parameters)
{
	// Two hand-built floor blobs that share no boundary: the carve is forced regardless of CA randomness,
	// so CorridorWidth is the only variable between the two runs below.
	FCellularAutomataGridData NarrowData = CACorridorRef_MakeTwoBlobGrid();
	const int32				  FloorBefore = CACorridorRef_CountFloor(NarrowData);

	FRandomStream NarrowStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(NarrowData, 1.0f, 1, NarrowStream);
	const int32 NarrowFloor = CACorridorRef_CountFloor(NarrowData);

	FCellularAutomataGridData WideData = CACorridorRef_MakeTwoBlobGrid();
	FRandomStream			  WideStream(42);
	UCellularAutomataGenerator2D::CarveCorridors(WideData, 1.0f, 3, WideStream);
	const int32 WideFloor = CACorridorRef_CountFloor(WideData);

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
