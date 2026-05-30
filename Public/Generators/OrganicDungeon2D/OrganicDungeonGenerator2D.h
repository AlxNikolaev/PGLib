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

	// Transition metadata: the entrance/exit rooms (graph-diameter endpoints) and the exit hand-off point.
	int32	  StartRoomIndex = -1;
	int32	  EndRoomIndex = -1;
	FVector2D ExitAnchorPos;	// world position of the end room's outward doorway (next-part hand-off)
	FVector2D ExitAnchorNormal; // outward direction at the exit

	// Per-location (chained-segment) start-room global index, in cluster location order. Used to seed each
	// location's zone allocation / debug label inside its own start room instead of the shared cluster center.
	TArray<int32> LocationStartRoomIndex;

	FLayoutDiagram2D Diagram; // full floor (rooms + corridors)
};

/** Continuous-space layout (rooms + corridors + start/end) produced before rasterization. Internal. */
struct PROCEDURALGEOMETRY_API FOrganicLayout
{
	TArray<FOrganicRoom>	 Rooms;
	TArray<FOrganicCorridor> Corridors;
	int32					 StartRoomIdx = -1;
	int32					 EndRoomIdx = -1;
	FVector2D				 ExitAnchorPos = FVector2D::ZeroVector;
	FVector2D				 ExitAnchorNormal = FVector2D(1.0f, 0.0f);
	int32					 RequestedRoomCount = 0;
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

	/** Returns full intermediate data (rooms, corridors, grid). For visualization/testing only. */
	FOrganicDungeonGridData GenerateWithGridData();

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

	/** Rasterizes the merged cluster layout into the final grid + diagram. */
	FOrganicDungeonGridData RasterizeLayout(const FOrganicLayout& Layout);
};
