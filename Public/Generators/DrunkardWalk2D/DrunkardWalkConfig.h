#pragma once

#include "CoreMinimal.h"
#include "DrunkardWalkConfig.generated.h"

/** Visual style preset for drunkard walk dungeon generation. Each style maps to a known-good parameter combination. */
UENUM(BlueprintType)
enum class EDungeonStyle : uint8
{
	OpenRuins UMETA(DisplayName = "Open Ruins", ToolTip = "Wide corridors with scattered rooms. Good for hub areas or early floors."),
	TightMaze UMETA(DisplayName = "Tight Maze", ToolTip = "Single narrow winding path with no rooms. High tactical density."),
	RoomHeavy UMETA(DisplayName = "Room Heavy", ToolTip = "Many large rooms connected by corridors. Best for encounter-based layouts."),
	Labyrinth UMETA(DisplayName = "Labyrinth", ToolTip = "Many branching narrow paths. Complex navigation with few rooms."),
	Caverns	  UMETA(DisplayName = "Caverns", ToolTip = "Wide open carved areas with few rooms. Natural underground feel."),
};

/** Controls corridor width and room radius together. */
UENUM(BlueprintType)
enum class EDungeonScale : uint8
{
	Small  UMETA(DisplayName = "Small", ToolTip = "Narrow corridors (1 cell), small rooms (2 cell radius)."),
	Medium UMETA(DisplayName = "Medium", ToolTip = "Standard corridors (2 cells), medium rooms (3 cell radius)."),
	Large  UMETA(DisplayName = "Large", ToolTip = "Wide corridors (4 cells), large rooms (5 cell radius)."),
};

/**
 * Resolved DW parameters ready for consumption by UDrunkardWalkGenerator2D.
 * Plain C++ struct -- NOT a USTRUCT. Return type of FDrunkardWalkConfig::Resolve().
 */
struct PROCEDURALGEOMETRY_API FDrunkardWalkResolvedParams
{
	int32 WalkLength = 500;
	int32 NumWalkers = 1;
	float BranchProbability = 0.0f;
	int32 CorridorWidth = 1;
	float RoomChance = 0.0f;
	int32 RoomRadiusMin = 1;
	int32 RoomRadiusMax = 1;
	int32 MinRoomSpacing = 0;
	int32 MaxRoomCount = 0;
	float DirectionalMomentum = 0.0f;
	float ExplorationBias = 0.0f;
};

/**
 * Encapsulates all drunkard walk dungeon generation parameters behind designer-friendly semantic controls.
 * Designers select a style preset (EDungeonStyle) and adjust sliders for density, complexity, and room frequency.
 * An advanced override mode allows direct parameter specification for power users.
 *
 * Call Resolve() to convert semantic parameters into raw DW parameters for the generator.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FDrunkardWalkConfig
{
	GENERATED_BODY()

	// --- Core semantic parameters (designer-facing) ---

	UPROPERTY(
		EditAnywhere, BlueprintReadWrite, Category = "Dungeon Generation", meta = (ToolTip = "Visual style preset. Determines base walk parameters."))
	EDungeonStyle DungeonStyle = EDungeonStyle::OpenRuins;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "0 = sparse (less carved floor), 1 = dense (more carved floor). Scales WalkLength +/- 50% from style base."))
	float Density = 0.5f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip =
				"0 = simple (fewer walkers, no branching), 1 = complex (more walkers, more branching). Scales NumWalkers and BranchProbability."))
	float Complexity = 0.5f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0, ClampMax = 1.0, ToolTip = "0 = no rooms, 1 = maximum room placement. Scales RoomChance."))
	float RoomFrequency = 0.5f;

	UPROPERTY(
		EditAnywhere, BlueprintReadWrite, Category = "Dungeon Generation", meta = (ToolTip = "Controls corridor width and room radius together."))
	EDungeonScale Scale = EDungeonScale::Medium;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Probability that a walker continues in its last direction each step. 0 = fully random, 1 = always straight."))
	float DirectionalMomentum = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Preference for directions leading to uncarved cells. 0 = no preference, 1 = always prefer unvisited."))
	float ExplorationBias = 0.0f;

	// --- Advanced override (power-user mode) ---

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (ToolTip = "When enabled, ignores all semantic controls and uses raw values below."))
	bool bUseAdvancedOverride = false;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 1, ToolTip = "Raw walk step count."))
	int32 AdvancedWalkLength = 500;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 1, ToolTip = "Raw number of initial walkers."))
	int32 AdvancedNumWalkers = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta =
			(EditCondition = "bUseAdvancedOverride", ClampMin = 0.0, ClampMax = 1.0, ToolTip = "Raw probability of spawning a new walker per step."))
	float AdvancedBranchProbability = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 1, ToolTip = "Raw corridor carve width in cells."))
	int32 AdvancedCorridorWidth = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 0.0, ClampMax = 1.0, ToolTip = "Raw probability of placing a room per step."))
	float AdvancedRoomChance = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 1, ToolTip = "Raw maximum room carve radius in cells."))
	int32 AdvancedRoomRadius = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 1, ToolTip = "Raw minimum room carve radius in cells."))
	int32 AdvancedRoomRadiusMin = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 0, ToolTip = "Raw minimum Manhattan distance between room centers. 0 = disabled."))
	int32 AdvancedMinRoomSpacing = 0;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride", ClampMin = 0, ToolTip = "Raw maximum number of rooms. 0 = unlimited."))
	int32 AdvancedMaxRoomCount = 0;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride",
			ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Raw directional momentum. Probability that a walker continues in its last direction."))
	float AdvancedDirectionalMomentum = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation|Advanced",
		meta = (EditCondition = "bUseAdvancedOverride",
			ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Raw exploration bias. Preference for directions leading to uncarved cells."))
	float AdvancedExplorationBias = 0.0f;

	/** Resolves semantic parameters into raw DW parameters for the generator. Pure function, no side effects. */
	FDrunkardWalkResolvedParams Resolve() const;
};
