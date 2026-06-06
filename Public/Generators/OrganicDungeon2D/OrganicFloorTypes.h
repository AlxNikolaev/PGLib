// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * A single closed walkable-region boundary polygon with optional hole rings.
 *
 * Winding convention (enforced by FOrganicFloorBuilder::ComputeWalkableContour):
 *   Outer — CCW (positive signed area via the 2D shoelace formula).
 *   Holes — CW  (negative signed area).
 *
 * All coordinates are world-space XY (UE units).  Z is ignored here; the floor
 * builder applies FloorHeight when generating the actual mesh geometry.
 */
struct PROCEDURALGEOMETRY_API FWalkablePolygon
{
	/** Outer boundary ring (CCW, world-space XY). */
	TArray<FVector2D> Outer;

	/** Inner hole rings (CW each, world-space XY).  May be empty for simple regions. */
	TArray<TArray<FVector2D>> Holes;
};

/**
 * Full walkable-region contour for one OD cluster: all disconnected walkable
 * areas expressed as polygons-with-holes.  Normally contains exactly one polygon
 * (most clusters are a single connected floor region).
 *
 * Computed by FOrganicFloorBuilder::ComputeWalkableContour and persisted on
 * AGeneratedLevelActor for the future wall-generation pass to consume.
 */
struct PROCEDURALGEOMETRY_API FWalkableRegionContour
{
	/** One polygon-with-holes per disconnected walkable region (typically one per cluster). */
	TArray<FWalkablePolygon> Polygons;
};
