#pragma once

#include "CoreMinimal.h"
#include "SelectionRules.h"
#include "DrunkardWalkConfig.generated.h"

/**
 * Authoring rule for a room type. SelectionWeight is always a relative share; footprints are in grid cells.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FRoomTypeConfig : public FWeightedPoolRangeEntry
{
	GENERATED_BODY()

	/** Stable identity of this room type in the resolved placement queue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type", meta = (ToolTip = "Identifier for this room type."))
	FName Tag = NAME_None;

	/** Full room width measured in grid cells; one or more cells are required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type", meta = (ClampMin = 1, ToolTip = "Room footprint width in grid cells."))
	int32 FootprintWidthCells = 4;

	/** Full room height measured in grid cells; one or more cells are required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type", meta = (ClampMin = 1, ToolTip = "Room footprint height in grid cells."))
	int32 FootprintHeightCells = 4;
};

template <> struct TStructOpsTypeTraits<FRoomTypeConfig> : TStructOpsTypeTraitsBase2<FRoomTypeConfig>
{
	enum
	{
		WithPostSerialize = true
	};
};

/** Generator input: resolved quantities, with no selection weights or quotas. */
struct FResolvedRoomType
{
	FName Tag;
	int32 FootprintWidthCells = 4;
	int32 FootprintHeightCells = 4;
	int32 ResolvedCount = 0;
};

/**
 * Resolved DW parameters for UDrunkardWalkGenerator2D. Deliberately not a USTRUCT, and defined here because
 * it is the return type of FDrunkardWalkConfig::ResolveForTotal() that other modules must see.
 */
struct PROCEDURALGEOMETRY_API FDrunkardWalkResolvedParams
{
	TArray<FResolvedRoomType> RoomTypes;
	int32					  CorridorLengthMin = 3;
	int32					  CorridorLengthMax = 8;
	int32					  CorridorWidthMin = 1;
	int32					  CorridorWidthMax = 1;
	float					  CorridorTurnProbability = 0.0f;
	float					  CorridorBranchProbability = 0.0f;
	int32					  RoomBorderMargin = 1;
	int32					  WallThickness = 1;
	int32					  MaxPlacementAttemptsPerExit = 8;
	bool					  bShuffleRoomOrder = true;
	float					  BranchProbability = 0.0f;
};

/**
 * Room-and-corridor dungeon generation parameters. The generator alternates rooms from the queue with
 * self-avoiding corridors and ignores any input bounds: the walk's own geometry defines the extents.
 * ResolveForTotal(TotalRooms) validates and clamps these into raw DW parameters.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FDrunkardWalkConfig
{
	GENERATED_BODY()

	/** Room identities, footprints and quotas; weights distribute the requested total, not raw room counts. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ToolTip = "Room types and quotas. Relative selection weights distribute the total room budget; they are never room counts."))
	TArray<FRoomTypeConfig> RoomTypes;

	/** Shortest corridor span between consecutive rooms, in grid cells. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Minimum corridor length (cells) between consecutive rooms."))
	int32 CorridorLengthMin = 3;

	/** Longest corridor span between consecutive rooms, in grid cells. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Maximum corridor length (cells) between consecutive rooms."))
	int32 CorridorLengthMax = 8;

	/** Minimum carved corridor width in grid cells. */
	UPROPERTY(
		EditAnywhere, BlueprintReadWrite, Category = "Dungeon Generation", meta = (ClampMin = 1, ToolTip = "Minimum corridor carve width in cells."))
	int32 CorridorWidthMin = 1;

	/** Maximum carved corridor width in grid cells; width can drift along a corridor. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Maximum corridor carve width in cells. Width drifts between min and max along the corridor."))
	int32 CorridorWidthMax = 1;

	/** Per-step probability from zero to one of a right-angle corridor turn. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Per-step probability that a corridor turns 90 degrees while being traced. 0 = straight corridors."))
	float CorridorTurnProbability = 0.0f;

	/** Per-step probability from zero to one of forking a side corridor to another room. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Per-step probability that a corridor forks off a side branch to an additional room. 0 = no forking."))
	float CorridorBranchProbability = 0.0f;

	/** Empty border margin around rooms and corridors, in grid cells. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0,
			ToolTip = "Wall gap (cells) reserved around each room and corridor. 1 = removable border ring where a passage is punched."))
	int32 RoomBorderMargin = 1;

	/** Retained wall thickness around floor, in grid cells. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1,
			ToolTip = "Thickness (cells) of wall kept around floor. Cells farther than this from any floor become empty (no wall)."))
	int32 WallThickness = 1;

	/** Placement attempts per room exit before the walk backtracks. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 1, ToolTip = "Placement retries per room exit before backtracking to the previous room."))
	int32 MaxPlacementAttemptsPerExit = 8;

	/** Shuffles the resolved room queue using the generation seed; off preserves declaration order. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ToolTip = "Shuffle the room placement queue (seeded). When false, rooms are placed in declared order."))
	bool bShuffleRoomOrder = true;

	/** Chance that the next room branches from an earlier room instead of the most recent one. */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Dungeon Generation",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip =
				"Probability that the next room grows from a random earlier room instead of the most recent one. 0 = a single winding path, 1 = a highly branching tree."))
	float BranchProbability = 0.0f;

	/**
	 * Validates the pool and allocates TotalRooms by SelectionWeight after reserving minima, respecting enabled caps.
	 * Authoring weights are not rewritten. Invalid budgets produce an explicit error and no selected rooms.
	 */
	FDrunkardWalkResolvedParams ResolveForTotal(int32 TotalRooms) const;
};
