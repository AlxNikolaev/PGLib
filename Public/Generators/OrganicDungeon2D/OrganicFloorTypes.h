// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * A single closed walkable-region boundary polygon with optional hole rings.
 *
 * Winding convention (normalized by FOrganicFloorBuilder::BuildVectorContour):
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
 * Built by FOrganicFloorBuilder::BuildVectorContour (vector corridor-ribbon ∪
 * room-OBB union) and persisted on AGeneratedLevelActor for the wall-generation pass.
 */
struct PROCEDURALGEOMETRY_API FWalkableRegionContour
{
	/** One polygon-with-holes per disconnected walkable region (typically one per cluster). */
	TArray<FWalkablePolygon> Polygons;
};

/**
 * A single doorway opening in WORLD-space XY: where a wall boundary should be cut
 * open so a corridor connects to a room (or an out-portal).
 *
 * Produced from FOrganicBakedDoorway declarations (transformed
 * to world space) and consumed by FOrganicFloorBuilder when cutting boundary loops:
 * the boundary span nearest Position along the boundary is removed over a Width-wide
 * arc, turning a closed wall ring into an open polyline at the opening.
 *
 * The FLOOR cap still spans the opening (built from the un-cut closed loops); only
 * the WALL mesh and the wall SPLINE skip the gap.
 */
struct PROCEDURALGEOMETRY_API FWalkableDoorway
{
	/** Center of the opening, world-space XY. */
	FVector2D Position = FVector2D::ZeroVector;

	/** Outward unit direction (away from the room interior); the corridor approaches from here. */
	FVector2D OutwardDir = FVector2D(1.0f, 0.0f);

	/** Clear opening width, world units. The boundary is cut open over this span. */
	float Width = 100.0f;
};

/**
 * A boundary loop after doorway cutting: either a fully-closed wall ring (no doorway
 * touched it) or an OPEN polyline (one or more doorway gaps removed from the ring).
 *
 * Consumed by the wall-mesh extrusion (one wall quad per consecutive point pair,
 * skipping the implicit closing edge when bClosed is false) and by the wall-spline
 * emission (USplineComponent::SetClosedLoop(bClosed)).
 */
struct PROCEDURALGEOMETRY_API FWalkableBoundaryLoop
{
	/** Ordered boundary points, world-space XY. */
	TArray<FVector2D> Points;

	/** True when the loop wraps continuously (no doorway gap); false when it is an open polyline. */
	bool bClosed = true;
};
