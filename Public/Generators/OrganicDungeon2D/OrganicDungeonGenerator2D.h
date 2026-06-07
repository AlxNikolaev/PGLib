#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"
#include "OrganicDungeonGenerator2D.generated.h"

/** Cell type constants for OrganicDungeon grid cells. */
namespace EOrganicCellType
{
	constexpr uint8 Wall = 0;	  // non-floor cell within WallThickness of floor (a real wall)
	constexpr uint8 Corridor = 1; // floor (cave corridor)
	constexpr uint8 Room = 2;	  // floor (room footprint)
	constexpr uint8 Empty = 3;	  // non-floor cell beyond WallThickness — carved away
} // namespace EOrganicCellType

/** A doorway candidate (punch-point) on a room's perimeter, in world space. */
struct PROCEDURALGEOMETRY_API FOrganicDoorway
{
	FVector2D Pos;
	FVector2D OutwardNormal;
	bool	  bUsed = false;

	/** Declared opening width (world units). 0 = legacy/synthetic doorway (no width constraint). */
	float Width = 0.0f;

	/**
	 * True when this doorway was built from a designer-declared FOrganicBakedDoorway (marker-baked).
	 * False for legacy synthetic per-edge punch-points.
	 * Only declared doorways are used as mandatory corridor attachment points.
	 */
	bool bDeclared = false;
};

/** A placed room — a level instance to spawn — in continuous world space. */
struct PROCEDURALGEOMETRY_API FOrganicRoom
{
	FVector2D				Center;
	float					RotationDeg = 0.0f;
	FVector2D				HalfExtent; // half width/height of the footprint
	int32					TypeIndex = -1;
	int32					LocationIndex = 0;							   // which cluster location (subgraph) this room belongs to
	TSoftObjectPtr<UWorld>	RoomLevel;									   // the prefab level instance to spawn at this transform
	FVector2D				FootprintCenterOffset = FVector2D::ZeroVector; // bounds-center offset from level origin (local)
	TArray<FOrganicDoorway> Doorways;
};

/** What a corridor endpoint attaches to. */
enum class EOrganicAnchorType : uint8
{
	Room,	  // a room doorway
	Corridor, // a point on another corridor (T/Y junction)
	Free,	  // an open end (dead-end alcove)
	Junction, // a perimeter attach point on a junction node (FOrganicAnchor::Index -> Junctions)
};

/** A corridor endpoint: what it connects to, plus its world position/outward normal. */
struct PROCEDURALGEOMETRY_API FOrganicAnchor
{
	EOrganicAnchorType Type = EOrganicAnchorType::Free;
	int32			   Index = -1; // room index (Room), corridor index (Corridor), or junction index (Junction); unused for Free
	FVector2D		   Pos;
	FVector2D		   Normal; // outward direction at this endpoint
};

/**
 * A junction — a free network node represented as a slightly-deformed circle in continuous world space.
 * Unlike a room it has no fixed doorways: a corridor attaches at the perimeter point facing its other
 * endpoint (see AttachCorridorToJunction). Junctions are part of the WALKABLE network and are never
 * obstacles for room/corridor collision tests; each junction is one cell in the room-cell diagram and is
 * emitted as a closed deformed-circle boundary spline. Plain C++ struct (never a USTRUCT) — mirrors FOrganicRoom.
 */
struct PROCEDURALGEOMETRY_API FOrganicJunction
{
	FVector2D		  Center;			 // disc center, world space
	float			  Radius = 0.0f;	 // disc radius (world units); clamped >= CellSize so it is never degenerate
	TArray<FVector2D> Perimeter;		 // jittered closed CCW polygon (world space) — the deformed-circle boundary
	int32			  LocationIndex = 0; // which cluster location (subgraph) this junction belongs to

	/**
	 * The two endpoint anchors of the existing corridor this junction was inserted ON (ConnectRoomViaJunction /
	 * StitchRoomViaJunction tap a junction onto a host corridor's curve and branch from it to an otherwise
	 * unreachable room). The junction physically overlaps that host corridor, so in the room-cell diagram it is
	 * adjacent to the host corridor's endpoint nodes as well as to its branched room. BuildDiagramFromLayout uses
	 * these to bridge the junction into the connected component. Empty for unit-test-built junctions (no host).
	 */
	TArray<FOrganicAnchor> HostCorridorAnchors;
};

/** A corridor as a sampled world-space centerline between two anchors. */
struct PROCEDURALGEOMETRY_API FOrganicCorridor
{
	TArray<FVector2D> Centerline;
	TArray<float>	  Radii; // per-sample carve radius (world units), parallel to Centerline
	FOrganicAnchor	  AnchorA;
	FOrganicAnchor	  AnchorB;
	bool			  bIsLoop = false;	// loop generation is removed; kept (permanently false) for cross-module visualizers
	bool			  bIsLink = false;	// a branch that springs from another corridor
	bool			  bIsSpine = false; // part of the critical path (start -> farthest room)
};

/**
 * One end-room hand-off point — the far-endpoint room with its outward doorway. Used for
 * inter-location stitching (chain leaf) and to seed the cluster's single exit. Produced by
 * ComputeChainLeaf; the cluster-scope exit is the merged end room (FOrganicLayout::EndRoomIdx).
 */
struct PROCEDURALGEOMETRY_API FOrganicEndRoomAnchor
{
	/** Index into the rooms array. INDEX_NONE when no end room could be resolved. */
	int32 RoomIndex = INDEX_NONE;

	/** World-space position of the outward doorway (corridor hand-off / portal placement point). */
	FVector2D Pos = FVector2D::ZeroVector;

	/** Outward unit vector at the doorway (away from cluster interior). */
	FVector2D Normal = FVector2D(1.0f, 0.0f);
};

/**
 * Debug/visualization data from the organic dungeon pipeline. NOT a stable production API.
 * Exposes the gridless vector layout (world-space rooms / corridors / junctions) plus the room-cell diagram
 * built directly from it. The OrganicDungeon generator is gridless: there is no rasterized fine grid — each
 * room and junction is one FLayoutCell2D, and adjacency is corridor adjacency (Neighbors), not shared edges.
 */
struct PROCEDURALGEOMETRY_API FOrganicDungeonGridData
{
	// Cell size in world units (the generator's GridSize). Retained because the runtime plumbs it into
	// ConvertLayoutToGridDiagram as FVoronoiGridDiagram::GridCellSize, which drives the zone-allocation length
	// threshold AND the synthetic corridor-mouth length in FVoronoiGridDiagram::GetSharedEdge.
	float					 CellSize = 0.0f;
	TArray<FOrganicRoom>	 Rooms;		// world-space placed rooms
	TArray<FOrganicCorridor> Corridors; // world-space corridors
	TArray<FOrganicJunction> Junctions; // world-space free junction nodes (deformed-circle network hubs)

	// Requested constraint counts (what the location asset asked for), carried alongside the achieved geometry
	// so the diagnostics dump can compute achieved-vs-requested without re-resolving the config. Each mirrors the
	// RequestedRoomCount precedent and is summed across chained segments at the merge site.
	int32 RequestedRoomCount = 0;
	int32 RequestedDeadEndCount = 0; // total DeadEndCount requested across segments

	// Transition metadata: the single entrance room and the single exit room (graph-diameter endpoints).
	int32 StartRoomIndex = -1;
	int32 EndRoomIndex = -1; // cluster-wide exit room (the far endpoint opposite StartRoomIndex)

	// Per-location (chained-segment) start-room global index, in cluster location order. Used to seed each
	// location's zone allocation / debug label inside its own start room instead of the shared cluster center.
	TArray<int32> LocationStartRoomIndex;

	FLayoutDiagram2D Diagram; // room-cell diagram: one cell per room/junction, adjacency = corridors
};

/**
 * Mutable working state for a single GenerateLocationSubgraph build. Holds the rooms/corridors/junctions and
 * the bookkeeping (open list, queue cursor, spine/diameter indices, placement-spawn-tree edges/adjacency,
 * per-build stat counters) that the placement → connectivity phases thread through in order. Defined in the
 * .cpp; only the subgraph phase helpers of UOrganicDungeonGenerator2D touch it.
 */
struct FOrgSubgraphBuild;

/** Continuous-space layout (rooms + corridors + entrance + exit anchors) — the vector layout the diagram is built from. Internal. */
struct PROCEDURALGEOMETRY_API FOrganicLayout
{
	TArray<FOrganicRoom>	 Rooms;
	TArray<FOrganicCorridor> Corridors;
	TArray<FOrganicJunction> Junctions;
	int32					 StartRoomIdx = -1;
	int32					 EndRoomIdx = -1; // chain-leaf / cluster exit room (far endpoint opposite StartRoomIdx)
	int32					 RequestedRoomCount = 0;
	int32					 RequestedDeadEndCount = 0; // DeadEndCount requested for this segment
	int32					 PlacedCount = 0;
	TArray<int32>			 LocationStartRoomIndex; // per chained segment: global index of its start room
};

UCLASS()
class PROCEDURALGEOMETRY_API UOrganicDungeonGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	FOrganicDungeonResolvedParams		  Params;	// first/only segment (back-compat)
	TArray<FOrganicDungeonResolvedParams> Segments; // chained segments (>=1); each is one OD location

public:
	UOrganicDungeonGenerator2D();

	// Covariant base overrides
	virtual UOrganicDungeonGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UOrganicDungeonGenerator2D* SetSeed(const FString& InSeed) override;
	virtual UOrganicDungeonGenerator2D* SetGridSize(int32 InSize) override;
	virtual UOrganicDungeonGenerator2D* SetCenter(const FVector2D& InCenter) override;

	/** Applies a fully resolved parameter set (single segment). */
	UOrganicDungeonGenerator2D* ApplyResolvedParams(const FOrganicDungeonResolvedParams& InParams);

	/** Applies an ordered list of segment params — the dungeon is generated as chained segments (one per OD location). */
	UOrganicDungeonGenerator2D* ApplyResolvedParamsList(const TArray<FOrganicDungeonResolvedParams>& InSegments);

	// Generation
	virtual FLayoutDiagram2D Generate() override;

	/** Returns full intermediate data (rooms, corridors, junctions + the room-cell diagram). For visualization/testing only. */
	FOrganicDungeonGridData GenerateWithGridData();

	/**
	 * Pure orientation solver for declared-doorway rooms: finds a rotation θ and a doorway assignment
	 * that maps every mandatory connection direction to a distinct declared doorway within tolerance.
	 *
	 * NOTE: pipeline-unused. The connectivity pipeline now uses the placement-spawn tree as its backbone
	 * (rooms grow already-doorway-aligned, so there is no orientation-solve-or-drop stage). This static
	 * method is retained for its direct unit-test coverage of the orientation-assignment math.
	 *
	 * Candidate rotations are generated by aligning each declared doorway, in turn, to the primary
	 * mandatory connection (first entry in MandatoryDirs), then testing whether the remaining mandatory
	 * directions each match a distinct unused declared doorway within AngularToleranceDeg.
	 *
	 * @param Declared           Local-space baked doorways for this room type.
	 * @param MandatoryDirs      World-space unit vectors toward each MST neighbour (and optional exit).
	 * @param AngularToleranceDeg  Maximum angle between a rotated doorway outward-dir and a mandatory dir.
	 * @param CandidateRotations   θ values to try, in order (generated by caller from declared dirs + primaryDir).
	 * @param OutRotationDeg       Best rotation found.
	 * @param OutDoorwayForConnection  Per mandatory-dir index → declared-doorway index (parallel to MandatoryDirs).
	 * @return true if a feasible assignment was found; false when connections > doorways or no rotation aligns.
	 */
	static bool SolveRoomOrientation(const TArray<FOrganicBakedDoorway>& Declared,
		const TArray<FVector2D>&										 MandatoryDirs,
		float															 AngularToleranceDeg,
		TArrayView<const float>											 CandidateRotations,
		float&															 OutRotationDeg,
		TArray<int32>&													 OutDoorwayForConnection);

	/**
	 * Densely samples a corridor's few-point centerline into a smooth polyline on demand (Catmull-Rom
	 * through the control points, doorway-normal phantom endpoints). Used by collision and the spline
	 * emitters so they follow the visible curve while the stored Centerline stays few-point.
	 *
	 * For two control points this returns a straight evenly-stepped lerp identical to the legacy dense
	 * centerline. Pure math: no UObject, no FRandomStream draws (deterministic).
	 *
	 * @param ControlPoints  The stored few-point centerline (>=2 points).
	 * @param StartTangent   Outward doorway normal at the start (phantom pre-endpoint direction).
	 * @param EndTangent     Outward doorway normal at the end (phantom post-endpoint direction).
	 * @param StepLen        Approximate sample spacing in world units (e.g. CellSize*0.5).
	 * @param OutDense       Filled with the dense sampled polyline (always includes both endpoints).
	 */
	static void SampleCorridorCurve(const TArray<FVector2D>& ControlPoints,
		const FVector2D&									 StartTangent,
		const FVector2D&									 EndTangent,
		float												 StepLen,
		TArray<FVector2D>&									 OutDense);

	/**
	 * Builds a junction node at Center: a slightly-deformed circle sized from the corridor width range.
	 * Diameter is drawn from RandomStream.FRandRange(MinThickness, MaxWidth); Radius is clamped to at least
	 * one grid cell so the disc/spline are never degenerate. The Perimeter is a closed CCW polygon whose
	 * vertices sit at Radius scaled by a small per-vertex jitter, so PCG can build a wobbly junction floor.
	 * Draws RandomStream (one diameter draw + one jitter draw per perimeter vertex) — call only on the
	 * connectivity fallback path so the draw sequence stays deterministic.
	 *
	 * Exposed publicly (mirroring SampleCorridorCurve/SolveRoomOrientation) so the junction unit tests can
	 * build a deterministic junction directly. The pipeline calls it from ConnectRoomViaJunction /
	 * StitchRoomViaJunction to insert an on-demand junction on an existing corridor and branch to an otherwise
	 * unreachable room.
	 */
	FOrganicJunction MakeJunction(const FOrganicDungeonResolvedParams& Params, const FVector2D& Center);

	/**
	 * Picks where a corridor heading toward Toward attaches to junction J: the perimeter vertex whose
	 * direction from the junction center best matches Toward. Pure geometry — no RandomStream draws.
	 *
	 * @param J                The junction to attach to.
	 * @param Toward           World-space direction from the junction toward the corridor's other endpoint.
	 * @param OutAttachPos     The chosen perimeter attach point (world space).
	 * @param OutOutwardNormal Outward unit normal at the attach point (from center through the vertex).
	 */
	static void AttachCorridorToJunction(const FOrganicJunction& J, const FVector2D& Toward, FVector2D& OutAttachPos, FVector2D& OutOutwardNormal);

private:
	FOrganicDungeonGridData GenerateInternal();

	/**
	 * Generates one location's subgraph (placement + its own topology) in world space around CenterPoint.
	 * Placement avoids every room in Obstacles (rooms already placed by other locations), so the combined
	 * cluster graph never overlaps. Returns the subgraph with local (0-based) room/corridor indices.
	 */
	FOrganicLayout GenerateLocationSubgraph(
		const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles);

	/**
	 * Builds a cave/clean corridor linking one location's end room to the next location's start room. The
	 * stitch is MANDATORY: if its curve would cross a third room body it reroutes around the blocker with a
	 * single nearest-corner waypoint; only when the reroute still clips does it drop (writing bOutProduced =
	 * false and logging a connectivity gap). AllRooms is the merged cluster room array (FromIdx/ToIdx index
	 * into it) used as the obstacle set. The caller must skip adding the corridor when bOutProduced is false.
	 */
	FOrganicCorridor BuildInterLocationCorridor(
		TArray<FOrganicRoom>& AllRooms, int32 FromIdx, int32 ToIdx, const FOrganicDungeonResolvedParams& LinkParams, bool& bOutProduced);

	/**
	 * Cluster-scope junction-on-demand fallback for the inter-location stitch: when BuildInterLocationCorridor
	 * drops (its single-waypoint reroute still clipped a room), this guarantees the dropped location still joins
	 * the cluster region by inserting a junction on the nearest existing merged corridor and branching a
	 * room-avoiding corridor to ToRoom's best outward doorway. Mirrors ConnectRoomViaJunction but works on the
	 * merged cluster arrays (AllRooms / Corridors / Junctions). Honors T5 room-avoidance. Returns true on
	 * success; false if no merged corridor could host a clean junction branch.
	 */
	bool StitchRoomViaJunction(TArray<FOrganicRoom>& AllRooms,
		TArray<FOrganicCorridor>&					 Corridors,
		TArray<FOrganicJunction>&					 Junctions,
		int32										 ToRoomIdx,
		const FOrganicDungeonResolvedParams&		 LinkParams);

	/**
	 * Shared Bezier corridor builder — called by both GenerateLocationSubgraph (via a thin BuildCorridor
	 * wrapper lambda) and BuildInterLocationCorridor. Callers are responsible for pre-computing
	 * MinRadius/MaxRadius from their own param context (e.g. inter-location links clamp MinRadius to
	 * CellSize so the corridor is never narrower than one grid cell).
	 *
	 * @param AP/AN      Start doorway world position and outward normal.
	 * @param BP/BN      End doorway world position and outward normal.
	 * @param Waviness   Control-point lateral offset scale in [0, 1].
	 * @param MinRadius  Minimum corridor half-width (world units).
	 * @param MaxRadius  Maximum corridor half-width (world units).
	 * @param Style      Clean (constant width) or Cave (variable noise width).
	 * @param RadiusScale Uniform multiplier applied to every radius sample (used for spine widening).
	 *
	 * The number of intermediate perpendicular control points is drawn internally per corridor from this
	 * generator's RandomStream (a single RandRange(2, 3) per call) — every corridor randomly bows with 2 or 3
	 * intermediate points for variety. The stored Centerline/Radii stay few-point; collision and the spline
	 * emitters sample the curve on demand. The draw keeps run-to-run determinism (same seed -> same output).
	 */
	FOrganicCorridor BuildBezierCorridor(const FVector2D& AP,
		const FVector2D&								  AN,
		const FVector2D&								  BP,
		const FVector2D&								  BN,
		float											  Waviness,
		float											  MinRadius,
		float											  MaxRadius,
		EOrganicCorridorStyle							  Style,
		float											  RadiusScale = 1.0f);

	/**
	 * Builds the room-cell diagram DIRECTLY from the merged cluster's vector layout (gridless — no rasterization).
	 * Each room becomes one FLayoutCell2D (Vertices = its 4 OBB corners) and each junction becomes one cell
	 * (Vertices = its deformed-circle perimeter). Cell Neighbors are corridor adjacency (the indices of the nodes a
	 * cell shares a corridor with), so the disjoint cells still drive FLocation zone allocation / transitions via
	 * FVoronoiGridDiagram::GetSharedEdge's corridor-aware synthetic-mouth fallback. Pure geometry — no RandomStream
	 * draws, so the diagram step is deterministic.
	 */
	FOrganicDungeonGridData BuildDiagramFromLayout(const FOrganicLayout& Layout);

	// --- Isolated algorithmic stages (stateless; can be called and tested independently) ---

	/**
	 * Builds the placement queue of room type indices, interleaving types to avoid long same-type runs.
	 * When bShuffleRoomOrder is set, picks randomly weighted by remaining count; otherwise uses a
	 * deterministic most-remaining-first spread.
	 * @return Total number of rooms requested across all room types.
	 */
	static int32 BuildRoomQueue(const FOrganicDungeonResolvedParams& Params, FRandomStream& RandomStream, TArray<int32>& OutQueue);

	/**
	 * BFS-based graph-diameter search over a node adjacency list (the placement-spawn tree).
	 * Identifies the spine (critical path): the path between the two farthest rooms.
	 * Start room = diameter endpoint closer to CenterPoint (the level entrance).
	 * End room   = the farther endpoint (the exit / hand-off point for the next location).
	 *
	 * Used ONLY to choose start/end endpoints + spine edges over the spawn-tree backbone — never to decide
	 * connectivity (that is the spawn tree itself) and never to drop a room.
	 *
	 * @param Adj              Symmetric adjacency list (Adj[i] = neighbours of room i); the spawn-tree adjacency.
	 * @param OutStartRoomIdx  Entrance room index.
	 * @param OutEndRoomIdx    Exit room index.
	 * @param OutSpineEdges    Packed edge keys (see EdgeKey in .cpp) for every edge on the path.
	 */
	static void FindSpine(const TArray<FOrganicRoom>& Rooms,
		const TArray<TArray<int32>>&				  Adj,
		const FVector2D&							  CenterPoint,
		int32&										  OutStartRoomIdx,
		int32&										  OutEndRoomIdx,
		TSet<uint64>&								  OutSpineEdges);

	// --- GenerateLocationSubgraph phase helpers (instance; share RandomStream/GridSize through `this`) ---
	// These are a behavior-preserving decomposition of GenerateLocationSubgraph: each consumes the build
	// context in place and is invoked in a fixed order so the FRandomStream draw sequence is unchanged.

	/**
	 * Generates a room's doorways from its current Center/RotationDeg. Declared-doorway rooms transform
	 * each baked opening local→world; legacy rooms fall back to one synthetic centered punch-point per
	 * footprint edge. Room.RotationDeg must already be set.
	 */
	void GenerateDoorways(FOrganicRoom& Room, const FOrganicDungeonResolvedParams& Params) const;

	/** Builds a fully-populated room (type/center/rotation/footprint + doorways) for the given type. */
	FOrganicRoom MakeRoom(const FOrganicDungeonResolvedParams& Params, int32 TypeIdx, const FVector2D& Center, float RotDeg) const;

	/**
	 * Layer 2 — continuous placement. Seeds the first room at CenterPoint (pushed clear of Obstacles),
	 * then grows rooms from open-list doorways while avoiding all placed rooms and obstacles. Records each
	 * parent→child spawn edge (with the parent doorway it grew off) into Ctx.SpawnEdges so ConnectRooms can
	 * use the spawn tree as the connectivity backbone. Mutates Ctx.Rooms/OpenList/QueueIdx/SpawnEdges and the
	 * retry/backtrack/placed stat counters. Draws RandomStream in the existing order (source doorway pick,
	 * corridor length, branch test, room rotation) — recording spawn edges adds no draws.
	 */
	void PlaceRooms(FOrgSubgraphBuild&		 Ctx,
		const FOrganicDungeonResolvedParams& Params,
		const FVector2D&					 CenterPoint,
		const TArray<FOrganicRoom>&			 Obstacles,
		const TArray<int32>&				 Queue,
		int32								 RequestedRoomCount);

	/**
	 * Layer 1 — connectivity. Uses the placement-spawn tree recorded by PlaceRooms as the connectivity
	 * backbone (no geometric MST, no room drops): builds the spawn-tree adjacency, runs FindSpine over it
	 * for start/end endpoints + spine edges, then carves one backbone corridor per spawn edge (spine widened).
	 *
	 * Connectivity is then GUARANTEED count-free: a connected set is seeded from the entrance room and grown
	 * over the carved corridors. Every room not yet in the set (iterated in fixed ascending index order) is
	 * reconnected — first by retrying a free/aligned-doorway corridor to its spawn parent or the nearest
	 * connected room, and, failing that, by INSERTING an on-demand junction onto the nearest existing corridor
	 * and branching a corridor from the junction to the room (junctions have unlimited ports, so this always
	 * succeeds). The result is ONE connected walkable region — the all-single-door "star" connects. No loop /
	 * link / dead-end / junction COUNTS drive any of this. Mutates the rooms/corridors/junctions/spawn-adjacency
	 * working state, the start/end diameter indices, and the spine stat counter. Honors T5 room-avoidance on
	 * every carve (backbone, reconnection, and junction branch). Only runs its body when Ctx.Rooms has >= 1 room.
	 */
	void ConnectRooms(
		FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles);

	/**
	 * Junction-on-demand fallback used by ConnectRooms (and the inter-location stitch) when a room cannot reach
	 * the connected network through a free/aligned doorway. Scans every existing corridor's sampled curve for the
	 * point nearest RoomIdx's best outward doorway (pure geometry, deterministic ascending tie-break), inserts a
	 * junction there via MakeJunction, and branches a room-avoiding corridor (BuildBezierCorridor +
	 * CorridorClearsRooms) from the junction perimeter to the room doorway. The tapped corridor is NOT physically
	 * split — the junction is recorded as a first-class FOrganicJunction node and the new branch anchors to it
	 * (EOrganicAnchorType::Junction), which is sufficient for corridor-adjacency connectivity. MakeJunction is the
	 * lone FRandomStream draw on this path. Returns true on success (room joined the network); false only if no
	 * existing corridor could host a clean junction branch (the caller then leaves the room logged but kept).
	 */
	bool ConnectRoomViaJunction(FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, int32 RoomIdx);

	/**
	 * Bootstrap connectivity fallback for the all-single-door "star": connects an orphan room to an already
	 * connected room by inserting a free junction in the open space BETWEEN their two outward doorways and
	 * branching BOTH rooms to it (ConnectedRoom→Junction and Orphan→Junction). Unlike ConnectRoomViaJunction —
	 * which taps an EXISTING corridor — this needs no prior corridor, so it succeeds even when ConnectRooms has
	 * carved nothing yet (every room-to-room carve failed because the single doorways are misaligned). The
	 * junction's two branch corridors anchor as Junction↔Room, so BuildDiagramFromLayout's corridor-adjacency
	 * pass wires ConnectedRoom—Junction—Orphan directly. Branches prefer to clear third rooms but, since this is
	 * the guaranteed-connectivity last resort, are accepted (with a logged shortfall) rather than dropped if a
	 * tight cluster leaves no clean path. MakeJunction is the lone FRandomStream draw. Returns true unless the
	 * orphan has no usable doorway (degenerate room).
	 */
	bool ConnectRoomsViaSharedJunction(FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, int32 ConnectedRoom, int32 Orphan);

	/**
	 * Computes the chain-leaf end-room anchor (the diameter endpoint opposite the entrance) used for
	 * inter-location stitching and as the segment's exit room. Performs the entrance-room prefab swap when
	 * bHasStartRoom is set and the exit-room prefab swap when bHasEndRoom is set. The doorway is NOT reserved.
	 */
	FOrganicEndRoomAnchor ComputeChainLeaf(FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint);

	/**
	 * Carves a corridor between two rooms via their best-facing doorways, consuming those doorways and
	 * appending to Ctx.Corridors. AvoidPolicy controls how a room-body crossing is handled (rooms are HARD
	 * obstacles; junctions and other corridors are not):
	 *   None    — no room test (legacy / explicitly-unchecked callers).
	 *   Reroute — MANDATORY edge: insert one nearest-corner waypoint past the blocking room and rebuild the
	 *             few-point curve through it, retry once; drop only as a last resort (logged + StatMandatoryDrops).
	 *   Drop    — OPTIONAL edge: discard silently on a crossing (the historic reject behavior).
	 * An endpoint-room body crossing beyond the shallow doorway approach span is always rejected (a reroute
	 * waypoint cannot fix a corridor that traverses its own endpoint room's interior). Returns true if a
	 * corridor was carved (incrementing StatReroutes when a reroute succeeded).
	 *
	 * AvoidPolicy is an int taking values of the file-private ECorridorAvoidPolicy enum (kept opaque in the
	 * header so the enum stays in the .cpp anonymous namespace alongside the collision primitives).
	 */
	bool MakeRoomCorridor(FOrgSubgraphBuild& Ctx,
		const FOrganicDungeonResolvedParams& Params,
		int32								 A,
		int32								 B,
		bool								 bLoop,
		bool								 bSpine,
		float								 RadiusScale,
		int32								 AvoidPolicy);
};
