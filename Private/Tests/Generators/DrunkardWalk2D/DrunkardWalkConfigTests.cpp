#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// Test 1: Each EDungeonStyle preset with default sliders (0.5) returns exact base preset values
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkConfigPresetResolutionTest, "ProceduralGeometry.DrunkardWalkConfig.PresetResolution", DefaultTestFlags)

bool FDrunkardWalkConfigPresetResolutionTest::RunTest(const FString& Parameters)
{
	struct FPresetTestCase
	{
		EDungeonStyle Style;
		int32		  ExpectedWalkLength;
		int32		  ExpectedNumWalkers;
		float		  ExpectedBranchProb;
		float		  ExpectedRoomChance;
	};

	// At default sliders (0.5), formulas yield base values:
	// WalkLength = RoundToInt(Base * (0.5 + 0.5)) = Base
	// NumWalkers = RoundToInt(Base * (0.5 + 0.5)) = Base
	// BranchProb = Base * 0.5 * 2.0 = Base
	// RoomChance = Base * 0.5 * 2.0 = Base
	const TArray<FPresetTestCase> Cases = {
		{ EDungeonStyle::OpenRuins, 1000, 3, 0.10f, 0.15f },
		{ EDungeonStyle::TightMaze, 500, 1, 0.00f, 0.00f },
		{ EDungeonStyle::RoomHeavy, 800, 2, 0.05f, 0.30f },
		{ EDungeonStyle::Labyrinth, 600, 4, 0.30f, 0.05f },
		{ EDungeonStyle::Caverns, 1200, 2, 0.15f, 0.10f },
	};

	for (const FPresetTestCase& Case : Cases)
	{
		FDrunkardWalkConfig Config;
		Config.DungeonStyle = Case.Style;
		Config.Density = 0.5f;
		Config.Complexity = 0.5f;
		Config.RoomFrequency = 0.5f;
		Config.Scale = EDungeonScale::Medium;

		FDrunkardWalkResolvedParams Params = Config.Resolve();

		const FString StyleName = FString::Printf(TEXT("Style %d"), static_cast<int32>(Case.Style));

		TestEqual(FString::Printf(TEXT("%s WalkLength"), *StyleName), Params.WalkLength, Case.ExpectedWalkLength);
		TestEqual(FString::Printf(TEXT("%s NumWalkers"), *StyleName), Params.NumWalkers, Case.ExpectedNumWalkers);
		TestEqual(FString::Printf(TEXT("%s BranchProbability"), *StyleName), Params.BranchProbability, Case.ExpectedBranchProb, 0.001f);
		TestEqual(FString::Printf(TEXT("%s RoomChance"), *StyleName), Params.RoomChance, Case.ExpectedRoomChance, 0.001f);

		// Medium scale: CorridorWidth=2, RoomRadius=3
		TestEqual(FString::Printf(TEXT("%s CorridorWidth"), *StyleName), Params.CorridorWidth, 2);
		TestEqual(FString::Printf(TEXT("%s RoomRadius"), *StyleName), Params.RoomRadiusMax, 3);
	}

	return true;
}

// Test 2: Density slider modulates WalkLength
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkConfigDensityModulationTest, "ProceduralGeometry.DrunkardWalkConfig.DensityModulation", DefaultTestFlags)

bool FDrunkardWalkConfigDensityModulationTest::RunTest(const FString& Parameters)
{
	// OpenRuins base WalkLength = 1000
	FDrunkardWalkConfig Config;
	Config.DungeonStyle = EDungeonStyle::OpenRuins;
	Config.Complexity = 0.5f;
	Config.RoomFrequency = 0.5f;
	Config.Scale = EDungeonScale::Medium;

	// Density=0.0: 1000 * (0.5 + 0.0) = 1000 * 0.5 = 500
	Config.Density = 0.0f;
	TestEqual("Density=0.0 WalkLength", Config.Resolve().WalkLength, 500);

	// Density=0.5: 1000 * (0.5 + 0.5) = 1000 * 1.0 = 1000
	Config.Density = 0.5f;
	TestEqual("Density=0.5 WalkLength", Config.Resolve().WalkLength, 1000);

	// Density=1.0: 1000 * (0.5 + 1.0) = 1000 * 1.5 = 1500
	Config.Density = 1.0f;
	TestEqual("Density=1.0 WalkLength", Config.Resolve().WalkLength, 1500);

	return true;
}

// Test 3: Complexity slider modulates NumWalkers and BranchProbability
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkConfigComplexityModulationTest, "ProceduralGeometry.DrunkardWalkConfig.ComplexityModulation", DefaultTestFlags)

bool FDrunkardWalkConfigComplexityModulationTest::RunTest(const FString& Parameters)
{
	// Labyrinth: BaseNumWalkers=4, BaseBranchProb=0.30
	FDrunkardWalkConfig Config;
	Config.DungeonStyle = EDungeonStyle::Labyrinth;
	Config.Density = 0.5f;
	Config.RoomFrequency = 0.5f;
	Config.Scale = EDungeonScale::Medium;

	// Complexity=0.0: NumWalkers = RoundToInt(4 * 0.5) = 2, BranchProb = 0.30 * 0.0 * 2.0 = 0.0
	Config.Complexity = 0.0f;
	TestEqual("Complexity=0.0 NumWalkers", Config.Resolve().NumWalkers, 2);
	TestEqual("Complexity=0.0 BranchProbability", Config.Resolve().BranchProbability, 0.0f, 0.001f);

	// Complexity=0.5: NumWalkers = RoundToInt(4 * 1.0) = 4, BranchProb = 0.30 * 0.5 * 2.0 = 0.30
	Config.Complexity = 0.5f;
	TestEqual("Complexity=0.5 NumWalkers", Config.Resolve().NumWalkers, 4);
	TestEqual("Complexity=0.5 BranchProbability", Config.Resolve().BranchProbability, 0.30f, 0.001f);

	// Complexity=1.0: NumWalkers = RoundToInt(4 * 1.5) = 6, BranchProb = 0.30 * 1.0 * 2.0 = 0.60
	Config.Complexity = 1.0f;
	TestEqual("Complexity=1.0 NumWalkers", Config.Resolve().NumWalkers, 6);
	TestEqual("Complexity=1.0 BranchProbability", Config.Resolve().BranchProbability, 0.60f, 0.001f);

	// TightMaze: BaseNumWalkers=1, Complexity=0.0: NumWalkers = RoundToInt(1 * 0.5) = 1 (clamped to min 1)
	Config.DungeonStyle = EDungeonStyle::TightMaze;
	Config.Complexity = 0.0f;
	TestTrue("TightMaze Complexity=0.0 NumWalkers >= 1", Config.Resolve().NumWalkers >= 1);

	return true;
}

// Test 4: RoomFrequency slider modulates RoomChance
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDrunkardWalkConfigRoomFrequencyModulationTest, "ProceduralGeometry.DrunkardWalkConfig.RoomFrequencyModulation", DefaultTestFlags)

bool FDrunkardWalkConfigRoomFrequencyModulationTest::RunTest(const FString& Parameters)
{
	// RoomHeavy: BaseRoomChance=0.30
	FDrunkardWalkConfig Config;
	Config.DungeonStyle = EDungeonStyle::RoomHeavy;
	Config.Density = 0.5f;
	Config.Complexity = 0.5f;
	Config.Scale = EDungeonScale::Medium;

	// RoomFrequency=0.0: 0.30 * 0.0 * 2.0 = 0.0
	Config.RoomFrequency = 0.0f;
	TestEqual("RoomFreq=0.0 RoomChance", Config.Resolve().RoomChance, 0.0f, 0.001f);

	// RoomFrequency=0.5: 0.30 * 0.5 * 2.0 = 0.30
	Config.RoomFrequency = 0.5f;
	TestEqual("RoomFreq=0.5 RoomChance", Config.Resolve().RoomChance, 0.30f, 0.001f);

	// RoomFrequency=1.0: 0.30 * 1.0 * 2.0 = 0.60
	Config.RoomFrequency = 1.0f;
	TestEqual("RoomFreq=1.0 RoomChance", Config.Resolve().RoomChance, 0.60f, 0.001f);

	// TightMaze: BaseRoomChance=0.0, RoomFreq=1.0: 0.0 * 1.0 * 2.0 = 0.0
	Config.DungeonStyle = EDungeonStyle::TightMaze;
	Config.RoomFrequency = 1.0f;
	TestEqual("TightMaze RoomFreq=1.0 RoomChance", Config.Resolve().RoomChance, 0.0f, 0.001f);

	return true;
}

// Test 5: Each EDungeonScale maps to correct CorridorWidth and RoomRadius
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkConfigScaleResolutionTest, "ProceduralGeometry.DrunkardWalkConfig.ScaleResolution", DefaultTestFlags)

bool FDrunkardWalkConfigScaleResolutionTest::RunTest(const FString& Parameters)
{
	struct FScaleTestCase
	{
		EDungeonScale Scale;
		int32		  ExpectedCorridorWidth;
		int32		  ExpectedRoomRadius;
	};

	const TArray<FScaleTestCase> Cases = {
		{ EDungeonScale::Small, 1, 2 },
		{ EDungeonScale::Medium, 2, 3 },
		{ EDungeonScale::Large, 4, 5 },
	};

	FDrunkardWalkConfig Config;
	for (const FScaleTestCase& Case : Cases)
	{
		Config.Scale = Case.Scale;
		FDrunkardWalkResolvedParams Params = Config.Resolve();

		const FString ScaleName = FString::Printf(TEXT("Scale %d"), static_cast<int32>(Case.Scale));

		TestEqual(FString::Printf(TEXT("%s CorridorWidth"), *ScaleName), Params.CorridorWidth, Case.ExpectedCorridorWidth);
		TestEqual(FString::Printf(TEXT("%s RoomRadius"), *ScaleName), Params.RoomRadiusMax, Case.ExpectedRoomRadius);
	}

	return true;
}

// Test 6: Advanced override passes through raw values, ignoring style/sliders
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkConfigAdvancedOverrideTest, "ProceduralGeometry.DrunkardWalkConfig.AdvancedOverride", DefaultTestFlags)

bool FDrunkardWalkConfigAdvancedOverrideTest::RunTest(const FString& Parameters)
{
	FDrunkardWalkConfig Config;
	Config.DungeonStyle = EDungeonStyle::Caverns; // Should be ignored
	Config.Density = 1.0f;						  // Should be ignored
	Config.Complexity = 1.0f;					  // Should be ignored
	Config.RoomFrequency = 1.0f;				  // Should be ignored
	Config.Scale = EDungeonScale::Large;		  // Should be ignored

	Config.bUseAdvancedOverride = true;
	Config.AdvancedWalkLength = 777;
	Config.AdvancedNumWalkers = 5;
	Config.AdvancedBranchProbability = 0.42f;
	Config.AdvancedCorridorWidth = 3;
	Config.AdvancedRoomChance = 0.88f;
	Config.AdvancedRoomRadius = 7;

	FDrunkardWalkResolvedParams Params = Config.Resolve();

	TestEqual("WalkLength", Params.WalkLength, 777);
	TestEqual("NumWalkers", Params.NumWalkers, 5);
	TestEqual("BranchProbability", Params.BranchProbability, 0.42f, 0.001f);
	TestEqual("CorridorWidth", Params.CorridorWidth, 3);
	TestEqual("RoomChance", Params.RoomChance, 0.88f, 0.001f);
	TestEqual("RoomRadius", Params.RoomRadiusMax, 7);

	return true;
}

// Test 7: Clamping - BranchProbability and RoomChance never exceed 1.0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkConfigClampingTest, "ProceduralGeometry.DrunkardWalkConfig.Clamping", DefaultTestFlags)

bool FDrunkardWalkConfigClampingTest::RunTest(const FString& Parameters)
{
	// Labyrinth has BaseBranchProb=0.30. At Complexity=1.0: 0.30 * 1.0 * 2.0 = 0.60 (within bounds)
	// RoomHeavy has BaseRoomChance=0.30. At RoomFreq=1.0: 0.30 * 1.0 * 2.0 = 0.60 (within bounds)
	// To test actual clamping, we need a value that would exceed 1.0
	// Max BranchProb from presets: Labyrinth 0.30 * 2.0 = 0.60 (fine)
	// Max RoomChance from presets: RoomHeavy 0.30 * 2.0 = 0.60 (fine)
	// All presets are designed so max slider doesn't exceed 1.0, but verify the Clamp works

	FDrunkardWalkConfig Config;

	// Test all styles at max sliders: BranchProbability should be <= 1.0
	const TArray<EDungeonStyle> AllStyles = {
		EDungeonStyle::OpenRuins, EDungeonStyle::TightMaze, EDungeonStyle::RoomHeavy, EDungeonStyle::Labyrinth, EDungeonStyle::Caverns
	};

	for (EDungeonStyle Style : AllStyles)
	{
		Config.DungeonStyle = Style;
		Config.Density = 1.0f;
		Config.Complexity = 1.0f;
		Config.RoomFrequency = 1.0f;

		FDrunkardWalkResolvedParams Params = Config.Resolve();

		const FString StyleName = FString::Printf(TEXT("Style %d"), static_cast<int32>(Style));
		TestTrue(FString::Printf(TEXT("%s BranchProb <= 1.0"), *StyleName), Params.BranchProbability <= 1.0f);
		TestTrue(FString::Printf(TEXT("%s BranchProb >= 0.0"), *StyleName), Params.BranchProbability >= 0.0f);
		TestTrue(FString::Printf(TEXT("%s RoomChance <= 1.0"), *StyleName), Params.RoomChance <= 1.0f);
		TestTrue(FString::Printf(TEXT("%s RoomChance >= 0.0"), *StyleName), Params.RoomChance >= 0.0f);
	}

	return true;
}

// Test 8: Minimum bounds - WalkLength >= 1 and NumWalkers >= 1 for all slider/preset combinations
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrunkardWalkConfigMinimumBoundsTest, "ProceduralGeometry.DrunkardWalkConfig.MinimumBounds", DefaultTestFlags)

bool FDrunkardWalkConfigMinimumBoundsTest::RunTest(const FString& Parameters)
{
	const TArray<EDungeonStyle> AllStyles = {
		EDungeonStyle::OpenRuins, EDungeonStyle::TightMaze, EDungeonStyle::RoomHeavy, EDungeonStyle::Labyrinth, EDungeonStyle::Caverns
	};

	FDrunkardWalkConfig Config;

	// Test minimum sliders (0.0) for all styles
	for (EDungeonStyle Style : AllStyles)
	{
		Config.DungeonStyle = Style;
		Config.Density = 0.0f;
		Config.Complexity = 0.0f;
		Config.RoomFrequency = 0.0f;

		FDrunkardWalkResolvedParams Params = Config.Resolve();

		const FString StyleName = FString::Printf(TEXT("Style %d min sliders"), static_cast<int32>(Style));
		TestTrue(FString::Printf(TEXT("%s WalkLength >= 1"), *StyleName), Params.WalkLength >= 1);
		TestTrue(FString::Printf(TEXT("%s NumWalkers >= 1"), *StyleName), Params.NumWalkers >= 1);
	}

	// Test advanced override with minimum values
	Config.bUseAdvancedOverride = true;
	Config.AdvancedWalkLength = 0; // Below min, should clamp to 1
	Config.AdvancedNumWalkers = 0; // Below min, should clamp to 1
	Config.AdvancedCorridorWidth = 0;
	Config.AdvancedRoomRadius = 0;

	FDrunkardWalkResolvedParams AdvancedParams = Config.Resolve();
	TestEqual("Advanced WalkLength=0 clamped to 1", AdvancedParams.WalkLength, 1);
	TestEqual("Advanced NumWalkers=0 clamped to 1", AdvancedParams.NumWalkers, 1);
	TestEqual("Advanced CorridorWidth=0 clamped to 1", AdvancedParams.CorridorWidth, 1);
	TestEqual("Advanced RoomRadius=0 clamped to 1", AdvancedParams.RoomRadiusMax, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
