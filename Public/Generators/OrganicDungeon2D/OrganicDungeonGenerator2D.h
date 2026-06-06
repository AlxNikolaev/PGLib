#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"
#include "Generators/OrganicDungeon2D/OrganicFloorTypes.h"
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
};

/** A corridor endpoint: what it connects to, plus its world position/outward normal. */
struct PROCEDURALGEOMETRY_API FOrganicAnchor
{
	EOrganicAnchorType Type = EOrganicAnchorType::Free;
	int32			   Index = -1; // room index, or corridor index, depending on Type
	FVector2D		   Pos;
	FVector2D		   Normal; // outward direction at this endpoint
};

/** A corridor as a sampled world-space centerline between two anchors. */
struct PROCEDURALGEOMETRY_API FOrganicCorridor
{
	TArray<FVector2D> Centerline;
	TArray<float>	  Radii; // per-sample carve radius (world units), parallel to Centerline
	FOrganicAnchor	  AnchorA;
	FOrganicAnchor	  AnchorB;
	bool			  bIsLoop = false;
	bool			  bIsLink = false;	// a branch that springs from another corridor
	bool			  bIsSpine = false; // part of the critical path (start -> farthest room)
};

/**
 * One cluster exit anchor — a far-leaf room with a reserved outward doorway that serves as
 * an out-portal attachment point. Produced by SelectExitAnchors; stored on FOrganicDungeonGridData.
 */
struct PROCEDURALGEOMETRY_API FOrganicExitAnchor
{
	/** Index into FOrganicDungeonGridData::Rooms. INDEX_NONE for a pure dead-end stub with no room. */
	int32 RoomIndex = INDEX_NONE;

	/** World-space position of the reserved outward doorway (corridor hand-off / portal placement point). */
	FVector2D Pos = FVector2D::ZeroVector;

	/** Outward unit vector at the doorway (away from cluster interior). */
	FVector2D Normal = FVector2D(1.0f, 0.0f);

	/**
	 * BFS hops from the entrance room (StartRoomIndex) to this anchor room.
	 * Anchors are sorted far-to-near; anchors closer to the entrance are fallbacks.
	 */
	int32 GraphDistance = 0;

	/** How the runtime realizes this anchor: portal-room prefab or bare portal stub. */
	EOrganicTerminusForm Form = EOrganicTerminusForm::PortalStub;

	/**
	 * True when this anchor was synthesized because far leaves were insufficient for the
	 * requested exit count. The runtime still places a portal here but emits a Warning.
	 */
	bool bIsFallbackStub = false;
};

/**
 * Debug/visualization data from the organic dungeon pipeline. NOT a stable production API.
 * Exposes raw intermediate state (world-space rooms/corridors + the rasterized fine grid).
 */
struct PROCEDURALGEOMETRY_API FOrganicDungeonGridData
{
	TArray<bool>			  Grid;		 // true = floor
	TArray<uint8>			  CellType;	 // EOrganicCellType per cell
	TArray<int32>			  RegionIds; // per-cell region id (-1 = non-floor)
	TArray<TArray<FIntPoint>> Regions;	 // cell coords per region
	int32					  CenterRegionId = -1;
	int32					  GridWidth = 0;
	int32					  GridHeight = 0;
	float					  CellSize = 0.0f;
	FVector2D				  GridOriginWorld;	  // world position of grid cell (0,0) min corner
	TArray<FOrganicRoom>	  Rooms;			  // world-space placed rooms
	TArray<FOrganicCorridor>  Corridors;		  // world-space corridors
	TArray<TArray<FIntPoint>> RoomFootprintCells; // per-room rasterized cells (array coords) for the visualizer
	int32					  RequestedRoomCount = 0;

	// Transition metadata: the entrance room and all far-leaf exit anchors.
	int32					   StartRoomIndex = -1;
	TArray<FOrganicExitAnchor> ExitAnchors; // cluster-wide exit hand-off points (sorted far→near)

	// Per-location (chained-segment) start-room global index, in cluster location order. Used to seed each
	// location's zone allocation / debug label inside its own start room instead of the shared cluster center.
	TArray<int32> LocationStartRoomIndex;

	FLayoutDiagram2D Diagram; // full floor (rooms + corridors)

	// ---- Floor-mesh data (populated by FOrganicFloorBuilder::ComputeWalkableContour) ----

	/**
	 * Walkable-region boundary contour for this cluster: corridor-ribbon ∪ room-footprint,
	 * derived from the rasterized floor grid so the union is exact.
	 * Populated at the end of UOrganicDungeonGenerator2D::GenerateWithGridData / RasterizeLayout.
	 * Consumed by FOrganicFloorBuilder::BuildCorridorFloorMesh (grid-contour path) and persisted
	 * on AGeneratedLevelActor for the future wall-generation pass.
	 */
	FWalkableRegionContour WalkableContour;

	/**
	 * Generator's recommendation for which floor-mesh strategy the runtime should use.
	 * true  → use the grid-contour path (marching-squares triangulation of WalkableContour).
	 * false → use the ribbon-extrusion path (corridor centerlines + room rectangles).
	 *
	 * Set by the generator to true when corridor smoothing is enabled OR when cave-style
	 * corridors are used (variable-width discs that diverge significantly from centerlines).
	 * Decouples the runtime from generator parameters.
	 */
	bool bUseGridContourFloor = false;
};

/** Continuous-space layout (rooms + corridors + entrance + exit anchors) produced before rasterization. Internal. */
struct PROCEDURALGEOMETRY_API FOrganicLayout
{
	TArray<FOrganicRoom>	   Rooms;
	TArray<FOrganicCorridor>   Corridors;
	int32					   StartRoomIdx = -1;
	TArray<FOrganicExitAnchor> ExitAnchors; // populated after SelectExitAnchors; [0] is chain-leaf for multi-loc merge
	int32					   RequestedRoomCount = 0;
	int32					   PlacedCount = 0;
	TArray<int32>			   LocationStartRoomIndex; // per chained segment: global index of its start room
};

UCLASS()
class PROCEDURALGEOMETRY_API UOrganicDungeonGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	FOrganicDungeonResolvedParams		  Params;				   // first/only segment (back-compat)
	TArray<FOrganicDungeonResolvedParams> Segments;				   // chained segments (>=1); each is one OD location
	int32								  RequiredExitAnchors = 1; // how many distinct far-leaf anchors to produce

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

	/**
	 * Sets the number of distinct far-leaf exit anchors the generator should produce.
	 * Default 1 (preserves single-exit / preview behavior). The runtime calls this with the
	 * cluster's out-edge count before generation. Returns `this` for chaining.
	 */
	UOrganicDungeonGenerator2D* SetRequiredExitAnchors(int32 InCount);

	/**
	 * Pure, stateless far-leaf anchor selector. Ranks MST leaves by graph distance from the entrance
	 * (BFS on MstAdj), picks the top RequiredCount distinct rooms, and populates their best free
	 * outward doorways as exit anchors.
	 *
	 * When fewer distinct far leaves exist than RequiredCount, falls back to the deepest rooms with
	 * any free doorway (setting bIsFallbackStub=true). If the pool is exhausted, OutShortfall > 0
	 * and the caller must emit a Warning. The distinctness invariant (no two anchors share a room or
	 * doorway) is always maintained.
	 *
	 * Exposed as public static so unit tests can exercise anchor selection on hand-built graphs
	 * without running full generation.
	 *
	 * @param Rooms              Placed room array (world space); doorway bUsed flags are mutated.
	 * @param MstAdj             MST adjacency list parallel to Rooms.
	 * @param EntranceRoomIdx    Index of the cluster entrance room (excluded from anchor candidates).
	 * @param RequiredCount      Desired number of distinct exit anchors (>=1).
	 * @param DefaultForm        Terminus form assigned to each anchor.
	 * @param OutAnchors         Resulting anchors, sorted far→near by GraphDistance.
	 * @param OutShortfall       RequiredCount minus the number of anchors actually produced (0 = success).
	 */
	static void SelectExitAnchors(TArray<FOrganicRoom>& Rooms,
		const TArray<TArray<int32>>&					MstAdj,
		int32											EntranceRoomIdx,
		int32											RequiredCount,
		EOrganicTerminusForm							DefaultForm,
		TArray<FOrganicExitAnchor>&						OutAnchors,
		int32&											OutShortfall);

	// Generation
	virtual FLayoutDiagram2D Generate() override;

	/** Returns full intermediate data (rooms, corridors, grid). For visualization/testing only. */
	FOrganicDungeonGridData GenerateWithGridData();

	/**
	 * Pure orientation solver for declared-doorway rooms: finds a rotation θ and a doorway assignment
	 * that maps every mandatory connection direction to a distinct declared doorway within tolerance.
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

private:
	FOrganicDungeonGridData GenerateInternal();

	/**
	 * Generates one location's subgraph (placement + its own topology) in world space around CenterPoint.
	 * Placement avoids every room in Obstacles (rooms already placed by other locations), so the combined
	 * cluster graph never overlaps. Returns the subgraph with local (0-based) room/corridor indices.
	 */
	FOrganicLayout GenerateLocationSubgraph(
		const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles);

	/** Builds a cave/clean corridor linking one location's end room to the next location's start room. */
	FOrganicCorridor BuildInterLocationCorridor(
		FOrganicRoom& FromRoom, int32 FromIdx, FOrganicRoom& ToRoom, int32 ToIdx, const FOrganicDungeonResolvedParams& LinkParams);

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

	/** Rasterizes the merged cluster layout into the final grid + diagram. */
	FOrganicDungeonGridData RasterizeLayout(const FOrganicLayout& Layout);

	// --- Isolated algorithmic stages (stateless; can be called and tested independently) ---

	/**
	 * Builds the placement queue of room type indices, interleaving types to avoid long same-type runs.
	 * When bShuffleRoomOrder is set, picks randomly weighted by remaining count; otherwise uses a
	 * deterministic most-remaining-first spread.
	 * @return Total number of rooms requested across all room types.
	 */
	static int32 BuildRoomQueue(const FOrganicDungeonResolvedParams& Params, FRandomStream& RandomStream, TArray<int32>& OutQueue);

	/**
	 * Prim's O(N^2) minimum spanning tree over room centers.
	 * @param OutEdges  Parent→child pairs in tree-visit order (NumRooms-1 edges for N rooms).
	 * @param OutAdj    Symmetric adjacency list (OutAdj[i] = neighbours of room i in the MST).
	 */
	static void BuildMST(const TArray<FOrganicRoom>& Rooms, TArray<TPair<int32, int32>>& OutEdges, TArray<TArray<int32>>& OutAdj);

	/**
	 * BFS-based graph-diameter search over the MST adjacency list.
	 * Identifies the spine (critical path): the path between the two farthest rooms.
	 * Start room = diameter endpoint closer to CenterPoint (the level entrance).
	 * End room   = the farther endpoint (the exit / hand-off point for the next location).
	 *
	 * @param OutStartRoomIdx  Entrance room index.
	 * @param OutEndRoomIdx    Exit room index.
	 * @param OutSpineEdges    Packed edge keys (see EdgeKey in .cpp) for every edge on the path.
	 */
	static void FindSpine(const TArray<FOrganicRoom>& Rooms,
		const TArray<TArray<int32>>&				  MstAdj,
		const FVector2D&							  CenterPoint,
		int32&										  OutStartRoomIdx,
		int32&										  OutEndRoomIdx,
		TSet<uint64>&								  OutSpineEdges);
};
