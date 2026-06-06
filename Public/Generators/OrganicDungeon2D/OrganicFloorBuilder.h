// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Generators/OrganicDungeon2D/OrganicFloorTypes.h"

struct FOrganicDungeonGridData;
struct FOrganicRoom;
struct FMeshData;

/**
 * Static geometry builder for OD-cluster corridor floors.
 *
 * Stateless and free of UObject / actor / world access — mirrors the role
 * UProceduralMeshFactory plays for Voronoi-foundation clusters.  All inputs are
 * plain data structs; all outputs are FMeshData or FWalkableRegionContour values.
 *
 * Two floor-mesh strategies are supported, selected via Grid.bUseGridContourFloor:
 *
 *   Ribbon (default, thin/clean corridors)
 *     Extrudes each FOrganicCorridor's centerline into a ribbon strip (one quad per
 *     segment) and adds a rectangle for each FOrganicRoom's footprint.  Very cheap;
 *     correct for corridors whose floor is close to the centerline ribbon.
 *
 *   Grid-contour (cave / smoothed corridors)
 *     Triangulates Grid.WalkableContour (extracted by ComputeWalkableContour) into
 *     the floor mesh using ear-clipping.  Produces far fewer triangles than the
 *     per-cell-quad debug overlay while correctly covering blobs that diverge from
 *     the centerline (smoothed / wide cave corridors).
 */
struct PROCEDURALGEOMETRY_API FOrganicFloorBuilder
{
	/**
	 * Extract the walkable-region boundary contour from the rasterized floor grid.
	 *
	 * Treats a cell as walkable when Grid.Grid[i] == true (Corridor or Room cells).
	 * Runs a grid-edge boundary-tracing pass to produce closed directed loops, removes
	 * collinear vertices from each loop, classifies loops as outer (CCW, positive signed
	 * area) or hole (CW, negative signed area) via the 2D shoelace formula, groups holes
	 * into their containing outer polygon via point-in-polygon, then converts all vertices
	 * to world-space XY using Grid.GridOriginWorld and Grid.CellSize.
	 *
	 * Because rooms AND corridors are already unioned in the rasterized grid, the result
	 * is the corridor-ribbon ∪ room-footprint contour with no polygon-clipping required.
	 *
	 * @param Grid  Fully rasterized grid (GridWidth, GridHeight, Grid, CellSize, GridOriginWorld
	 *              must be valid).
	 * @param Out   Receives the extracted contour.  Cleared before writing.
	 */
	static void ComputeWalkableContour(const FOrganicDungeonGridData& Grid, FWalkableRegionContour& Out);

	/**
	 * Build a single merged floor FMeshData for one OD cluster.
	 *
	 * The top surface is placed at Z = FloorHeight, coplanar with the prefab-room floor
	 * plane (rooms spawn at ActorZ + FoundationHeight).  The mesh is a thin horizontal
	 * cap (no bottom/side walls) — sufficient for collision and navmesh rasterisation.
	 *
	 * Chooses the build path via Grid.bUseGridContourFloor:
	 *   false → ribbon extrusion from FOrganicCorridor centerlines + FOrganicRoom rectangles.
	 *   true  → ear-clipping triangulation of Grid.WalkableContour polygons-with-holes.
	 *
	 * All mesh vertices share a flat upward normal (0,0,1) and a (1,0,0) tangent.  UVs are
	 * world XY * 0.01 (same scale as the Voronoi foundation mesh).
	 *
	 * @param Grid         Rasterized grid data.  Corridors and Rooms must be valid for the
	 *                     ribbon path; WalkableContour must be populated for the contour path.
	 * @param FloorHeight  Z at which the top surface is placed (FoundationHeight from the level actor).
	 * @param OutMesh      Receives the generated mesh data.  Cleared before writing.
	 * @return true when at least one triangle was generated; false when there is no walkable content.
	 */
	static bool BuildCorridorFloorMesh(const FOrganicDungeonGridData& Grid, float FloorHeight, FMeshData& OutMesh);

private:
	/**
	 * Emit one floor quad (two CCW triangles) for a single corridor ribbon segment.
	 * The quad spans from (P0 ± perp*R0) to (P1 ± perp*R1) at the given Z height.
	 */
	static void EmitRibbonQuad(const FVector2D& P0, float R0, const FVector2D& P1, float R1, float Z, FMeshData& OutMesh);

	/**
	 * Emit a flat rectangle (two CCW triangles) for a room's oriented footprint.
	 * Uses the room's center + rotation + half-extents to derive the four OBB corners.
	 */
	static void EmitRoomRect(const FOrganicRoom& Room, float Z, FMeshData& OutMesh);

	/**
	 * Triangulate a polygon-with-holes.
	 *
	 * For each hole, finds the rightmost vertex of the hole and the nearest visible outer-polygon
	 * vertex reachable by a horizontal ray, then merges the hole into the outer ring via a
	 * "bridge" duplicate pair.  The merged simple polygon is passed to EarClipSimple.
	 *
	 * Holes are processed in decreasing order of their rightmost-X coordinate so that each
	 * bridge is inserted into an already-simplified outer ring.
	 */
	static void TriangulateWithHoles(const FWalkablePolygon& Poly, float Z, FMeshData& OutMesh);
};
