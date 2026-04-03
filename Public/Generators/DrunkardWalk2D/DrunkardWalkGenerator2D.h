#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "DrunkardWalkGenerator2D.generated.h"

/** Cell type constants for DrunkardWalk grid cells. */
namespace EDrunkardWalkCellType
{
	constexpr uint8 Wall = 0;
	constexpr uint8 Corridor = 1;
	constexpr uint8 Room = 2;
} // namespace EDrunkardWalkCellType

/**
 * Debug/visualization data from the drunkard walk generation pipeline.
 * NOT a stable production API — use Generate() for production callers.
 * This struct exposes raw intermediate state for visualization and testing.
 * Fields and layout may change between tasks without backward compatibility guarantees.
 */
struct PROCEDURALGEOMETRY_API FDrunkardWalkGridData
{
	TArray<bool>			  Grid;			  // true = floor, false = wall
	TArray<uint8>			  CellType;		  // EDrunkardWalkCellType per cell
	TArray<int32>			  RegionIds;	  // Per-cell region ID (-1 = wall)
	TArray<TArray<FIntPoint>> Regions;		  // Cell coordinates per region
	int32					  CenterRegionId; // Region containing grid center (-1 if none)
	TArray<TArray<FIntPoint>> WalkerPaths;	  // Position history per walker (index = walker ID)
	int32					  GridWidth;
	int32					  GridHeight;
	float					  CellSize;
	FLayoutDiagram2D		  Diagram; // Final output (existing)
};

UCLASS()
class PROCEDURALGEOMETRY_API UDrunkardWalkGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	int32 WalkLength;
	int32 NumWalkers;
	float BranchProbability;
	int32 CorridorWidth;
	float RoomChance;
	int32 RoomRadius;

public:
	UDrunkardWalkGenerator2D();

	// Covariant base class overrides
	virtual UDrunkardWalkGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UDrunkardWalkGenerator2D* SetSeed(const FString& InSeed) override;
	virtual UDrunkardWalkGenerator2D* SetGridSize(int32 InSize) override;
	virtual UDrunkardWalkGenerator2D* SetCenter(const FVector2D& InCenter) override;

	// Generator-specific config
	UDrunkardWalkGenerator2D* SetWalkLength(int32 InWalkLength);
	UDrunkardWalkGenerator2D* SetNumWalkers(int32 InNumWalkers);
	UDrunkardWalkGenerator2D* SetBranchProbability(float InProbability);
	UDrunkardWalkGenerator2D* SetCorridorWidth(int32 InWidth);
	UDrunkardWalkGenerator2D* SetRoomChance(float InChance);
	UDrunkardWalkGenerator2D* SetRoomRadius(int32 InRadius);

	// Generation
	virtual FLayoutDiagram2D Generate() override;

	/** Returns full intermediate grid data including walker paths and regions. For visualization/testing only. */
	FDrunkardWalkGridData GenerateWithGridData();

private:
	/** Core generation pipeline shared by Generate() and GenerateWithGridData(). */
	FDrunkardWalkGridData GenerateInternal();

	void CarveCell(TArray<bool>& Grid, TArray<uint8>& CellType, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const;
	void CarveRoom(TArray<bool>& Grid, TArray<uint8>& CellType, int32 CenterX, int32 CenterY, int32 GridWidth, int32 GridHeight) const;
};
