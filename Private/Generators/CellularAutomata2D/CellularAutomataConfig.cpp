#include "Generators/CellularAutomata2D/CellularAutomataConfig.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoguelikeGeometry, Log, All);

namespace
{
	/** Preset data for a single ECaveStyle entry. */
	struct FCaveStylePreset
	{
		TArray<int32> BirthRule;
		TArray<int32> SurvivalRule;
		float		  BaseFillProbability;
	};

	/** Returns the preset B/S rules and base fill probability for the given cave style. */
	const FCaveStylePreset& GetCaveStylePreset(ECaveStyle Style)
	{
		static const FCaveStylePreset OpenChambers = { { 5, 6, 7, 8 }, { 4, 5, 6, 7, 8 }, 0.40f };
		static const FCaveStylePreset NaturalCaves = { { 6, 7, 8 }, { 3, 4, 5, 6, 7, 8 }, 0.45f };
		static const FCaveStylePreset DenseCaverns = { { 6, 7, 8 }, { 2, 3, 4, 5 }, 0.50f };
		static const FCaveStylePreset TightTunnels = { { 3, 5, 7, 8 }, { 1, 2, 3, 4, 5 }, 0.55f };
		static const FCaveStylePreset SwissCheese = { { 6, 7, 8 }, { 3, 4, 5 }, 0.45f };

		switch (Style)
		{
			case ECaveStyle::OpenChambers:
				return OpenChambers;
			case ECaveStyle::NaturalCaves:
				return NaturalCaves;
			case ECaveStyle::DenseCaverns:
				return DenseCaverns;
			case ECaveStyle::TightTunnels:
				return TightTunnels;
			case ECaveStyle::SwissCheese:
				return SwissCheese;
			default:
				return NaturalCaves;
		}
	}

	/** Preset data for a single ECaveRegionScale entry. */
	struct FCaveRegionScalePreset
	{
		int32 GridDensityMultiplier;
		int32 MinRegionSize;
	};

	/** Returns the grid density and min region size for the given region scale. */
	FCaveRegionScalePreset GetRegionScalePreset(ECaveRegionScale Scale)
	{
		switch (Scale)
		{
			case ECaveRegionScale::Small:
				return { 5, 10 };
			case ECaveRegionScale::Medium:
				return { 10, 30 };
			case ECaveRegionScale::Large:
				return { 15, 60 };
			case ECaveRegionScale::Massive:
				return { 20, 100 };
			default:
				return { 10, 30 };
		}
	}

} // namespace

/**
 * Parses a digit group (e.g., "678") into an array of neighbor counts.
 * Validates that each character is a digit 0-8, with no duplicates.
 *
 * @param Digits     The digit characters to parse
 * @param GroupName  Human-readable name for error messages (e.g., "birth" or "survival")
 * @param OutArray   Output array to populate with parsed digits
 * @param OutError   Populated with error description if parsing fails
 * @return           true if parsing succeeded, false on error
 */
static bool ParseDigitGroup(const FString& Digits, const TCHAR* GroupName, TArray<int32>& OutArray, FString& OutError)
{
	TSet<int32> Seen;

	for (const TCHAR Char : Digits)
	{
		if (Char < TEXT('0') || Char > TEXT('9'))
		{
			OutError = FString::Printf(TEXT("Invalid character '%c' in %s rule"), Char, GroupName);
			return false;
		}

		const int32 Value = static_cast<int32>(Char - TEXT('0'));

		if (Value > 8)
		{
			OutError = FString::Printf(TEXT("Digit '9' out of range (0-8) in %s rule"), GroupName);
			return false;
		}

		if (Seen.Contains(Value))
		{
			OutError = FString::Printf(TEXT("Duplicate digit '%d' in %s rule"), Value, GroupName);
			return false;
		}

		Seen.Add(Value);
		OutArray.Add(Value);
	}

	return true;
}

FCARuleParseResult ParseBSRuleNotation(const FString& RuleString)
{
	FCARuleParseResult Result;

	// Strip ALL whitespace (leading, trailing, and internal)
	FString Cleaned;
	Cleaned.Reserve(RuleString.Len());
	for (const TCHAR Char : RuleString)
	{
		if (!FChar::IsWhitespace(Char))
		{
			Cleaned.AppendChar(Char);
		}
	}

	// Empty string is valid — means "use defaults"
	if (Cleaned.IsEmpty())
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[CAConfig] ParseBSRuleNotation: empty input, caller should use defaults"));
		return Result;
	}

	// Convert to uppercase for case-insensitive parsing
	Cleaned.ToUpperInline();

	// Must start with 'B'
	if (Cleaned.Len() == 0 || Cleaned[0] != TEXT('B'))
	{
		Result.ErrorMessage = TEXT("Rule must start with 'B'");
		return Result;
	}

	// Find '/S' separator
	const int32 SeparatorIndex = Cleaned.Find(TEXT("/S"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
	if (SeparatorIndex == INDEX_NONE)
	{
		Result.ErrorMessage = TEXT("Expected '/S' separator");
		return Result;
	}

	// Extract digit groups
	const FString BirthDigits = Cleaned.Mid(1, SeparatorIndex - 1); // Between 'B' and '/'
	const FString SurvivalDigits = Cleaned.Mid(SeparatorIndex + 2); // After '/S'

	// Empty digit groups are invalid — use empty string for defaults instead
	if (BirthDigits.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Birth rule must contain at least one digit");
		return Result;
	}
	if (SurvivalDigits.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Survival rule must contain at least one digit");
		return Result;
	}

	// Parse birth digits
	if (!ParseDigitGroup(BirthDigits, TEXT("birth"), Result.BirthRule, Result.ErrorMessage))
	{
		Result.BirthRule.Empty();
		Result.SurvivalRule.Empty();
		return Result;
	}

	// Parse survival digits
	if (!ParseDigitGroup(SurvivalDigits, TEXT("survival"), Result.SurvivalRule, Result.ErrorMessage))
	{
		Result.BirthRule.Empty();
		Result.SurvivalRule.Empty();
		return Result;
	}

	return Result;
}

FCellularAutomataResolvedParams FCellularAutomataConfig::Resolve() const
{
	FCellularAutomataResolvedParams Params;
	Params.bKeepCenterRegion = bKeepCenterRegion;

	if (bUseAdvancedOverride)
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[CAConfig] Resolve: using advanced override"));

		static const TArray<int32> DefaultBirthFallback = { 6, 7, 8 };
		static const TArray<int32> DefaultSurvivalFallback = { 3, 4, 5 };

		FCARuleParseResult ParseResult = ParseBSRuleNotation(AdvancedRuleNotation);

		if (ParseResult.IsValid() && ParseResult.BirthRule.Num() > 0 && ParseResult.SurvivalRule.Num() > 0)
		{
			Params.BirthRule = MoveTemp(ParseResult.BirthRule);
			Params.SurvivalRule = MoveTemp(ParseResult.SurvivalRule);
		}
		else
		{
			if (!ParseResult.IsValid())
			{
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[CAConfig] Resolve: invalid AdvancedRuleNotation \"%s\": %s — falling back to defaults (B678/S345)"),
					*AdvancedRuleNotation,
					*ParseResult.ErrorMessage);
			}
			else
			{
				UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[CAConfig] Resolve: empty AdvancedRuleNotation, falling back to defaults (B678/S345)"));
			}

			Params.BirthRule = DefaultBirthFallback;
			Params.SurvivalRule = DefaultSurvivalFallback;
		}

		Params.FillProbability = AdvancedFillProbability;
		Params.Iterations = AdvancedIterations;
		Params.GridDensityMultiplier = FMath::Clamp(AdvancedGridDensityMultiplier, 1, 30);
		Params.MinRegionSize = FMath::Max(AdvancedMinRegionSize, 1);
	}
	else
	{
		const FCaveStylePreset& StylePreset = GetCaveStylePreset(CaveStyle);

		Params.BirthRule = StylePreset.BirthRule;
		Params.SurvivalRule = StylePreset.SurvivalRule;

		// Modulate fill probability based on Openness:
		// At Openness=0.0, fill increases by 0.15 (more walls)
		// At Openness=0.5, fill is unchanged (base value)
		// At Openness=1.0, fill decreases by 0.15 (more open)
		Params.FillProbability = FMath::Clamp(StylePreset.BaseFillProbability + (0.5f - Openness) * 0.30f, 0.1f, 0.9f);

		Params.Iterations = Smoothness;

		const FCaveRegionScalePreset ScalePreset = GetRegionScalePreset(RegionScale);
		Params.GridDensityMultiplier = ScalePreset.GridDensityMultiplier;
		Params.MinRegionSize = ScalePreset.MinRegionSize;

		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[CAConfig] Resolve: style=%d, openness=%.2f, smoothness=%d, scale=%d -> fill=%.2f, iter=%d, density=%d, minRegion=%d"),
			static_cast<int32>(CaveStyle),
			Openness,
			Smoothness,
			static_cast<int32>(RegionScale),
			Params.FillProbability,
			Params.Iterations,
			Params.GridDensityMultiplier,
			Params.MinRegionSize);
	}

	return Params;
}
