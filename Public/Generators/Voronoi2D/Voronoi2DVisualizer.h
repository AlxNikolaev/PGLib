#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Voronoi2DVisualizer.generated.h"

class UVoronoiGenerator2D;

UCLASS()
class PROCEDURALGEOMETRY_API AVoronoi2DVisualizer : public AActor
{
	GENERATED_BODY()

	/** Generator retained through preview construction so Unreal GC cannot reclaim it mid-build. */
	UPROPERTY()
	UVoronoiGenerator2D* Generator;

	/** Spaces preview sites with Poisson sampling when enabled; otherwise uses uniform random positions. */
	UPROPERTY(EditInstanceOnly, Category = "Voronoi Preview")
	bool bUsePoissonDisc = false;

	/** Requested number of preview sites; a Poisson sample may return fewer when bounds are crowded. */
	UPROPERTY(EditInstanceOnly, Category = "Voronoi Preview", meta = (ClampMin = "0"))
	int32 NumSites = 10;

	/** XY region clipped into Voronoi cells, in centimetres. */
	UPROPERTY(EditInstanceOnly, Category = "Voronoi Preview", meta = (Units = "cm"))
	FBox2D Bounds;

	/** Deterministic preview seed; an empty seed is replaced during generation and changes on construction. */
	UPROPERTY(EditInstanceOnly, Category = "Voronoi Preview")
	FString Seed = TEXT("Preview");

public:
	AVoronoi2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};
