// Fill out your copyright notice in the Description page of Project Settings.

#include "Generators/OrganicDungeon2D/OrganicFloorBuilder.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Factories/ProceduralMeshFactory.h"
#include "ProceduralGeometry.h"

// ============================================================================
// Internal helpers — local to this translation unit.
// ============================================================================

namespace OrganicFloorBuilderImpl
{
	/** Flat floor UV scale (world units → UV coords), matching the foundation mesh UVs. */
	static constexpr float UVScale = 0.01f;

	/** Maximum number of vertices per extracted loop before we cap iteration (degenerate guard). */
	static constexpr int32 MaxLoopVerts = 65536;

	// -------------------------------------------------------------------------
	// Math helpers
	// -------------------------------------------------------------------------

	/** 2D signed cross product of vectors (A-O) and (B-O).  Positive = CCW turn. */
	FORCEINLINE float Cross2D(const FVector2D& O, const FVector2D& A, const FVector2D& B)
	{
		return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X);
	}

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

	/**
	 * Ray-casting point-in-polygon (world-space 2D).
	 * Returns true when Point is strictly inside Polygon (winding order independent).
	 */
	bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Poly)
	{
		const int32 N = Poly.Num();
		bool		bInside = false;
		for (int32 i = 0, j = N - 1; i < N; j = i++)
		{
			const FVector2D& A = Poly[i];
			const FVector2D& B = Poly[j];
			if (((A.Y > Point.Y) != (B.Y > Point.Y)) && (Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	/**
	 * Returns true when vertex k is strictly inside the triangle (A, B, C) —
	 * using the sign-of-cross-product test (works for both CW and CCW triangles).
	 */
	bool IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		const float d1 = Cross2D(P, A, B);
		const float d2 = Cross2D(P, B, C);
		const float d3 = Cross2D(P, C, A);
		const bool	bHasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
		const bool	bHasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
		return !(bHasNeg && bHasPos); // all-same-sign → inside or on edge
	}

	// -------------------------------------------------------------------------
	// Mesh emission helpers
	// -------------------------------------------------------------------------

	/** Append one flat-floor vertex (Z = FloorHeight, normal up, tangent +X). */
	void AddFlatVert(const FVector2D& XY, float Z, FMeshData& Mesh)
	{
		Mesh.Vertices.Add(FVector(XY.X, XY.Y, Z));
		Mesh.Normals.Add(FVector(0.0f, 0.0f, 1.0f));
		Mesh.UVs.Add(FVector2D(XY.X * UVScale, XY.Y * UVScale));
		Mesh.VertexColors.Add(FLinearColor::White);
		Mesh.Tangents.Add(FProcMeshTangent(FVector(1.0f, 0.0f, 0.0f), false));
	}

	/** Append four flat-floor vertices and two CCW triangles forming a quad. */
	void EmitQuad(const FVector2D& V0, const FVector2D& V1, const FVector2D& V2, const FVector2D& V3, float Z, FMeshData& Mesh)
	{
		const int32 Base = Mesh.Vertices.Num();
		AddFlatVert(V0, Z, Mesh);
		AddFlatVert(V1, Z, Mesh);
		AddFlatVert(V2, Z, Mesh);
		AddFlatVert(V3, Z, Mesh);
		// Triangle 0: Base, Base+1, Base+2
		Mesh.Triangles.Add(Base);
		Mesh.Triangles.Add(Base + 1);
		Mesh.Triangles.Add(Base + 2);
		// Triangle 1: Base, Base+2, Base+3
		Mesh.Triangles.Add(Base);
		Mesh.Triangles.Add(Base + 2);
		Mesh.Triangles.Add(Base + 3);
	}

	// -------------------------------------------------------------------------
	// Collinear simplification
	// -------------------------------------------------------------------------

	/**
	 * Remove collinear vertices from a closed polygon.
	 * A vertex V[i] is collinear when Cross2D(V[i-1], V[i], V[i+1]) == 0.
	 * Uses a small epsilon to tolerate floating-point grid noise.
	 */
	TArray<FVector2D> SimplifyCollinear(const TArray<FVector2D>& Poly)
	{
		TArray<FVector2D> Out;
		const int32		  N = Poly.Num();
		if (N < 3)
			return Poly;

		Out.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& Prev = Poly[(i + N - 1) % N];
			const FVector2D& Curr = Poly[i];
			const FVector2D& Next = Poly[(i + 1) % N];
			// Keep vertex only when it is NOT collinear with its neighbours.
			if (FMath::Abs(Cross2D(Prev, Curr, Next)) > KINDA_SMALL_NUMBER)
			{
				Out.Add(Curr);
			}
		}
		return Out;
	}

	// -------------------------------------------------------------------------
	// Ear-clipping triangulation (simple polygon, CCW winding)
	// -------------------------------------------------------------------------

	/**
	 * Ear-clipping triangulation for a simple convex-or-concave polygon.
	 *
	 * Assumes CCW winding (positive signed area).  O(n²) per pass; for the grid-contour
	 * polygons produced after collinear simplification, n is small even for large grids
	 * because most runs of cells share straight edges that collapse to a single vertex.
	 *
	 * A degenerate guard (MaxIter = n²) prevents infinite loops on pathological input.
	 */
	void EarClipSimple(const TArray<FVector2D>& Poly, float Z, FMeshData& OutMesh)
	{
		const int32 N = Poly.Num();
		if (N < 3)
			return;

		// Add all polygon vertices to the mesh once; triangles reference these by index offset.
		const int32 BaseIdx = OutMesh.Vertices.Num();
		for (const FVector2D& V : Poly)
		{
			AddFlatVert(V, Z, OutMesh);
		}

		// Working ring of indices into Poly (removed as ears are clipped).
		TArray<int32> Ring;
		Ring.SetNum(N);
		for (int32 i = 0; i < N; ++i)
			Ring[i] = i;

		const int32 MaxIter = N * N + N;
		int32		Iter = 0;

		while (Ring.Num() > 3 && ++Iter <= MaxIter)
		{
			const int32 Cnt = Ring.Num();
			bool		bFoundEar = false;

			for (int32 i = 0; i < Cnt; ++i)
			{
				const int32 Pi = (i + Cnt - 1) % Cnt;
				const int32 Ni = (i + 1) % Cnt;

				const FVector2D& Pv = Poly[Ring[Pi]];
				const FVector2D& Cv = Poly[Ring[i]];
				const FVector2D& Nv = Poly[Ring[Ni]];

				// For a CCW polygon a convex vertex has positive cross product.
				if (Cross2D(Pv, Cv, Nv) <= KINDA_SMALL_NUMBER)
				{
					continue; // reflex or degenerate vertex — not an ear
				}

				// Verify no other ring vertex lies inside the candidate ear triangle.
				bool bIsEar = true;
				for (int32 k = 0; k < Cnt; ++k)
				{
					if (k == Pi || k == i || k == Ni)
						continue;
					if (IsPointInTriangle(Poly[Ring[k]], Pv, Cv, Nv))
					{
						bIsEar = false;
						break;
					}
				}

				if (bIsEar)
				{
					OutMesh.Triangles.Add(BaseIdx + Ring[Pi]);
					OutMesh.Triangles.Add(BaseIdx + Ring[i]);
					OutMesh.Triangles.Add(BaseIdx + Ring[Ni]);
					Ring.RemoveAt(i);
					bFoundEar = true;
					break;
				}
			}

			if (!bFoundEar)
			{
				// Degenerate polygon — no ear found in a full pass.
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[OrganicFloor] EarClipSimple: no ear found with %d ring vertices remaining; stopping"),
					Ring.Num());
				break;
			}
		}

		// Emit the final triangle (Ring has exactly 3 vertices).
		if (Ring.Num() == 3)
		{
			OutMesh.Triangles.Add(BaseIdx + Ring[0]);
			OutMesh.Triangles.Add(BaseIdx + Ring[1]);
			OutMesh.Triangles.Add(BaseIdx + Ring[2]);
		}
	}

} // namespace OrganicFloorBuilderImpl

// ============================================================================
// FOrganicFloorBuilder — public API
// ============================================================================

void FOrganicFloorBuilder::ComputeWalkableContour(const FOrganicDungeonGridData& Grid, FWalkableRegionContour& Out)
{
	Out.Polygons.Reset();

	const int32 W = Grid.GridWidth;
	const int32 H = Grid.GridHeight;

	if (W <= 0 || H <= 0 || Grid.Grid.Num() != W * H)
	{
		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[OrganicFloor] ComputeWalkableContour: empty/invalid grid (%dx%d grid=%d), returning empty contour"),
			W,
			H,
			Grid.Grid.Num());
		return;
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[OrganicFloor] ComputeWalkableContour: grid=%dx%d cellSize=%.0f"), W, H, Grid.CellSize);

	// -------------------------------------------------------------------------
	// Step 1: Build directed boundary-edge map.
	//
	// Convention: floor cell is on the RIGHT of the directed edge.
	//
	// For each floor cell (cx, cy) with a non-floor or out-of-bounds neighbour:
	//   West  non-floor: (cx,   cy+1) → (cx,   cy  )   [southward along left edge]
	//   East  non-floor: (cx+1, cy  ) → (cx+1, cy+1)   [northward along right edge]
	//   South non-floor: (cx,   cy  ) → (cx+1, cy  )   [eastward along bottom edge]
	//   North non-floor: (cx+1, cy+1) → (cx,   cy+1)   [westward along top edge]
	//
	// This convention produces a polygon with POSITIVE signed area for an outer
	// boundary (CCW in standard Y-up math coordinates).
	// -------------------------------------------------------------------------

	// Adjacency list for directed boundary edges (start → list of outgoing ends).
	// Using a per-vertex array instead of a single-value TMap handles pinch-point topology:
	// at a diagonal pinch (two floor cells touching only at a shared corner) the same vertex
	// is the start of TWO outgoing boundary edges; a simple TMap would overwrite the first.
	// TMap grows safely if the Reserve hint underestimates (worst case ≈ 4×FloorCells edges).
	TMap<FIntPoint, TArray<FIntPoint>> EdgeMap;
	EdgeMap.Reserve(2 * (W + H));

	auto IsFloor = [&](int32 cx, int32 cy) -> bool {
		if (cx < 0 || cx >= W || cy < 0 || cy >= H)
			return false;
		return Grid.Grid[cy * W + cx];
	};

	for (int32 cy = 0; cy < H; ++cy)
	{
		for (int32 cx = 0; cx < W; ++cx)
		{
			if (!Grid.Grid[cy * W + cx])
				continue;

			// West non-floor
			if (!IsFloor(cx - 1, cy))
				EdgeMap.FindOrAdd(FIntPoint(cx, cy + 1)).Add(FIntPoint(cx, cy));
			// East non-floor
			if (!IsFloor(cx + 1, cy))
				EdgeMap.FindOrAdd(FIntPoint(cx + 1, cy)).Add(FIntPoint(cx + 1, cy + 1));
			// South non-floor
			if (!IsFloor(cx, cy - 1))
				EdgeMap.FindOrAdd(FIntPoint(cx, cy)).Add(FIntPoint(cx + 1, cy));
			// North non-floor
			if (!IsFloor(cx, cy + 1))
				EdgeMap.FindOrAdd(FIntPoint(cx + 1, cy + 1)).Add(FIntPoint(cx, cy + 1));
		}
	}

	if (EdgeMap.IsEmpty())
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[OrganicFloor] ComputeWalkableContour: no boundary edges — grid has no floor cells"));
		return;
	}

	// -------------------------------------------------------------------------
	// Step 2: Trace closed loops by following the directed edge chains.
	// -------------------------------------------------------------------------

	// Loops in GRID vertex coordinates (integer, before world-space conversion).
	TArray<TArray<FIntPoint>> GridLoops;

	while (!EdgeMap.IsEmpty())
	{
		// Pick the lexicographically smallest start key for deterministic output.
		FIntPoint StartVert = EdgeMap.begin().Key();
		for (auto& Kvp : EdgeMap)
		{
			if (Kvp.Value.IsEmpty())
				continue;
			if (Kvp.Key.X < StartVert.X || (Kvp.Key.X == StartVert.X && Kvp.Key.Y < StartVert.Y))
			{
				StartVert = Kvp.Key;
			}
		}

		TArray<FIntPoint> Loop;
		Loop.Reserve(64);

		FIntPoint Current = StartVert;
		int32	  Guard = 0;

		while (true)
		{
			TArray<FIntPoint>* NextList = EdgeMap.Find(Current);
			if (!NextList || NextList->IsEmpty())
			{
				// Dangling edge — should not happen for a valid closed loop.
				// Can occur at pinch points if all outgoing edges were already consumed.
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[OrganicFloor] ComputeWalkableContour: dangling edge at (%d,%d); loop discarded"),
					Current.X,
					Current.Y);
				break;
			}

			Loop.Add(Current);
			// Take the last outgoing edge from this vertex; remove the entry when exhausted.
			FIntPoint Next = NextList->Last();
			NextList->Pop(EAllowShrinking::No);
			if (NextList->IsEmpty())
			{
				EdgeMap.Remove(Current);
			}
			Current = Next;

			if (Current == StartVert)
			{
				break; // closed loop
			}

			if (++Guard >= OrganicFloorBuilderImpl::MaxLoopVerts)
			{
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[OrganicFloor] ComputeWalkableContour: loop exceeded %d vertices guard; truncated"),
					OrganicFloorBuilderImpl::MaxLoopVerts);
				break;
			}
		}

		if (Loop.Num() >= 3)
		{
			GridLoops.Add(MoveTemp(Loop));
		}
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[OrganicFloor] ComputeWalkableContour: extracted %d raw loops"), GridLoops.Num());

	// -------------------------------------------------------------------------
	// Step 3: Convert to world-space, simplify collinear vertices, classify
	//         outer (CCW) vs hole (CW) by signed area.
	// -------------------------------------------------------------------------

	struct FClassifiedLoop
	{
		TArray<FVector2D> Verts; // world-space
		float			  SignedArea;
	};

	TArray<FClassifiedLoop> Classified;
	Classified.Reserve(GridLoops.Num());

	const FVector2D& Origin = Grid.GridOriginWorld;
	const float		 Cell = Grid.CellSize;

	for (const TArray<FIntPoint>& GLoop : GridLoops)
	{
		// Convert grid vertex coords to world XY.
		TArray<FVector2D> WorldVerts;
		WorldVerts.Reserve(GLoop.Num());
		for (const FIntPoint& GV : GLoop)
		{
			WorldVerts.Add(Origin + FVector2D(GV.X * Cell, GV.Y * Cell));
		}

		// Remove collinear vertices (improves triangulation performance for axis-aligned shapes).
		WorldVerts = OrganicFloorBuilderImpl::SimplifyCollinear(WorldVerts);
		if (WorldVerts.Num() < 3)
			continue;

		FClassifiedLoop CL;
		CL.Verts = MoveTemp(WorldVerts);
		CL.SignedArea = OrganicFloorBuilderImpl::SignedArea2D(CL.Verts);
		Classified.Add(MoveTemp(CL));
	}

	// -------------------------------------------------------------------------
	// Step 4: Group holes into outer polygons via point-in-polygon.
	// -------------------------------------------------------------------------

	// Partition into outers and holes.
	TArray<int32> OuterIndices;
	TArray<int32> HoleIndices;
	for (int32 i = 0; i < Classified.Num(); ++i)
	{
		if (Classified[i].SignedArea > 0.0f)
			OuterIndices.Add(i);
		else
			HoleIndices.Add(i);
	}

	// Build one FWalkablePolygon per outer ring.
	Out.Polygons.Reserve(OuterIndices.Num());
	for (int32 OI : OuterIndices)
	{
		FWalkablePolygon WP;
		WP.Outer = Classified[OI].Verts;
		Out.Polygons.Add(MoveTemp(WP));
	}

	// Assign each hole to the first outer polygon that contains it.
	for (int32 HI : HoleIndices)
	{
		if (Classified[HI].Verts.IsEmpty())
			continue;
		const FVector2D& TestPt = Classified[HI].Verts[0];

		bool bAssigned = false;
		for (FWalkablePolygon& WP : Out.Polygons)
		{
			if (OrganicFloorBuilderImpl::IsPointInPolygon(TestPt, WP.Outer))
			{
				WP.Holes.Add(Classified[HI].Verts);
				bAssigned = true;
				break;
			}
		}

		if (!bAssigned && !Out.Polygons.IsEmpty())
		{
			// Fallback: attach to the first outer polygon (handles rare floating-point edge cases).
			Out.Polygons[0].Holes.Add(Classified[HI].Verts);
		}
	}

	int32 TotalHoles = 0;
	for (const FWalkablePolygon& WP : Out.Polygons)
		TotalHoles += WP.Holes.Num();

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[OrganicFloor] ComputeWalkableContour: %d outer polygon(s), %d hole(s)"), Out.Polygons.Num(), TotalHoles);
}

// ============================================================================

void FOrganicFloorBuilder::EmitRibbonQuad(const FVector2D& P0, float R0, const FVector2D& P1, float R1, float Z, FMeshData& OutMesh)
{
	// Perpendicular direction to the segment P0→P1 (normalized, 90° CCW).
	const FVector2D Dir = (P1 - P0);
	const float		Len = Dir.Size();
	if (Len < KINDA_SMALL_NUMBER)
		return; // degenerate segment

	// Perpendicular: rotate Dir by 90° CCW.
	const FVector2D Perp = FVector2D(-Dir.Y, Dir.X) / Len;

	const FVector2D Left0 = P0 + Perp * R0;
	const FVector2D Right0 = P0 - Perp * R0;
	const FVector2D Left1 = P1 + Perp * R1;
	const FVector2D Right1 = P1 - Perp * R1;

	// Emit quad CCW when viewed from above (+Z): Right0 → Right1 → Left1 → Left0.
	// This matches EmitRoomRect (BL→BR→TR→TL) and EarClipSimple (CCW outer ring) so all
	// triangles in the merged section share the same winding and face up with one-sided materials.
	OrganicFloorBuilderImpl::EmitQuad(Right0, Right1, Left1, Left0, Z, OutMesh);
}

void FOrganicFloorBuilder::EmitRoomRect(const FOrganicRoom& Room, float Z, FMeshData& OutMesh)
{
	const float DegRad = FMath::DegreesToRadians(Room.RotationDeg);
	const float C = FMath::Cos(DegRad);
	const float S = FMath::Sin(DegRad);

	// OBB axes.
	const FVector2D AxisX(C, S);
	const FVector2D AxisY(-S, C);

	// Four OBB corners.
	const FVector2D BL = Room.Center - AxisX * Room.HalfExtent.X - AxisY * Room.HalfExtent.Y;
	const FVector2D BR = Room.Center + AxisX * Room.HalfExtent.X - AxisY * Room.HalfExtent.Y;
	const FVector2D TR = Room.Center + AxisX * Room.HalfExtent.X + AxisY * Room.HalfExtent.Y;
	const FVector2D TL = Room.Center - AxisX * Room.HalfExtent.X + AxisY * Room.HalfExtent.Y;

	// Emit quad: BL, BR, TR, TL — CCW when viewed from above.
	OrganicFloorBuilderImpl::EmitQuad(BL, BR, TR, TL, Z, OutMesh);
}

void FOrganicFloorBuilder::TriangulateWithHoles(const FWalkablePolygon& Poly, float Z, FMeshData& OutMesh)
{
	if (Poly.Outer.Num() < 3)
		return;

	if (Poly.Holes.IsEmpty())
	{
		// Common case: no holes — triangulate the outer ring directly.
		OrganicFloorBuilderImpl::EarClipSimple(Poly.Outer, Z, OutMesh);
		return;
	}

	// --- Hole merging via horizontal bridge (right-most vertex of hole → outer ring) ---
	//
	// We merge all holes into one combined outer polygon, one hole at a time.
	// Each hole is merged by:
	//   1. Finding the hole vertex with maximum X  (the "bridge point" H).
	//   2. Casting a +X ray from H to find the nearest outer-polygon edge intersection.
	//   3. Identifying the outer vertex with the largest X on the intersected edge side.
	//   4. Splicing the hole into the outer polygon at that vertex via duplicated bridge verts.
	//
	// After all holes are merged the result is one simple polygon, passed to EarClipSimple.

	// Sort holes by decreasing rightmost-X so each successive bridge is added to an
	// already-updated polygon (avoids incorrect visibility when bridges overlap).
	// Pre-compute maxX per hole to avoid pointer-comparator template deduction issues.
	TArray<TPair<float, int32>> HoleSortKeys;
	HoleSortKeys.Reserve(Poly.Holes.Num());
	for (int32 Hi = 0; Hi < Poly.Holes.Num(); ++Hi)
	{
		float MaxX = -MAX_FLT;
		for (const FVector2D& V : Poly.Holes[Hi])
			MaxX = FMath::Max(MaxX, (float)V.X);
		HoleSortKeys.Add(TPair<float, int32>(MaxX, Hi));
	}
	HoleSortKeys.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });

	// Start with a copy of the outer polygon that we progressively splice holes into.
	TArray<FVector2D> Merged = Poly.Outer;

	for (const TPair<float, int32>& SortedEntry : HoleSortKeys)
	{
		const TArray<FVector2D>& Hole = Poly.Holes[SortedEntry.Value];
		if (Hole.Num() < 3)
			continue;

		// 1. Find the hole vertex with maximum X.
		int32 BridgeHoleIdx = 0;
		for (int32 i = 1; i < Hole.Num(); ++i)
		{
			if (Hole[i].X > Hole[BridgeHoleIdx].X)
				BridgeHoleIdx = i;
		}
		const FVector2D& HoleVert = Hole[BridgeHoleIdx];

		// 2. Find the nearest outer polygon vertex to the right of HoleVert.X that has a
		//    Y coordinate closest to HoleVert.Y (simple nearest-right-vertex strategy).
		int32 BridgeOuterIdx = -1;
		float BestDist = MAX_FLT;

		for (int32 i = 0; i < Merged.Num(); ++i)
		{
			const FVector2D& OV = Merged[i];
			if (OV.X < HoleVert.X)
				continue; // must be to the right or at same X
			const float Dist = FVector2D::DistSquared(HoleVert, OV);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BridgeOuterIdx = i;
			}
		}

		if (BridgeOuterIdx < 0)
		{
			// Fallback: no outer vertex to the right — just use the nearest overall.
			for (int32 i = 0; i < Merged.Num(); ++i)
			{
				const float Dist = FVector2D::DistSquared(HoleVert, Merged[i]);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BridgeOuterIdx = i;
				}
			}
		}

		if (BridgeOuterIdx < 0)
			continue; // empty outer ring — shouldn't happen

		// 3. Splice the hole into Merged.
		// New ring: Merged[0..BridgeOuterIdx], BridgeOuter, Hole[BridgeHoleIdx..N-1],
		//           Hole[0..BridgeHoleIdx], BridgeHoleVert (duplicate), Merged[BridgeOuterIdx..end].
		TArray<FVector2D> NewMerged;
		NewMerged.Reserve(Merged.Num() + Hole.Num() + 2);

		for (int32 i = 0; i <= BridgeOuterIdx; ++i)
			NewMerged.Add(Merged[i]);

		// Walk hole starting from BridgeHoleIdx, wrapping around.
		for (int32 k = 0; k <= Hole.Num(); ++k)
			NewMerged.Add(Hole[(BridgeHoleIdx + k) % Hole.Num()]);

		// Duplicate bridge vertices to close the bridge seam.
		NewMerged.Add(Merged[BridgeOuterIdx]);

		for (int32 i = BridgeOuterIdx + 1; i < Merged.Num(); ++i)
			NewMerged.Add(Merged[i]);

		Merged = MoveTemp(NewMerged);
	}

	// Triangulate the merged simple polygon.
	OrganicFloorBuilderImpl::EarClipSimple(Merged, Z, OutMesh);
}

// ============================================================================

bool FOrganicFloorBuilder::BuildCorridorFloorMesh(const FOrganicDungeonGridData& Grid, float FloorHeight, FMeshData& OutMesh)
{
	OutMesh.Clear();

	if (Grid.bUseGridContourFloor)
	{
		// --- Grid-contour path ---
		// Triangulate the pre-computed walkable contour polygons.
		if (Grid.WalkableContour.Polygons.IsEmpty())
		{
			UE_LOG(LogRoguelikeGeometry,
				Warning,
				TEXT("[OrganicFloor] BuildCorridorFloorMesh (contour): WalkableContour is empty; no floor mesh generated"));
			return false;
		}

		UE_LOG(LogRoguelikeGeometry,
			Log,
			TEXT("[OrganicFloor] BuildCorridorFloorMesh (contour): triangulating %d polygon(s) at Z=%.1f"),
			Grid.WalkableContour.Polygons.Num(),
			FloorHeight);

		for (const FWalkablePolygon& Poly : Grid.WalkableContour.Polygons)
		{
			TriangulateWithHoles(Poly, FloorHeight, OutMesh);
		}
	}
	else
	{
		// --- Ribbon path ---
		// Emit a quad strip per corridor + a rectangle per room.

		const bool bHasContent = !Grid.Corridors.IsEmpty() || !Grid.Rooms.IsEmpty();
		if (!bHasContent)
		{
			UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[OrganicFloor] BuildCorridorFloorMesh (ribbon): no corridors or rooms"));
			return false;
		}

		UE_LOG(LogRoguelikeGeometry,
			Log,
			TEXT("[OrganicFloor] BuildCorridorFloorMesh (ribbon): corridors=%d rooms=%d at Z=%.1f"),
			Grid.Corridors.Num(),
			Grid.Rooms.Num(),
			FloorHeight);

		// Emit corridor ribbons.
		for (const FOrganicCorridor& Cor : Grid.Corridors)
		{
			if (Cor.Centerline.Num() < 2 || Cor.Radii.Num() != Cor.Centerline.Num())
			{
				UE_LOG(LogRoguelikeGeometry,
					Verbose,
					TEXT("[OrganicFloor] BuildCorridorFloorMesh: corridor has %d centerline pts / %d radii — skipped"),
					Cor.Centerline.Num(),
					Cor.Radii.Num());
				continue;
			}

			for (int32 i = 0; i + 1 < Cor.Centerline.Num(); ++i)
			{
				EmitRibbonQuad(Cor.Centerline[i], Cor.Radii[i], Cor.Centerline[i + 1], Cor.Radii[i + 1], FloorHeight, OutMesh);
			}
		}

		// Emit room rectangles.
		for (const FOrganicRoom& Room : Grid.Rooms)
		{
			EmitRoomRect(Room, FloorHeight, OutMesh);
		}
	}

	const bool bHasGeometry = !OutMesh.Triangles.IsEmpty();

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[OrganicFloor] BuildCorridorFloorMesh: result — vertices=%d triangles=%d (path=%s)"),
		OutMesh.Vertices.Num(),
		OutMesh.Triangles.Num() / 3,
		Grid.bUseGridContourFloor ? TEXT("contour") : TEXT("ribbon"));

	return bHasGeometry;
}
