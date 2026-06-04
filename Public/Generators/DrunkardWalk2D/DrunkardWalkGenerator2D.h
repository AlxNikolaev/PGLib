#pragma once

#include "CoreMinimal.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"
#include "Generators/LayoutGenerator.h"
#include "DrunkardWalkGenerator2D.generated.h"

/** Cell type constants for DrunkardWalk grid cells. */
namespace EDrunkardWalkCellType
{
	constexpr uint8 Wall = 0;	  // non-floor cell within WallThickness of floor (a real wall)
	constexpr uint8 Corridor = 1; // floor
	constexpr uint8 Room = 2;	  // floor
	constexpr uint8 Empty = 3;	  // non-floor cell beyond WallThickness — not a wall, carved away
} // namespace EDrunkardWalkCellType

/** A room placed during generation, expressed in final grid-array coordinates (after extent offset). */
struct PROCEDURALGEOMETRY_API FDrunkardWalkPlacedRoom
{
	FIntPoint Min;		 // footprint min corner (inclusive), grid-array coords
	int32	  Width = 0; // footprint width in cells
	int32	  Height = 0;
	int32	  TypeIndex = -1; // index into the resolved RoomTypes array (-1 = unknown)
};

/**
 * Debug/visualization data from the drunkard walk generation pipeline.
 * NOT a stable production API — use Generate() for production callers.
 * This struct exposes raw intermediate state for visualization and testing.
 * Fields and layout may change between tasks without backward compatibility guarantees.
 */
struct PROCEDURALGEOMETRY_API FDrunkardWalkGridData
{
	TArray<bool>					Grid;					// true = floor, false = wall
	TArray<uint8>					CellType;				// EDrunkardWalkCellType per cell
	TArray<int32>					RegionIds;				// Per-cell region ID (-1 = wall)
	TArray<TArray<FIntPoint>>		Regions;				// Cell coordinates per region
	int32							CenterRegionId;			// Region containing grid center (-1 if none)
	TArray<TArray<FIntPoint>>		WalkerPaths;			// One corridor polyline per placed segment (grid-array coords)
	TArray<int32>					CorridorSourceRoom;		// PlacedRooms index the corridor starts from (parallel to WalkerPaths)
	TArray<int32>					CorridorTargetRoom;		// PlacedRooms index the corridor leads to   (parallel to WalkerPaths)
	TArray<FIntPoint>				RoomCenters;			// Grid positions of placed room centers
	TArray<FDrunkardWalkPlacedRoom> PlacedRooms;			// Full placed-room records
	int32							RequestedRoomCount = 0; // Total rooms requested (sum of type counts)
	int32							GridWidth;
	int32							GridHeight;
	float							CellSize;
	FLayoutDiagram2D				Diagram; // Final output (existing)
};

UCLASS()
class PROCEDURALGEOMETRY_API UDrunkardWalkGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	TArray<FRoomTypeConfig> RoomTypes;
	int32					CorridorLengthMin;
	int32					CorridorLengthMax;
	int32					CorridorWidthMin;
	int32					CorridorWidthMax;
	float					CorridorTurnProbability;
	float					CorridorBranchProbability;
	int32					RoomBorderMargin;
	int32					WallThickness;
	int32					MaxPlacementAttemptsPerExit;
	bool					bShuffleRoomOrder;
	float					BranchProbability;

public:
	UDrunkardWalkGenerator2D();

	// Covariant base class overrides
	virtual UDrunkardWalkGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UDrunkardWalkGenerator2D* SetSeed(const FString& InSeed) override;
	virtual UDrunkardWalkGenerator2D* SetGridSize(int32 InSize) override;
	virtual UDrunkardWalkGenerator2D* SetCenter(const FVector2D& InCenter) override;

	// Generator-specific config
	UDrunkardWalkGenerator2D* SetRoomTypes(const TArray<FRoomTypeConfig>& InRoomTypes);
	UDrunkardWalkGenerator2D* SetCorridorLengthRange(int32 InMin, int32 InMax);

	/** Sets a fixed corridor width (min == max == InWidth). */
	UDrunkardWalkGenerator2D* SetCorridorWidth(int32 InWidth);

	/** Sets the corridor width range; width drifts within [InMin, InMax] along the corridor (bounded random walk). */
	UDrunkardWalkGenerator2D* SetCorridorWidthRange(int32 InMin, int32 InMax);

	/** Sets the per-step probability [0,1] that a corridor turns 90 degrees. */
	UDrunkardWalkGenerator2D* SetCorridorTurnProbability(float InProbability);

	/** Sets the per-step probability [0,1] that a corridor forks a side branch to an additional room. */
	UDrunkardWalkGenerator2D* SetCorridorBranchProbability(float InProbability);

	UDrunkardWalkGenerator2D* SetRoomBorderMargin(int32 InMargin);

	/** Sets wall thickness in cells; non-floor cells farther than this from any floor become Empty (no wall). */
	UDrunkardWalkGenerator2D* SetWallThickness(int32 InThickness);
	UDrunkardWalkGenerator2D* SetMaxPlacementAttemptsPerExit(int32 InAttempts);
	UDrunkardWalkGenerator2D* SetShuffleRoomOrder(bool bInShuffle);

	/** Sets the probability [0,1] that a room grows from a random earlier room instead of the most recent (branching). */
	UDrunkardWalkGenerator2D* SetBranchProbability(float InProbability);

	/** Applies a fully resolved parameter set in one call. */
	UDrunkardWalkGenerator2D* ApplyResolvedParams(const FDrunkardWalkResolvedParams& Params);

	// Generation
	virtual FLayoutDiagram2D Generate() override;

	/** Returns full intermediate grid data including placed rooms and regions. For visualization/testing only. */
	FDrunkardWalkGridData GenerateWithGridData();

private:
	/** Core generation pipeline shared by Generate() and GenerateWithGridData(). */
	FDrunkardWalkGridData GenerateInternal();

	/**
	 * Expands RoomTypes into a flat queue of type indices (one entry per room, count = Weight) and
	 * optionally shuffles it with Fisher-Yates so the placement order varies per seed.
	 * Extracted so it can be verified in isolation from the full walk pipeline.
	 */
	static TArray<int32> BuildRoomQueue(const TArray<FRoomTypeConfig>& RoomTypes, bool bShuffle, FRandomStream& RandomStream);
};
