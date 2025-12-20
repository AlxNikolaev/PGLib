// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Voronoi2DVisualizer.generated.h"

class UVoronoiGenerator2D;

UCLASS()
class PROCEDURALGEOMETRY_API AVoronoi2DVisualizer : public AActor
{
	GENERATED_BODY()

	UPROPERTY()
	UVoronoiGenerator2D* Generator;

	UPROPERTY(EditInstanceOnly)
	bool bUsePoissonDisc = false;

	UPROPERTY(EditInstanceOnly)
	bool bGenerateRandSites = true;

	UPROPERTY(EditInstanceOnly)
	int32 NumSites = 10;

	UPROPERTY(EditInstanceOnly)
	FBox2D Bounds;

public:
	AVoronoi2DVisualizer();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
};