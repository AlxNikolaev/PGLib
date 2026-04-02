#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DrunkardWalk2DVisualizer.generated.h"

UCLASS()
class PROCEDURALGEOMETRY_API ADrunkardWalk2DVisualizer : public AActor
{
	GENERATED_BODY()

	UPROPERTY(EditInstanceOnly)
	FBox2D Bounds;

	UPROPERTY(EditInstanceOnly)
	FString Seed;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 10))
	int32 GridSize = 100;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 WalkLength = 500;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 NumWalkers = 1;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 0, ClampMax = 1))
	float BranchProbability = 0.0f;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 CorridorWidth = 1;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 0, ClampMax = 1))
	float RoomChance = 0.0f;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 RoomRadius = 1;

public:
	ADrunkardWalk2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
