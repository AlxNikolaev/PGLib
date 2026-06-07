// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicFloorBuilder.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Factories/ProceduralMeshFactory.h"
#include "ProceduralGeometry.h"

#include "Algo/Reverse.h"
#include "ConstrainedDelaunay2.h"
#include "Curve/GeneralPolygon2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
#include "Polygon2.h"

// ============================================================================
// Internal helpers — local to this translation unit.
// ============================================================================

namespace OrganicFloorBuilderImpl
{
	/** Flat floor UV scale (world units → UV coords), matching the foundation mesh UVs. */
	static constexpr float UVScale = 0.01f;

	using UE::Geometry::FGeneralPolygon2d;
	using UE::Geometry::FPolygon2d;
	using FVec2d = UE::Math::TVector2<double>;

	// -------------------------------------------------------------------------
	// Math helpers
	// -------------------------------------------------------------------------

	/**
	 * Shoelace signed area for a polygon (world-space 2D).
	 * Positive = CCW (outer ring), Negative = CW (hole).
	 */
	float SignedArea2D(const TArray<FVector2D>& Poly)
	{
		float		Area = 0.0f;
		const int32 N = Poly.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& Pi = Poly[i];
			const FVector2D& Pn = Poly[(i + 1) % N];
			Area += Pi.X * Pn.Y - Pn.X * Pi.Y;
		}
		return Area * 0.5f;
	}

	/** Force a ring to the requested winding (CCW when bWantCCW, else CW), reversing in place if needed. */
	void EnsureWinding(TArray<FVector2D>& Ring, bool bWantCCW)
	{
		if (Ring.Num() < 3)
		{
			return;
		}
		const bool bIsCCW = SignedArea2D(Ring) > 0.0f;
		if (bIsCCW != bWantCCW)
		{
			Algo::Reverse(Ring);
		}
	}

	// -------------------------------------------------------------------------
	// Conversions between engine geometry types and FWalkable* structs
	// -------------------------------------------------------------------------

	/** Convert a TPolygon2<double> ring to a world-space FVector2D ring. */
	TArray<FVector2D> ToRing(const FPolygon2d& Poly)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Poly.VertexCount());
		for (const FVec2d& V : Poly.GetVertices())
		{
			Out.Emplace(static_cast<float>(V.X), static_cast<float>(V.Y));
		}
		return Out;
	}

	/** Build a TPolygon2<double> from a world-space FVector2D ring. */
	FPolygon2d ToPolygon2d(const TArray<FVector2D>& Ring)
	{
		FPolygon2d Poly;
		for (const FVector2D& V : Ring)
		{
			Poly.AppendVertex(FVec2d(V.X, V.Y));
		}
		return Poly;
	}

	/**
	 * Convert a union result (array of general-polygons-with-holes) into FWalkablePolygon(s),
	 * normalizing winding to CCW outer / CW holes regardless of the source winding convention.
	 */
	void ToWalkableContour(const TArray<FGeneralPolygon2d>& Source, FWalkableRegionContour& Out)
	{
		Out.Polygons.Reset();
		for (const FGeneralPolygon2d& GP : Source)
		{
			TArray<FVector2D> Outer = ToRing(GP.GetOuter());
			if (Outer.Num() < 3)
			{
				continue;
			}

			FWalkablePolygon WP;
			EnsureWinding(Outer, /*bWantCCW=*/true);
			WP.Outer = MoveTemp(Outer);

			for (const FPolygon2d& HolePoly : GP.GetHoles())
			{
				TArray<FVector2D> Hole = ToRing(HolePoly);
				if (Hole.Num() < 3)
				{
					continue;
				}
				EnsureWinding(Hole, /*bWantCCW=*/false);
				WP.Holes.Add(MoveTemp(Hole));
			}

			Out.Polygons.Add(MoveTemp(WP));
		}
	}

	// -------------------------------------------------------------------------
	// Mesh emission helpers (shared conventions with UProceduralMeshFactory)
	// -------------------------------------------------------------------------

	/**
	 * Densify a closed ring by inserting points along each edge no farther apart than MaxSpacing.
	 * Used before doorway cutting so a Width-wide gap always lands on existing vertices even on
	 * long straight edges (e.g. a room rectangle's side has no vertex at the doorway center).
	 */
	TArray<FVector2D> DensifyRing(const TArray<FVector2D>& Ring, float MaxSpacing)
	{
		const int32 N = Ring.Num();
		if (N < 3 || MaxSpacing <= KINDA_SMALL_NUMBER)
		{
			return Ring;
		}

		TArray<FVector2D> Out;
		Out.Reserve(N * 2);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Ring[i];
			const FVector2D& B = Ring[(i + 1) % N];
			Out.Add(A);

			const float EdgeLen = FVector2D::Distance(A, B);
			const int32 Subdiv = FMath::FloorToInt(EdgeLen / MaxSpacing);
			for (int32 k = 1; k <= Subdiv; ++k)
			{
				const float T = static_cast<float>(k) / static_cast<float>(Subdiv + 1);
				Out.Add(FMath::Lerp(A, B, T));
			}
		}
		return Out;
	}

	/** Append one flat-floor vertex (Z = FloorHeight, normal up, tangent +X). */
	void AddFlatVert(const FVector2D& XY, float Z, FMeshData& Mesh)
	{
		Mesh.Vertices.Add(FVector(XY.X, XY.Y, Z));
		Mesh.Normals.Add(FVector(0.0f, 0.0f, 1.0f));
		Mesh.UVs.Add(FVector2D(XY.X * UVScale, XY.Y * UVScale));
		Mesh.VertexColors.Add(FLinearColor::White);
		Mesh.Tangents.Add(FProcMeshTangent(FVector(1.0f, 0.0f, 0.0f), false));
	}

} // namespace OrganicFloorBuilderImpl

// ============================================================================
// FOrganicFloorBuilder — Chaikin smoothing (B1)
// ============================================================================

void FOrganicFloorBuilder::SmoothCenterline(
	const TArray<FVector2D>& InCenterline, const TArray<float>& InRadii, int32 Iterations, TArray<FVector2D>& OutCenterline, TArray<float>& OutRadii)
{
	OutCenterline.Reset();
	OutRadii.Reset();

	const int32 N = InCenterline.Num();
	if (N < 2 || InRadii.Num() != N)
	{
		// Pass through unchanged when there is nothing to smooth.
		OutCenterline = InCenterline;
		OutRadii = InRadii;
		return;
	}

	Iterations = FMath::Clamp(Iterations, 1, 4);

	// Work on parallel point + radius arrays so radii follow the smoothed corners.
	TArray<FVector2D> Pts = InCenterline;
	TArray<float>	  Rad = InRadii;

	for (int32 Pass = 0; Pass < Iterations; ++Pass)
	{
		const int32 M = Pts.Num();
		if (M < 3)
		{
			break; // a 2-point segment has no interior corners to cut
		}

		TArray<FVector2D> NextPts;
		TArray<float>	  NextRad;
		NextPts.Reserve(M * 2);
		NextRad.Reserve(M * 2);

		// Pin the first endpoint (anchors to its room/doorway).
		NextPts.Add(Pts[0]);
		NextRad.Add(Rad[0]);

		// Chaikin corner-cutting on each interior segment: emit Q (1/4) and R (3/4).
		for (int32 i = 0; i + 1 < M; ++i)
		{
			const FVector2D& P0 = Pts[i];
			const FVector2D& P1 = Pts[i + 1];
			const float		 R0 = Rad[i];
			const float		 R1 = Rad[i + 1];

			const FVector2D Q = P0 * 0.75f + P1 * 0.25f;
			const FVector2D R = P0 * 0.25f + P1 * 0.75f;

			NextPts.Add(Q);
			NextRad.Add(R0 * 0.75f + R1 * 0.25f);
			NextPts.Add(R);
			NextRad.Add(R0 * 0.25f + R1 * 0.75f);
		}

		// Pin the last endpoint.
		NextPts.Add(Pts.Last());
		NextRad.Add(Rad.Last());

		Pts = MoveTemp(NextPts);
		Rad = MoveTemp(NextRad);
	}

	OutCenterline = MoveTemp(Pts);
	OutRadii = MoveTemp(Rad);
}

// ============================================================================
// FOrganicFloorBuilder — vector contour union (B2)
// ============================================================================

bool FOrganicFloorBuilder::BuildVectorContour(
	const TArray<FOrganicCorridor>& Corridors, const TArray<FOrganicRoom>& Rooms, int32 SmoothIterations, FWalkableRegionContour& Out)
{
	using namespace OrganicFloorBuilderImpl;
	using namespace UE::Geometry;

	Out.Polygons.Reset();

	// Collect one closed general-polygon per region (corridor ribbon or room rect).
	TArray<FGeneralPolygon2d> Regions;
	Regions.Reserve(Corridors.Num() + Rooms.Num());

	// --- Corridor ribbons: variable-width offset of the smoothed centerline (per-vertex radius). ---
	for (const FOrganicCorridor& Cor : Corridors)
	{
		if (Cor.Centerline.Num() < 2 || Cor.Radii.Num() != Cor.Centerline.Num())
		{
			continue;
		}

		TArray<FVector2D> Smoothed;
		TArray<float>	  SmoothedRadii;
		SmoothCenterline(Cor.Centerline, Cor.Radii, FMath::Max(1, SmoothIterations), Smoothed, SmoothedRadii);

		if (Smoothed.Num() < 2)
		{
			continue;
		}

		// Variable-width ribbon: offset every centerline sample by its OWN radius so Cave-style
		// width variation (waviness) survives; Clean-style corridors carry constant radii and
		// yield a uniform ribbon. PolygonsUnion below absorbs the ribbon into the floor region.
		const int32 N = Smoothed.Num();
		if (SmoothedRadii.Num() != N)
		{
			continue;
		}

		// Per-vertex outward normals (average of the adjacent segment normals).
		TArray<FVector2D> Normals;
		Normals.SetNumZeroed(N);
		for (int32 i = 0; i < N; ++i)
		{
			FVector2D NSum(0.0f, 0.0f);
			if (i > 0)
			{
				const FVector2D D = (Smoothed[i] - Smoothed[i - 1]).GetSafeNormal();
				NSum += FVector2D(-D.Y, D.X);
			}
			if (i + 1 < N)
			{
				const FVector2D D = (Smoothed[i + 1] - Smoothed[i]).GetSafeNormal();
				NSum += FVector2D(-D.Y, D.X);
			}
			Normals[i] = NSum.GetSafeNormal();
		}

		// Closed ribbon: up the left side, back down the right side; each sample offset by its radius.
		TArray<FVector2D> Ribbon;
		Ribbon.Reserve(N * 2);
		for (int32 i = 0; i < N; ++i)
		{
			Ribbon.Add(Smoothed[i] + Normals[i] * SmoothedRadii[i]);
		}
		for (int32 i = N - 1; i >= 0; --i)
		{
			Ribbon.Add(Smoothed[i] - Normals[i] * SmoothedRadii[i]);
		}

		// Normalize to CCW winding (positive signed area) for the union outer ring.
		double Area2 = 0.0;
		for (int32 i = 0, Count = Ribbon.Num(); i < Count; ++i)
		{
			const FVector2D& A = Ribbon[i];
			const FVector2D& B = Ribbon[(i + 1) % Count];
			Area2 += static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
		}
		if (Area2 < 0.0)
		{
			for (int32 i = 0, j = Ribbon.Num() - 1; i < j; ++i, --j)
			{
				Ribbon.Swap(i, j);
			}
		}

		Regions.Add(FGeneralPolygon2d(ToPolygon2d(Ribbon)));
	}

	// --- Room rectangles: OBB → closed CCW polygon. ---
	for (const FOrganicRoom& Room : Rooms)
	{
		const float DegRad = FMath::DegreesToRadians(Room.RotationDeg);
		const float C = FMath::Cos(DegRad);
		const float S = FMath::Sin(DegRad);

		const FVector2D AxisX(C, S);
		const FVector2D AxisY(-S, C);

		const FVector2D BL = Room.Center - AxisX * Room.HalfExtent.X - AxisY * Room.HalfExtent.Y;
		const FVector2D BR = Room.Center + AxisX * Room.HalfExtent.X - AxisY * Room.HalfExtent.Y;
		const FVector2D TR = Room.Center + AxisX * Room.HalfExtent.X + AxisY * Room.HalfExtent.Y;
		const FVector2D TL = Room.Center - AxisX * Room.HalfExtent.X + AxisY * Room.HalfExtent.Y;

		TArray<FVector2D> Rect = { BL, BR, TR, TL };
		FGeneralPolygon2d GP(ToPolygon2d(Rect));
		Regions.Add(MoveTemp(GP));
	}

	if (Regions.IsEmpty())
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[OrganicFloor] BuildVectorContour: no corridor/room regions to union"));
		return false;
	}

	// --- Union all regions into outer-with-holes polygon(s). ---
	TArray<FGeneralPolygon2d> Unioned;
	const bool				  bUnionOk = PolygonsUnion(Regions, Unioned, /*bCopyInputOnFailure=*/true);
	if (!bUnionOk)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[OrganicFloor] BuildVectorContour: PolygonsUnion failed; using copied input (%d region(s))"),
			Unioned.Num());
	}

	ToWalkableContour(Unioned, Out);

	int32 TotalHoles = 0;
	for (const FWalkablePolygon& WP : Out.Polygons)
	{
		TotalHoles += WP.Holes.Num();
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[OrganicFloor] BuildVectorContour: %d region(s) → %d polygon(s), %d hole(s)"),
		Regions.Num(),
		Out.Polygons.Num(),
		TotalHoles);

	return !Out.Polygons.IsEmpty();
}

// ============================================================================
// FOrganicFloorBuilder — doorway gap cutting (B3)
// ============================================================================

void FOrganicFloorBuilder::CutDoorwayGaps(
	const FWalkablePolygon& Poly, const TArray<FWalkableDoorway>& Doorways, TArray<FWalkableBoundaryLoop>& OutLoops)
{
	using namespace OrganicFloorBuilderImpl;

	OutLoops.Reset();

	// Gather every ring (outer + holes) as a closed loop candidate.
	TArray<TArray<FVector2D>> Rings;
	if (Poly.Outer.Num() >= 3)
	{
		Rings.Add(Poly.Outer);
	}
	for (const TArray<FVector2D>& Hole : Poly.Holes)
	{
		if (Hole.Num() >= 3)
		{
			Rings.Add(Hole);
		}
	}

	// Densify rings finely enough that any doorway gap lands on existing vertices.  Spacing is a
	// fraction of the smallest doorway width (clamped) so even narrow openings cut cleanly.
	float MinDoorWidth = MAX_FLT;
	for (const FWalkableDoorway& Door : Doorways)
	{
		MinDoorWidth = FMath::Min(MinDoorWidth, Door.Width);
	}
	const float DensifySpacing = Doorways.IsEmpty() ? 0.0f : FMath::Clamp(MinDoorWidth * 0.25f, 10.0f, 200.0f);

	for (const TArray<FVector2D>& SrcRing : Rings)
	{
		const TArray<FVector2D> Ring = (DensifySpacing > 0.0f) ? DensifyRing(SrcRing, DensifySpacing) : SrcRing;
		const int32				N = Ring.Num();

		// Find the doorways that fall on this ring: the doorway center must be near the ring
		// boundary, and the cut removes the arc of vertices within Width/2 of the doorway center.
		TArray<bool> bRemove;
		bRemove.Init(false, N);

		bool bAnyCut = false;

		for (const FWalkableDoorway& Door : Doorways)
		{
			// Locate the nearest ring vertex to the doorway center.
			int32 NearestIdx = INDEX_NONE;
			float NearestDistSq = MAX_FLT;
			for (int32 i = 0; i < N; ++i)
			{
				const float DistSq = FVector2D::DistSquared(Ring[i], Door.Position);
				if (DistSq < NearestDistSq)
				{
					NearestDistSq = DistSq;
					NearestIdx = i;
				}
			}

			if (NearestIdx == INDEX_NONE)
			{
				continue;
			}

			// Reject doorways that are not actually on this ring (e.g. belong to another ring):
			// the nearest vertex must be within roughly one opening-width of the doorway center.
			const float Reach = FMath::Max(Door.Width, 50.0f);
			if (NearestDistSq > Reach * Reach)
			{
				continue;
			}

			// Mark all vertices within Width/2 (arc distance approximated by straight distance to
			// the doorway center) for removal — this opens a gap centered on the doorway.
			const float HalfWidth = Door.Width * 0.5f;
			for (int32 i = 0; i < N; ++i)
			{
				if (FVector2D::Distance(Ring[i], Door.Position) <= HalfWidth)
				{
					bRemove[i] = true;
					bAnyCut = true;
				}
			}

			// Guarantee at least the nearest vertex is removed so a degenerate-narrow doorway
			// still produces a visible opening.
			if (!bAnyCut)
			{
				bRemove[NearestIdx] = true;
				bAnyCut = true;
			}
		}

		if (!bAnyCut)
		{
			// No doorway touched this ring — emit it as a closed loop.
			FWalkableBoundaryLoop Loop;
			Loop.Points = Ring;
			Loop.bClosed = true;
			OutLoops.Add(MoveTemp(Loop));
			continue;
		}

		// Rotate the ring so it starts at a KEPT vertex following a removed vertex, then split into
		// open runs of kept vertices (each run is one open boundary loop). This turns a closed ring
		// with one or more gaps into one open polyline per surviving span.
		int32 StartKept = INDEX_NONE;
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Prev = (i + N - 1) % N;
			if (!bRemove[i] && bRemove[Prev])
			{
				StartKept = i;
				break;
			}
		}

		if (StartKept == INDEX_NONE)
		{
			// Every vertex removed (over-wide doorway) — drop the ring entirely.
			continue;
		}

		TArray<FVector2D> CurrentRun;
		for (int32 k = 0; k < N; ++k)
		{
			const int32 Idx = (StartKept + k) % N;
			if (bRemove[Idx])
			{
				if (CurrentRun.Num() >= 2)
				{
					FWalkableBoundaryLoop Loop;
					Loop.Points = MoveTemp(CurrentRun);
					Loop.bClosed = false;
					OutLoops.Add(MoveTemp(Loop));
				}
				CurrentRun.Reset();
			}
			else
			{
				CurrentRun.Add(Ring[Idx]);
			}
		}

		// Flush the trailing run (the ring is open, so it does not wrap back to the start).
		if (CurrentRun.Num() >= 2)
		{
			FWalkableBoundaryLoop Loop;
			Loop.Points = MoveTemp(CurrentRun);
			Loop.bClosed = false;
			OutLoops.Add(MoveTemp(Loop));
		}
	}
}

// ============================================================================
// FOrganicFloorBuilder — unified floor + wall mesh (B4)
// ============================================================================

void FOrganicFloorBuilder::TriangulateFloorCap(const FWalkablePolygon& Poly, float Z, FMeshData& OutMesh)
{
	using namespace OrganicFloorBuilderImpl;
	using namespace UE::Geometry;

	if (Poly.Outer.Num() < 3)
	{
		return;
	}

	// Build a general polygon with the FWalkablePolygon winding (CCW outer, CW holes).
	// AddHole enforces opposite winding to the outer, which our normalized rings already satisfy.
	FGeneralPolygon2d GP(ToPolygon2d(Poly.Outer));
	for (const TArray<FVector2D>& Hole : Poly.Holes)
	{
		if (Hole.Num() >= 3)
		{
			GP.AddHole(ToPolygon2d(Hole), /*bCheckContainment=*/false, /*bCheckOrientation=*/false);
		}
	}

	// Triangulate with bOutputCCW so every cap triangle is emitted CCW in XY; combined with the
	// +Z flat normal this makes the floor cap front-facing for one-sided materials (matching the
	// up-facing top cap UProceduralMeshFactory produces for Voronoi foundations).
	FConstrainedDelaunay2d Triangulation;
	Triangulation.FillRule = FConstrainedDelaunay2d::EFillRule::Positive;
	Triangulation.bOutputCCW = true;
	Triangulation.Add(GP);
	Triangulation.Triangulate();

	const TArray<FIndex3i>& Tris = Triangulation.Triangles;
	const TArray<FVec2d>&	Verts = Triangulation.Vertices;

	if (Tris.IsEmpty() || Verts.IsEmpty())
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[OrganicFloor] TriangulateFloorCap: CDT produced no triangles"));
		return;
	}

	const int32 BaseIdx = OutMesh.Vertices.Num();
	for (const FVec2d& V : Verts)
	{
		AddFlatVert(FVector2D(static_cast<float>(V.X), static_cast<float>(V.Y)), Z, OutMesh);
	}

	for (const FIndex3i& T : Tris)
	{
		OutMesh.Triangles.Add(BaseIdx + T.A);
		OutMesh.Triangles.Add(BaseIdx + T.B);
		OutMesh.Triangles.Add(BaseIdx + T.C);
	}
}

void FOrganicFloorBuilder::ExtrudeWallLoop(const TArray<FVector2D>& Loop, bool bClosed, float FloorHeight, float WallHeight, FMeshData& OutMesh)
{
	using namespace OrganicFloorBuilderImpl;

	const int32 N = Loop.Num();
	if (N < 2)
	{
		return;
	}

	// One side-quad per consecutive point pair, matching UProceduralMeshFactory::BuildSideGeometry:
	//   four verts (bottom_i, bottom_next, top_i, top_next), accumulated-length U, height V,
	//   side normal = -cross(top_i - bottom_i, bottom_next - bottom_i), winding {0,3,1, 0,2,3}.
	const float TopZ = FloorHeight + WallHeight;
	const int32 SegCount = bClosed ? N : (N - 1);
	float		AccumulatedLength = 0.0f;

	for (int32 i = 0; i < SegCount; ++i)
	{
		const int32 NextIndex = (i + 1) % N;

		const FVector Bottom0(Loop[i].X, Loop[i].Y, FloorHeight);
		const FVector Bottom1(Loop[NextIndex].X, Loop[NextIndex].Y, FloorHeight);
		const FVector Top0(Loop[i].X, Loop[i].Y, TopZ);
		const FVector Top1(Loop[NextIndex].X, Loop[NextIndex].Y, TopZ);

		const int32 Base = OutMesh.Vertices.Num();
		OutMesh.Vertices.Append({ Bottom0, Bottom1, Top0, Top1 });

		const float EdgeLength = FVector2D::Distance(Loop[i], Loop[NextIndex]);
		const float UStart = AccumulatedLength * UVScale;
		const float UEnd = (AccumulatedLength + EdgeLength) * UVScale;
		AccumulatedLength += EdgeLength;

		OutMesh.UVs.Append(
			{ FVector2D(UStart, 0.0f), FVector2D(UEnd, 0.0f), FVector2D(UStart, WallHeight * UVScale), FVector2D(UEnd, WallHeight * UVScale) });

		const FVector SideNormal =
			-FVector::CrossProduct(OutMesh.Vertices[Base + 2] - OutMesh.Vertices[Base], OutMesh.Vertices[Base + 1] - OutMesh.Vertices[Base])
				 .GetSafeNormal();

		for (int32 j = 0; j < 4; ++j)
		{
			OutMesh.Normals.Add(SideNormal);
			OutMesh.VertexColors.Add(FLinearColor::White);
			OutMesh.Tangents.Add(FProcMeshTangent(FVector(1.0f, 0.0f, 0.0f), false));
		}

		OutMesh.Triangles.Append({ Base, Base + 3, Base + 1, Base, Base + 2, Base + 3 });
	}
}

bool FOrganicFloorBuilder::BuildFoundationMesh(const FWalkablePolygon& Poly,
	const TArray<FWalkableBoundaryLoop>&							   WallLoops,
	float															   FloorHeight,
	float															   WallHeight,
	float															   WallThickness,
	FMeshData&														   OutMesh)
{
	OutMesh.Clear();

	// Floor cap from the CLOSED polygon (spans doorway openings).
	TriangulateFloorCap(Poly, FloorHeight, OutMesh);

	// Walls from the (doorway-cut) boundary loops.
	const float SafeWallHeight = FMath::Max(WallHeight, 1.0f);
	for (const FWalkableBoundaryLoop& Loop : WallLoops)
	{
		ExtrudeWallLoop(Loop.Points, Loop.bClosed, FloorHeight, SafeWallHeight, OutMesh);
	}

	const bool bHasGeometry = !OutMesh.Triangles.IsEmpty();

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[OrganicFloor] BuildFoundationMesh: vertices=%d triangles=%d (wallLoops=%d, wallThickness=%.0f)"),
		OutMesh.Vertices.Num(),
		OutMesh.Triangles.Num() / 3,
		WallLoops.Num(),
		WallThickness);

	return bHasGeometry;
}
