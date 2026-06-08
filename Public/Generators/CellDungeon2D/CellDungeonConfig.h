#pragma once

// CellDungeonConfig.h
//
// Configuration data for the CellDungeon procedural generator.
//
// Defines room-type descriptions (footprint, doorways, weighting/min/max) and the
// top-level dungeon configuration (start/end/middle room types, corridor/cell size,
// target room count). These are plain C++ structs (NOT USTRUCT) exported for use by
// the generator and its headless verification (automation test + SVG dump).
//
// All randomness is driven through a seeded FRandomStream for full determinism:
// the same seed must always produce an identical placement queue.

#include "CoreMinimal.h"

/**
 * A single doorway on a room footprint, expressed in room-local space.
 * LocalPosition is relative to the footprint CENTER (axis-aligned at rotation 0).
 * LocalOutwardDir is a unit vector pointing out of the room through the doorway.
 */
struct PROCEDURALGEOMETRY_API FCellDoorway
{
	// Room-local position, relative to the footprint CENTER.
	FVector2D LocalPosition = FVector2D::ZeroVector;

	// Unit, room-local outward normal.
	FVector2D LocalOutwardDir = FVector2D(1.f, 0.f);
};

/**
 * Description of one kind of room: its footprint rectangle, doorway set, and the
 * weighting/min/max rules used when expanding the middle-room placement queue.
 */
struct PROCEDURALGEOMETRY_API FCellRoomType
{
	// Full Width,Height (world units), axis-aligned rect at rotation 0.
	FVector2D Footprint = FVector2D(300.f, 300.f);

	// Doorways on this room type, in room-local space.
	TArray<FCellDoorway> Doorways;

	// Selection weight when filling the middle-room queue.
	int32 Weight = 1;

	// Minimum number of instances to place (honored before weighting).
	int32 Min = 0;

	// Maximum number of instances to place; 0 = uncapped.
	int32 Max = 0;
};

/**
 * Top-level configuration for a CellDungeon generation request.
 */
struct PROCEDURALGEOMETRY_API FCellDungeonConfig
{
	// The single start room.
	FCellRoomType StartRoom;

	// The single end room.
	FCellRoomType EndRoom;

	// Pool of middle-room types to draw from.
	TArray<FCellRoomType> MiddleRooms;

	// Corridor width == Voronoi cell size (world units).
	float CorridorSize = 300.f;

	// Coarse PLACEMENT cell size (world units): the spacing of the room-layout Voronoi. 0 = auto, which
	// falls back to the largest room footprint DIMENSION (max of W,H across all room types). No extra gap is
	// added — rooms sit one-per-cell and the router keeps a 1-cell clearance, so cells sized to the largest
	// room already leave room for corridors. Raise this to spread rooms further apart.
	float PlacementCellSize = 0.f;

	// TOTAL rooms including start + end.
	int32 TargetRoomCount = 8;

	/**
	 * Expand MiddleRooms into a placement queue of indices into MiddleRooms, length =
	 * max(0, TargetRoomCount - 2), honoring Min first, then Weight, never exceeding Max
	 * (0 = uncapped). Deterministic given Rng. Implementation lives in the .cpp.
	 */
	TArray<int32> ResolveMiddleQueue(FRandomStream& Rng) const;
};
