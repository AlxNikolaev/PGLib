#include "Voro2DTests.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"

#include "ProceduralGeometry.h"
#include "UObject/UnrealType.h"
#include "../../PGStructuralHash.h"
#include "../../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

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
			return false;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiCellPropertiesTest, "ProceduralGeometry.Voronoi.Cell.Properties", DefaultTestFlags)

bool FVoronoiCellPropertiesTest::RunTest(const FString& Parameters)
{
	FVoronoiCell2D Cell;
	Cell.Vertices.Add(FVector2D(0, 0));
	Cell.Vertices.Add(FVector2D(10, 0));
	Cell.Vertices.Add(FVector2D(10, 10));
	Cell.Vertices.Add(FVector2D(0, 10));
	Cell.SiteLocation = FVector2D(5, 5);
	Cell.bIsValid = true;

	float ExpectedArea = 100.0f;
	float ActualArea = Cell.GetArea();
	TestEqual("Cell area should be 100", ActualArea, ExpectedArea, 0.01f);

	const FVector2D ExpectedCentroid(5, 5);
	const FVector2D ActualCentroid = Cell.GetCentroid();
	TestEqual("Centroid X", static_cast<float>(ActualCentroid.X), static_cast<float>(ExpectedCentroid.X), 0.01f);
	TestEqual("Centroid Y", static_cast<float>(ActualCentroid.Y), static_cast<float>(ExpectedCentroid.Y), 0.01f);

	TestTrue("Center point should be inside", Cell.ContainsPoint(FVector2D(5, 5)));
	TestTrue("Corner point should be inside", Cell.ContainsPoint(FVector2D(1, 1)));
	TestFalse("Outside point should not be inside", Cell.ContainsPoint(FVector2D(15, 15)));
	TestFalse("Negative point should not be inside", Cell.ContainsPoint(FVector2D(-5, 5)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiFixedSitesTest, "ProceduralGeometry.Voronoi.Generator.FixedSites", SmokeTestFlags)

bool FVoronoiFixedSitesTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	TArray<FVector2D> Sites;
	Sites.Add(FVector2D(25, 25));
	Sites.Add(FVector2D(75, 25));
	Sites.Add(FVector2D(25, 75));
	Sites.Add(FVector2D(75, 75));

	FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);

	TestEqual("Should have 4 cells", Diagram.Cells.Num(), 4);
	TestEqual("Should have 4 sites", Diagram.Sites.Num(), 4);

	for (const FVoronoiCell2D& Cell : Diagram.Cells)
	{
		TestTrue("Cell should be valid", Cell.bIsValid);
		TestTrue("Cell should have at least 3 vertices", Cell.Vertices.Num() >= 3);
	}

	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		TestTrue("Site should be in its own cell", Diagram.Cells[i].ContainsPoint(Diagram.Sites[i]));
	}

	TestTrue("Corner cells should be boundary cells", Diagram.Cells[0].bIsBoundaryCell);
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[1].bIsBoundaryCell);
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[2].bIsBoundaryCell);
	TestTrue("Corner cells should be boundary cells", Diagram.Cells[3].bIsBoundaryCell);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiNeighborsTest, "ProceduralGeometry.Voronoi.Generator.Neighbors", DefaultTestFlags)

bool FVoronoiNeighborsTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	TArray<FVector2D> Sites;
	Sites.Add(FVector2D(25, 50));
	Sites.Add(FVector2D(50, 50));
	Sites.Add(FVector2D(75, 50));

	FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);

	TestEqual("Middle cell should have 2 neighbors", Diagram.Cells[1].Neighbors.Num(), 2);
	TestTrue("Should be neighbor with cell 0", Diagram.Cells[1].Neighbors.Contains(0));
	TestTrue("Should be neighbor with cell 2", Diagram.Cells[1].Neighbors.Contains(2));

	TestEqual("First cell should have 1 neighbor", Diagram.Cells[0].Neighbors.Num(), 1);
	TestTrue("Should be neighbor with cell 1", Diagram.Cells[0].Neighbors.Contains(1));

	TestEqual("Last cell should have 1 neighbor", Diagram.Cells[2].Neighbors.Num(), 1);
	TestTrue("Should be neighbor with cell 1", Diagram.Cells[2].Neighbors.Contains(1));

	FVector2D EdgeStart, EdgeEnd;
	TestTrue("Cells 0 and 1 should share an edge", Diagram.GetSharedEdge(0, 1, EdgeStart, EdgeEnd));
	TestTrue("Cells 1 and 2 should share an edge", Diagram.GetSharedEdge(1, 2, EdgeStart, EdgeEnd));
	TestFalse("Cells 0 and 2 should not share an edge", Diagram.GetSharedEdge(0, 2, EdgeStart, EdgeEnd));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiRandomGenerationTest, "ProceduralGeometry.Voronoi.Generator.Random", DefaultTestFlags)

bool FVoronoiRandomGenerationTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed("TestSeed123");

	const int32		  NumSites = 20;
	FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(NumSites, false);

	TestEqual("Should have correct number of cells", Diagram.Cells.Num(), NumSites);

	int32 ValidCells = 0;
	float TotalArea = 0.0f;

	for (const FVoronoiCell2D& Cell : Diagram.Cells)
	{
		if (Cell.bIsValid)
		{
			ValidCells++;
			TotalArea += Cell.GetArea();

			TestTrue("Cell should be convex", FVoronoiTestBase::IsConvexPolygon(Cell.Vertices));
		}
	}

	TestEqual("All cells should be valid", ValidCells, NumSites);

	float BoundsArea = 1000.0f * 1000.0f;
	TestEqual("Total cell area should match bounds", TotalArea, BoundsArea, BoundsArea * 0.01f);

	return true;
}

// Generation cost is measured in half-plane clips, the same integer on every machine; the durations are logged
// but a wall clock on a shared machine can never decide a red.
// Two site counts, one per regime: below MinSitesForSpatialPruning the scan is the full pairwise sweep, N*(N-1);
// above it the index must skip clips, and a build that stopped skipping would land back on that exact number.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPerformanceTest, "ProceduralGeometry.Voronoi.Generator.Performance", PerfTestFlags)

bool FVoronoiPerformanceTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000)));
	Generator->SetSeed("PerfTest");

	// 50 sits below the pruning threshold, 500 well above it.
	const TArray<int32> TestSizes = { 10, 50, 100, 500 };

	TMap<int32, int64> ClipsBySize;

	for (const int32 NumSites : TestSizes)
	{
		VoronoiUtils::ResetHalfPlaneClipCount();
		const double StartTime = FPlatformTime::Seconds();

		const FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(NumSites, false);

		const double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		const int64	 Clips = VoronoiUtils::GetHalfPlaneClipCount();
		ClipsBySize.Add(NumSites, Clips);

		AddInfo(FString::Printf(TEXT("%d sites: %.2f ms, %lld half-plane clips"), NumSites, ElapsedTime * 1000.0, Clips));

		TestEqual(FString::Printf(TEXT("Should have %d cells"), NumSites), Diagram.Cells.Num(), NumSites);
	}

	// Distinct random sites, so no bisector is skipped as degenerate: every site clips against every other exactly once.
	TestEqual(TEXT("50 sites cost the full pairwise sweep (no pruning below the threshold)"), ClipsBySize[50], static_cast<int64>(50 * 49));

	// The whole point of the index. Equality with the exhaustive count means pruning silently stopped engaging.
	TestTrue(FString::Printf(TEXT("500 sites skip clips (%lld performed vs %d exhaustive)"), ClipsBySize[500], 500 * 499),
		ClipsBySize[500] < static_cast<int64>(500 * 499));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPointLocationTest, "ProceduralGeometry.Voronoi.Diagram.PointLocation", DefaultTestFlags)

bool FVoronoiPointLocationTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)));

	TArray<FVector2D> Sites;
	Sites.Add(FVector2D(25, 25));
	Sites.Add(FVector2D(75, 25));
	Sites.Add(FVector2D(25, 75));
	Sites.Add(FVector2D(75, 75));

	FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);

	TestEqual("Point near first site", Diagram.FindCellContainingPoint(FVector2D(20, 20)), 0);
	TestEqual("Point near second site", Diagram.FindCellContainingPoint(FVector2D(80, 20)), 1);
	TestEqual("Point near third site", Diagram.FindCellContainingPoint(FVector2D(20, 80)), 2);
	TestEqual("Point near fourth site", Diagram.FindCellContainingPoint(FVector2D(80, 80)), 3);

	int32 CenterCell = Diagram.FindCellContainingPoint(FVector2D(50, 50));
	TestTrue("Center point should be in a valid cell", CenterCell >= 0 && CenterCell < 4);

	TestEqual("Point outside bounds", Diagram.FindCellContainingPoint(FVector2D(-10, -10)), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiDegenerateInputTest, "ProceduralGeometry.Voronoi.DegenerateInput", DefaultTestFlags)

bool FVoronoiDegenerateInputTest::RunTest(const FString& Parameters)
{
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	// Seeded because the zero-site case goes through GenerateRandomSites, which treats an unseeded generator as a violation.
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(100, 100)))->SetSeed(TEXT("DegenerateInput"));

	{
		FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(0, false);
		TestEqual("Zero sites: should have 0 cells", Diagram.Cells.Num(), 0);
		TestEqual("Zero sites: should have 0 sites", Diagram.Sites.Num(), 0);
	}

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

	// Counts are blind to iteration-order drift that moves vertices and reshuffles neighbours; the hash is not.
	if (!TestTrue(TEXT("Deterministic seed produced a non-empty diagram to compare"), Diagram1.Cells.Num() == NumSites))
	{
		return false;
	}

	TestEqual(TEXT("Same-seed diagrams are structurally identical"),
		PGTestHash::HashVoronoiDiagram2D(Diagram1),
		PGTestHash::HashVoronoiDiagram2D(Diagram2));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiLloydRelaxationTest, "ProceduralGeometry.Voronoi.LloydRelaxation", DefaultTestFlags)

bool FVoronoiLloydRelaxationTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("RelaxationTest");

	UVoronoiGenerator2D* GenNoRelax = NewObject<UVoronoiGenerator2D>();
	GenNoRelax->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenNoRelax->SetRelaxationIterations(0);
	FVoronoiDiagram2D DiagramNoRelax = GenNoRelax->GenerateRelaxed(20);

	UVoronoiGenerator2D* GenRelaxed = NewObject<UVoronoiGenerator2D>();
	GenRelaxed->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenRelaxed->SetRelaxationIterations(5);
	FVoronoiDiagram2D DiagramRelaxed = GenRelaxed->GenerateRelaxed(20);

	TestEqual("Both should have same cell count", DiagramNoRelax.Cells.Num(), DiagramRelaxed.Cells.Num());

	float TotalDisplacement = 0.0f;
	int32 SiteCount = FMath::Min(DiagramNoRelax.Sites.Num(), DiagramRelaxed.Sites.Num());
	for (int32 i = 0; i < SiteCount; ++i)
	{
		TotalDisplacement += FVector2D::Distance(DiagramNoRelax.Sites[i], DiagramRelaxed.Sites[i]);
	}
	float AvgDisplacement = (SiteCount > 0) ? TotalDisplacement / SiteCount : 0.0f;
	TestTrue("Relaxation should move sites (avg displacement > 0)", AvgDisplacement > 0.1f);

	for (int32 i = 0; i < DiagramRelaxed.Sites.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Relaxed site %d within bounds"), i), TestBounds.IsInside(DiagramRelaxed.Sites[i]));
	}

	for (const FVoronoiCell2D& Cell : DiagramRelaxed.Cells)
	{
		TestTrue("Relaxed cell should be valid", Cell.bIsValid);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPoissonDiscTest, "ProceduralGeometry.Voronoi.PoissonDisc", DefaultTestFlags)

bool FVoronoiPoissonDiscTest::RunTest(const FString& Parameters)
{
	const FBox2D TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));

	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(TestBounds)->SetSeed(TEXT("PoissonTest"));
	Generator->SetMinSiteDistance(10.0f);
	FVoronoiDiagram2D Diagram = Generator->GenerateRandomSites(50, true);

	TestTrue("Should have sites generated", Diagram.Sites.Num() > 0);

	for (int32 i = 0; i < Diagram.Sites.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Site %d within bounds"), i), TestBounds.IsInside(Diagram.Sites[i]));
	}

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

	for (int32 i = 0; i < Diagrams.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("Diagram with %d iterations has correct site count"), IterationCounts[i]),
			Diagrams[i].Sites.Num(),
			Diagrams[0].Sites.Num());
	}

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

	// The tolerance is strict on purpose: admitting equality would pass a relaxation that applies only its first pass.
	const float Tolerance = 0.01f;

	TestTrue("1 iteration should move sites from baseline", Disp1 > 0.1f);
	TestTrue("3 iterations should move strictly more than 1", Disp3 > Disp1 + Tolerance);
	TestTrue("10 iterations should move strictly more than 1", Disp10 > Disp1 + Tolerance);

	// Lloyd converges, so the 10-iteration displacement settles at, not below, the 3-iteration one within a rounding step.
	TestTrue("10 iterations should not move sites back toward the baseline", Disp10 > Disp3 - Tolerance);

	float DispDelta_0_3 = Disp3;
	float DispDelta_3_10 = ComputeAvgDisplacement(Diagrams[2].Sites, Diagrams[3].Sites);
	TestTrue("Convergence: delta 3->10 should be strictly less than delta 0->3", DispDelta_3_10 < DispDelta_0_3 - Tolerance);

	UVoronoiGenerator2D* GenNeg = NewObject<UVoronoiGenerator2D>();
	GenNeg->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenNeg->SetRelaxationIterations(-5);
	FVoronoiDiagram2D DiagramNeg = GenNeg->GenerateRelaxed(NumSites);
	TestEqual("Negative iterations clamped: same site count", DiagramNeg.Sites.Num(), Diagrams[0].Sites.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiMinSiteDistanceTest, "ProceduralGeometry.Voronoi.MinSiteDistance", DefaultTestFlags)

bool FVoronoiMinSiteDistanceTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(0, 0), FVector2D(1000, 1000));
	const FString TestSeed = TEXT("MinDistTest");

	UVoronoiGenerator2D* GenSmall = NewObject<UVoronoiGenerator2D>();
	GenSmall->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenSmall->SetMinSiteDistance(10.0f);
	FVoronoiDiagram2D DiagramSmall = GenSmall->GenerateRandomSites(30, true);

	UVoronoiGenerator2D* GenLarge = NewObject<UVoronoiGenerator2D>();
	GenLarge->SetBounds(TestBounds)->SetSeed(TestSeed);
	GenLarge->SetMinSiteDistance(50.0f);
	FVoronoiDiagram2D DiagramLarge = GenLarge->GenerateRandomSites(30, true);

	TestTrue("Small distance: should have sites", DiagramSmall.Sites.Num() > 0);
	TestTrue("Large distance: should have sites", DiagramLarge.Sites.Num() > 0);

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

	// Strict because equality is the regression: both configs share a seed, so an ignored MinSiteDistance repeats the sites.
	const float AvgDistSmall = ComputeAvgPairwiseDist(DiagramSmall.Sites);
	const float AvgDistLarge = ComputeAvgPairwiseDist(DiagramLarge.Sites);
	TestTrue(FString::Printf(TEXT("Larger MinSiteDistance produces larger average pairwise distance (%.2f vs %.2f)"), AvgDistLarge, AvgDistSmall),
		AvgDistLarge > AvgDistSmall + 0.01f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiRelaxSitesDuplicateGuardTest, "ProceduralGeometry.Voronoi.RelaxSites.DuplicateGuard", DefaultTestFlags)

bool FVoronoiRelaxSitesDuplicateGuardTest::RunTest(const FString& Parameters)
{
	const FBox2D TestBounds(FVector2D(0, 0), FVector2D(100, 100));

	// A large MinSiteDistance yields very few sites, so one relaxation pass can exercise the duplicate-site clip path.
	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(TestBounds);
	Generator->SetSeed(TEXT("DuplicateGuardTest"));
	Generator->SetMinSiteDistance(40.0f);
	Generator->SetRelaxationIterations(1);

	FVoronoiDiagram2D Diagram = Generator->GenerateRelaxed(3);

	TestEqual("Cell count equals site count after relaxation", Diagram.Cells.Num(), Diagram.Sites.Num());

	for (int32 i = 0; i < Diagram.Sites.Num(); ++i)
	{
		const FVector2D& Site = Diagram.Sites[i];
		TestTrue(FString::Printf(TEXT("Relaxed site %d X within bounds"), i), Site.X >= 0.0f && Site.X <= 100.0f);
		TestTrue(FString::Printf(TEXT("Relaxed site %d Y within bounds"), i), Site.Y >= 0.0f && Site.Y <= 100.0f);
	}

	for (const FVoronoiCell2D& Cell : Diagram.Cells)
	{
		if (Cell.bIsValid)
		{
			TestTrue("Relaxed cell should be convex", FVoronoiTestBase::IsConvexPolygon(Cell.Vertices));
		}
	}

	return true;
}

// A regular lattice makes four cells meet at one point: corner-only contact is not adjacency, and every recorded
// neighbour pair must be retrievable as a shared edge.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiCornerContactTest, "ProceduralGeometry.Voronoi.CornerContactIsNotAdjacent", DefaultTestFlags)

bool FVoronoiCornerContactTest::RunTest(const FString& Parameters)
{
	// Zero jitter, so cells are exact 100x100 squares: diagonal pairs meet at a point, orthogonal ones share a border.
	constexpr int32 GridSide = 3;
	constexpr float CellSize = 100.f;

	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(0, 0), FVector2D(GridSide * CellSize, GridSide * CellSize)));

	TArray<FVector2D> Sites;
	Sites.Reserve(GridSide * GridSide);
	for (int32 Row = 0; Row < GridSide; ++Row)
	{
		for (int32 Col = 0; Col < GridSide; ++Col)
		{
			Sites.Emplace((Col + 0.5f) * CellSize, (Row + 0.5f) * CellSize);
		}
	}

	const FVoronoiDiagram2D Diagram = Generator->GenerateFromSites(Sites);
	TestEqual("Diagram should have one cell per site", Diagram.Cells.Num(), Sites.Num());

	for (int32 IndexA = 0; IndexA < Diagram.Cells.Num(); ++IndexA)
	{
		const int32 ColA = IndexA % GridSide;
		const int32 RowA = IndexA / GridSide;

		for (int32 IndexB = IndexA + 1; IndexB < Diagram.Cells.Num(); ++IndexB)
		{
			const int32 ColB = IndexB % GridSide;
			const int32 RowB = IndexB / GridSide;

			const int32 ColStep = FMath::Abs(ColA - ColB);
			const int32 RowStep = FMath::Abs(RowA - RowB);
			const bool	bOrthogonal = (ColStep + RowStep) == 1;
			const bool	bDiagonal = ColStep == 1 && RowStep == 1;

			const bool bRecordedAB = Diagram.Cells[IndexA].Neighbors.Contains(IndexB);
			const bool bRecordedBA = Diagram.Cells[IndexB].Neighbors.Contains(IndexA);
			TestTrue(FString::Printf(TEXT("Adjacency %d<->%d is symmetric"), IndexA, IndexB), bRecordedAB == bRecordedBA);

			if (bOrthogonal)
			{
				TestTrue(FString::Printf(TEXT("Cells %d and %d share a border and must be neighbours"), IndexA, IndexB), bRecordedAB);
			}
			else
			{
				TestFalse(FString::Printf(TEXT("Cells %d and %d touch at most at a corner and must not be neighbours"), IndexA, IndexB), bRecordedAB);
			}

			if (bDiagonal)
			{
				FVector2D CornerStart, CornerEnd;
				TestFalse(FString::Printf(TEXT("Corner contact %d/%d yields no shared edge"), IndexA, IndexB),
					Diagram.GetSharedEdge(IndexA, IndexB, CornerStart, CornerEnd));
			}
		}
	}

	// Every neighbour the diagram claims must be retrievable as an edge, in both directions.
	for (int32 IndexA = 0; IndexA < Diagram.Cells.Num(); ++IndexA)
	{
		for (const int32 IndexB : Diagram.Cells[IndexA].Neighbors)
		{
			FVector2D EdgeStart, EdgeEnd;
			TestTrue(FString::Printf(TEXT("Neighbour pair %d->%d has a retrievable shared edge"), IndexA, IndexB),
				Diagram.GetSharedEdge(IndexA, IndexB, EdgeStart, EdgeEnd));
			TestEqual(FString::Printf(TEXT("Shared edge %d->%d spans the full cell border"), IndexA, IndexB),
				static_cast<float>(FVector2D::Distance(EdgeStart, EdgeEnd)),
				CellSize,
				0.01f);
		}
	}

	return true;
}

// An unseeded generator is a contract violation, but the diagram must still record the seed it used.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoronoiUnseededSeedRecordedTest, "ProceduralGeometry.Voronoi.UnseededGeneratorRecordsTheSeedItUsed", DefaultTestFlags)

bool FVoronoiUnseededSeedRecordedTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(TEXT("Generated without SetSeed"), EAutomationExpectedErrorFlags::Contains, 1);

	const FBox2D TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const int32	 NumSites = 12;

	UVoronoiGenerator2D* Unseeded = NewObject<UVoronoiGenerator2D>();
	Unseeded->SetBounds(TestBounds);
	const FVoronoiDiagram2D Diagram = Unseeded->GenerateRandomSites(NumSites, false);

	if (!TestFalse(TEXT("An unseeded diagram records the seed it was generated from"), Diagram.Seed.IsEmpty()))
	{
		return false;
	}

	UVoronoiGenerator2D* Replay = NewObject<UVoronoiGenerator2D>();
	Replay->SetBounds(TestBounds)->SetSeed(Diagram.Seed);
	const FVoronoiDiagram2D Replayed = Replay->GenerateRandomSites(NumSites, false);

	if (!TestEqual(TEXT("Replaying the recorded seed reproduces the site count"), Replayed.Sites.Num(), Diagram.Sites.Num()))
	{
		return false;
	}

	int32 SiteMismatches = 0;
	for (int32 Index = 0; Index < Diagram.Sites.Num(); ++Index)
	{
		if (!Replayed.Sites[Index].Equals(Diagram.Sites[Index], 0.001))
		{
			++SiteMismatches;
		}
	}
	TestEqual(TEXT("Replaying the recorded seed reproduces every site"), SiteMismatches, 0);

	return true;
}

// The stream is re-derived at every RNG entry point, so a second generate call cannot continue where the first stopped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiReuseIsIdenticalTest, "ProceduralGeometry.Voronoi.GenerateTwiceOnOneInstanceIsIdentical", DefaultTestFlags)

bool FVoronoiReuseIsIdenticalTest::RunTest(const FString& Parameters)
{
	const FBox2D TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const int32	 NumSites = 16;

	UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
	Generator->SetBounds(TestBounds)->SetSeed(TEXT("ReuseIsIdentical"));

	const FVoronoiDiagram2D First = Generator->GenerateRandomSites(NumSites, false);
	const FVoronoiDiagram2D Second = Generator->GenerateRandomSites(NumSites, false);

	if (!TestEqual(TEXT("Both runs produce the same site count"), Second.Sites.Num(), First.Sites.Num()))
	{
		return false;
	}

	int32 SiteMismatches = 0;
	for (int32 Index = 0; Index < First.Sites.Num(); ++Index)
	{
		if (!Second.Sites[Index].Equals(First.Sites[Index], 0.001))
		{
			++SiteMismatches;
		}
	}
	TestEqual(TEXT("Reusing one generator reproduces every site"), SiteMismatches, 0);
	TestEqual(TEXT("Reusing one generator reproduces the cell count"), Second.Cells.Num(), First.Cells.Num());

	return true;
}

// Seed is reflected and RandomStream is not, so a seed that arrived by property copy has no stream derived from it;
// generation must still come from the seed the diagram reports.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoronoiDeserializedSeedTest, "ProceduralGeometry.Voronoi.SeedSetWithoutSetSeedStillDrivesGeneration", DefaultTestFlags)

bool FVoronoiDeserializedSeedTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const int32	  NumSites = 16;
	const FString TestSeed = TEXT("ArrivedByPropertyCopy");

	// Write the reflected Seed directly, the way a load or an archetype copy would, bypassing SetSeed.
	UVoronoiGenerator2D* Deserialized = NewObject<UVoronoiGenerator2D>();
	const FStrProperty*	 SeedProperty = CastField<FStrProperty>(UVoronoiGenerator2D::StaticClass()->FindPropertyByName(TEXT("Seed")));
	if (!SeedProperty)
	{
		AddError(TEXT("UVoronoiGenerator2D::Seed is no longer a reflected FString; this test cannot simulate a load"));
		return false;
	}
	SeedProperty->SetPropertyValue_InContainer(Deserialized, TestSeed);

	Deserialized->SetBounds(TestBounds);
	const FVoronoiDiagram2D Loaded = Deserialized->GenerateRandomSites(NumSites, false);

	UVoronoiGenerator2D* Explicit = NewObject<UVoronoiGenerator2D>();
	Explicit->SetBounds(TestBounds)->SetSeed(TestSeed);
	const FVoronoiDiagram2D Reference = Explicit->GenerateRandomSites(NumSites, false);

	TestEqual(TEXT("A deserialized seed reports itself on the diagram"), Loaded.Seed, TestSeed);
	if (!TestEqual(TEXT("A deserialized seed produces the same site count as SetSeed"), Loaded.Sites.Num(), Reference.Sites.Num()))
	{
		return false;
	}

	int32 SiteMismatches = 0;
	for (int32 Index = 0; Index < Reference.Sites.Num(); ++Index)
	{
		if (!Loaded.Sites[Index].Equals(Reference.Sites[Index], 0.001))
		{
			++SiteMismatches;
		}
	}
	TestEqual(TEXT("A deserialized seed produces the same sites as SetSeed"), SiteMismatches, 0);

	return true;
}

#endif
