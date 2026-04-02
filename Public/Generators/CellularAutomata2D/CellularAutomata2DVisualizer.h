#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CellularAutomata2DVisualizer.generated.h"

UCLASS()
class PROCEDURALGEOMETRY_API ACellularAutomata2DVisualizer : public AActor
{
	GENERATED_BODY()

	UPROPERTY(EditInstanceOnly)
	FBox2D Bounds;

	UPROPERTY(EditInstanceOnly)
	FString Seed;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 10))
	int32 GridSize = 100;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 0, ClampMax = 1))
	float FillProbability = 0.45f;

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 0))
	int32 Iterations = 5;

	UPROPERTY(EditInstanceOnly)
	TArray<int32> BirthRule = {6, 7, 8};

	UPROPERTY(EditInstanceOnly)
	TArray<int32> SurvivalRule = {3, 4, 5};

	UPROPERTY(EditInstanceOnly, meta = (ClampMin = 1))
	int32 MinRegionSize = 20;

	UPROPERTY(EditInstanceOnly)
	bool bKeepCenterRegion = true;

public:
	ACellularAutomata2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
