#pragma once

// CellDungeonGenerator2D.h
//
// The CellDungeon procedural generator.
//
// Discretizes space into Voronoi cells, marks room-occupied cells, then routes
// corridors ONLY through EMPTY/CORRIDOR cells via cell-graph pathfinding. This yields
// overlap-free, fully-connected room+corridor layouts even when every room has a single
// doorway, because corridors branch through empty/corridor cells that act as cell-graph
// junctions (so a room's doorway count never limits overall connectivity).
//
// Built entirely inside the ProceduralGeometry module and verified headlessly. This file
// declares the result/state types and the UObject generator; implementation is in the .cpp.

#include "CoreMinimal.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "Generators/CellDungeon2D/CellDungeonConfig.h"
#include "CellDungeonGenerator2D.generated.h"

/** Per-cell occupancy classification on the Voronoi substrate. */
enum class ECellState : uint8
{
	Empty,
	RoomOccupied,
	Corridor,
	Blocked
};

/**
 * A room placed onto the cell substrate, with its world-space doorway data and the
 * cell indices it occupies. Doorway arrays are parallel (one entry per doorway).
 */
struct PROCEDURALGEOMETRY_API FCellPlacedRoom
{
	// World-space footprint center.
	FVector2D Center = FVector2D::ZeroVector;

	// World-space rotation in degrees.
	float RotationDeg = 0.f;

	// Full Width,Height (world units) of the footprint.
	FVector2D Footprint = FVector2D::ZeroVector;

	// -2 = start, -3 = end, >= 0 = index into Config.MiddleRooms.
	int32 TypeIndex = -1;

	// Cell array indices this room covers.
	TArray<int32> OccupiedCells;

	// World-space doorway position (parallel per doorway).
	TArray<FVector2D> DoorwayPos;

	// World-space outward unit direction (parallel per doorway).
	TArray<FVector2D> DoorwayDir;

	// First non-room cell outside the footprint along DoorwayDir; INDEX_NONE if invalid.
	TArray<int32> DoorwayAnchorCell;

	// Whether each doorway has been consumed by a corridor.
	TArray<bool> DoorwayUsed;
};

/**
 * Full output of a CellDungeon generation pass: the Voronoi substrate, per-cell state,
 * placed rooms, carved corridor paths, and start/end indices.
 */
struct PROCEDURALGEOMETRY_API FCellDungeonResult
{
	// The substrate (cells indexed 0..N-1).
	FVoronoiDiagram2D Diagram;

	// Parallel to Diagram.Cells.
	TArray<ECellState> CellState;

	// Room index occupying a RoomOccupied cell, else INDEX_NONE.
	TArray<int32> CellRoomIndex;

	// All placed rooms.
	TArray<FCellPlacedRoom> Rooms;

	// Each entry = ordered cell indices of one carved corridor.
	TArray<TArray<int32>> CorridorPaths;

	// One corridor cell per room (just outside its base doorway) — the ONLY corridor cells allowed to
	// touch a room. Every other corridor cell keeps a 1-cell gap from room walls.
	TArray<int32> CorridorSeeds;

	// Index into Rooms of the start room.
	int32 StartRoomIndex = -1;

	// Index into Rooms of the end room.
	int32 EndRoomIndex = -1;

	// The TargetRoomCount that was requested.
	int32 RequestedRoomCount = 0;

	// True if generation produced a valid, fully-connected layout.
	bool bValid = false;
};

/**
 * UObject generator for CellDungeon layouts. Configure via the fluent setters, then call
 * Generate(). Determinism is guaranteed: the same seed produces an identical result.
 */
UCLASS()
class PROCEDURALGEOMETRY_API UCellDungeonGenerator2D : public UObject
{
	GENERATED_BODY()

public:
	// Sets the deterministic seed string; returns this for fluent chaining.
	UCellDungeonGenerator2D* SetSeed(const FString& InSeed);

	// Sets the world-space generation bounds; returns this for fluent chaining.
	UCellDungeonGenerator2D* SetBounds(const FBox2D& InBounds);

	// Sets the dungeon configuration; returns this for fluent chaining.
	UCellDungeonGenerator2D* SetConfig(const FCellDungeonConfig& InConfig);

	// Runs the full generation pipeline and returns the result.
	FCellDungeonResult Generate();

private:
	FString Seed;

	FBox2D Bounds = FBox2D(FVector2D(-5000.f, -5000.f), FVector2D(5000.f, 5000.f));

	FCellDungeonConfig Config;

	FRandomStream Rng;

	// --- Private helpers (implemented in the .cpp) ---

	// Builds the Voronoi substrate (relaxed cells) for the current bounds/seed/cell size.
	FVoronoiDiagram2D BuildSubstrate() const;

	// Estimates the number of Voronoi sites needed to fill Bounds at CorridorSize cell size.
	int32 EstimateSiteCount() const;

	// Marks the cells covered by a room footprint as RoomOccupied; records OccupiedCells.
	void MarkRoomCells(FCellDungeonResult& Result, FCellPlacedRoom& Room, int32 RoomIndex) const;

	// Tests whether the cells a candidate room would occupy are all currently Empty.
	bool CanPlaceRoom(const FCellDungeonResult& Result, const FCellPlacedRoom& Room) const;

	// Computes world-space doorway positions/dirs and resolves doorway anchor cells.
	void ResolveDoorways(const FCellDungeonResult& Result, FCellPlacedRoom& Room) const;

	// Attempts to place all rooms (start, middle queue, end) without overlap.
	bool PlaceRooms(FCellDungeonResult& Result);

	// Routes corridors between room doorway anchors through Empty/Corridor cells only,
	// carving (marking Corridor) along discovered paths; returns true if fully connected.
	bool RouteCorridors(FCellDungeonResult& Result);

	// Cell-graph pathfinding through Empty/Corridor cells (excludes RoomOccupied/Blocked,
	// except the explicitly allowed start/goal cells). Returns ordered cell indices, empty if none.
	TArray<int32> FindCellPath(const FCellDungeonResult& Result, int32 StartCell, int32 GoalCell) const;
};
