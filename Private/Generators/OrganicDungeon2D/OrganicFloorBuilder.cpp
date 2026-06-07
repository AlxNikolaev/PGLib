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

	/**
	 * Hard curvature clamp for a variable-width centerline (B1.5).
	 *
	 * BuildVectorContour offsets every centerline sample outward by its OWN radius to form the
	 * corridor ribbon. At a concave corner whose local radius of curvature falls below the offset
	 * radius, that offset polyline crosses itself, producing a figure-eight lobe that the union +
	 * Delaunay then fill twice — giving inverted normals and overlapping coincident floor patches.
	 *
	 * This helper reconciles geometry and radius so the inner offset can never fold, taming even very
	 * high waviness. For each interior vertex it:
	 *   (a) relaxes the vertex toward its neighbours' chord midpoint when the turn exceeds
	 *       MaxTurnPerVertex, raising the local curvature radius (endpoints are left pinned); and
	 *   (b) caps the stored radius to r_max = SafetyFactor * shorterHalfSegment * tan(halfTurn) so a
	 *       concave corner physically cannot self-cross. The cap only ever REDUCES a radius, and never
	 *       below the polyline's existing minimum radius, so the offset width band is preserved.
	 *
	 * The threshold is derived from the LOCAL geometry (adjacent segment lengths + turn angle), not a
	 * single global angle, so legitimate gentle waviness is left untouched.
	 */
	void ClampCenterlineCurvature(TArray<FVector2D>& Pts, TArray<float>& Rad)
	{
		const int32 N = Pts.Num();
		if (N < 3 || Rad.Num() != N)
		{
			return;
		}

		// Geometry-derived fold safety: keep the offset strictly inside the corner.
		static constexpr float SafetyFactor = 0.95f;
		// Largest per-vertex turn left untouched; sharper turns are relaxed toward the neighbour chord.
		static constexpr float MaxTurnDegPerVertex = 60.0f;
		const float			   MaxTurnRad = FMath::DegreesToRadians(MaxTurnDegPerVertex);
		// Fraction of the over-turn corrected per visit (gentle nudge so corners are tamed, not flattened).
		static constexpr float RelaxStrength = 0.5f;

		// Lower bound for any clamped radius: never shrink the band below its current minimum width.
		float MinRadius = MAX_FLT;
		for (const float R : Rad)
		{
			MinRadius = FMath::Min(MinRadius, R);
		}

		for (int32 i = 1; i + 1 < N; ++i)
		{
			const FVector2D Din = (Pts[i] - Pts[i - 1]).GetSafeNormal();
			const FVector2D Dout = (Pts[i + 1] - Pts[i]).GetSafeNormal();
			if (Din.IsNearlyZero() || Dout.IsNearlyZero())
			{
				continue;
			}

			const float Dot = FMath::Clamp(FVector2D::DotProduct(Din, Dout), -1.0f, 1.0f);
			const float TurnAngle = FMath::Acos(Dot); // 0 = straight, π = reversal
			if (TurnAngle <= KINDA_SMALL_NUMBER)
			{
				continue; // collinear — nothing can fold here
			}

			// (a) Relax the vertex toward the neighbour-chord midpoint when the turn is too sharp; this
			//     raises the local curvature radius so the offset has room to clear the corner. Endpoints
			//     (i == 0 / i == N-1) are intentionally excluded from this loop, keeping them pinned.
			if (TurnAngle > MaxTurnRad)
			{
				const float OverTurn = (TurnAngle - MaxTurnRad) / FMath::Max(PI - MaxTurnRad, KINDA_SMALL_NUMBER);
				const float Strength = RelaxStrength * FMath::Clamp(OverTurn, 0.0f, 1.0f);
				const FVector2D ChordMid = (Pts[i - 1] + Pts[i + 1]) * 0.5f;
				Pts[i] = FMath::Lerp(Pts[i], ChordMid, Strength);
			}

			// (b) Cap the radius against the (possibly relaxed) local geometry so the inner offset stays
			//     inside the corner. r_max = SafetyFactor * shorterHalfSeg * tan(halfTurn).
			const float HalfTurn = TurnAngle * 0.5f;
			const float TanHalf = FMath::Tan(HalfTurn);
			if (TanHalf > KINDA_SMALL_NUMBER)
			{
				const float PrevHalfLen = FVector2D::Distance(Pts[i], Pts[i - 1]) * 0.5f;
				const float NextHalfLen = FVector2D::Distance(Pts[i], Pts[i + 1]) * 0.5f;
				const float ShorterHalf = FMath::Min(PrevHalfLen, NextHalfLen);
				const float RMax = SafetyFactor * ShorterHalf * TanHalf;

				// Only ever reduce, and never below the band's existing minimum width.
				const float ClampedR = FMath::Clamp(RMax, MinRadius, Rad[i]);
				Rad[i] = ClampedR;
			}
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

	/**
	 * Inset a boundary loop inward by Thickness, producing the inner-face polyline that runs parallel
	 * to the outer wall face Thickness units toward the walkable interior.
	 *
	 * The inward direction for an edge whose travel direction is D is left-of-travel: (-D.Y, D.X).
	 * This is correct for every loop the wall builder receives because the rings are normalized to the
	 * FWalkablePolygon convention (CCW outer / CW holes), and CutDoorwayGaps walks each ring in its
	 * original order WITHOUT reversing it — so an open (doorway-cut) span inherits its source ring's
	 * winding and the same left-of-travel rule points toward the interior for both open and closed
	 * loops. Each interior vertex averages the inward normals of its two adjacent edges; open-loop
	 * endpoints use their single adjacent edge so the ends do not splay.
	 *
	 * (Pin this assumption: if a future change makes CutDoorwayGaps reverse open spans, the inner
	 *  faces emitted here would silently flip inward/outward.)
	 */
	TArray<FVector2D> InsetLoop(const TArray<FVector2D>& Loop, bool bClosed, float Thickness)
	{
		const int32 N = Loop.Num();
		TArray<FVector2D> Inset;
		Inset.SetNumUninitialized(N);
		if (N < 2)
		{
			for (int32 i = 0; i < N; ++i)
			{
				Inset[i] = Loop[i];
			}
			return Inset;
		}

		const int32 SegCount = bClosed ? N : (N - 1);

		// Per-edge inward unit normal (left of travel). For a closed loop there are N edges; for an
		// open loop there are N-1 edges (indexed by their start vertex).
		TArray<FVector2D> EdgeNormal;
		EdgeNormal.SetNumZeroed(SegCount);
		for (int32 i = 0; i < SegCount; ++i)
		{
			const int32		Next = (i + 1) % N;
			const FVector2D D = (Loop[Next] - Loop[i]).GetSafeNormal();
			EdgeNormal[i] = FVector2D(-D.Y, D.X);
		}

		for (int32 i = 0; i < N; ++i)
		{
			FVector2D NSum(0.0f, 0.0f);

			if (bClosed)
			{
				// Average the incoming edge (ending at i) and the outgoing edge (starting at i).
				const int32 PrevEdge = (i + SegCount - 1) % SegCount;
				NSum = EdgeNormal[PrevEdge] + EdgeNormal[i];
			}
			else
			{
				// Open loop: endpoints have a single adjacent edge; interior vertices average two.
				if (i > 0)
				{
					NSum += EdgeNormal[i - 1]; // incoming edge ends at i
				}
				if (i < SegCount)
				{
					NSum += EdgeNormal[i]; // outgoing edge starts at i
				}
			}

			FVector2D Dir = NSum.GetSafeNormal();
			if (Dir.IsNearlyZero())
			{
				// Near-reversal corner: averaged normal collapses; fall back to either adjacent edge.
				if (bClosed)
				{
					Dir = EdgeNormal[i % SegCount];
				}
				else
				{
					Dir = (i < SegCount) ? EdgeNormal[i] : EdgeNormal[i - 1];
				}
			}

			Inset[i] = Loop[i] + Dir * Thickness;
		}

		return Inset;
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

	// Hard curvature clamp on the smoothed centerline so the variable-width ribbon that
	// BuildVectorContour offsets from it can never self-intersect at sharp (high-waviness) turns.
	// Endpoints stay pinned and radii are only ever reduced within their existing range.
	OrganicFloorBuilderImpl::ClampCenterlineCurvature(Pts, Rad);

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
			FVector2D PrevPerp(0.0f, 0.0f);
			FVector2D NextPerp(0.0f, 0.0f);
			if (i > 0)
			{
				const FVector2D D = (Smoothed[i] - Smoothed[i - 1]).GetSafeNormal();
				PrevPerp = FVector2D(-D.Y, D.X);
				NSum += PrevPerp;
			}
			if (i + 1 < N)
			{
				const FVector2D D = (Smoothed[i + 1] - Smoothed[i]).GetSafeNormal();
				NextPerp = FVector2D(-D.Y, D.X);
				NSum += NextPerp;
			}

			Normals[i] = NSum.GetSafeNormal();

			// At a hard corner where the incoming and outgoing segments nearly reverse, the averaged
			// normal collapses toward zero, which would pinch the ribbon to zero width. Fall back to a
			// single adjacent segment's perpendicular so the offset always has a valid direction.
			if (Normals[i].IsNearlyZero())
			{
				Normals[i] = !NextPerp.IsNearlyZero() ? NextPerp : PrevPerp;
			}
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

	// Defensive emission: bOutputCCW already winds well-formed triangles CCW, but the PolygonsUnion
	// fallback path (bCopyInputOnFailure) can pass folded/back-facing input straight through. Every
	// surviving cap triangle must be CCW in XY so its unconditional +Z normal stays front-facing, so
	// guard against zero-area degenerates and corrective-flip any CW (negative-area) triangle.
	static constexpr double TriAreaEpsilon = 1.0; // world-unit² — well below any real cap triangle
	for (const FIndex3i& T : Tris)
	{
		const FVec2d& A = Verts[T.A];
		const FVec2d& B = Verts[T.B];
		const FVec2d& C = Verts[T.C];

		// Twice the signed area (shoelace). Positive = CCW (front-facing against +Z), negative = CW.
		const double SignedArea2x = (B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y);
		if (FMath::Abs(SignedArea2x) < TriAreaEpsilon)
		{
			continue; // drop degenerate / zero-area sliver
		}

		OutMesh.Triangles.Add(BaseIdx + T.A);
		if (SignedArea2x < 0.0)
		{
			// CW triangle (only the fallback path should reach here): swap B/C to face +Z.
			OutMesh.Triangles.Add(BaseIdx + T.C);
			OutMesh.Triangles.Add(BaseIdx + T.B);
		}
		else
		{
			OutMesh.Triangles.Add(BaseIdx + T.B);
			OutMesh.Triangles.Add(BaseIdx + T.C);
		}
	}
}

void FOrganicFloorBuilder::ExtrudeWallLoop(
	const TArray<FVector2D>& Loop, bool bClosed, float FloorHeight, float WallHeight, float WallThickness, FMeshData& OutMesh)
{
	using namespace OrganicFloorBuilderImpl;

	const int32 N = Loop.Num();
	if (N < 2)
	{
		return;
	}

	const float TopZ = FloorHeight + WallHeight;
	const int32 SegCount = bClosed ? N : (N - 1);

	// Inner-face polyline: the loop inset inward (toward the walkable interior) by WallThickness.
	const TArray<FVector2D> Inner = InsetLoop(Loop, bClosed, WallThickness);

	// One side-quad per consecutive point pair, matching UProceduralMeshFactory::BuildSideGeometry:
	//   four verts (bottom_i, bottom_next, top_i, top_next), accumulated-length U, height V,
	//   side normal = -cross(top_i - bottom_i, bottom_next - bottom_i), winding {0,3,1, 0,2,3}.
	// bReverse flips the winding and side normal so the inner face points the opposite way (into the
	// walkable interior) from the outer face.
	const auto EmitSideQuad = [&](const FVector2D& P0, const FVector2D& P1, float& AccumLength, bool bReverse)
	{
		const FVector Bottom0(P0.X, P0.Y, FloorHeight);
		const FVector Bottom1(P1.X, P1.Y, FloorHeight);
		const FVector Top0(P0.X, P0.Y, TopZ);
		const FVector Top1(P1.X, P1.Y, TopZ);

		const int32 Base = OutMesh.Vertices.Num();
		OutMesh.Vertices.Append({ Bottom0, Bottom1, Top0, Top1 });

		const float EdgeLength = FVector2D::Distance(P0, P1);
		const float UStart = AccumLength * UVScale;
		const float UEnd = (AccumLength + EdgeLength) * UVScale;
		AccumLength += EdgeLength;

		OutMesh.UVs.Append(
			{ FVector2D(UStart, 0.0f), FVector2D(UEnd, 0.0f), FVector2D(UStart, WallHeight * UVScale), FVector2D(UEnd, WallHeight * UVScale) });

		FVector SideNormal =
			-FVector::CrossProduct(OutMesh.Vertices[Base + 2] - OutMesh.Vertices[Base], OutMesh.Vertices[Base + 1] - OutMesh.Vertices[Base])
				 .GetSafeNormal();
		if (bReverse)
		{
			SideNormal = -SideNormal;
		}

		for (int32 j = 0; j < 4; ++j)
		{
			OutMesh.Normals.Add(SideNormal);
			OutMesh.VertexColors.Add(FLinearColor::White);
			OutMesh.Tangents.Add(FProcMeshTangent(FVector(1.0f, 0.0f, 0.0f), false));
		}

		if (bReverse)
		{
			OutMesh.Triangles.Append({ Base, Base + 1, Base + 3, Base, Base + 3, Base + 2 });
		}
		else
		{
			OutMesh.Triangles.Append({ Base, Base + 3, Base + 1, Base, Base + 2, Base + 3 });
		}
	};

	// --- Outer wall face (byte-identical to the legacy single-curtain recipe). ---
	float OuterAccum = 0.0f;
	for (int32 i = 0; i < SegCount; ++i)
	{
		const int32 NextIndex = (i + 1) % N;
		EmitSideQuad(Loop[i], Loop[NextIndex], OuterAccum, /*bReverse=*/false);
	}

	// --- Inner wall face (inset copy, reversed so it faces the walkable interior). ---
	float InnerAccum = 0.0f;
	for (int32 i = 0; i < SegCount; ++i)
	{
		const int32 NextIndex = (i + 1) % N;
		EmitSideQuad(Inner[i], Inner[NextIndex], InnerAccum, /*bReverse=*/true);
	}

	// --- Top cap strip: bridge outer-top to inner-top at Z = TopZ, facing up (+Z). ---
	// One quad per wall segment; open loops emit no cap across the doorway gap (only per-segment caps)
	// so doorway openings stay open through the full wall thickness.
	for (int32 i = 0; i < SegCount; ++i)
	{
		const int32 NextIndex = (i + 1) % N;

		const FVector OuterTop0(Loop[i].X, Loop[i].Y, TopZ);
		const FVector OuterTop1(Loop[NextIndex].X, Loop[NextIndex].Y, TopZ);
		const FVector InnerTop0(Inner[i].X, Inner[i].Y, TopZ);
		const FVector InnerTop1(Inner[NextIndex].X, Inner[NextIndex].Y, TopZ);

		const int32 Base = OutMesh.Vertices.Num();
		OutMesh.Vertices.Append({ OuterTop0, OuterTop1, InnerTop1, InnerTop0 });

		// Flat top-cap UVs from world XY (matching AddFlatVert's flat-UV convention).
		OutMesh.UVs.Append({ FVector2D(Loop[i].X * UVScale, Loop[i].Y * UVScale),
			FVector2D(Loop[NextIndex].X * UVScale, Loop[NextIndex].Y * UVScale),
			FVector2D(Inner[NextIndex].X * UVScale, Inner[NextIndex].Y * UVScale),
			FVector2D(Inner[i].X * UVScale, Inner[i].Y * UVScale) });

		for (int32 j = 0; j < 4; ++j)
		{
			OutMesh.Normals.Add(FVector(0.0f, 0.0f, 1.0f));
			OutMesh.VertexColors.Add(FLinearColor::White);
			OutMesh.Tangents.Add(FProcMeshTangent(FVector(1.0f, 0.0f, 0.0f), false));
		}

		// Emit the two cap triangles CCW in XY so they stay front-facing against the +Z normal. The
		// quad's XY winding depends on the loop's travel direction (outer CCW vs hole CW), so pick the
		// triangle order from the first triangle's signed area rather than assuming one fixed order.
		const double SignedArea2x =
			(OuterTop1.X - OuterTop0.X) * (InnerTop1.Y - OuterTop0.Y) - (InnerTop1.X - OuterTop0.X) * (OuterTop1.Y - OuterTop0.Y);
		if (SignedArea2x >= 0.0)
		{
			// CCW quad (0,1,2,3): split into {0,1,2} and {0,2,3}.
			OutMesh.Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}
		else
		{
			// CW quad: reverse so both triangles are CCW (front-facing up).
			OutMesh.Triangles.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
		}
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

	// Walls from the (doorway-cut) boundary loops. Walls are true-thick: an outer face, an inner face
	// inset inward by WallThickness, and a top cap bridging the two. Clamp thickness to a small
	// positive minimum so a zero/negative value can never collapse the inner face onto the outer one.
	const float SafeWallHeight = FMath::Max(WallHeight, 1.0f);
	const float SafeWallThickness = FMath::Max(WallThickness, 1.0f);
	for (const FWalkableBoundaryLoop& Loop : WallLoops)
	{
		ExtrudeWallLoop(Loop.Points, Loop.bClosed, FloorHeight, SafeWallHeight, SafeWallThickness, OutMesh);
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
