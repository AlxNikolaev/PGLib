#include "Generators/CellularAutomata2D/CellularAutomataConfig.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// Test 1: Default-constructed config resolves to NaturalCaves preset values
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigDefaultResolveTest, "ProceduralGeometry.CellularAutomataConfig.DefaultResolve", DefaultTestFlags)

bool FCellularAutomataConfigDefaultResolveTest::RunTest(const FString& Parameters)
{
	FCellularAutomataConfig			Config;
	FCellularAutomataResolvedParams Params = Config.Resolve();

	// NaturalCaves: B678 / S345678
	TestEqual("BirthRule count", Params.BirthRule.Num(), 3);
	if (Params.BirthRule.Num() == 3)
	{
		TestEqual("BirthRule[0]", Params.BirthRule[0], 6);
		TestEqual("BirthRule[1]", Params.BirthRule[1], 7);
		TestEqual("BirthRule[2]", Params.BirthRule[2], 8);
	}

	TestEqual("SurvivalRule count", Params.SurvivalRule.Num(), 6);
	if (Params.SurvivalRule.Num() == 6)
	{
		TestEqual("SurvivalRule[0]", Params.SurvivalRule[0], 3);
		TestEqual("SurvivalRule[1]", Params.SurvivalRule[1], 4);
		TestEqual("SurvivalRule[2]", Params.SurvivalRule[2], 5);
		TestEqual("SurvivalRule[3]", Params.SurvivalRule[3], 6);
		TestEqual("SurvivalRule[4]", Params.SurvivalRule[4], 7);
		TestEqual("SurvivalRule[5]", Params.SurvivalRule[5], 8);
	}

	TestEqual("FillProbability", Params.FillProbability, 0.45f, 0.01f);
	TestEqual("Iterations", Params.Iterations, 5);
	TestEqual("GridDensityMultiplier", Params.GridDensityMultiplier, 10);
	TestEqual("MinRegionSize", Params.MinRegionSize, 30);
	TestTrue("bKeepCenterRegion", Params.bKeepCenterRegion);

	return true;
}

// Test 2: All 5 ECaveStyle presets resolve to correct B/S rules and base fill probability
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigAllStylePresetsTest, "ProceduralGeometry.CellularAutomataConfig.AllStylePresets", DefaultTestFlags)

bool FCellularAutomataConfigAllStylePresetsTest::RunTest(const FString& Parameters)
{
	struct FStyleTestCase
	{
		ECaveStyle	  Style;
		TArray<int32> ExpectedBirth;
		TArray<int32> ExpectedSurvival;
		float		  ExpectedBaseFill;
	};

	const TArray<FStyleTestCase> Cases = {
		{ ECaveStyle::OpenChambers, { 5, 6, 7, 8 }, { 4, 5, 6, 7, 8 }, 0.40f },
		{ ECaveStyle::NaturalCaves, { 6, 7, 8 }, { 3, 4, 5, 6, 7, 8 }, 0.45f },
		{ ECaveStyle::DenseCaverns, { 6, 7, 8 }, { 2, 3, 4, 5 }, 0.50f },
		{ ECaveStyle::TightTunnels, { 3, 5, 7, 8 }, { 1, 2, 3, 4, 5 }, 0.55f },
		{ ECaveStyle::SwissCheese, { 6, 7, 8 }, { 3, 4, 5 }, 0.45f },
	};

	for (const FStyleTestCase& Case : Cases)
	{
		FCellularAutomataConfig Config;
		Config.CaveStyle = Case.Style;
		Config.Openness = 0.5f; // No modulation at midpoint

		FCellularAutomataResolvedParams Params = Config.Resolve();

		const FString StyleName = FString::Printf(TEXT("Style %d"), static_cast<int32>(Case.Style));

		TestEqual(FString::Printf(TEXT("%s BirthRule count"), *StyleName), Params.BirthRule.Num(), Case.ExpectedBirth.Num());
		for (int32 i = 0; i < FMath::Min(Params.BirthRule.Num(), Case.ExpectedBirth.Num()); ++i)
		{
			TestEqual(FString::Printf(TEXT("%s BirthRule[%d]"), *StyleName, i), Params.BirthRule[i], Case.ExpectedBirth[i]);
		}

		TestEqual(FString::Printf(TEXT("%s SurvivalRule count"), *StyleName), Params.SurvivalRule.Num(), Case.ExpectedSurvival.Num());
		for (int32 i = 0; i < FMath::Min(Params.SurvivalRule.Num(), Case.ExpectedSurvival.Num()); ++i)
		{
			TestEqual(FString::Printf(TEXT("%s SurvivalRule[%d]"), *StyleName, i), Params.SurvivalRule[i], Case.ExpectedSurvival[i]);
		}

		TestEqual(FString::Printf(TEXT("%s FillProbability"), *StyleName), Params.FillProbability, Case.ExpectedBaseFill, 0.01f);
	}

	return true;
}

// Test 3: Openness slider adjusts fill probability correctly
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigOpennessTest, "ProceduralGeometry.CellularAutomataConfig.OpennessModulatesFillProbability", DefaultTestFlags)

bool FCellularAutomataConfigOpennessTest::RunTest(const FString& Parameters)
{
	// NaturalCaves has base fill 0.45
	FCellularAutomataConfig Config;
	Config.CaveStyle = ECaveStyle::NaturalCaves;

	// Openness=0.0 -> fill = 0.45 + (0.5 - 0.0) * 0.30 = 0.45 + 0.15 = 0.60
	Config.Openness = 0.0f;
	TestEqual("Openness=0 fill", Config.Resolve().FillProbability, 0.60f, 0.01f);

	// Openness=0.5 -> fill = 0.45 + 0.0 = 0.45 (no change)
	Config.Openness = 0.5f;
	TestEqual("Openness=0.5 fill", Config.Resolve().FillProbability, 0.45f, 0.01f);

	// Openness=1.0 -> fill = 0.45 + (0.5 - 1.0) * 0.30 = 0.45 - 0.15 = 0.30
	Config.Openness = 1.0f;
	TestEqual("Openness=1.0 fill", Config.Resolve().FillProbability, 0.30f, 0.01f);

	// Verify clamping: TightTunnels base fill 0.55, Openness=0.0 -> 0.55 + 0.15 = 0.70 (within [0.1, 0.9])
	Config.CaveStyle = ECaveStyle::TightTunnels;
	Config.Openness = 0.0f;
	TestEqual("TightTunnels Openness=0 fill", Config.Resolve().FillProbability, 0.70f, 0.01f);

	return true;
}

// Test 4: Smoothness value passes through directly as Iterations
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigSmoothnessTest, "ProceduralGeometry.CellularAutomataConfig.SmoothnessMapsToIterations", DefaultTestFlags)

bool FCellularAutomataConfigSmoothnessTest::RunTest(const FString& Parameters)
{
	FCellularAutomataConfig Config;

	Config.Smoothness = 1;
	TestEqual("Smoothness=1", Config.Resolve().Iterations, 1);

	Config.Smoothness = 5;
	TestEqual("Smoothness=5", Config.Resolve().Iterations, 5);

	Config.Smoothness = 10;
	TestEqual("Smoothness=10", Config.Resolve().Iterations, 10);

	return true;
}

// Test 5: All ECaveRegionScale presets resolve to correct values
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigRegionScaleTest, "ProceduralGeometry.CellularAutomataConfig.AllRegionScalePresets", DefaultTestFlags)

bool FCellularAutomataConfigRegionScaleTest::RunTest(const FString& Parameters)
{
	struct FScaleTestCase
	{
		ECaveRegionScale Scale;
		int32			 ExpectedDensity;
		int32			 ExpectedMinRegion;
	};

	const TArray<FScaleTestCase> Cases = {
		{ ECaveRegionScale::Small, 5, 10 },
		{ ECaveRegionScale::Medium, 10, 30 },
		{ ECaveRegionScale::Large, 15, 60 },
		{ ECaveRegionScale::Massive, 20, 100 },
	};

	FCellularAutomataConfig Config;
	for (const FScaleTestCase& Case : Cases)
	{
		Config.RegionScale = Case.Scale;
		FCellularAutomataResolvedParams Params = Config.Resolve();

		const FString ScaleName = FString::Printf(TEXT("Scale %d"), static_cast<int32>(Case.Scale));

		TestEqual(FString::Printf(TEXT("%s GridDensityMultiplier"), *ScaleName), Params.GridDensityMultiplier, Case.ExpectedDensity);
		TestEqual(FString::Printf(TEXT("%s MinRegionSize"), *ScaleName), Params.MinRegionSize, Case.ExpectedMinRegion);
	}

	return true;
}

// Test 6: Advanced override bypasses semantic parameters
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigAdvancedOverrideTest, "ProceduralGeometry.CellularAutomataConfig.AdvancedOverrideBypassesSemantic", DefaultTestFlags)

bool FCellularAutomataConfigAdvancedOverrideTest::RunTest(const FString& Parameters)
{
	FCellularAutomataConfig Config;
	Config.CaveStyle = ECaveStyle::OpenChambers; // B5678/S45678 — should be ignored

	Config.bUseAdvancedOverride = true;
	Config.AdvancedRuleNotation = TEXT("B12/S34");
	Config.AdvancedFillProbability = 0.77f;
	Config.AdvancedIterations = 12;
	Config.AdvancedGridDensityMultiplier = 25;
	Config.AdvancedMinRegionSize = 50;

	FCellularAutomataResolvedParams Params = Config.Resolve();

	// Birth/survival should come from advanced strings, NOT from OpenChambers preset
	TestEqual("BirthRule count", Params.BirthRule.Num(), 2);
	if (Params.BirthRule.Num() == 2)
	{
		TestEqual("BirthRule[0]", Params.BirthRule[0], 1);
		TestEqual("BirthRule[1]", Params.BirthRule[1], 2);
	}

	TestEqual("SurvivalRule count", Params.SurvivalRule.Num(), 2);
	if (Params.SurvivalRule.Num() == 2)
	{
		TestEqual("SurvivalRule[0]", Params.SurvivalRule[0], 3);
		TestEqual("SurvivalRule[1]", Params.SurvivalRule[1], 4);
	}

	TestEqual("FillProbability", Params.FillProbability, 0.77f, 0.01f);
	TestEqual("Iterations", Params.Iterations, 12);
	TestEqual("GridDensityMultiplier", Params.GridDensityMultiplier, 25);
	TestEqual("MinRegionSize", Params.MinRegionSize, 50);

	return true;
}

// Test 7: Resolve integration tests with AdvancedRuleNotation
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigResolveRuleNotationTest, "ProceduralGeometry.CellularAutomataConfig.ResolveAdvancedRuleNotation", DefaultTestFlags)

bool FCellularAutomataConfigResolveRuleNotationTest::RunTest(const FString& Parameters)
{
	// Valid notation resolves correctly
	{
		FCellularAutomataConfig Config;
		Config.bUseAdvancedOverride = true;
		Config.AdvancedRuleNotation = TEXT("B12/S34");
		Config.AdvancedFillProbability = 0.60f;
		Config.AdvancedIterations = 8;

		FCellularAutomataResolvedParams Params = Config.Resolve();

		TestEqual("Valid: BirthRule count", Params.BirthRule.Num(), 2);
		if (Params.BirthRule.Num() == 2)
		{
			TestEqual("Valid: BirthRule[0]", Params.BirthRule[0], 1);
			TestEqual("Valid: BirthRule[1]", Params.BirthRule[1], 2);
		}
		TestEqual("Valid: SurvivalRule count", Params.SurvivalRule.Num(), 2);
		if (Params.SurvivalRule.Num() == 2)
		{
			TestEqual("Valid: SurvivalRule[0]", Params.SurvivalRule[0], 3);
			TestEqual("Valid: SurvivalRule[1]", Params.SurvivalRule[1], 4);
		}
		TestEqual("Valid: FillProbability", Params.FillProbability, 0.60f, 0.01f);
		TestEqual("Valid: Iterations", Params.Iterations, 8);
	}

	// Invalid notation falls back to defaults
	{
		FCellularAutomataConfig Config;
		Config.bUseAdvancedOverride = true;
		Config.AdvancedRuleNotation = TEXT("INVALID");
		Config.AdvancedFillProbability = 0.60f;
		Config.AdvancedIterations = 8;

		FCellularAutomataResolvedParams Params = Config.Resolve();

		TestEqual("Invalid: BirthRule count", Params.BirthRule.Num(), 3);
		if (Params.BirthRule.Num() == 3)
		{
			TestEqual("Invalid: BirthRule[0]", Params.BirthRule[0], 6);
			TestEqual("Invalid: BirthRule[1]", Params.BirthRule[1], 7);
			TestEqual("Invalid: BirthRule[2]", Params.BirthRule[2], 8);
		}
		TestEqual("Invalid: SurvivalRule count", Params.SurvivalRule.Num(), 3);
		if (Params.SurvivalRule.Num() == 3)
		{
			TestEqual("Invalid: SurvivalRule[0]", Params.SurvivalRule[0], 3);
			TestEqual("Invalid: SurvivalRule[1]", Params.SurvivalRule[1], 4);
			TestEqual("Invalid: SurvivalRule[2]", Params.SurvivalRule[2], 5);
		}
		TestEqual("Invalid: FillProbability still from advanced", Params.FillProbability, 0.60f, 0.01f);
		TestEqual("Invalid: Iterations still from advanced", Params.Iterations, 8);
	}

	// Empty notation falls back to defaults
	{
		FCellularAutomataConfig Config;
		Config.bUseAdvancedOverride = true;
		Config.AdvancedRuleNotation = TEXT("");

		FCellularAutomataResolvedParams Params = Config.Resolve();

		TestEqual("Empty: BirthRule count", Params.BirthRule.Num(), 3);
		if (Params.BirthRule.Num() == 3)
		{
			TestEqual("Empty: BirthRule[0]", Params.BirthRule[0], 6);
			TestEqual("Empty: BirthRule[1]", Params.BirthRule[1], 7);
			TestEqual("Empty: BirthRule[2]", Params.BirthRule[2], 8);
		}
		TestEqual("Empty: SurvivalRule count", Params.SurvivalRule.Num(), 3);
		if (Params.SurvivalRule.Num() == 3)
		{
			TestEqual("Empty: SurvivalRule[0]", Params.SurvivalRule[0], 3);
			TestEqual("Empty: SurvivalRule[1]", Params.SurvivalRule[1], 4);
			TestEqual("Empty: SurvivalRule[2]", Params.SurvivalRule[2], 5);
		}
	}

	return true;
}

// Test 10: ParseBSRuleNotation valid inputs — direct parser tests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigParserValidTest, "ProceduralGeometry.CellularAutomataConfig.ParseBSRuleNotation_ValidInputs", DefaultTestFlags)

bool FCellularAutomataConfigParserValidTest::RunTest(const FString& Parameters)
{
	// Standard valid input
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678/S345"));
		TestTrue("B678/S345 valid", Result.IsValid());
		TestEqual("B678/S345 birth count", Result.BirthRule.Num(), 3);
		if (Result.BirthRule.Num() == 3)
		{
			TestEqual("B678/S345 birth[0]", Result.BirthRule[0], 6);
			TestEqual("B678/S345 birth[1]", Result.BirthRule[1], 7);
			TestEqual("B678/S345 birth[2]", Result.BirthRule[2], 8);
		}
		TestEqual("B678/S345 survival count", Result.SurvivalRule.Num(), 3);
		if (Result.SurvivalRule.Num() == 3)
		{
			TestEqual("B678/S345 survival[0]", Result.SurvivalRule[0], 3);
			TestEqual("B678/S345 survival[1]", Result.SurvivalRule[1], 4);
			TestEqual("B678/S345 survival[2]", Result.SurvivalRule[2], 5);
		}
	}

	// Case-insensitive
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("b678/s345"));
		TestTrue("b678/s345 valid", Result.IsValid());
		TestEqual("lowercase birth count", Result.BirthRule.Num(), 3);
		TestEqual("lowercase survival count", Result.SurvivalRule.Num(), 3);
	}

	// Edge: zero is a valid digit
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B0/S0"));
		TestTrue("B0/S0 valid", Result.IsValid());
		TestEqual("B0/S0 birth count", Result.BirthRule.Num(), 1);
		if (Result.BirthRule.Num() == 1)
		{
			TestEqual("B0/S0 birth[0]", Result.BirthRule[0], 0);
		}
		TestEqual("B0/S0 survival count", Result.SurvivalRule.Num(), 1);
		if (Result.SurvivalRule.Num() == 1)
		{
			TestEqual("B0/S0 survival[0]", Result.SurvivalRule[0], 0);
		}
	}

	// Empty string: valid, both arrays empty
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT(""));
		TestTrue("empty valid", Result.IsValid());
		TestEqual("empty birth count", Result.BirthRule.Num(), 0);
		TestEqual("empty survival count", Result.SurvivalRule.Num(), 0);
	}

	// All whitespace stripped (leading, trailing, internal)
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT(" B 6 7 8 / S 3 4 5 "));
		TestTrue("whitespace stripped valid", Result.IsValid());
		TestEqual("whitespace birth count", Result.BirthRule.Num(), 3);
		if (Result.BirthRule.Num() == 3)
		{
			TestEqual("whitespace birth[0]", Result.BirthRule[0], 6);
			TestEqual("whitespace birth[1]", Result.BirthRule[1], 7);
			TestEqual("whitespace birth[2]", Result.BirthRule[2], 8);
		}
		TestEqual("whitespace survival count", Result.SurvivalRule.Num(), 3);
		if (Result.SurvivalRule.Num() == 3)
		{
			TestEqual("whitespace survival[0]", Result.SurvivalRule[0], 3);
			TestEqual("whitespace survival[1]", Result.SurvivalRule[1], 4);
			TestEqual("whitespace survival[2]", Result.SurvivalRule[2], 5);
		}
	}

	// All valid digits
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B12345678/S012345678"));
		TestTrue("all digits valid", Result.IsValid());
		TestEqual("all digits birth count", Result.BirthRule.Num(), 8);
		TestEqual("all digits survival count", Result.SurvivalRule.Num(), 9);
	}

	return true;
}

// Test 11: ParseBSRuleNotation invalid inputs — direct parser tests
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigParserInvalidTest, "ProceduralGeometry.CellularAutomataConfig.ParseBSRuleNotation_InvalidInputs", DefaultTestFlags)

bool FCellularAutomataConfigParserInvalidTest::RunTest(const FString& Parameters)
{
	// Missing B prefix
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("678/S345"));
		TestFalse("no B prefix: invalid", Result.IsValid());
		TestTrue("no B prefix: has error", !Result.ErrorMessage.IsEmpty());
		TestEqual("no B prefix: birth empty", Result.BirthRule.Num(), 0);
		TestEqual("no B prefix: survival empty", Result.SurvivalRule.Num(), 0);
	}

	// Missing / separator
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678S345"));
		TestFalse("no separator: invalid", Result.IsValid());
		TestTrue("no separator: has error", !Result.ErrorMessage.IsEmpty());
	}

	// Digit 9 in birth
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B679/S345"));
		TestFalse("9 in birth: invalid", Result.IsValid());
		TestTrue("9 in birth: has error", !Result.ErrorMessage.IsEmpty());
		TestEqual("9 in birth: birth empty", Result.BirthRule.Num(), 0);
	}

	// Digit 9 in survival
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678/S349"));
		TestFalse("9 in survival: invalid", Result.IsValid());
		TestTrue("9 in survival: has error", !Result.ErrorMessage.IsEmpty());
		TestEqual("9 in survival: survival empty", Result.SurvivalRule.Num(), 0);
	}

	// Duplicate in birth
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B668/S345"));
		TestFalse("dup birth: invalid", Result.IsValid());
		TestTrue("dup birth: has error", !Result.ErrorMessage.IsEmpty());
	}

	// Duplicate in survival
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678/S335"));
		TestFalse("dup survival: invalid", Result.IsValid());
		TestTrue("dup survival: has error", !Result.ErrorMessage.IsEmpty());
	}

	// Invalid char in birth
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B6x8/S345"));
		TestFalse("invalid char birth: invalid", Result.IsValid());
		TestTrue("invalid char birth: has error", !Result.ErrorMessage.IsEmpty());
	}

	// Invalid char in survival
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678/S3a5"));
		TestFalse("invalid char survival: invalid", Result.IsValid());
		TestTrue("invalid char survival: has error", !Result.ErrorMessage.IsEmpty());
	}

	// No /S section
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678"));
		TestFalse("no /S: invalid", Result.IsValid());
		TestTrue("no /S: has error", !Result.ErrorMessage.IsEmpty());
	}

	// No B section (starts with /)
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("/S345"));
		TestFalse("no B: invalid", Result.IsValid());
		TestTrue("no B: has error", !Result.ErrorMessage.IsEmpty());
	}

	// B/S — empty digit groups (both)
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B/S"));
		TestFalse("B/S: invalid", Result.IsValid());
		TestTrue("B/S: has error", !Result.ErrorMessage.IsEmpty());
	}

	// B/S345 — empty birth digit group
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B/S345"));
		TestFalse("B/S345: invalid", Result.IsValid());
		TestTrue("B/S345: has error", !Result.ErrorMessage.IsEmpty());
	}

	// B678/S — empty survival digit group
	{
		FCARuleParseResult Result = ParseBSRuleNotation(TEXT("B678/S"));
		TestFalse("B678/S: invalid", Result.IsValid());
		TestTrue("B678/S: has error", !Result.ErrorMessage.IsEmpty());
	}

	return true;
}

// Test 8: bKeepCenterRegion passes through in both modes
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigKeepCenterTest, "ProceduralGeometry.CellularAutomataConfig.KeepCenterRegionPassthrough", DefaultTestFlags)

bool FCellularAutomataConfigKeepCenterTest::RunTest(const FString& Parameters)
{
	FCellularAutomataConfig Config;

	// Semantic mode: set false
	Config.bKeepCenterRegion = false;
	TestFalse("Semantic mode: bKeepCenterRegion=false", Config.Resolve().bKeepCenterRegion);

	// Semantic mode: set true
	Config.bKeepCenterRegion = true;
	TestTrue("Semantic mode: bKeepCenterRegion=true", Config.Resolve().bKeepCenterRegion);

	// Advanced mode: set true
	Config.bUseAdvancedOverride = true;
	Config.bKeepCenterRegion = true;
	TestTrue("Advanced mode: bKeepCenterRegion=true", Config.Resolve().bKeepCenterRegion);

	// Advanced mode: set false
	Config.bKeepCenterRegion = false;
	TestFalse("Advanced mode: bKeepCenterRegion=false", Config.Resolve().bKeepCenterRegion);

	return true;
}

// Test 9: Defensive clamping on advanced override values
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCellularAutomataConfigBoundsValidationTest, "ProceduralGeometry.CellularAutomataConfig.AdvancedOverrideBoundsValidation", DefaultTestFlags)

bool FCellularAutomataConfigBoundsValidationTest::RunTest(const FString& Parameters)
{
	FCellularAutomataConfig Config;
	Config.bUseAdvancedOverride = true;

	// GridDensityMultiplier=0 should clamp to 1
	Config.AdvancedGridDensityMultiplier = 0;
	Config.AdvancedMinRegionSize = 20;
	TestEqual("GridDensity=0 clamped to 1", Config.Resolve().GridDensityMultiplier, 1);

	// MinRegionSize=0 should clamp to 1
	Config.AdvancedGridDensityMultiplier = 10;
	Config.AdvancedMinRegionSize = 0;
	TestEqual("MinRegionSize=0 clamped to 1", Config.Resolve().MinRegionSize, 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
