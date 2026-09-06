// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/Voronoi2D/Voronoi2DVisualizer.h"

#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#if UE_ENABLE_DEBUG_DRAWING
	#include "DrawDebugHelpers.h"
#endif

AVoronoi2DVisualizer::AVoronoi2DVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;
	Generator = CreateDefaultSubobject<UVoronoiGenerator2D>(TEXT("Generator"));
}

void AVoronoi2DVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!Generator)
	{
		Generator = NewObject<UVoronoiGenerator2D>();
	}

	if (!Bounds.bIsValid)
	{
		return;
	}

#if UE_ENABLE_DEBUG_DRAWING
	const auto Layout = Generator->SetBounds(Bounds)->SetSeed(Seed)->GenerateRandomSites(NumSites, bUsePoissonDisc);

	FlushPersistentDebugLines(GetWorld());
	Layout.DrawDebug(GetWorld());
#endif
}
