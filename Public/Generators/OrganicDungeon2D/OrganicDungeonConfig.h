#pragma once

#include "CoreMinimal.h"
#include "OrganicDungeonConfig.generated.h"

class UWorld;

/** Corridor visual style. */
UENUM(BlueprintType)
enum class EOrganicCorridorStyle : uint8
{
	Clean UMETA(DisplayName = "Clean", ToolTip = "Constant thin width, smooth — tidy hallways."),
	Cave  UMETA(DisplayName = "Cave", ToolTip = "Variable width with waviness — organic cave tunnels."),
};

/**
 * A room type for the organic dungeon. Each type is a **level instance** (prefab world) to spawn; its
 * footprint is derived from the level's bounds (or a manual override). The generator relies on the
 * prefab being ringed by punchable walls — modeled here as doorway candidates on the footprint edges.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FOrganicRoomType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (AllowedClasses = "/Script/Engine.World",
			ToolTip = "Level instance (prefab world) spawned for this room. Footprint is measured from its bounds."))
	TSoftObjectPtr<UWorld> RoomLevel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Room Type", meta = (ClampMin = 0, ToolTip = "How many rooms of this type to place."))
	int32 Count = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ClampMin = 1, ToolTip = "Candidate doorways (punch-points) per footprint edge."))
	int32 DoorwaysPerEdge = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ToolTip = "Manual footprint in world units. If either axis is <= 0 it is measured from RoomLevel's bounds instead."))
	FVector2D FootprintOverride = FVector2D::ZeroVector;
};

/** A resolved room type with a concrete footprint. Plain C++ struct. */
struct PROCEDURALGEOMETRY_API FOrganicResolvedRoomType
{
	TSoftObjectPtr<UWorld> RoomLevel;
	int32				   Count = 0;
	int32				   DoorwaysPerEdge = 1;
	float				   FootprintWidth = 600.0f;
	float				   FootprintHeight = 600.0f;
	FVector2D			   FootprintCenter = FVector2D::ZeroVector; // bounds center offset from level origin (XY)
	FName				   DisplayName;
};

/** Resolved organic-dungeon parameters. Plain C++ struct — NOT a USTRUCT. */
struct PROCEDURALGEOMETRY_API FOrganicDungeonResolvedParams
{
	TArray<FOrganicResolvedRoomType> RoomTypes;

	// Special entrance/exit rooms placed at the two graph-diameter endpoints (optional).
	bool					 bHasStartRoom = false;
	FOrganicResolvedRoomType StartRoom;
	bool					 bHasEndRoom = false;
	FOrganicResolvedRoomType EndRoom;

	// Placement
	float MinRoomGap = 100.0f;
	int32 MaxPlacementAttempts = 12;
	bool  bRandomRotation = true;
	float BranchProbability = 0.0f;
	bool  bShuffleRoomOrder = true;

	// Corridors
	EOrganicCorridorStyle CorridorStyle = EOrganicCorridorStyle::Cave;
	float				  MinThickness = 100.0f;
	float				  MaxWidth = 300.0f;
	float				  Waviness = 0.4f;
	float				  CorridorLengthMin = 400.0f;
	float				  CorridorLengthMax = 1200.0f;

	// Topology
	int32 LoopCount = 0;
	float LoopMaxDistance = 1500.0f;
	float SpineWidthScale = 1.6f;
	int32 DeadEndCount = 0;
	float DeadEndLength = 500.0f;
	int32 CorridorLinkCount = 0;
	float LinkMaxDistance = 1200.0f;

	// Realization (note: rasterization grid cell size is an actor-level setting, not per-location)
	int32 WallThickness = 1;
	bool  bSmoothCorridors = false;
	int32 SmoothIterations = 2;
};

/**
 * Organic dungeon generation parameters: prefab rooms placed in continuous space (with rotation),
 * connected by cave-like corridors that branch, loop, and dead-end. Call Resolve() to clamp into raw
 * parameters for UOrganicDungeonGenerator2D.
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FOrganicDungeonConfig
{
	GENERATED_BODY()

	// --- Rooms ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Organic Dungeon|Rooms", meta = (ToolTip = "Room types and counts to place."))
	TArray<FOrganicRoomType> RoomTypes;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Rooms",
		meta = (ToolTip = "Optional entrance prefab, placed at one graph-diameter endpoint (the start)."))
	FOrganicRoomType StartRoom;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Rooms",
		meta = (ToolTip =
					"Optional exit prefab, placed at the farthest graph-diameter endpoint (the end). Its outward doorway is the transition to the next part."))
	FOrganicRoomType EndRoom;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Rooms",
		meta = (ToolTip = "Place rooms at random rotations (true non-grid layout)."))
	bool bRandomRotation = true;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Rooms",
		meta = (ToolTip = "Shuffle the room placement queue (seeded). When false, rooms are placed in declared order."))
	bool bShuffleRoomOrder = true;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Placement",
		meta = (ClampMin = 0.0, ToolTip = "Minimum clear gap between room footprints, world units."))
	float MinRoomGap = 100.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Placement",
		meta = (ClampMin = 1, ToolTip = "Placement retries per room before backtracking."))
	int32 MaxPlacementAttempts = 12;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Placement",
		meta = (ClampMin = 0.0,
			ClampMax = 1.0,
			ToolTip = "Probability the next room grows from a random earlier room instead of the most recent (branching)."))
	float BranchProbability = 0.0f;

	// --- Corridors ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Organic Dungeon|Corridors", meta = (ToolTip = "Clean hallways vs organic caves."))
	EOrganicCorridorStyle CorridorStyle = EOrganicCorridorStyle::Cave;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Corridors",
		meta = (ClampMin = 1.0, ToolTip = "Minimum corridor width, world units."))
	float MinThickness = 100.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Corridors",
		meta = (ClampMin = 1.0, ToolTip = "Maximum corridor width, world units (Cave style varies between min and max)."))
	float MaxWidth = 300.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Corridors",
		meta = (ClampMin = 0.0, ClampMax = 1.0, ToolTip = "How much the corridor centerline wanders sideways (0 = straight-ish, 1 = very wavy)."))
	float Waviness = 0.4f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Corridors",
		meta = (ClampMin = 1.0, ToolTip = "Minimum corridor length between rooms, world units."))
	float CorridorLengthMin = 400.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Corridors",
		meta = (ClampMin = 1.0, ToolTip = "Maximum corridor length between rooms, world units."))
	float CorridorLengthMax = 1200.0f;

	// --- Topology extras ---
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 0,
			ToolTip = "Extra loop corridors added beyond the connecting backbone (the shortest non-backbone edges). 0 = pure tree."))
	int32 LoopCount = 0;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 0.0, ToolTip = "Maximum room-center distance eligible for a loop, world units."))
	float LoopMaxDistance = 1500.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 1.0, ToolTip = "Width multiplier for the critical-path (spine) corridors from the start room to the farthest room."))
	float SpineWidthScale = 1.6f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 0, ToolTip = "Number of dead-end corridor stubs to add."))
	int32 DeadEndCount = 0;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 1.0, ToolTip = "Length of a dead-end stub, world units."))
	float DeadEndLength = 500.0f;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 0,
			ToolTip =
				"Number of corridor-link branches to add. Each springs from a corridor and joins the nearest room doorway or another corridor."))
	int32 CorridorLinkCount = 0;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Topology",
		meta = (ClampMin = 1.0, ToolTip = "Maximum length of a corridor-link branch, world units."))
	float LinkMaxDistance = 1200.0f;

	// --- Realization ---
	// NOTE: the rasterization grid cell size is configured on the GeneratedLevelActor (OrganicGridCellSize),
	// since the whole cluster rasterizes into one shared grid.
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Realization",
		meta = (ClampMin = 1, ToolTip = "Wall thickness in cells; non-floor cells farther than this from floor become empty."))
	int32 WallThickness = 1;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Realization",
		meta = (ToolTip = "Run cellular-automata smoothing passes on corridor cells for extra cave feel."))
	bool bSmoothCorridors = false;

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Realization",
		meta = (ClampMin = 1, ClampMax = 6, EditCondition = "bSmoothCorridors", ToolTip = "Number of smoothing passes."))
	int32 SmoothIterations = 2;

	/** Validates and clamps into raw parameters for the generator. */
	FOrganicDungeonResolvedParams Resolve() const;
};
