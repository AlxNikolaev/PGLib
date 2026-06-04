#pragma once

#include "CoreMinimal.h"
#include "DrunkardWalkConfig.generated.h"

/**
 * A single room type for room-and-corridor generation.
 * Each type contributes Count room instances to the placement queue.
 * Footprint is expressed in grid cells; in the future this will be derived from a
 * prefab room's bounds via ceil(prefabBounds / cellSize) and carry a prefab reference.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FRoomTypeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type", meta = (ToolTip = "Identifier for this room type (e.g. 'Small', 'Boss')."))
	FName Tag = NAME_None;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ClampMin = 1, ToolTip = "Room footprint width in grid cells. Later derived from prefab bounds."))
	int32 FootprintWidthCells = 4;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ClampMin = 1, ToolTip = "Room footprint height in grid cells. Later derived from prefab bounds."))
	int32 FootprintHeightCells = 4;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ClampMin = 1,
			ToolTip =
				"Relative weight for this type when distributing a total room budget across types (ResolveForTotal). Higher values mean proportionally more rooms. Equal weights give equal shares."))
	int32 Weight = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ClampMin = 0,
			ToolTip =
				"Minimum rooms of this type guaranteed each time a total room budget is distributed (ResolveForTotal). Mandatory minimums are filled before weight-based distribution. 0 = no guarantee."))
	int32 Min = 0;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ClampMin = 0, ToolTip = "Maximum rooms of this type allowed when distributing a total room budget (ResolveForTotal). 0 = uncapped."))
	int32 Max = 0;

	// Future: TSoftClassPtr<AActor> / TSoftObjectPtr<UWorld> RoomPrefab — the actual room instance.
};

/**
 * Resolved DW parameters ready for consumption by UDrunkardWalkGenerator2D.
 * Plain C++ struct -- NOT a USTRUCT. Return type of FDrunkardWalkConfig::Resolve().
 */
struct PROCEDURALGEOMETRY_API FDrunkardWalkResolvedParams
{
	TArray<FRoomTypeConfig> RoomTypes;
	int32					CorridorLengthMin = 3;
	int32					CorridorLengthMax = 8;
	int32					CorridorWidthMin = 1;
	int32					CorridorWidthMax = 1;
	float					CorridorTurnProbability = 0.0f;
	float					CorridorBranchProbability = 0.0f;
	int32					RoomBorderMargin = 1;
	int32					WallThickness = 1;
	int32					MaxPlacementAttemptsPerExit = 8;
	bool					bShuffleRoomOrder = true;
	float					BranchProbability = 0.0f;
};

/**
 * Room-and-corridor dungeon generation parameters.
 *
 * The generator starts in a room, walks a self-avoiding corridor of randomized length, then places
 * the next room from the queue (built from RoomTypes x Count), picks a fresh exit side, and repeats.
 * It ignores any input bounds — the walk's own geometry defines the extents. Two primary knobs are
 * the grid cell size (set on the generator) and the room queue described here.
 *
 * Call Resolve() to validate/clamp into raw DW parameters for the generator.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FDrunkardWalkConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ToolTip = "Room types to place. Each type contributes Count rooms to the generation queue."))
	TArray<FRoomTypeConfig> RoomTypes;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Minimum corridor length (cells) between consecutive rooms."))
	int32 CorridorLengthMin = 3;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Maximum corridor length (cells) between consecutive rooms."))
	int32 CorridorLengthMax = 8;

	UPROPERTY(
		EditAnywhere, BlueprintReadWrite, Category = "Dungeon Generation", meta = (ClampMin = 1, ToolTip = "Minimum corridor carve width in cells."))
	int32 CorridorWidthMin = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Maximum corridor carve width in cells. Width drifts between min and max along the corridor."))
	int32 CorridorWidthMax = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Per-step probability that a corridor turns 90 degrees while being traced. 0 = straight corridors."))
	float CorridorTurnProbability = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Per-step probability that a corridor forks off a side branch to an additional room. 0 = no forking."))
	float CorridorBranchProbability = 0.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0,
			ToolTip = "Wall gap (cells) reserved around each room and corridor. 1 = removable border ring where a passage is punched."))
	int32 RoomBorderMargin = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1,
			ToolTip = "Thickness (cells) of wall kept around floor. Cells farther than this from any floor become empty (no wall)."))
	int32 WallThickness = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Placement retries per room exit before backtracking to the previous room."))
	int32 MaxPlacementAttemptsPerExit = 8;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ToolTip = "Shuffle the room placement queue (seeded). When false, rooms are placed in declared order."))
	bool bShuffleRoomOrder = true;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip =
				"Probability that the next room grows from a random earlier room instead of the most recent one. 0 = a single winding path, 1 = a highly branching tree."))
	float BranchProbability = 0.0f;

	/** Validates and clamps semantic parameters into raw DW parameters for the generator. Pure function, no side effects. */
	FDrunkardWalkResolvedParams Resolve() const;

	/**
	 * Like Resolve(), but distributes exactly TotalRooms rooms across types proportionally to their Count
	 * (used as a relative weight). This is the preferred entry point when total room count is driven by
	 * Location Size on the level graph node, removing the need to manually set per-type counts.
	 */
	FDrunkardWalkResolvedParams ResolveForTotal(int32 TotalRooms) const;
};
