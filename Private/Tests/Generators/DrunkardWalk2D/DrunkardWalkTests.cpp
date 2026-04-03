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

#endif // WITH_DEV_AUTOMATION_TESTS
