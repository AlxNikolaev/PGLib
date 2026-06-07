#pragma once

#include "CoreMinimal.h"
#include "OrganicDungeonConfig.generated.h"

class UWorld;

/**
 * A single OD doorway: a declared opening on a room footprint where a corridor may connect.
 * Authored directly on the Location asset's room config (FOrganicRoomType::BakedDoorways), so a room
 * advertises its walkable openings as data instead of via ADoorwayMarker actors baked from the room .umap.
 *
 * Coordinate convention: prefab-level-origin-local XY (same space the OD generator uses).
 *   LocalPosition   – center of the doorway opening.
 *   LocalOutwardDir – unit vector pointing away from the room interior (a corridor approaches from here).
 */
USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FOrganicBakedDoorway
{
	GENERATED_BODY()

	/**
	 * Center of the doorway opening in prefab-level-local XY space.
	 * Authored visually via the Location preview viewport's Doorway Authoring mode (read-only here).
	 */
	UPROPERTY(VisibleAnywhere,
		BlueprintReadWrite,
		Category = "Doorway",
		meta = (ToolTip = "Center of the doorway opening in room-local XY (world units). Authored in the viewport."))
	FVector2D LocalPosition = FVector2D::ZeroVector;

	/**
	 * Outward normal (unit vector, local XY): the direction a corridor approaches from.
	 * Authored visually via the Location preview viewport's Doorway Authoring mode (read-only here).
	 */
	UPROPERTY(VisibleAnywhere,
		BlueprintReadWrite,
		Category = "Doorway",
		meta = (ToolTip =
					"Outward normal in room-local XY (need not be normalized): the direction a corridor approaches from. Authored in the viewport."))
	FVector2D LocalOutwardDir = FVector2D(1.0f, 0.0f);

	/**
	 * Clear opening width in world units. Corridor radius is clamped to Width/2 at this doorway.
	 * Authored visually via the Location preview viewport's Doorway Authoring mode (read-only here).
	 */
	UPROPERTY(VisibleAnywhere,
		BlueprintReadWrite,
		Category = "Doorway",
		meta = (ClampMin = 10.0,
			ToolTip = "Clear opening width in world units. Corridor radius is clamped to Width/2 at this doorway. Authored in the viewport."))
	float Width = 100.0f;
};

/**
 * How an exit anchor is realized at the end of an OD cluster corridor.
 * PortalRoomPrefab places the designer-authored ExitPortalRoom prefab level at the anchor room.
 * PortalStub leaves a bare corridor dead-end; the runtime drops an APortal actor at the doorway.
 */
UENUM(BlueprintType)
enum class EOrganicTerminusForm : uint8
{
	PortalRoomPrefab UMETA(DisplayName = "Portal Room Prefab",
		ToolTip = "Swap in the ExitPortalRoom level instance at the anchor room (requires ExitPortalRoom to be set)."),
	PortalStub UMETA(DisplayName = "Portal Stub", ToolTip = "Leave the dead-end bare; the runtime drops an APortal actor at the outward doorway."),
};

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

	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Room Type",
		meta = (ToolTip = "Manual footprint in world units. If either axis is <= 0 it is measured from RoomLevel's bounds instead."))
	FVector2D FootprintOverride = FVector2D::ZeroVector;

	/**
	 * Authored doorway openings for this room, defined as data on the Location asset.
	 * Each entry is a declared opening (XY position + outward dir + width) in room-local space.
	 * When non-empty, the generator uses these declared openings instead of synthesising a single
	 * per-edge punch-point, ensuring corridors connect to real walkable doorway geometry.
	 * When empty, the generator falls back to exactly one synthetic doorway per footprint edge.
	 *
	 * Authored exclusively in-editor via the Location asset's preview viewport Doorway Authoring mode
	 * (doorway handles write their transforms here). Read-only in the Details panel — do not hand-edit.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Room Type", meta = (TitleProperty = "Width"))
	TArray<FOrganicBakedDoorway> BakedDoorways;
};

/** A resolved room type with a concrete footprint. Plain C++ struct. */
struct PROCEDURALGEOMETRY_API FOrganicResolvedRoomType
{
	TSoftObjectPtr<UWorld> RoomLevel;
	int32				   Count = 0;  // resolved absolute room count (set by ResolveForTotal)
	int32				   Weight = 1; // relative weight carried from FOrganicRoomType (for ResolveForTotal)
	int32				   Min = 0;	   // mandatory minimum carried from FOrganicRoomType (for ResolveForTotal)
	int32				   Max = 0;	   // cap carried from FOrganicRoomType (for ResolveForTotal; 0 = uncapped)
	float				   FootprintWidth = 600.0f;
	float				   FootprintHeight = 600.0f;
	FVector2D			   FootprintCenter = FVector2D::ZeroVector; // bounds center offset from level origin (XY)
	FName				   DisplayName;

	/**
	 * Baked doorway declarations copied from FOrganicRoomType::BakedDoorways by ResolveRoomFootprint().
	 * Non-empty → "declared-doorway" room: generator uses only these openings (no synthetic per-edge candidates).
	 * Empty → "legacy" room: generator synthesises doorways from the footprint bounding box (back-compat).
	 */
	TArray<FOrganicBakedDoorway> Doorways;
};

/** Resolved organic-dungeon parameters. Plain C++ struct — NOT a USTRUCT. */
struct PROCEDURALGEOMETRY_API FOrganicDungeonResolvedParams
{
	TArray<FOrganicResolvedRoomType> RoomTypes;

	// Special entrance room placed at the graph-diameter start endpoint (optional).
	bool					 bHasStartRoom = false;
	FOrganicResolvedRoomType StartRoom;

	// Exit terminus configuration: portal-room prefab for PortalRoomPrefab anchors (optional).
	bool					 bHasExitPortalRoom = false;
	FOrganicResolvedRoomType ExitPortalRoom;
	EOrganicTerminusForm	 ExitTerminusForm = EOrganicTerminusForm::PortalStub;

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
		meta = (ToolTip = "Optional entrance prefab, placed at the graph-diameter start endpoint."))
	FOrganicRoomType StartRoom;

	/**
	 * Default terminus form for exit anchors — how each out-portal attachment point is realized.
	 * PortalStub (default): bare corridor dead-end; the runtime drops an APortal actor at the doorway.
	 * PortalRoomPrefab: swap in the ExitPortalRoom prefab level at each anchor room.
	 * Designers opt into PortalRoomPrefab by also setting ExitPortalRoom.
	 */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Rooms",
		meta = (ToolTip = "How exit anchor doorways are realized (portal-room prefab or bare portal stub)."))
	EOrganicTerminusForm DefaultExitTerminusForm = EOrganicTerminusForm::PortalStub;

	/**
	 * Optional portal-room prefab placed at PortalRoomPrefab exit anchors.
	 * Small room sized to a portal footprint with baked doorway markers.
	 * When RoomLevel is null, all anchors fall back to PortalStub.
	 */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Organic Dungeon|Rooms",
		meta = (ToolTip = "Portal-room prefab level placed at each PortalRoomPrefab exit anchor. Requires baked doorway markers."))
	FOrganicRoomType ExitPortalRoom;

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

	/**
	 * Like Resolve(), but distributes exactly TotalRooms regular rooms across types proportionally to their
	 * Count (used as a relative weight). Start/end rooms are unaffected. This is the preferred entry point
	 * when total room count is driven by Location Size on the level graph node, removing the need to
	 * manually set per-type counts.
	 */
	FOrganicDungeonResolvedParams ResolveForTotal(int32 TotalRooms) const;

	/**
	 * Drops the cached footprint for a single prefab level so the next Resolve() re-measures it from the
	 * level's current bounds. Measuring a footprint loads the prefab UWorld synchronously, so results are
	 * cached per level path; this clears one stale entry. Thread-safe.
	 *
	 * Safe to call from editor modules (which depend on ProceduralGeometry) on prefab save, asset reimport,
	 * or preview re-roll, so designers see footprint changes take effect without an editor restart.
	 *
	 * @param LevelPath  Soft object path of the prefab UWorld (FOrganicRoomType::RoomLevel.ToSoftObjectPath()).
	 */
	static void InvalidateFootprintCache(const FSoftObjectPath& LevelPath);

	/**
	 * Drops every cached footprint so all prefab levels are re-measured on the next Resolve(). Thread-safe.
	 * Safe to call from editor modules on bulk reimport or when many prefabs may have changed.
	 */
	static void InvalidateAllFootprints();

	/**
	 * Pure class-level test: true if a primitive of this component class is excluded from a prefab's measured
	 * footprint regardless of its runtime state. Excludes trigger shapes (UShapeComponent and subclasses),
	 * spline path guides (USplineComponent), and editor markers / visualization gizmos
	 * (UArrowComponent / UBillboardComponent / UTextRenderComponent). Everything else (meshes) is included.
	 *
	 * This is the class portion of the footprint filter, exposed so it can be unit-tested without loading a
	 * UWorld. The full per-component filter additionally drops unregistered and editor-only instances.
	 */
	static bool IsFootprintExcludedComponentClass(const UClass* ComponentClass);
};
