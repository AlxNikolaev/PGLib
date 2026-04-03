#pragma once

#include "CoreMinimal.h"
#include "CellularAutomataConfig.generated.h"

/** Visual style preset for cellular automata cave generation. Each style maps to a known-good B/S rule family. */
UENUM(BlueprintType)
enum class ECaveStyle : uint8
{
	OpenChambers UMETA(DisplayName = "Open Chambers", ToolTip = "Large open areas with few walls. Best for boss arenas or hub rooms."),
	NaturalCaves UMETA(DisplayName = "Natural Caves", ToolTip = "Organic caverns with smooth walls. Good general-purpose cave style."),
	DenseCaverns UMETA(DisplayName = "Dense Caverns", ToolTip = "Moderate density with connected chambers. Balanced exploration feel."),
	TightTunnels UMETA(DisplayName = "Tight Tunnels", ToolTip = "Narrow passages with maze-like connectivity. High tactical density."),
	SwissCheese	 UMETA(DisplayName = "Swiss Cheese", ToolTip = "Many small isolated pockets. Fragmented layout, use with corridor carving."),
};

/** Controls grid cell density and minimum region size for cave generation. */
UENUM(BlueprintType)
enum class ECaveRegionScale : uint8
{
	Small	UMETA(DisplayName = "Small", ToolTip = "Grid density 5x, min region 10 cells. Coarse features, fast generation."),
	Medium	UMETA(DisplayName = "Medium", ToolTip = "Grid density 10x, min region 30 cells. Balanced detail and performance."),
	Large	UMETA(DisplayName = "Large", ToolTip = "Grid density 15x, min region 60 cells. Fine detail, more cells."),
	Massive UMETA(DisplayName = "Massive", ToolTip = "Grid density 20x, min region 100 cells. Maximum detail, slower generation."),
};

/**
 * Result of parsing a B/S notation rule string (e.g., "B678/S345").
 * Contains the parsed birth/survival arrays on success, or an error message on failure.
 */
struct PROCEDURALGEOMETRY_API FCARuleParseResult
{
	TArray<int32> BirthRule;
	TArray<int32> SurvivalRule;
	FString		  ErrorMessage;

	/** Returns true if the parse succeeded (no error). */
	bool IsValid() const { return ErrorMessage.IsEmpty(); }
};

/**
 * Parses a B/S notation rule string into birth and survival neighbor-count arrays.
 *
 * Expected format: "B{digits}/S{digits}" — e.g., "B678/S345", "b12/s0345".
 * Case-insensitive. All whitespace is stripped before parsing.
 * An empty string is valid and returns empty arrays (caller should use defaults).
 *
 * @param RuleString  The rule string to parse (e.g., "B678/S345"). Empty string is valid.
 * @return            Parse result with arrays populated on success, or ErrorMessage on failure.
 */
PROCEDURALGEOMETRY_API FCARuleParseResult ParseBSRuleNotation(const FString& RuleString);

/**
 * Resolved CA parameters ready for consumption by UCellularAutomataGenerator2D.
 * Plain C++ struct — NOT a USTRUCT. Defined here because it is the return type of
 * FCellularAutomataConfig::Resolve(), so callers in other modules must see its definition.
 */
struct PROCEDURALGEOMETRY_API FCellularAutomataResolvedParams
{
	TArray<int32> BirthRule;
	TArray<int32> SurvivalRule;
	float		  FillProbability = 0.45f;
	int32		  Iterations = 5;
	int32		  GridDensityMultiplier = 10;
	int32		  MinRegionSize = 30;
	bool		  bKeepCenterRegion = true;
};

/**
 * Encapsulates all cellular automata cave generation parameters behind designer-friendly semantic controls.
 * Designers select a visual preset (ECaveStyle) and adjust sliders for openness, smoothness, and scale.
 * An advanced override mode allows direct B/S rule specification for power users.
 *
 * Call Resolve() to convert semantic parameters into raw CA parameters for the generator.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FCellularAutomataConfig
{
	GENERATED_BODY()

	// --- Core semantic parameters (designer-facing) ---

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation",
		meta = (ToolTip = "Visual style preset. Determines base B/S rules and fill probability."))
	ECaveStyle CaveStyle = ECaveStyle::NaturalCaves;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "0 = dense/more walls, 1 = sparse/more open. Adjusts fill probability +/- 0.15 from the style's base."))
	float Openness = 0.5f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation",
		meta = (ClampMin = 1, ClampMax = 10, ToolTip = "Number of CA iterations. Higher = smoother, more settled cave walls."))
	int32 Smoothness = 5;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation",
		meta = (ToolTip = "Controls grid cell density and minimum region size. Larger = finer detail but slower."))
	ECaveRegionScale RegionScale = ECaveRegionScale::Medium;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation",
		meta = (ToolTip = "If true, the center region is always kept even if below MinRegionSize."))
	bool bKeepCenterRegion = true;

	// --- Advanced override (power-user mode) ---

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Advanced",
		meta = (ToolTip = "When enabled, ignores CaveStyle/Openness/Smoothness/RegionScale and uses raw values below."))
	bool bUseAdvancedOverride = false;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride",
			ToolTip =
				"B/S rule notation: B=birth (dead cell becomes alive), S=survival (alive cell stays alive). Digits are neighbor counts (0-8). Example: 'B678/S345'. Case-insensitive. Empty = use defaults (B678/S345)."))
	FString AdvancedRuleNotation;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride",
			ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Initial wall probability. Higher = more walls in initial random grid."))
	float AdvancedFillProbability = 0.45f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 1, ClampMax = 20, ToolTip = "Number of CA smoothing iterations."))
	int32 AdvancedIterations = 5;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride",
			ClampMin = 1,
			ClampMax = 30,
			ToolTip = "Grid density multiplier. CellSize = ZoneSize / this value. Higher = finer grid."))
	int32 AdvancedGridDensityMultiplier = 10;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride",
			ClampMin = 1,
			ToolTip = "Regions with fewer cells than this are culled (unless center region is kept)."))
	int32 AdvancedMinRegionSize = 20;

	// --- Corridor carving (future feature — declared now, consumed in a later task) ---

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Corridors",
		meta = (ClampMin = 0.0, ClampMax = 1.0, ToolTip = "[Future] Probability of carving corridors between disconnected regions. 0 = off."))
	float CorridorProbability = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Cave Generation|Corridors",
		meta = (ClampMin = 1, ClampMax = 5, ToolTip = "[Future] Width of carved corridors in grid cells."))
	int32 CorridorWidth = 2;

	/** Resolves semantic parameters into raw CA parameters for the generator. Pure function, no side effects. */
	FCellularAutomataResolvedParams Resolve() const;
};
