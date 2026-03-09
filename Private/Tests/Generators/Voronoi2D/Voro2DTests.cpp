#include "Voro2DTests.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoguelikeGeometry, Log, All);

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

		UE_LOG(LogRoguelikeGeometry, Log, TEXT("Generated %d sites in %f seconds"), NumSites, ElapsedTime);

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

// Test 10: Degenerate Input Handling
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiDegenerateInputTest, "ProceduralGeometry.Voronoi.DegenerateInput", DefaultTestFlags)

bool FVoronoiDegenerateInputTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	// A) Zero sites
	{
		FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(0, false);
		TestEqual("Zero sites: should have 0 cells", Diagram.Cells.Num(), 0);
		TestEqual("Zero sites: should have 0 sites", Diagram.Sites.Num(), 0);
	}

	// B) One site
	{
		TArray<FVector2D> Sites;
		Sites.Add(FVector2D(50, 50));
		FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);
		TestEqual("One site: should have 1 cell", Diagram.Cells.Num(), 1);
		if (Diagram.Cells.Num() > 0)
		{
			TestTrue("One site: cell should be valid", Diagram.Cells[0].bIsValid);
			TestTrue("One site: cell should contain its site", Diagram.Cells[0].ContainsPoint(FVector2D(50, 50)));
		}
	}

	// C) Two sites
	{
		TArray<FVector2D> Sites;
		Sites.Add(FVector2D(25, 50));
		Sites.Add(FVector2D(75, 50));
		FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);
		TestEqual("Two sites: should have 2 cells", Diagram.Cells.Num(), 2);
		for (const FVoronoiCell2D& Cell : Diagram.Cells)
		{
			TestTrue("Two sites: cell should be valid", Cell.bIsValid);
			TestTrue("Two sites: cell should have >= 3 vertices", Cell.Vertices.Num() >= 3);
		}
		FVector2D EdgeStart, EdgeEnd;
		TestTrue("Two sites: should share an edge", Diagram.GetSharedEdge(0, 1, EdgeStart, EdgeEnd));
	}

	// D) Collinear sites
	{
		TArray<FVector2D> Sites;
		Sites.Add(FVector2D(10, 50));
		Sites.Add(FVector2D(50, 50));
		Sites.Add(FVector2D(90, 50));
		FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);
		TestEqual("Collinear: should have 3 cells", Diagram.Cells.Num(), 3);
		for (const FVoronoiCell2D& Cell : Diagram.Cells)
		{
			TestTrue("Collinear: cell should be valid", Cell.bIsValid);
			TestTrue("Collinear: cell should be convex", FVoronoiTestBase::IsConvexPolygon(Cell.Vertices));
		}
	}

	// E) Duplicate sites (verify no crash)
	{
		TArray<FVector2D> Sites;
		Sites.Add(FVector2D(50, 50));
		Sites.Add(FVector2D(50, 50));
		Sites.Add(FVector2D(75, 75));
		FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);
		TestEqual("Duplicate: should have 3 cells", Diagram.Cells.Num(), 3);
		// Behavior is undefined for duplicates; just verify no crash occurred
	}

	return true;
}

// Test 11: Deterministic Seeding
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiDeterministicSeedTest, "ProceduralGeometry.Voronoi.DeterministicSeed", DefaultTestFlags)

bool FVoronoiDeterministicSeedTest::RunTest(const FString& Parameters)
{
	const int32	  NumSites = 20;
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("DeterministicTest");

	UVoronoiGenerator2D* Generator1 = NewObject<UVoronoiGenerator2D>();
	Generator1->SetBounds(TestBounds)->SetSeed(TestSeed);
	FVoronoiDiagram2D Diagram1 = Generator1->GenerateRandomSites(NumSites, false);

	UVoronoiGenerator2D* Generator2 = NewObject<UVoronoiGenerator2D>();
	Generator2->SetBounds(TestBounds)->SetSeed(TestSeed);
	FVoronoiDiagram2D Diagram2 = Generator2->GenerateRandomSites(NumSites, false);

	TestEqual("Same cell count", Diagram1.Cells.Num(), Diagram2.Cells.Num());
	TestEqual("Same site count", Diagram1.Sites.Num(), Diagram2.Sites.Num());

	for (int32 i = 0; i < FMath::Min(Diagram1.Sites.Num(), Diagram2.Sites.Num()); ++i)
	{
		TestEqual(
			FString::Printf(TEXT("Site %d X match"), i), static_cast<float>(Diagram1.Sites[i].X), static_cast<float>(Diagram2.Sites[i].X), 0.01f);
		TestEqual(
			FString::Printf(TEXT("Site %d Y match"), i), static_cast<float>(Diagram1.Sites[i].Y), static_cast<float>(Diagram2.Sites[i].Y), 0.01f);
	}

	for (int32 i = 0; i < FMath::Min(Diagram1.Cells.Num(), Diagram2.Cells.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Cell %d vertex count match"), i), Diagram1.Cells[i].Vertices.Num(), Diagram2.Cells[i].Vertices.Num());
		TestEqual(FString::Printf(TEXT("Cell %d neighbor count match"), i), Diagram1.Cells[i].Neighbors.Num(), Diagram2.Cells[i].Neighbors.Num());
	}

	return true;
}

// Test 12: Lloyd Relaxation
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiLloydRelaxationTest, "ProceduralGeometry.Voronoi.LloydRelaxation", DefaultTestFlags)

bool FVoronoiLloydRelaxationTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("RelaxationTest");

	// Generate without relaxation
	UVoronoiGenerator2D* GenNoRelax = NewObject<UVoronoiGenerator2D>();
	GenNoRelax->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenNoRelax->SetRelaxationIterations(0);
	FVoronoiDiagram2D DiagramNoRelax = GenNoRelax->GenerateRelaxed(20);

	// Generate with relaxation
	UVoronoiGenerator2D* GenRelaxed = NewObject<UVoronoiGenerator2D>();
	GenRelaxed->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenRelaxed->SetRelaxationIterations(5);
	FVoronoiDiagram2D DiagramRelaxed = GenRelaxed->GenerateRelaxed(20);

	TestEqual("Both should have same cell count", DiagramNoRelax.Cells.Num(), DiagramRelaxed.Cells.Num());

	// Relaxed sites should have moved from original positions
	float TotalDisplacement = 0.0f;
	int32 SiteCount = FMath::Min(DiagramNoRelax.Sites.Num(), DiagramRelaxed.Sites.Num());
	for (int32 i = 0; i < SiteCount; ++i)
	{
		TotalDisplacement += FVector2D::Distance(DiagramNoRelax.Sites[i], DiagramRelaxed.Sites[i]);
	}
	float AvgDisplacement = (SiteCount > 0) ? TotalDisplacement / SiteCount : 0.0f;
	TestTrue("Relaxation should move sites (avg displacement > 0)", AvgDisplacement > 0.1f);

	// Relaxed sites should remain within bounds
	for (int32 i = 0; i < DiagramRelaxed.Sites.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Relaxed site %d within bounds"), i), TestBounds.IsInside(DiagramRelaxed.Sites[i]));
	}

	// Relaxed cells should still be valid
	for (const FVoronoiCell2D& Cell : DiagramRelaxed.Cells)
	{
		TestTrue("Relaxed cell should be valid", Cell.bIsValid);
	}

	return true;
}

// Test 13: Poisson Disc Sampling
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPoissonDiscTest, "ProceduralGeometry.Voronoi.PoissonDisc", DefaultTestFlags)

bool FVoronoiPoissonDiscTest::RunTest(const FString& Parameters)
{
	const FBox2D TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));

	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(TestBounds)->SetSeed(TEXT("PoissonTest"));
	Generator->SetMinSiteDistance(10.0f);
	FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(50, true);

	TestTrue("Should have sites generated", Diagram.Sites.Num() > 0);

	// All sites within bounds
	for (int32 i = 0; i < Diagram.Sites.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Site %d within bounds"), i), TestBounds.IsInside(Diagram.Sites[i]));
	}

	// Minimum distance constraint: all pairs must be >= MinSiteDistance apart
	const float MinSiteDistance = 10.0f;
	for (int32 i = 0; i < Diagram.Sites.Num(); ++i)
	{
		for (int32 j = i + 1; j < Diagram.Sites.Num(); ++j)
		{
			float Dist = FVector2D::Distance(Diagram.Sites[i], Diagram.Sites[j]);
			TestTrue(FString::Printf(TEXT("Sites %d and %d distance >= MinSiteDistance (%.2f)"), i, j, Dist), Dist >= MinSiteDistance - 0.01f);
		}
	}

	return true;
}

// Test 14: Relaxation Iterations Behavior
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiRelaxationIterationsTest, "ProceduralGeometry.Voronoi.RelaxationIterations", DefaultTestFlags)

bool FVoronoiRelaxationIterationsTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("IterTest");
	const int32	  NumSites = 15;

	TArray<int32>			  IterationCounts = { 0, 1, 3, 10 };
	TArray<FVoronoiDiagram2D> Diagrams;

	for (int32 Iters : IterationCounts)
	{
		UVoronoiGenerator2D* Gen = NewObject<UVoronoiGenerator2D>();
		Gen->SetBounds(TestBounds)->SetSeed(TestSeed);
		Gen->SetRelaxationIterations(Iters);
		Diagrams.Add(Gen->GenerateRelaxed(NumSites));
	}

	// All diagrams should have same number of sites
	for (int32 i = 0; i < Diagrams.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("Diagram with %d iterations has correct site count"), IterationCounts[i]),
			Diagrams[i].Sites.Num(),
			Diagrams[0].Sites.Num());
	}

	// Compute average displacement from 0-iteration baseline
	auto ComputeAvgDisplacement = [](const TArray<FVector2D>& Sites1, const TArray<FVector2D>& Sites2) -> float {
		float Total = 0.0f;
		int32 Count = FMath::Min(Sites1.Num(), Sites2.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			Total += FVector2D::Distance(Sites1[i], Sites2[i]);
		}
		return (Count > 0) ? Total / Count : 0.0f;
	};

	float Disp1 = ComputeAvgDisplacement(Diagrams[0].Sites, Diagrams[1].Sites);
	float Disp3 = ComputeAvgDisplacement(Diagrams[0].Sites, Diagrams[2].Sites);
	float Disp10 = ComputeAvgDisplacement(Diagrams[0].Sites, Diagrams[3].Sites);

	TestTrue("1 iteration should move sites from baseline", Disp1 > 0.1f);
	TestTrue("3 iterations should move more than 1", Disp3 > Disp1 - 0.01f);

	// Convergence: difference between 3 and 10 iterations should be smaller than between 0 and 3
	float DispDelta_0_3 = Disp3;
	float DispDelta_3_10 = ComputeAvgDisplacement(Diagrams[2].Sites, Diagrams[3].Sites);
	TestTrue("Convergence: delta 3->10 should be less than delta 0->3", DispDelta_3_10 < DispDelta_0_3 + 0.01f);

	// Negative iteration count should be clamped to 0
	UVoronoiGenerator2D* GenNeg = NewObject<UVoronoiGenerator2D>();
	GenNeg->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenNeg->SetRelaxationIterations(-5);
	FVoronoiDiagram2D DiagramNeg = GenNeg->GenerateRelaxed(NumSites);
	TestEqual("Negative iterations clamped: same site count", DiagramNeg.Sites.Num(), Diagrams[0].Sites.Num());

	return true;
}

// Test 15: Minimum Site Distance Parameter
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiMinSiteDistanceTest, "ProceduralGeometry.Voronoi.MinSiteDistance", DefaultTestFlags)

bool FVoronoiMinSiteDistanceTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("MinDistTest");

	// Generate with small MinSiteDistance
	UVoronoiGenerator2D* GenSmall = NewObject<UVoronoiGenerator2D>();
	GenSmall->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenSmall->SetMinSiteDistance(10.0f);
	FVoronoiDiagram2D DiagramSmall = GenSmall->GenerateRandomSites(30, true);

	// Generate with larger MinSiteDistance
	UVoronoiGenerator2D* GenLarge = NewObject<UVoronoiGenerator2D>();
	GenLarge->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenLarge->SetMinSiteDistance(50.0f);
	FVoronoiDiagram2D DiagramLarge = GenLarge->GenerateRandomSites(30, true);

	// Both should produce valid diagrams
	TestTrue("Small distance: should have sites", DiagramSmall.Sites.Num() > 0);
	TestTrue("Large distance: should have sites", DiagramLarge.Sites.Num() > 0);

	// Verify minimum distance constraints for both
	for (int32 i = 0; i < DiagramSmall.Sites.Num(); ++i)
	{
		for (int32 j = i + 1; j < DiagramSmall.Sites.Num(); ++j)
		{
			float Dist = FVector2D::Distance(DiagramSmall.Sites[i], DiagramSmall.Sites[j]);
			TestTrue("Small: distance >= 10", Dist >= 10.0f - 0.01f);
		}
	}
	for (int32 i = 0; i < DiagramLarge.Sites.Num(); ++i)
	{
		for (int32 j = i + 1; j < DiagramLarge.Sites.Num(); ++j)
		{
			float Dist = FVector2D::Distance(DiagramLarge.Sites[i], DiagramLarge.Sites[j]);
			TestTrue("Large: distance >= 50", Dist >= 50.0f - 0.01f);
		}
	}

	// Compare average pairwise distances
	auto ComputeAvgPairwiseDist = [](const TArray<FVector2D>& Sites) -> float {
		float Total = 0.0f;
		int32 Count = 0;
		for (int32 i = 0; i < Sites.Num(); ++i)
		{
			for (int32 j = i + 1; j < Sites.Num(); ++j)
			{
				Total += FVector2D::Distance(Sites[i], Sites[j]);
				Count++;
			}
		}
		return (Count > 0) ? Total / Count : 0.0f;
	};

	float AvgDistSmall = ComputeAvgPairwiseDist(DiagramSmall.Sites);
	float AvgDistLarge = ComputeAvgPairwiseDist(DiagramLarge.Sites);
	TestTrue("Larger MinSiteDistance produces larger average pairwise distance", AvgDistLarge > AvgDistSmall - 0.01f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS