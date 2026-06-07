// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Generators/OrganicDungeon2D/OrganicFloorTypes.h"

struct FOrganicCorridor;
struct FOrganicRoom;
struct FMeshData;

/**
 * Static geometry builder for OD-cluster floors and walls.
 *
 * Stateless and free of UObject / actor / world access — mirrors the role
 * UProceduralMeshFactory plays for Voronoi-foundation clusters.  All inputs are
 * plain data structs; all outputs are FMeshData / FWalkableRegionContour /
 * FWalkableBoundaryLoop values.
 *
 * The boundary geometry is derived from VECTOR layout primitives (smoothed corridor
 * centerlines + room OBBs), NOT from the rasterized floor grid:
 *
 *   1. Each FOrganicCorridor centerline is smoothed (Chaikin) into a few-point curve.
 *   2. Each smoothed corridor becomes an offset "ribbon" polygon; each room becomes
 *      an OBB rectangle.  All regions are 2D-polygon-UNIONed into walkable
 *      polygon(s)-with-holes with a smooth boundary (BuildVectorContour).
 *   3. Declared doorways cut gaps in the boundary loops, producing the OPEN loops the
 *      walls / splines follow while the CLOSED loops still cap the floor across the
 *      opening (CutDoorwayGaps).
 *   4. The union boundary is extruded into a unified FLOOR + WALL mesh using the same
 *      vertex/normal/UV conventions as UProceduralMeshFactory so OD geometry matches
 *      the Voronoi foundation exactly (BuildFoundationMesh).
 *
 * The rasterized grid is retained by the generator for flood-fill / region ids / cave
 * smoothing; only the boundary-geometry SOURCE is the vector union, not the grid trace.
 */
struct PROCEDURALGEOMETRY_API FOrganicFloorBuilder
{
	/**
	 * Smooth one corridor centerline into a few-point curve via Chaikin corner-cutting.
	 *
	 * Endpoints are pinned (the first and last centerline points are preserved exactly)
	 * so the smoothed curve still anchors to its room doorways.  Per-point radii are
	 * resampled by arc-length-linear interpolation so each output point keeps a sensible
	 * carve radius.
	 *
	 * @param InCenterline  Source centerline (>= 2 points).
	 * @param InRadii       Per-point radii parallel to InCenterline (same length).
	 * @param Iterations    Chaikin iterations (clamped to [1, 4]; 2 matches SmoothIterations).
	 * @param OutCenterline Smoothed centerline.
	 * @param OutRadii      Resampled radii parallel to OutCenterline.
	 */
	static void SmoothCenterline(const TArray<FVector2D>& InCenterline,
		const TArray<float>&							  InRadii,
		int32											  Iterations,
		TArray<FVector2D>&								  OutCenterline,
		TArray<float>&									  OutRadii);

	/**
	 * Build the walkable-region boundary contour from vector primitives.
	 *
	 * Each corridor's smoothed centerline is offset by its (representative) radius into a
	 * rounded ribbon polygon; each room becomes an OBB rectangle.  All polygons are
	 * 2D-UNIONed (engine GeometryAlgorithms) into outer-with-holes polygons whose winding
	 * is normalized to the FWalkablePolygon convention (CCW outer, CW holes).
	 *
	 * @param Corridors        World-space corridors (centerline + radii).
	 * @param Rooms            World-space placed rooms (OBB footprints).
	 * @param SmoothIterations Chaikin iterations applied to each centerline before offsetting.
	 * @param Out              Receives the union contour.  Cleared before writing.
	 * @return true when at least one non-degenerate polygon was produced.
	 */
	static bool BuildVectorContour(
		const TArray<FOrganicCorridor>& Corridors, const TArray<FOrganicRoom>& Rooms, int32 SmoothIterations, FWalkableRegionContour& Out);

	/**
	 * Cut doorway gaps into the boundary rings of one walkable polygon.
	 *
	 * For each doorway, the boundary span nearest Position (projected along OutwardDir) is
	 * removed over a Width-wide arc, splitting that closed ring into an OPEN polyline.
	 * Rings untouched by any doorway are emitted as closed loops.
	 *
	 * @param Poly      Source polygon-with-holes (closed rings).
	 * @param Doorways  World-space doorway openings to cut.
	 * @param OutLoops  Receives the resulting boundary loops (closed or open).  Cleared first.
	 */
	static void CutDoorwayGaps(const FWalkablePolygon& Poly, const TArray<FWalkableDoorway>& Doorways, TArray<FWalkableBoundaryLoop>& OutLoops);

	/**
	 * Build a unified FLOOR + WALL FMeshData for one OD cluster region.
	 *
	 * The floor cap is triangulated from the CLOSED union polygon (outer + holes) via
	 * ConstrainedDelaunay2 so it correctly fills concave regions and spans doorway
	 * openings.  Walls are TRUE-THICK: each boundary loop emits an outer face, an inner
	 * face inset inward by WallThickness, and a top cap bridging the two.  Both faces use
	 * the same side-quad recipe as UProceduralMeshFactory::BuildSideGeometry (bottom/top
	 * vert pairs, side normal = -cross(top-bottom, next-bottom), accumulated-length U UVs),
	 * so OD walls match Voronoi walls visually.
	 *
	 * Top floor surface sits at Z = FloorHeight; walls rise from FloorHeight to
	 * FloorHeight + WallHeight.  The outer face follows the boundary loop; the inner face
	 * is WallThickness world units toward the walkable interior, giving the wall real
	 * thickness with an up-facing top cap.
	 *
	 * @param Poly           Closed union polygon (floor cap source).
	 * @param WallLoops      Doorway-cut boundary loops (wall source); open loops skip their gap.
	 * @param FloorHeight    Z of the floor top surface (FoundationHeight from the level actor).
	 * @param WallHeight     Wall extrusion height, world units.
	 * @param WallThickness  Wall thickness, world units (OD WallThickness * grid cell size).
	 * @param OutMesh        Receives the merged floor+wall mesh.  Cleared before writing.
	 * @return true when at least one triangle was generated.
	 */
	static bool BuildFoundationMesh(const FWalkablePolygon& Poly,
		const TArray<FWalkableBoundaryLoop>&				WallLoops,
		float												FloorHeight,
		float												WallHeight,
		float												WallThickness,
		FMeshData&											OutMesh);

private:
	/** Triangulate one closed polygon-with-holes into a flat floor cap at Z (ConstrainedDelaunay2). */
	static void TriangulateFloorCap(const FWalkablePolygon& Poly, float Z, FMeshData& OutMesh);

	/**
	 * Extrude one boundary loop into a TRUE-THICK wall from FloorHeight to FloorHeight+WallHeight.
	 *
	 * Emits three connected strips per loop: an OUTER face along the loop, an INNER face along the
	 * loop inset inward by WallThickness (toward the walkable interior, with reversed normal/winding),
	 * and a TOP CAP bridging outer-top to inner-top (up-facing).  When bClosed the implicit closing
	 * edge (last→first) is also walled; open (doorway-cut) loops leave the doorway gap open through the
	 * full wall thickness (no cap across the gap).
	 */
	static void ExtrudeWallLoop(
		const TArray<FVector2D>& Loop, bool bClosed, float FloorHeight, float WallHeight, float WallThickness, FMeshData& OutMesh);
};
