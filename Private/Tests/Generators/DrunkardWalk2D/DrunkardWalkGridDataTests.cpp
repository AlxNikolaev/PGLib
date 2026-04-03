#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// Test 1: GridData structural consistency — parallel arrays, valid dimensions, valid CenterRegionId
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkGridDataStructuralTest, "ProceduralGeometry.DrunkardWalkGenerator2D.GridDataStructuralConsistency", DefaultTestFlags)

bool FDrunkardWalkGridDataStructuralTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed(TEXT("GridDataStructuralTest"));

	FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

	// Grid array matches declared dimensions
	TestEqual("Grid.Num() == GridWidth * GridHeight", GridData.Grid.Num(), GridData.GridWidth * GridData.GridHeight);

	// CellType array is parallel to grid
	TestEqual("CellType.Num() == Grid.Num()", GridData.CellType.Num(), GridData.Grid.Num());

	// Region ID array is parallel to grid
	TestEqual("RegionIds.Num() == Grid.Num()", GridData.RegionIds.Num(), GridData.Grid.Num());

	// Non-degenerate grid
	TestTrue("GridWidth > 0", GridData.GridWidth > 0);
	TestTrue("GridHeight > 0", GridData.GridHeight > 0);

	// Valid cell size
	TestTrue("CellSize > 0", GridData.CellSize > 0.0f);

	// CenterRegionId is valid
	TestTrue("CenterRegionId is -1 or within [0, Regions.Num())",
		GridData.CenterRegionId == -1 || (GridData.CenterRegionId >= 0 && GridData.CenterRegionId < GridData.Regions.Num()));

	// Diagram has cells
	TestTrue("Diagram has cells", GridData.Diagram.Cells.Num() > 0);

	return true;
}

// Test 2: Walker paths populated — at least one walker, each path non-empty, branching creates extra walkers
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkGridDataWalkerPathsTest, "ProceduralGeometry.DrunkardWalkGenerator2D.GridDataWalkerPaths", DefaultTestFlags)

bool FDrunkardWalkGridDataWalkerPathsTest::RunTest(const FString& Parameters)
{
	// Basic walker path test
	{
		UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
		Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
		Generator->SetSeed(TEXT("WalkerPathsBasic"));
		Generator->SetNumWalkers(2);

		FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

		TestTrue("WalkerPaths.Num() >= 1", GridData.WalkerPaths.Num() >= 1);

		for (int32 i = 0; i < GridData.WalkerPaths.Num(); ++i)
		{
			const TArray<FIntPoint>& Path = GridData.WalkerPaths[i];
			TestTrue(FString::Printf(TEXT("Walker %d path is non-empty"), i), Path.Num() > 0);

			if (Path.Num() > 0)
			{
				const FIntPoint& Start = Path[0];
				TestTrue(FString::Printf(TEXT("Walker %d start X in grid bounds"), i), Start.X >= 0 && Start.X < GridData.GridWidth);
				TestTrue(FString::Printf(TEXT("Walker %d start Y in grid bounds"), i), Start.Y >= 0 && Start.Y < GridData.GridHeight);
			}
		}
	}

	// Branching test — high branch probability should create extra walkers
	{
		UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
		Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
		Generator->SetSeed(TEXT("WalkerPathsBranch"));
		Generator->SetNumWalkers(1);
		Generator->SetBranchProbability(0.5f);
		Generator->SetWalkLength(1000);

		FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

		TestTrue("Branching with high probability creates extra walkers", GridData.WalkerPaths.Num() > 1);
	}

	return true;
}

// Test 3: CellType consistency — wall/floor/room agreement
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkGridDataCellTypeConsistencyTest, "ProceduralGeometry.DrunkardWalkGenerator2D.GridDataCellTypeConsistency", DefaultTestFlags)

bool FDrunkardWalkGridDataCellTypeConsistencyTest::RunTest(const FString& Parameters)
{
	// Test with rooms enabled
	{
		UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
		Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
		Generator->SetSeed(TEXT("CellTypeWithRooms"));
		Generator->SetRoomChance(0.3f);
		Generator->SetRoomRadius(2);
		Generator->SetWalkLength(500);

		FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

		bool bHasRoomCell = false;
		for (int32 i = 0; i < GridData.Grid.Num(); ++i)
		{
			if (GridData.CellType[i] != EDrunkardWalkCellType::Wall)
			{
				// Every non-wall cell type must correspond to a floor cell
				TestTrue(FString::Printf(TEXT("Non-wall cell %d has Grid[i]==true"), i), GridData.Grid[i]);
			}

			if (!GridData.Grid[i])
			{
				// Every wall cell in the grid must have Wall cell type
				TestEqual(FString::Printf(TEXT("Wall cell %d has CellType==Wall"), i), GridData.CellType[i], EDrunkardWalkCellType::Wall);
			}

			if (GridData.CellType[i] == EDrunkardWalkCellType::Room)
			{
				bHasRoomCell = true;
			}
		}

		TestTrue("With RoomChance > 0, at least one room cell exists", bHasRoomCell);
	}

	// Test without rooms
	{
		UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
		Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
		Generator->SetSeed(TEXT("CellTypeNoRooms"));
		Generator->SetRoomChance(0.0f);
		Generator->SetWalkLength(500);

		FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

		for (int32 i = 0; i < GridData.Grid.Num(); ++i)
		{
			TestTrue(FString::Printf(TEXT("With RoomChance=0, cell %d has no Room type"), i), GridData.CellType[i] != EDrunkardWalkCellType::Room);

			// Every floor cell should be Corridor (verifies CellType init to Wall and proper Corridor assignment)
			if (GridData.Grid[i])
			{
				TestEqual(FString::Printf(TEXT("With RoomChance=0, floor cell %d has CellType==Corridor"), i),
					GridData.CellType[i],
					EDrunkardWalkCellType::Corridor);
			}
		}
	}

	return true;
}

// Test 4: Region ID consistency — grid cells and region IDs are mutually consistent
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkGridDataRegionIdConsistencyTest, "ProceduralGeometry.DrunkardWalkGenerator2D.GridDataRegionIdConsistency", DefaultTestFlags)

bool FDrunkardWalkGridDataRegionIdConsistencyTest::RunTest(const FString& Parameters)
{
	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(FBox2D(FVector2D(-500, -500), FVector2D(500, 500)));
	Generator->SetSeed(TEXT("RegionIdConsistency"));

	FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

	// Every floor cell has RegionIds[i] >= 0
	for (int32 i = 0; i < GridData.Grid.Num(); ++i)
	{
		if (GridData.Grid[i])
		{
			TestTrue(FString::Printf(TEXT("Floor cell %d has valid RegionId (got %d)"), i, GridData.RegionIds[i]), GridData.RegionIds[i] >= 0);
		}
	}

	// Every wall cell has RegionIds == -1
	for (int32 i = 0; i < GridData.Grid.Num(); ++i)
	{
		if (!GridData.Grid[i])
		{
			TestEqual(FString::Printf(TEXT("Wall cell %d has RegionId -1 (got %d)"), i, GridData.RegionIds[i]), GridData.RegionIds[i], -1);
		}
	}

	// Every region has at least one floor cell and back-references are consistent
	for (int32 r = 0; r < GridData.Regions.Num(); ++r)
	{
		bool bHasFloorCell = false;
		for (const FIntPoint& Cell : GridData.Regions[r])
		{
			const int32 Index = Cell.Y * GridData.GridWidth + Cell.X;
			if (GridData.Grid[Index])
			{
				bHasFloorCell = true;
			}

			// Every cell in Regions[r] must have RegionIds[Index] == r
			TestEqual(FString::Printf(TEXT("Region %d cell (%d,%d) has matching RegionId"), r, Cell.X, Cell.Y), GridData.RegionIds[Index], r);
		}

		TestTrue(FString::Printf(TEXT("Region %d has at least one floor cell"), r), bHasFloorCell);
	}

	return true;
}

// Test 5: Generate() matches GenerateWithGridData().Diagram — refactor regression
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkGridDataRefactorRegressionTest, "ProceduralGeometry.DrunkardWalkGenerator2D.GenerateMatchesGridDataDiagram", DefaultTestFlags)

bool FDrunkardWalkGridDataRefactorRegressionTest::RunTest(const FString& Parameters)
{
	const FBox2D  TestBounds(FVector2D(-500, -500), FVector2D(500, 500));
	const FString TestSeed = TEXT("RefactorRegressionTest");

	// Two separate generator instances with identical config (same seed resets the random stream)
	UDrunkardWalkGenerator2D* Gen1 = NewObject<UDrunkardWalkGenerator2D>();
	Gen1->SetBounds(TestBounds)->SetSeed(TestSeed);
	FLayoutDiagram2D Diagram1 = Gen1->Generate();

	UDrunkardWalkGenerator2D* Gen2 = NewObject<UDrunkardWalkGenerator2D>();
	Gen2->SetBounds(TestBounds)->SetSeed(TestSeed);
	FDrunkardWalkGridData GridData2 = Gen2->GenerateWithGridData();

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

#endif // WITH_DEV_AUTOMATION_TESTS
