#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// Test 1: GridData structural consistency — parallel arrays, valid dimensions, valid CenterRegionId
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataGridDataStructuralTest, "ProceduralGeometry.CellularAutomataGenerator2D.GridDataStructuralConsistency", DefaultTestFlags)

bool FCellularAutomataGridDataStructuralTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed(TEXT("GridDataStructuralTest"));

	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();

	// Grid array matches declared dimensions
	TestEqual("Grid.Num() == GridWidth * GridHeight", GridData.Grid.Num(), GridData.GridWidth * GridData.GridHeight);

	// Region ID array is parallel to grid
	TestEqual("RegionIds.Num() == Grid.Num()", GridData.RegionIds.Num(), GridData.Grid.Num());

	// Survival flags parallel to regions
	TestEqual("SurvivingRegions.Num() == Regions.Num()", GridData.SurvivingRegions.Num(), GridData.Regions.Num());

	// Non-degenerate grid
	TestTrue("GridWidth > 0", GridData.GridWidth > 0);
	TestTrue("GridHeight > 0", GridData.GridHeight > 0);

	// Valid cell size
	TestTrue("CellSize > 0", GridData.CellSize > 0.0f);

	// CenterRegionId is valid
	TestTrue("CenterRegionId is -1 or within [0, Regions.Num())",
		GridData.CenterRegionId == -1 || (GridData.CenterRegionId >= 0 && GridData.CenterRegionId < GridData.Regions.Num()));

	// At least one region survived
	TestTrue("Diagram has cells", GridData.Diagram.Cells.Num() > 0);

	return true;
}

// Test 2: GridData region ID consistency — grid cells and region IDs are mutually consistent
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataGridDataRegionIdConsistencyTest, "ProceduralGeometry.CellularAutomataGenerator2D.GridDataRegionIdConsistency", DefaultTestFlags)

bool FCellularAutomataGridDataRegionIdConsistencyTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed(TEXT("GridDataStructuralTest"));

	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();

	// Every floor cell in a surviving region has RegionIds[i] >= 0
	for (int32 i = 0; i < GridData.Grid.Num(); ++i)
	{
		if (GridData.Grid[i])
		{
			const int32 RegionId = GridData.RegionIds[i];
			TestTrue(FString::Printf(TEXT("Floor cell %d has valid RegionId (got %d)"), i, RegionId), RegionId >= 0);
		}
	}

	// Every wall cell has RegionIds == -1 OR belongs to a culled region
	for (int32 i = 0; i < GridData.Grid.Num(); ++i)
	{
		if (!GridData.Grid[i])
		{
			const int32 RegionId = GridData.RegionIds[i];
			const bool	bIsNeverFloor = (RegionId == -1);
			const bool	bIsCulledRegion = (RegionId >= 0 && RegionId < GridData.SurvivingRegions.Num() && !GridData.SurvivingRegions[RegionId]);

			TestTrue(
				FString::Printf(TEXT("Wall cell %d has RegionId -1 or culled region (RegionId=%d)"), i, RegionId), bIsNeverFloor || bIsCulledRegion);
		}
	}

	// Every surviving region has at least one floor cell
	for (int32 r = 0; r < GridData.Regions.Num(); ++r)
	{
		if (!GridData.SurvivingRegions[r])
		{
			continue;
		}

		bool bHasFloorCell = false;
		for (const FIntPoint& Cell : GridData.Regions[r])
		{
			const int32 Index = Cell.Y * GridData.GridWidth + Cell.X;
			if (GridData.Grid[Index])
			{
				bHasFloorCell = true;
				break;
			}
		}

		TestTrue(FString::Printf(TEXT("Surviving region %d has at least one floor cell"), r), bHasFloorCell);
	}

	return true;
}

// Test 3: Generate() matches GenerateWithGridData().Diagram — refactor regression
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCellularAutomataGridDataRefactorRegressionTest,
	"ProceduralGeometry.CellularAutomataGenerator2D.GenerateMatchesGridDataDiagram",
	DefaultTestFlags)

bool FCellularAutomataGridDataRefactorRegressionTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("RefactorRegressionTest");

	// Two separate generator instances with identical config (same seed resets the random stream)
	UCellularAutomataGenerator2D* Gen1 = NewObject<UCellularAutomataGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed);
	FLayoutDiagram2D Diagram1 = Gen1->Generate();

	UCellularAutomataGenerator2D* Gen2 = NewObject<UCellularAutomataGenerator2D>();
	Gen2->SetBounds(TestBounds)->SetSeed(TestSeed);
	FCellularAutomataGridData GridData2 = Gen2->GenerateWithGridData();

	const FLayoutDiagram2D& Diagram2 = GridData2.Diagram;

	TestEqual("Cell count matches", Diagram1.Cells.Num(), Diagram2.Cells.Num());
	TestEqual("CenterCellIndex matches", Diagram1.CenterCellIndex, Diagram2.CenterCellIndex);

	const int32 NumCells = FMath::Min(Diagram1.Cells.Num(), Diagram2.Cells.Num());
	for (int32 i = 0; i < NumCells; ++i)
	{
		const FLayoutCell2D& C1 = Diagram1.Cells[i];
		const FLayoutCell2D& C2 = Diagram2.Cells[i];

		TestEqual(FString::Printf(TEXT("Cell %d Center.X"), i), static_cast<float>(C1.Center.X), static_cast<float>(C2.Center.X), 0.01f);
		TestEqual(FString::Printf(TEXT("Cell %d Center.Y"), i), static_cast<float>(C1.Center.Y), static_cast<float>(C2.Center.Y), 0.01f);
		TestEqual(FString::Printf(TEXT("Cell %d bIsExterior"), i), C1.bIsExterior, C2.bIsExterior);
		TestEqual(FString::Printf(TEXT("Cell %d Vertices.Num()"), i), C1.Vertices.Num(), C2.Vertices.Num());
		TestEqual(FString::Printf(TEXT("Cell %d Neighbors.Num()"), i), C1.Neighbors.Num(), C2.Neighbors.Num());
	}

	return true;
}

// Test 4: SurvivingRegions reflects MinRegionSize culling
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataGridDataCullingTest, "ProceduralGeometry.CellularAutomataGenerator2D.SurvivingRegionsReflectsCulling", DefaultTestFlags)

bool FCellularAutomataGridDataCullingTest::RunTest(const FString& Parameters)
{
	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed(TEXT("CullingTest"));
	Generator->SetMinRegionSize(10000);
	Generator->SetKeepCenterRegion(true);

	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();

	// At least one region should have been culled with such a high MinRegionSize
	bool bHasCulledRegion = false;
	for (int32 r = 0; r < GridData.SurvivingRegions.Num(); ++r)
	{
		if (!GridData.SurvivingRegions[r])
		{
			bHasCulledRegion = true;
			break;
		}
	}

	TestTrue("At least one region was culled with high MinRegionSize", bHasCulledRegion);

	// Center region should survive when bKeepCenterRegion is true
	if (GridData.CenterRegionId >= 0 && GridData.CenterRegionId < GridData.SurvivingRegions.Num())
	{
		TestTrue("Center region survived despite high MinRegionSize", GridData.SurvivingRegions[GridData.CenterRegionId]);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
