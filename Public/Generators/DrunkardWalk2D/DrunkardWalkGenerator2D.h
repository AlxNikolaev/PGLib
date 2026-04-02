#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "DrunkardWalkGenerator2D.generated.h"

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

private:
	void CarveCell(TArray<bool>& Grid, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const;
	void CarveRoom(TArray<bool>& Grid, int32 CenterX, int32 CenterY, int32 GridWidth, int32 GridHeight) const;
};
