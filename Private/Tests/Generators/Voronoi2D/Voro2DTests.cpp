#include "Voro2DTests.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Run in editor and game contexts, product-level test with medium priority
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;

	// Quick smoke tests
	constexpr EAutomationTestFlags SmokeTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::HighPriority;

	// Performance tests
	constexpr EAutomationTestFlags PerfTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::PerfFilter | EAutomationTestFlags::LowPriority;
} // namespace

bool FVoronoiTestBase::AreVerticesClockwise(const TArray<FVector2D>& Vertices)
{
	if (Vertices.Num() < 3)
		return false;

	float SignedArea = 0.0f;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& V1 = Vertices[i];
		const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];
		SignedArea += (V2.X - V1.X) * (V2.Y + V1.Y);
	}

	return SignedArea > 0.0f;
}

bool FVoronoiTestBase::IsConvexPolygon(const TArray<FVector2D>& Vertices)
{
	if (Vertices.Num() < 3)
		return false;

	bool bSignSet = false;
	bool bPositive = false;

	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& A = Vertices[i];
		const FVector2D& B = Vertices[(i + 1) % Vertices.Num()];
		const FVector2D& C = Vertices[(i + 2) % Vertices.Num()];

		FVector2D AB = B - A;
		FVector2D BC = C - B;
		float	  CrossProduct = AB.X * BC.Y - AB.Y * BC.X;

		if (!bSignSet)
		{
			bSignSet = true;
			bPositive = CrossProduct > 0;
		}
		else if ((CrossProduct > 0) != bPositive)
		{
			return false; // Sign changed, not convex
		}
	}

	return true;
}

float FVoronoiTestBase::CalculatePolygonArea(const TArray<FVector2D>& Vertices)
{
	float Area = 0.0f;
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		const FVector2D& V1 = Vertices[i];
		const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];
		Area += V1.X * V2.Y - V2.X * V1.Y;
	}
	return FMath::Abs(Area) * 0.5f;
}

// Test 1: Basic Cell Properties
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiCellPropertiesTest, "Voronoi.Cell.Properties", DefaultTestFlags)

bool FVoronoiCellPropertiesTest::RunTest(const FString& Parameters)
{
	// Create a simple square cell
	FVoronoiCell2D Cell;
	Cell.Vertices.Add(FVector2D(0, 0));
	Cell.Vertices.Add(FVector2D(10, 0));
	Cell.Vertices.Add(FVector2D(10, 10));
	Cell.Vertices.Add(FVector2D(0, 10));
	Cell.SiteLocation = FVector2D(5, 5);
	Cell.bIsValid = true;

	// Test area calculation
	float ExpectedArea = 100.0f;
	float ActualArea = Cell.GetArea();
	TestEqual("Cell area should be 100", ActualArea, ExpectedArea, 0.01f);

	// Test centroid calculation
	const FVector2D ExpectedCentroid(5, 5);
	const FVector2D ActualCentroid = Cell.GetCentroid();
	TestEqual("Centroid X", static_cast<float>(ActualCentroid.X), static_cast<float>(ExpectedCentroid.X), 0.01f);
	TestEqual("Centroid Y", static_cast<float>(ActualCentroid.Y), static_cast<float>(ExpectedCentroid.Y), 0.01f);

	// Test point containment
	TestTrue("Center point should be inside", Cell.ContainsPoint(FVector2D(5, 5)));
	TestTrue("Corner point should be inside", Cell.ContainsPoint(FVector2D(1, 1)));
	TestFalse("Outside point should not be inside", Cell.ContainsPoint(FVector2D(15, 15)));
	TestFalse("Negative point should not be inside", Cell.ContainsPoint(FVector2D(-5, 5)));

	return true;
}

// Test 2: Generator with Fixed Sites (Smoke Test)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiFixedSitesTest, "Voronoi.Generator.FixedSites", SmokeTestFlags)

bool FVoronoiFixedSitesTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	// Create a simple 2x2 grid of sites
	TArray<FVector2D> Sites;
	Sites.Add(FVector2D(25, 25));
	Sites.Add(FVector2D(75, 25));
	Sites.Add(FVector2D(25, 75));
	Sites.Add(FVector2D(75, 75));

	FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);

	// Verify basic properties
	TestEqual("Should have 4 cells", Diagram.Cells.Num(), 4);
	TestEqual("Should have 4 sites", Diagram.Sites.Num(), 4);

	// Each cell should be valid
	for (const FVoronoiCell2D& Cell : Diagram.Cells)
	{
		TestTrue("Cell should be valid", Cell.bIsValid);
		TestTrue("Cell should have at least 3 vertices", Cell.Vertices.Num() >= 3);
	}

	// Test that each site is contained in its cell
	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		TestTrue("Site should be in its own cell", Diagram.Cells[i].ContainsPoint(Diagram.Sites[i]));
	}

	// Corner cells should be boundary cells
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[0].bIsBoundaryCell);
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[1].bIsBoundaryCell);
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[2].bIsBoundaryCell);
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[3].bIsBoundaryCell);

	return true;
}

// Test 3: Neighbor Detection
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiNeighborsTest, "Voronoi.Generator.Neighbors", DefaultTestFlags)

bool FVoronoiNeighborsTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	TArray<FVector2D> Sites;
	Sites.Add(FVector2D(25, 50));
	Sites.Add(FVector2D(50, 50));
	Sites.Add(FVector2D(75, 50));

	FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);

	// Middle cell should have 2 neighbors
	TestEqual("Middle cell should have 2 neighbors", Diagram.Cells[1].Neighbors.Num(), 2);
	TestTrue("Should be neighbor with cell 0", Diagram.Cells[1].Neighbors.Contains(0));
	TestTrue("Should be neighbor with cell 2", Diagram.Cells[1].Neighbors.Contains(2));

	// End cells should have 1 neighbor each
	TestEqual("First cell should have 1 neighbor", Diagram.Cells[0].Neighbors.Num(), 1);
	TestTrue("Should be neighbor with cell 1", Diagram.Cells[0].Neighbors.Contains(1));

	TestEqual("Last cell should have 1 neighbor", Diagram.Cells[2].Neighbors.Num(), 1);
	TestTrue("Should be neighbor with cell 1", Diagram.Cells[2].Neighbors.Contains(1));

	// Test shared edge detection
	FVector2D EdgeStart, EdgeEnd;
	TestTrue("Cells 0 and 1 should share an edge", Diagram.GetSharedEdge(0, 1, EdgeStart, EdgeEnd));
	TestTrue("Cells 1 and 2 should share an edge", Diagram.GetSharedEdge(1, 2, EdgeStart, EdgeEnd));
	TestFalse("Cells 0 and 2 should not share an edge", Diagram.GetSharedEdge(0, 2, EdgeStart, EdgeEnd));

	return true;
}

// Test 4: Random Generation
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiRandomGenerationTest, "Voronoi.Generator.Random", DefaultTestFlags)

bool FVoronoiRandomGenerationTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed("TestSeed123");

	const int32		  NumSites = 20;
	FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(NumSites, false);

	TestEqual("Should have correct number of cells", Diagram.Cells.Num(), NumSites);

	// Verify all cells are valid and convex
	int32 ValidCells = 0;
	float TotalArea = 0.0f;

	for (const FVoronoiCell2D& Cell : Diagram.Cells)
	{
		if (Cell.bIsValid)
		{
			ValidCells++;
			TotalArea += Cell.GetArea();

			// Test convexity
			TestTrue("Cell should be convex", FVoronoiTestBase::IsConvexPolygon(Cell.Vertices));
		}
	}

	TestEqual("All cells should be valid", ValidCells, NumSites);

	// Total area should approximately equal bounds area
	float BoundsArea = 1000.0f * 1000.0f;
	TestEqual("Total cell area should match bounds", TotalArea, BoundsArea, BoundsArea * 0.01f);

	return true;
}

// Test 5: Performance Test
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPerformanceTest, "Voronoi.Generator.Performance", PerfTestFlags)

bool FVoronoiPerformanceTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000)));
	Generator->SetSeed("PerfTest");

	// Test different sizes
	TArray<int32> TestSizes = { 10, 50, 100, 500 };

	for (int32 NumSites : TestSizes)
	{
		double StartTime = FPlatformTime::Seconds();

		FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(NumSites, false);

		double ElapsedTime = FPlatformTime::Seconds() - StartTime;

		UE_LOG(LogTemp, Log, TEXT("Generated %d sites in %f seconds"), NumSites, ElapsedTime);

		// Basic validation
		TestEqual(FString::Printf(TEXT("Should have %d cells"), NumSites), Diagram.Cells.Num(), NumSites);

		// Performance thresholds (adjust based on target hardware)
		if (NumSites <= 100)
		{
			TestTrue(FString::Printf(TEXT("%d sites should generate in < 0.1s"), NumSites), ElapsedTime < 0.1);
		}
	}

	return true;
}

// Test 9: Point Location
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoronoiPointLocationTest, "Voronoi.Diagram.PointLocation", EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FVoronoiPointLocationTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	// Create known configuration
	TArray<FVector2D> Sites;
	Sites.Add(FVector2D(25, 25));
	Sites.Add(FVector2D(75, 25));
	Sites.Add(FVector2D(25, 75));
	Sites.Add(FVector2D(75, 75));

	FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);

	// Test points that should be in specific cells
	TestEqual("Point near first site", Diagram.FindCellContainingPoint(FVector2D(20, 20)), 0);
	TestEqual("Point near second site", Diagram.FindCellContainingPoint(FVector2D(80, 20)), 1);
	TestEqual("Point near third site", Diagram.FindCellContainingPoint(FVector2D(20, 80)), 2);
	TestEqual("Point near fourth site", Diagram.FindCellContainingPoint(FVector2D(80, 80)), 3);

	// Test center point
	int32 CenterCell = Diagram.FindCellContainingPoint(FVector2D(50, 50));
	TestTrue("Center point should be in a valid cell", CenterCell >= 0 && CenterCell < 4);

	// Test outside bounds
	TestEqual("Point outside bounds", Diagram.FindCellContainingPoint(FVector2D(-10, -10)), INDEX_NONE);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS