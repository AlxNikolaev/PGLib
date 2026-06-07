#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"

#include "Generators/WeightedDistribute.h"
#include "ProceduralGeometry.h"

namespace
{
	/** Rotates a 2D vector by degrees (CCW). */
	FORCEINLINE FVector2D RotateDeg(const FVector2D& V, float Deg)
	{
		const float R = FMath::DegreesToRadians(Deg);
		const float C = FMath::Cos(R);
		const float S = FMath::Sin(R);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}

	/** Oriented bounding box for SAT overlap / point tests. */
	struct FOBB2D
	{
		FVector2D C;	 // center
		FVector2D AxisX; // unit local-X axis (world)
		FVector2D AxisY; // unit local-Y axis (world)
		FVector2D H;	 // half extents along local X/Y
	};

	FOBB2D MakeOBB(const FVector2D& Center, float RotDeg, const FVector2D& HalfExtent, float Margin)
	{
		FOBB2D B;
		B.C = Center;
		B.AxisX = RotateDeg(FVector2D(1, 0), RotDeg);
		B.AxisY = RotateDeg(FVector2D(0, 1), RotDeg);
		B.H = HalfExtent + FVector2D(Margin, Margin);
		return B;
	}

	bool OverlapOnAxis(const FOBB2D& A, const FOBB2D& B, const FVector2D& L)
	{
		const float Dist = FMath::Abs(FVector2D::DotProduct(B.C - A.C, L));
		const float RA = A.H.X * FMath::Abs(FVector2D::DotProduct(A.AxisX, L)) + A.H.Y * FMath::Abs(FVector2D::DotProduct(A.AxisY, L));
		const float RB = B.H.X * FMath::Abs(FVector2D::DotProduct(B.AxisX, L)) + B.H.Y * FMath::Abs(FVector2D::DotProduct(B.AxisY, L));
		return Dist <= RA + RB;
	}

	bool OBBOverlap(const FOBB2D& A, const FOBB2D& B)
	{
		return OverlapOnAxis(A, B, A.AxisX) && OverlapOnAxis(A, B, A.AxisY) && OverlapOnAxis(A, B, B.AxisX) && OverlapOnAxis(A, B, B.AxisY);
	}

	bool PointInOBB(const FVector2D& P, const FOBB2D& B)
	{
		const FVector2D D = P - B.C;
		return FMath::Abs(FVector2D::DotProduct(D, B.AxisX)) <= B.H.X && FMath::Abs(FVector2D::DotProduct(D, B.AxisY)) <= B.H.Y;
	}

	// 2D segment-vs-segment intersection (proper crossing, including touching). Pure; no allocations.
	bool Segments2DIntersect(const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, const FVector2D& P4)
	{
		const FVector2D R = P2 - P1;
		const FVector2D S = P4 - P3;
		const float		RxS = R.X * S.Y - R.Y * S.X;
		const FVector2D QmP = P3 - P1;
		const float		QmPxR = QmP.X * R.Y - QmP.Y * R.X;

		if (FMath::IsNearlyZero(RxS))
		{
			// Parallel. Collinear-overlap is caught by the endpoint-in-OBB tests in SegmentHitsOBB,
			// so treat strictly-parallel segments here as non-crossing.
			return false;
		}

		const float T = (QmP.X * S.Y - QmP.Y * S.X) / RxS;
		const float U = QmPxR / RxS;
		return T >= 0.0f && T <= 1.0f && U >= 0.0f && U <= 1.0f;
	}

	// True if the segment AP->BP touches the OBB (either endpoint inside, or the segment crosses any edge).
	bool SegmentHitsOBB(const FVector2D& AP, const FVector2D& BP, const FOBB2D& OBB)
	{
		if (PointInOBB(AP, OBB) || PointInOBB(BP, OBB))
		{
			return true;
		}
		// Four OBB corners (CCW): C ± AxisX*H.X ± AxisY*H.Y.
		const FVector2D EX = OBB.AxisX * OBB.H.X;
		const FVector2D EY = OBB.AxisY * OBB.H.Y;
		const FVector2D K0 = OBB.C - EX - EY;
		const FVector2D K1 = OBB.C + EX - EY;
		const FVector2D K2 = OBB.C + EX + EY;
		const FVector2D K3 = OBB.C - EX + EY;
		return Segments2DIntersect(AP, BP, K0, K1) || Segments2DIntersect(AP, BP, K1, K2) || Segments2DIntersect(AP, BP, K2, K3)
			|| Segments2DIntersect(AP, BP, K3, K0);
	}

	FVector2D CubicBezier(const FVector2D& P0, const FVector2D& C1, const FVector2D& C2, const FVector2D& P3, float T)
	{
		const float U = 1.0f - T;
		const float W0 = U * U * U;
		const float W1 = 3.0f * U * U * T;
		const float W2 = 3.0f * U * T * T;
		const float W3 = T * T * T;
		return P0 * W0 + C1 * W1 + C2 * W2 + P3 * W3;
	}

	/** Nearest point on a polyline to P; returns the point and writes its squared distance. */
	FVector2D NearestOnPolyline(const TArray<FVector2D>& Poly, const FVector2D& P, float& OutDistSq)
	{
		FVector2D Best = Poly.Num() > 0 ? Poly[0] : P;
		OutDistSq = FLT_MAX;
		for (int32 i = 0; i + 1 < Poly.Num(); ++i)
		{
			const FVector2D A = Poly[i];
			const FVector2D B = Poly[i + 1];
			const FVector2D AB = B - A;
			const float		LenSq = AB.SizeSquared();
			const float		T = LenSq > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LenSq, 0.0f, 1.0f) : 0.0f;
			const FVector2D Q = A + AB * T;
			const float		D2 = FVector2D::DistSquared(P, Q);
			if (D2 < OutDistSq)
			{
				OutDistSq = D2;
				Best = Q;
			}
		}
		return Best;
	}

	/** Packs two room indices into a uint64 edge key (order-independent). Used by FindSpine and the backbone/loop carve. */
	uint64 EdgeKey(int32 A, int32 B)
	{
		const int32 Lo = FMath::Min(A, B);
		const int32 Hi = FMath::Max(A, B);
		return (static_cast<uint64>(Lo) << 32) | static_cast<uint32>(Hi);
	}

	// --- Pure read-only helpers shared across GenerateLocationSubgraph phases ---
	// Extracted verbatim from the former in-function lambdas; they only read the room array (and a couple
	// of scalars), so they are free functions taking explicit parameters rather than member methods. The
	// former SegmentHitsRoom / CorridorHitsRoom duplicates were unified into CorridorClearsRooms /
	// CheckCorridorAgainstRooms below (every builder now routes through that single helper).

	// True if any interior segment of Centerline penetrates the body of endpoint room R, beyond the
	// doorway approach span. A corridor legitimately enters R only at the doorway DoorPos along -DoorNormal;
	// penetration is tolerated only while within ApproachDepth of the doorway line. The first and last
	// centerline segments (which emanate from the two doorways) are excluded by the caller-supplied range.
	bool EndpointBodyCrossed(
		const FOrganicRoom& R, const TArray<FVector2D>& Centerline, const FVector2D& DoorPos, const FVector2D& DoorNormal, float ApproachDepth)
	{
		const FOBB2D OBB = MakeOBB(R.Center, R.RotationDeg, R.HalfExtent, 0.0f);
		// Inward direction (into the room body) is -DoorNormal; depth measured from the doorway line.
		for (int32 i = 0; i + 1 < Centerline.Num(); ++i)
		{
			const FVector2D A = Centerline[i];
			const FVector2D B = Centerline[i + 1];
			// Only consider a segment that actually touches this room's body.
			if (!SegmentHitsOBB(A, B, OBB))
			{
				continue;
			}
			// Allowed only if BOTH endpoints lie within the shallow doorway approach span.
			const float DepthA = FVector2D::DotProduct(A - DoorPos, -DoorNormal);
			const float DepthB = FVector2D::DotProduct(B - DoorPos, -DoorNormal);
			if (DepthA <= ApproachDepth && DepthB <= ApproachDepth)
			{
				continue;
			}
			return true;
		}
		return false;
	}

	// How a corridor builder reacts when its emitted curve would cross a room body it does not connect to.
	// Rooms are HARD obstacles; junctions and other corridors are NOT. Three states:
	//   None    — no test (legacy / explicitly-unchecked callers).
	//   Reroute — MANDATORY edge: insert one nearest-corner waypoint past the blocker, rebuild the curve
	//             through it, retry once; drop only as a last resort (logged as a mandatory-drop shortfall).
	//   Drop    — OPTIONAL edge: discard silently on a crossing (the historic reject behavior).
	enum class ECorridorAvoidPolicy : uint8
	{
		None,
		Reroute,
		Drop,
	};

	// Result of testing a corridor's sampled centerline against the room set: whether it clips a THIRD room
	// (one it does not connect), the index of that blocker (for reroute), and whether it penetrates an
	// endpoint room body beyond the shallow doorway approach span (which a reroute waypoint cannot fix).
	struct FCorridorRoomCheck
	{
		bool  bThirdRoomHit = false;
		int32 BlockingRoom = INDEX_NONE;
		bool  bEndpointBodyCrossed = false;
	};

	// Unified room-clearance test for a corridor's EMITTED (sampled) centerline. Walks each consecutive
	// segment vs every room OBB (margin 0.0f, exactly like the three legacy checks). AllowedRooms holds the
	// indices the corridor legitimately touches (its endpoint rooms / target room) — those are skipped for
	// the third-room scan. Returns false on the first third-room hit and writes its index (needed for the
	// reroute). Pure read-only geometry — draws ZERO FRandomStream values.
	bool CorridorClearsRooms(
		const TArray<FOrganicRoom>& Rooms, const TArray<FVector2D>& Centerline, TArrayView<const int32> AllowedRooms, int32& OutFirstBlockingRoom)
	{
		OutFirstBlockingRoom = INDEX_NONE;
		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			bool bAllowed = false;
			for (int32 a = 0; a < AllowedRooms.Num(); ++a)
			{
				if (AllowedRooms[a] == r)
				{
					bAllowed = true;
					break;
				}
			}
			if (bAllowed)
			{
				continue;
			}
			const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, 0.0f);
			for (int32 i = 0; i + 1 < Centerline.Num(); ++i)
			{
				if (SegmentHitsOBB(Centerline[i], Centerline[i + 1], OBB))
				{
					OutFirstBlockingRoom = r;
					return false;
				}
			}
		}
		return true;
	}

	// Top-level corridor-vs-rooms test used by every builder. Combines the third-room clearance scan (via
	// CorridorClearsRooms over AllowedRooms) with the endpoint-body-crossing rule (EndpointBodyCrossed), so
	// legitimate doorway entry into an endpoint room is never mistaken for a crossing. EndpointA/EndpointB
	// (each with its doorway Pos/Normal) may be null when a corridor has no Room endpoint on that side
	// (e.g. a junction tap or a dead-end stub). Pure read-only geometry — no FRandomStream draws.
	FCorridorRoomCheck CheckCorridorAgainstRooms(const TArray<FOrganicRoom>& Rooms,
		const TArray<FVector2D>&											 Centerline,
		TArrayView<const int32>												 AllowedRooms,
		const FOrganicRoom*													 EndpointA,
		const FVector2D&													 DoorPosA,
		const FVector2D&													 DoorNormalA,
		const FOrganicRoom*													 EndpointB,
		const FVector2D&													 DoorPosB,
		const FVector2D&													 DoorNormalB,
		float																 ApproachDepth)
	{
		FCorridorRoomCheck Result;
		Result.bThirdRoomHit = !CorridorClearsRooms(Rooms, Centerline, AllowedRooms, Result.BlockingRoom);
		if (EndpointA != nullptr && EndpointBodyCrossed(*EndpointA, Centerline, DoorPosA, DoorNormalA, ApproachDepth))
		{
			Result.bEndpointBodyCrossed = true;
		}
		if (EndpointB != nullptr && EndpointBodyCrossed(*EndpointB, Centerline, DoorPosB, DoorNormalB, ApproachDepth))
		{
			Result.bEndpointBodyCrossed = true;
		}
		return Result;
	}

	// Computes a single reroute waypoint that steers a corridor clear of blocking room R. Picks R's OBB
	// corner nearest the straight A->B chord, then pushes it outward (away from R's center) by ClearRadius
	// so the rebuilt A->waypoint->B curve passes outside the room body. Pure geometry — no FRandomStream
	// draws, so it never perturbs the global draw sequence (load-bearing for determinism).
	FVector2D ComputeRerouteWaypoint(const FOrganicRoom& R, const FVector2D& AP, const FVector2D& BP, float ClearRadius)
	{
		const FOBB2D	OBB = MakeOBB(R.Center, R.RotationDeg, R.HalfExtent, 0.0f);
		const FVector2D EX = OBB.AxisX * OBB.H.X;
		const FVector2D EY = OBB.AxisY * OBB.H.Y;
		const FVector2D Corners[4] = { OBB.C - EX - EY, OBB.C + EX - EY, OBB.C + EX + EY, OBB.C - EX + EY };

		// Nearest corner to the straight A->B chord (the corner the corridor would naturally bend around).
		const FVector2D AB = BP - AP;
		const float		ABLenSq = AB.SizeSquared();
		int32			BestCorner = 0;
		float			BestDistSq = FLT_MAX;
		for (int32 c = 0; c < 4; ++c)
		{
			const float T = (ABLenSq > KINDA_SMALL_NUMBER) ? FMath::Clamp(FVector2D::DotProduct(Corners[c] - AP, AB) / ABLenSq, 0.0f, 1.0f) : 0.0f;
			const FVector2D OnChord = AP + AB * T;
			const float		D2 = FVector2D::DistSquared(Corners[c], OnChord);
			if (D2 < BestDistSq)
			{
				BestDistSq = D2;
				BestCorner = c;
			}
		}

		// Push the corner outward along the center->corner direction so the waypoint clears the body.
		FVector2D Outward = (Corners[BestCorner] - OBB.C).GetSafeNormal();
		if (Outward.IsNearlyZero())
		{
			Outward = FVector2D(1.0f, 0.0f);
		}
		return Corners[BestCorner] + Outward * FMath::Max(1.0f, ClearRadius);
	}

	// The room indices a corridor connects (its Room-typed anchors); writes up to two, -1 if none.
	void CorridorRooms(const FOrganicCorridor& Cor, int32& OutA, int32& OutB)
	{
		OutA = (Cor.AnchorA.Type == EOrganicAnchorType::Room) ? Cor.AnchorA.Index : INDEX_NONE;
		OutB = (Cor.AnchorB.Type == EOrganicAnchorType::Room) ? Cor.AnchorB.Index : INDEX_NONE;
	}

	// Picks the doorway on RoomIdx best facing Toward — preferring an unused one, else the best of any.
	int32 PickDoorway(const TArray<FOrganicRoom>& Rooms, int32 RoomIdx, const FVector2D& Toward)
	{
		int32 BestFree = INDEX_NONE;
		float BestFreeDot = -FLT_MAX;
		int32 BestAny = INDEX_NONE;
		float BestAnyDot = -FLT_MAX;
		for (int32 d = 0; d < Rooms[RoomIdx].Doorways.Num(); ++d)
		{
			const float Dot = FVector2D::DotProduct(Rooms[RoomIdx].Doorways[d].OutwardNormal, Toward);
			if (Dot > BestAnyDot)
			{
				BestAnyDot = Dot;
				BestAny = d;
			}
			if (!Rooms[RoomIdx].Doorways[d].bUsed && Dot > BestFreeDot)
			{
				BestFreeDot = Dot;
				BestFree = d;
			}
		}
		return BestFree != INDEX_NONE ? BestFree : BestAny;
	}
} // namespace

// One placement-spawn edge: PlaceRooms grew Child off Parent room through Parent's ParentDoorIdx doorway.
// These edges form a connected spanning tree by construction (every non-seed room has exactly one parent),
// so they ARE the connectivity backbone — there is no geometric MST and no room is ever dropped.
struct FOrgSpawnEdge
{
	int32 Parent = INDEX_NONE;		  // source room index the child grew from
	int32 Child = INDEX_NONE;		  // newly-placed child room index
	int32 ParentDoorIdx = INDEX_NONE; // directional hint: the parent doorway the child grew off (PlaceRooms-chosen)
};

// Mutable working state for one GenerateLocationSubgraph build (see the forward declaration / doc comment
// in the header). Default member initializers reproduce the original stack-local initial values. The
// subgraph phase helpers move named stack locals into these identically-named fields, so statement and
// FRandomStream draw order are preserved verbatim across the extraction.
struct FOrgSubgraphBuild
{
	TArray<FOrganicRoom>	 Rooms;
	TArray<FOrganicCorridor> Corridors;
	TArray<FOrganicJunction> Junctions;	   // free deformed-circle network hubs (added by the final AddJunctions phase)
	TArray<int32>			 OpenList;	   // room indices still eligible to grow from
	int32					 QueueIdx = 1; // placement cursor (room 0 is seeded before placement)
	int32					 StartRoomIdx = -1;
	int32					 EndRoomIdx = -1;
	// Placement-spawn tree (recorded by PlaceRooms; consumed by ConnectRooms as the connectivity backbone).
	TArray<FOrgSpawnEdge> SpawnEdges;
	TArray<TArray<int32>> SpawnAdj; // symmetric adjacency built from SpawnEdges; fed to FindSpine for endpoints
	TSet<uint64>		  SpineEdges;

	// Per-build stat counters (logged in the final layout summary).
	int32 StatRetries = 0;
	int32 StatBacktracks = 0;
	int32 StatLoops = 0;
	int32 StatSpine = 0;
	int32 StatDeadEnds = 0;
	int32 StatLinks = 0;
	int32 StatJunctions = 0;
	int32 StatReroutes = 0;		  // mandatory corridors steered around a blocking room via a single waypoint
	int32 StatMandatoryDrops = 0; // mandatory corridors dropped as a last resort (reroute still clipped a room)
	int32 PlacedCount = 0;
};

UOrganicDungeonGenerator2D::UOrganicDungeonGenerator2D()
{
	Bounds = FBox2D(FVector2D(-1000, -1000), FVector2D(1000, 1000));
	GridSize = 50;
	InitializeRandomStream();
}

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Super::SetBounds(InBounds);
	return this;
}

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::SetSeed(const FString& InSeed)
{
	Super::SetSeed(InSeed);
	return this;
}

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::SetGridSize(int32 InSize)
{
	Super::SetGridSize(InSize);
	return this;
}

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::SetCenter(const FVector2D& InCenter)
{
	Super::SetCenter(InCenter);
	return this;
}

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::ApplyResolvedParams(const FOrganicDungeonResolvedParams& InParams)
{
	Params = InParams;
	Segments.Reset(1);
	Segments.Add(InParams);
	return this;
}

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::ApplyResolvedParamsList(const TArray<FOrganicDungeonResolvedParams>& InSegments)
{
	Segments = InSegments;
	Params = InSegments.Num() > 0 ? InSegments[0] : FOrganicDungeonResolvedParams{};
	return this;
}

FLayoutDiagram2D UOrganicDungeonGenerator2D::Generate()
{
	return GenerateInternal().Diagram;
}

FOrganicDungeonGridData UOrganicDungeonGenerator2D::GenerateWithGridData()
{
	return GenerateInternal();
}

FOrganicDungeonGridData UOrganicDungeonGenerator2D::GenerateInternal()
{
	const float CellSizeVal = static_cast<float>(GridSize);

	TArray<FOrganicDungeonResolvedParams> Segs = Segments;
	if (Segs.Num() == 0)
	{
		Segs.Add(Params);
	}

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[ORG] GenerateInternal: one cluster graph from %d location subgraph(s)."), Segs.Num());

	// Offset a corridor anchor's room/corridor/junction index into the merged arrays.
	auto OffsetAnchor = [](FOrganicAnchor& A, int32 RoomBase, int32 CorrBase, int32 JunctionBase) {
		if (A.Index < 0)
		{
			return;
		}
		if (A.Type == EOrganicAnchorType::Room)
		{
			A.Index += RoomBase;
		}
		else if (A.Type == EOrganicAnchorType::Corridor)
		{
			A.Index += CorrBase;
		}
		else if (A.Type == EOrganicAnchorType::Junction)
		{
			A.Index += JunctionBase;
		}
	};

	FOrganicLayout Merged;
	int32		   OverallStartIdx = INDEX_NONE;
	int32		   PrevEndGlobalIdx = INDEX_NONE;
	FVector2D	   PrevEndPos = CenterPoint;
	FVector2D	   PrevEndNormal = FVector2D(1.0f, 0.0f);
	bool		   bHavePrev = false;

	// Per-segment start-room index (kept aligned with cluster location order; -1 for an empty segment).
	Merged.LocationStartRoomIndex.Init(INDEX_NONE, Segs.Num());

	for (int32 s = 0; s < Segs.Num(); ++s)
	{
		const FOrganicDungeonResolvedParams& SegParams = Segs[s];

		// Anchor: first location at the cluster center; each later location seeded off the previous
		// location's end-room exit, growing into open space. Placement collision then keeps it clear.
		FVector2D Anchor = CenterPoint;
		if (bHavePrev)
		{
			const float Reach = FMath::Max(SegParams.CorridorLengthMin, SegParams.MinRoomGap + CellSizeVal);
			Anchor = PrevEndPos + PrevEndNormal * (Reach + CellSizeVal);
		}

		// Generate this location's subgraph in world space, avoiding every room placed so far (no overlaps).
		FOrganicLayout Sub = GenerateLocationSubgraph(SegParams, Anchor, Merged.Rooms);
		Merged.RequestedRoomCount += Sub.RequestedRoomCount;
		Merged.RequestedLoopCount += Sub.RequestedLoopCount;
		Merged.RequestedLinkCount += Sub.RequestedLinkCount;
		Merged.RequestedDeadEndCount += Sub.RequestedDeadEndCount;
		Merged.RequestedJunctionCount += Sub.RequestedJunctionCount;
		if (Sub.Rooms.Num() == 0)
		{
			UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[ORG] Location %d produced no rooms — skipped."), s);
			continue;
		}

		const int32 RoomBase = Merged.Rooms.Num();
		const int32 CorrBase = Merged.Corridors.Num();
		const int32 JunctionBase = Merged.Junctions.Num();

		for (FOrganicRoom& R : Sub.Rooms)
		{
			R.LocationIndex = s;
		}
		Merged.Rooms.Append(Sub.Rooms);
		for (FOrganicJunction& J : Sub.Junctions)
		{
			J.LocationIndex = s;
		}
		Merged.Junctions.Append(Sub.Junctions);
		for (FOrganicCorridor& C : Sub.Corridors)
		{
			OffsetAnchor(C.AnchorA, RoomBase, CorrBase, JunctionBase);
			OffsetAnchor(C.AnchorB, RoomBase, CorrBase, JunctionBase);
			Merged.Corridors.Add(MoveTemp(C));
		}

		const int32 ThisStartGlobal = (Sub.StartRoomIdx >= 0) ? RoomBase + Sub.StartRoomIdx : RoomBase;
		const int32 ThisEndGlobal = (Sub.EndRoomIdx >= 0) ? RoomBase + Sub.EndRoomIdx : RoomBase + Sub.Rooms.Num() - 1;

		Merged.LocationStartRoomIndex[s] = ThisStartGlobal;

		if (OverallStartIdx == INDEX_NONE)
		{
			OverallStartIdx = ThisStartGlobal;
		}

		// Stitch subgraphs into one graph: previous location's end room -> this location's start room. The
		// stitch is MANDATORY and reroutes around any room it would cross; only when the reroute still clips
		// is it dropped (bStitchProduced == false), logged as a cluster connectivity gap, and not added —
		// generation never fails on a missing stitch.
		if (bHavePrev && PrevEndGlobalIdx >= 0)
		{
			bool			 bStitchProduced = false;
			FOrganicCorridor Link = BuildInterLocationCorridor(Merged.Rooms, PrevEndGlobalIdx, ThisStartGlobal, SegParams, bStitchProduced);
			if (bStitchProduced)
			{
				Merged.Corridors.Add(MoveTemp(Link));
			}
			else
			{
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[ORG] Inter-location stitch from location end-room[%d] to location %d start-room[%d] could not be routed — "
						 "cluster connectivity gap (generation NOT failed)."),
					PrevEndGlobalIdx,
					s,
					ThisStartGlobal);
			}
		}

		// The cluster exit is the LAST segment's end room (the cluster's graph-diameter far endpoint).
		Merged.EndRoomIdx = ThisEndGlobal;

		PrevEndGlobalIdx = ThisEndGlobal;
		PrevEndPos = Merged.Rooms[ThisEndGlobal].Center;
		PrevEndNormal = FVector2D(1.0f, 0.0f);
		bHavePrev = true;
	}

	if (Merged.Rooms.Num() == 0)
	{
		FOrganicDungeonGridData Empty;
		Empty.CellSize = CellSizeVal;
		Empty.RequestedRoomCount = Merged.RequestedRoomCount;
		Empty.RequestedLoopCount = Merged.RequestedLoopCount;
		Empty.RequestedLinkCount = Merged.RequestedLinkCount;
		Empty.RequestedDeadEndCount = Merged.RequestedDeadEndCount;
		Empty.RequestedJunctionCount = Merged.RequestedJunctionCount;
		return Empty;
	}

	Merged.StartRoomIdx = OverallStartIdx;
	Merged.PlacedCount = Merged.Rooms.Num();

	// Exactly one entrance + one exit: StartRoomIdx and EndRoomIdx are scalars. EndRoomIdx was set to the
	// last segment's far endpoint above. Guard the degenerate single-room cluster so the exit does not
	// co-locate with a distinct entrance: a single room is necessarily both entrance and exit.
	if (!Merged.Rooms.IsValidIndex(Merged.EndRoomIdx))
	{
		Merged.EndRoomIdx = Merged.StartRoomIdx;
	}

	// Cluster-scope end-room prefab swap (when configured on the last segment): the cluster's single exit
	// room becomes the EndRoom prefab. Guarded so it never overwrites the entrance room of a single-room
	// cluster (where EndRoomIdx == StartRoomIdx).
	if (Segs.Num() > 0 && Segs.Last().bHasEndRoom && Merged.Rooms.IsValidIndex(Merged.EndRoomIdx) && Merged.EndRoomIdx != Merged.StartRoomIdx)
	{
		Merged.Rooms[Merged.EndRoomIdx].RoomLevel = Segs.Last().EndRoom.RoomLevel;
		Merged.Rooms[Merged.EndRoomIdx].FootprintCenterOffset = Segs.Last().EndRoom.FootprintCenter;
		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[ORG] Swapped end-room prefab at cluster exit room[%d] '%s'"),
			Merged.EndRoomIdx,
			*Segs.Last().EndRoom.DisplayName.ToString());
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG] Cluster graph: %d location(s) -> rooms=%d corridors=%d entrance=%d exit=%d"),
		Segs.Num(),
		Merged.Rooms.Num(),
		Merged.Corridors.Num(),
		Merged.StartRoomIdx,
		Merged.EndRoomIdx);

	return RasterizeLayout(Merged);
}

void UOrganicDungeonGenerator2D::SampleCorridorCurve(
	const TArray<FVector2D>& ControlPoints, const FVector2D& StartTangent, const FVector2D& EndTangent, float StepLen, TArray<FVector2D>& OutDense)
{
	OutDense.Reset();
	const int32 NumCP = ControlPoints.Num();
	if (NumCP == 0)
	{
		return;
	}
	if (NumCP == 1)
	{
		OutDense.Add(ControlPoints[0]);
		return;
	}

	const float SafeStep = FMath::Max(1.0f, StepLen);

	// Two control points: straight evenly-stepped lerp identical to the legacy dense centerline
	// (StepLen ~= CellSize*0.5 reproduces the old per-cell sampling).
	if (NumCP == 2)
	{
		const FVector2D AP = ControlPoints[0];
		const FVector2D BP = ControlPoints[1];
		const float		Dist = FMath::Max(1.0f, (BP - AP).Size());
		const int32		N = FMath::Clamp(FMath::CeilToInt(Dist / SafeStep), 2, 256);
		OutDense.Reserve(N + 1);
		for (int32 i = 0; i <= N; ++i)
		{
			OutDense.Add(FMath::Lerp(AP, BP, static_cast<float>(i) / N));
		}
		return;
	}

	// Catmull-Rom through the few control points. Phantom endpoints are derived from the doorway normals
	// so the curve eases out along the opening direction (deterministic, geometry-only).
	auto PointAt = [&](int32 Index) -> FVector2D {
		if (Index < 0)
		{
			// Phantom before the first point: step back along the start outward normal.
			return ControlPoints[0] + StartTangent.GetSafeNormal() * SafeStep;
		}
		if (Index >= NumCP)
		{
			// Phantom after the last point: step back along the end outward normal.
			return ControlPoints[NumCP - 1] + EndTangent.GetSafeNormal() * SafeStep;
		}
		return ControlPoints[Index];
	};

	OutDense.Add(ControlPoints[0]);
	for (int32 Seg = 0; Seg + 1 < NumCP; ++Seg)
	{
		const FVector2D P0 = PointAt(Seg - 1);
		const FVector2D P1 = PointAt(Seg);
		const FVector2D P2 = PointAt(Seg + 1);
		const FVector2D P3 = PointAt(Seg + 2);

		const float SegLen = FMath::Max(1.0f, (P2 - P1).Size());
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(SegLen / SafeStep), 1, 256);
		for (int32 s = 1; s <= Steps; ++s)
		{
			const float t = static_cast<float>(s) / Steps;
			const float t2 = t * t;
			const float t3 = t2 * t;
			// Catmull-Rom basis (uniform, tension 0.5).
			const FVector2D Pt =
				0.5f * ((2.0f * P1) + (-P0 + P2) * t + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t2 + (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t3);
			OutDense.Add(Pt);
		}
	}
}

FOrganicCorridor UOrganicDungeonGenerator2D::BuildBezierCorridor(const FVector2D& AP,
	const FVector2D&															  AN,
	const FVector2D&															  BP,
	const FVector2D&															  BN,
	float																		  Waviness,
	float																		  MinRadius,
	float																		  MaxRadius,
	EOrganicCorridorStyle														  Style,
	int32																		  WavinessControlPoints,
	float																		  RadiusScale)
{
	FOrganicCorridor Cor;

	// Build a FEW-point centerline: start doorway, k intermediate perpendicular-offset points along the
	// chord, end doorway. k=0 reproduces today's straight 2-point centerline exactly (backward-compatible).
	// Waviness offsets are derived purely from chord geometry (alternating sign, amplitude*Waviness) so the
	// builder draws ZERO FRandomStream values — existing seeds and draw order are unchanged.
	const FVector2D Chord = BP - AP;
	const float		ChordLen = FMath::Max(1.0f, Chord.Size());
	const FVector2D Dir = Chord / ChordLen;
	const FVector2D Perp(-Dir.Y, Dir.X);

	const int32 K = FMath::Max(0, WavinessControlPoints);
	const float HalfWidth = FMath::Max(1.0f, MinRadius * RadiusScale);

	// Per-control-point radius. Clean = constant HalfWidth. Cave = bounded deterministic variation derived
	// from the control-point index (no RandomStream), interpolated densely later during sampling.
	const float MaxHalf = FMath::Max(HalfWidth, MaxRadius * RadiusScale);
	auto		RadiusForIndex = [&](int32 Index, int32 Count) -> float {
		   if (Style == EOrganicCorridorStyle::Clean || Count <= 1)
		   {
			   return HalfWidth;
		   }
		   // Smooth deterministic bulge toward the middle, scaled by Waviness, bounded by [HalfWidth, MaxHalf].
		   const float U = static_cast<float>(Index) / static_cast<float>(Count - 1);
		   const float Bulge = FMath::Sin(U * PI); // 0 at ends, 1 at middle
		   return FMath::Clamp(HalfWidth + (MaxHalf - HalfWidth) * Bulge * Waviness, HalfWidth, MaxHalf);
	};

	const int32 NumPoints = K + 2;
	Cor.Centerline.Reserve(NumPoints);
	Cor.Radii.Reserve(NumPoints);

	Cor.Centerline.Add(AP);
	for (int32 j = 1; j <= K; ++j)
	{
		const float		Frac = static_cast<float>(j) / static_cast<float>(K + 1);
		const FVector2D Base = AP + Chord * Frac;
		// Bounded perpendicular amplitude: scaled by Waviness, capped so the bow can't balloon past the chord.
		const float Amp = FMath::Min(MaxHalf * 3.0f, ChordLen * 0.5f) * Waviness;
		const float Sign = (j % 2 == 1) ? 1.0f : -1.0f;
		Cor.Centerline.Add(Base + Perp * (Amp * Sign));
	}
	Cor.Centerline.Add(BP);

	for (int32 i = 0; i < NumPoints; ++i)
	{
		Cor.Radii.Add(RadiusForIndex(i, NumPoints));
	}
	return Cor;
}

FOrganicCorridor UOrganicDungeonGenerator2D::BuildInterLocationCorridor(
	TArray<FOrganicRoom>& AllRooms, int32 FromIdx, int32 ToIdx, const FOrganicDungeonResolvedParams& LinkParams, bool& bOutProduced)
{
	const float CellSizeVal = static_cast<float>(GridSize);

	FOrganicRoom& FromRoom = AllRooms[FromIdx];
	FOrganicRoom& ToRoom = AllRooms[ToIdx];

	const FVector2D ToB = (ToRoom.Center - FromRoom.Center).GetSafeNormal();

	// Reuse the shared PickDoorway helper (prefer an unused doorway facing Toward, else the best of any).
	FVector2D	AP = FromRoom.Center;
	FVector2D	AN = ToB;
	const int32 FromDoor = PickDoorway(AllRooms, FromIdx, ToB);
	if (FromRoom.Doorways.IsValidIndex(FromDoor))
	{
		AP = FromRoom.Doorways[FromDoor].Pos;
		AN = FromRoom.Doorways[FromDoor].OutwardNormal;
		FromRoom.Doorways[FromDoor].bUsed = true;
	}

	FVector2D	BP = ToRoom.Center;
	FVector2D	BN = -ToB;
	const int32 ToDoor = PickDoorway(AllRooms, ToIdx, -ToB);
	if (ToRoom.Doorways.IsValidIndex(ToDoor))
	{
		BP = ToRoom.Doorways[ToDoor].Pos;
		BN = ToRoom.Doorways[ToDoor].OutwardNormal;
		ToRoom.Doorways[ToDoor].bUsed = true;
	}

	// Inter-location links clamp MinR to CellSize so the passage is never narrower than one grid cell.
	const float MinR = FMath::Max(CellSizeVal, LinkParams.MinThickness * 0.5f);
	const float MaxR = FMath::Max(MinR, LinkParams.MaxWidth * 0.5f);

	const auto BuildLink = [&](const FVector2D& SAP, const FVector2D& SAN, const FVector2D& SBP, const FVector2D& SBN) -> FOrganicCorridor {
		return BuildBezierCorridor(SAP, SAN, SBP, SBN, LinkParams.Waviness, MinR, MaxR, LinkParams.CorridorStyle, LinkParams.WavinessControlPoints);
	};

	FOrganicCorridor Cor = BuildLink(AP, AN, BP, BN);

	// MANDATORY stitch: it must clear every room except its two endpoints. Reroute around a blocking third
	// room with one nearest-corner waypoint (pure geometry, no FRandomStream draws); drop only as a last
	// resort. Endpoint-body crossing is excluded so legitimate doorway entry is not flagged.
	bOutProduced = true;
	const float				  ApproachDepth = FMath::Max(LinkParams.MinThickness * 0.5f, CellSizeVal);
	const int32				  Allowed[2] = { FromIdx, ToIdx };
	const FOrganicRoom* const EndA = &AllRooms[FromIdx];
	const FOrganicRoom* const EndB = &AllRooms[ToIdx];

	TArray<FVector2D> Dense;
	SampleCorridorCurve(Cor.Centerline, AN, BN, CellSizeVal * 0.5f, Dense);
	FCorridorRoomCheck Check = CheckCorridorAgainstRooms(AllRooms, Dense, Allowed, EndA, AP, AN, EndB, BP, BN, ApproachDepth);

	if (Check.bThirdRoomHit && !Check.bEndpointBodyCrossed)
	{
		const float		ClearRadius = FMath::Max(MaxR, CellSizeVal) + CellSizeVal;
		const FVector2D Waypoint = ComputeRerouteWaypoint(AllRooms[Check.BlockingRoom], AP, BP, ClearRadius);
		const FVector2D ToW = (Waypoint - AP).GetSafeNormal();
		const FVector2D FromW = (BP - Waypoint).GetSafeNormal();

		FOrganicCorridor Leg1 = BuildLink(AP, AN, Waypoint, -ToW);
		FOrganicCorridor Leg2 = BuildLink(Waypoint, FromW, BP, BN);
		FOrganicCorridor Rerouted;
		Rerouted.Centerline = Leg1.Centerline;
		Rerouted.Radii = Leg1.Radii;
		for (int32 i = 1; i < Leg2.Centerline.Num(); ++i)
		{
			Rerouted.Centerline.Add(Leg2.Centerline[i]);
			Rerouted.Radii.Add(Leg2.Radii.IsValidIndex(i) ? Leg2.Radii[i] : Rerouted.Radii.Last());
		}

		TArray<FVector2D> RerouteDense;
		SampleCorridorCurve(Rerouted.Centerline, AN, BN, CellSizeVal * 0.5f, RerouteDense);
		FCorridorRoomCheck RerouteCheck = CheckCorridorAgainstRooms(AllRooms, RerouteDense, Allowed, EndA, AP, AN, EndB, BP, BN, ApproachDepth);
		if (RerouteCheck.bThirdRoomHit || RerouteCheck.bEndpointBodyCrossed)
		{
			bOutProduced = false;
			UE_LOG(LogRoguelikeGeometry,
				Warning,
				TEXT("[ORG] Inter-location stitch room[%d]->room[%d] dropped: single-waypoint reroute around room[%d] still crossed a room "
					 "(cluster connectivity gap, generation NOT failed)."),
				FromIdx,
				ToIdx,
				Check.BlockingRoom);
		}
		else
		{
			Cor = MoveTemp(Rerouted);
			UE_LOG(LogRoguelikeGeometry,
				Verbose,
				TEXT("[ORG] Inter-location stitch room[%d]->room[%d] rerouted around room[%d]."),
				FromIdx,
				ToIdx,
				Check.BlockingRoom);
		}
	}

	Cor.AnchorA = { EOrganicAnchorType::Room, FromIdx, AP, AN };
	Cor.AnchorB = { EOrganicAnchorType::Room, ToIdx, BP, BN };
	Cor.bIsSpine = true; // inter-location link is part of the main route
	return Cor;
}

// The Params parameter intentionally shadows the equally-named member so each location's subgraph
// generates against its own resolved params without rewriting the body.
#pragma warning(push)
#pragma warning(disable : 4458)
void UOrganicDungeonGenerator2D::GenerateDoorways(FOrganicRoom& Room, const FOrganicDungeonResolvedParams& Params) const
{
	const FOrganicResolvedRoomType& RT = Params.RoomTypes[Room.TypeIndex];
	const FVector2D					AxisX = RotateDeg(FVector2D(1, 0), Room.RotationDeg);
	const FVector2D					AxisY = RotateDeg(FVector2D(0, 1), Room.RotationDeg);
	Room.Doorways.Reset();

	if (RT.Doorways.Num() > 0)
	{
		// Declared-doorway path: transform each baked entry local→world.
		//
		// The room is placed so its bounds-center (FootprintCenterOffset from level origin)
		// maps to Room.Center.  A doorway at LocalPosition (relative to level origin) lands at:
		//   world = Room.Center + Rotate(LocalPosition - FootprintCenterOffset, RotDeg)
		Room.Doorways.Reserve(RT.Doorways.Num());
		for (const FOrganicBakedDoorway& Decl : RT.Doorways)
		{
			const FVector2D LocalRelCenter = Decl.LocalPosition - Room.FootprintCenterOffset;
			const FVector2D WorldPos = Room.Center + AxisX * LocalRelCenter.X + AxisY * LocalRelCenter.Y;
			const FVector2D WorldNormal = (AxisX * Decl.LocalOutwardDir.X + AxisY * Decl.LocalOutwardDir.Y).GetSafeNormal();

			FOrganicDoorway D;
			D.Pos = WorldPos;
			D.OutwardNormal = WorldNormal.IsNearlyZero() ? AxisX : WorldNormal;
			D.Width = Decl.Width;
			D.bDeclared = true;
			Room.Doorways.Add(D);
		}
		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[ORG] Room type '%s' rot=%.1f°: generated %d declared doorway(s)"),
			*RT.DisplayName.ToString(),
			Room.RotationDeg,
			Room.Doorways.Num());
	}
	else
	{
		// Legacy synthetic fallback (room has NO authored doorways): exactly one centered punch-point per footprint edge.
		constexpr int32 PerEdge = 1;
		// 4 edges: +X, -X, +Y, -Y (local normals)
		const FVector2D LocalNormals[4] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
		for (int32 e = 0; e < 4; ++e)
		{
			const FVector2D N = LocalNormals[e];
			const FVector2D WorldN = (AxisX * N.X + AxisY * N.Y).GetSafeNormal();
			const FVector2D LocalT(-N.Y, N.X);
			const float		EdgeHalf = (e < 2) ? Room.HalfExtent.Y : Room.HalfExtent.X;
			const float		Push = (e < 2) ? Room.HalfExtent.X : Room.HalfExtent.Y;
			for (int32 i = 0; i < PerEdge; ++i)
			{
				const float		Frac = (PerEdge == 1) ? 0.0f : (((i + 1.0f) / (PerEdge + 1.0f)) * 2.0f - 1.0f);
				const FVector2D LocalPos = N * Push + LocalT * (Frac * EdgeHalf);
				const FVector2D WorldPos = Room.Center + AxisX * LocalPos.X + AxisY * LocalPos.Y;
				FOrganicDoorway D;
				D.Pos = WorldPos;
				D.OutwardNormal = WorldN;
				// Width and bDeclared remain at defaults (0.0f / false).
				Room.Doorways.Add(D);
			}
		}
	}
}

FOrganicRoom UOrganicDungeonGenerator2D::MakeRoom(
	const FOrganicDungeonResolvedParams& Params, int32 TypeIdx, const FVector2D& Center, float RotDeg) const
{
	FOrganicRoom Room;
	Room.TypeIndex = TypeIdx;
	Room.Center = Center;
	Room.RotationDeg = RotDeg;
	Room.HalfExtent = FVector2D(Params.RoomTypes[TypeIdx].FootprintWidth * 0.5f, Params.RoomTypes[TypeIdx].FootprintHeight * 0.5f);
	Room.RoomLevel = Params.RoomTypes[TypeIdx].RoomLevel;
	Room.FootprintCenterOffset = Params.RoomTypes[TypeIdx].FootprintCenter;
	GenerateDoorways(Room, Params);
	return Room;
}

FOrganicJunction UOrganicDungeonGenerator2D::MakeJunction(const FOrganicDungeonResolvedParams& Params, const FVector2D& Center)
{
	const float CellSizeVal = static_cast<float>(GridSize);

	FOrganicJunction J;
	J.Center = Center;

	// Diameter spans the corridor width range so a junction reads as a fat hub a corridor melts into.
	const float Diameter = RandomStream.FRandRange(Params.MinThickness, Params.MaxWidth);
	// Clamp the radius to at least one cell so the disc carve / boundary spline are never degenerate.
	J.Radius = FMath::Max(0.5f * Diameter, CellSizeVal);

	// Build a closed CCW deformed-circle perimeter: N evenly-spaced vertices, each radius scaled by a small
	// per-vertex jitter so PCG gets a wobbly (not perfectly round) junction floor.
	constexpr int32 NumVerts = 16;
	constexpr float JitterFrac = 0.12f; // +/-12% radius wobble
	J.Perimeter.Reserve(NumVerts);
	for (int32 v = 0; v < NumVerts; ++v)
	{
		const float Angle = (2.0f * PI) * static_cast<float>(v) / static_cast<float>(NumVerts);
		const float Jitter = RandomStream.FRandRange(-JitterFrac, JitterFrac);
		const float R = J.Radius * (1.0f + Jitter);
		J.Perimeter.Add(Center + FVector2D(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R));
	}
	return J;
}

void UOrganicDungeonGenerator2D::AttachCorridorToJunction(
	const FOrganicJunction& J, const FVector2D& Toward, FVector2D& OutAttachPos, FVector2D& OutOutwardNormal)
{
	FVector2D Dir = Toward.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector2D(1.0f, 0.0f);
	}

	// Pick the perimeter vertex whose outward direction from the center best matches Toward.
	int32 Best = INDEX_NONE;
	float BestDot = -FLT_MAX;
	for (int32 v = 0; v < J.Perimeter.Num(); ++v)
	{
		const FVector2D Out = (J.Perimeter[v] - J.Center).GetSafeNormal();
		const float		Dot = FVector2D::DotProduct(Out, Dir);
		if (Dot > BestDot)
		{
			BestDot = Dot;
			Best = v;
		}
	}

	if (J.Perimeter.IsValidIndex(Best))
	{
		OutAttachPos = J.Perimeter[Best];
		OutOutwardNormal = (J.Perimeter[Best] - J.Center).GetSafeNormal();
	}
	else
	{
		// Degenerate fallback (no perimeter): analytic circle point along Toward.
		OutAttachPos = J.Center + Dir * J.Radius;
		OutOutwardNormal = Dir;
	}
	if (OutOutwardNormal.IsNearlyZero())
	{
		OutOutwardNormal = Dir;
	}
}

void UOrganicDungeonGenerator2D::PlaceRooms(FOrgSubgraphBuild& Ctx,
	const FOrganicDungeonResolvedParams&					   Params,
	const FVector2D&										   CenterPoint,
	const TArray<FOrganicRoom>&								   Obstacles,
	const TArray<int32>&									   Queue,
	int32													   RequestedRoomCount)
{
	const float CellSizeVal = static_cast<float>(GridSize);
	// Footprint half-extent of a room type (was the HalfExtentOf lambda).
	const auto HalfExtentOf = [&](int32 TypeIdx) {
		return FVector2D(Params.RoomTypes[TypeIdx].FootprintWidth * 0.5f, Params.RoomTypes[TypeIdx].FootprintHeight * 0.5f);
	};

	Ctx.Rooms.Reserve(RequestedRoomCount);

	// --- Layer 2: continuous placement ---
	// Seed the first room at the anchor, but push it clear of obstacle rooms (other locations) first.
	FVector2D FirstCenter = CenterPoint;
	{
		const FVector2D FirstHalf = HalfExtentOf(Queue[0]);
		const float		Step = FMath::Max(FirstHalf.X, FirstHalf.Y) + FMath::Max(1.0f, Params.MinRoomGap);
		FVector2D		PushDir(1.0f, 0.0f);
		if (Obstacles.Num() > 0)
		{
			FVector2D Centroid(0.0f, 0.0f);
			for (const FOrganicRoom& O : Obstacles)
			{
				Centroid += O.Center;
			}
			Centroid /= static_cast<float>(Obstacles.Num());
			PushDir = (CenterPoint - Centroid).GetSafeNormal();
			if (PushDir.IsNearlyZero())
			{
				PushDir = FVector2D(1.0f, 0.0f);
			}
		}
		for (int32 Tries = 0; Tries < 256; ++Tries)
		{
			const FOBB2D ProbeOBB = MakeOBB(FirstCenter, 0.0f, FirstHalf, Params.MinRoomGap * 0.5f);
			bool		 bClash = false;
			for (const FOrganicRoom& O : Obstacles)
			{
				const FOBB2D OBB = MakeOBB(O.Center, O.RotationDeg, O.HalfExtent, Params.MinRoomGap * 0.5f);
				if (OBBOverlap(ProbeOBB, OBB))
				{
					bClash = true;
					break;
				}
			}
			if (!bClash)
			{
				break;
			}
			FirstCenter += PushDir * Step;
		}
	}
	Ctx.Rooms.Add(MakeRoom(Params, Queue[0], FirstCenter, Params.bRandomRotation ? RandomStream.FRandRange(0.0f, 360.0f) : 0.0f));
	Ctx.OpenList.Add(0);

	Ctx.QueueIdx = 1;
	Ctx.StatRetries = 0;
	Ctx.StatBacktracks = 0;
	Ctx.SpawnEdges.Reset(); // record one edge per committed child (seed room 0 has no parent)

	while (Ctx.QueueIdx < Queue.Num() && Ctx.OpenList.Num() > 0)
	{
		int32 SrcListIdx;
		if (Params.BranchProbability > 0.0f && Ctx.OpenList.Num() > 1 && RandomStream.FRand() < Params.BranchProbability)
		{
			SrcListIdx = RandomStream.RandRange(0, Ctx.OpenList.Num() - 1);
		}
		else
		{
			SrcListIdx = Ctx.OpenList.Num() - 1;
		}
		const int32 SrcRoomIdx = Ctx.OpenList[SrcListIdx];

		const int32		NextType = Queue[Ctx.QueueIdx];
		const FVector2D NextHalf = HalfExtentOf(NextType);
		// Bounding-circle radius, not max(half): an OBB's support distance reaches sqrt(hx^2+hy^2) when
		// rotated, so max(hx,hy) under-pushes and large rotated rooms overlapped the source (placement
		// then failed every attempt and gave up after 1 room). The bounding radius clears any rotation.
		const float NextReach = NextHalf.Size();

		bool bPlaced = false;
		for (int32 Attempt = 0; Attempt < Params.MaxPlacementAttempts && !bPlaced; ++Attempt)
		{
			// Pick a random unused doorway on the source room.
			TArray<int32> UnusedDoors;
			for (int32 d = 0; d < Ctx.Rooms[SrcRoomIdx].Doorways.Num(); ++d)
			{
				if (!Ctx.Rooms[SrcRoomIdx].Doorways[d].bUsed)
				{
					UnusedDoors.Add(d);
				}
			}
			if (UnusedDoors.Num() == 0)
			{
				break; // source exhausted
			}
			const int32		DoorIdx = UnusedDoors[RandomStream.RandRange(0, UnusedDoors.Num() - 1)];
			const FVector2D DoorPos = Ctx.Rooms[SrcRoomIdx].Doorways[DoorIdx].Pos;
			const FVector2D DoorNormal = Ctx.Rooms[SrcRoomIdx].Doorways[DoorIdx].OutwardNormal;

			const float L = RandomStream.FRandRange(Params.CorridorLengthMin, Params.CorridorLengthMax);
			// A room-to-room gap can't be smaller than MinRoomGap, so the corridor is at least that long.
			// If MinRoomGap > the requested corridor length, the new room would always overlap the source
			// (inflated by MinRoomGap/2 each) and the location would starve to a single room — clamp up.
			const float		Spacing = FMath::Max(L, Params.MinRoomGap + CellSizeVal);
			const FVector2D NewCenter = DoorPos + DoorNormal * (Spacing + NextReach);
			FOrganicRoom	NewRoom = MakeRoom(Params, NextType, NewCenter, Params.bRandomRotation ? RandomStream.FRandRange(0.0f, 360.0f) : 0.0f);

			// Overlap test (with gap) against all placed rooms.
			const FOBB2D NewOBB = MakeOBB(NewRoom.Center, NewRoom.RotationDeg, NewRoom.HalfExtent, Params.MinRoomGap * 0.5f);
			bool		 bOverlap = false;
			for (int32 r = 0; r < Ctx.Rooms.Num(); ++r)
			{
				const FOBB2D OBB = MakeOBB(Ctx.Rooms[r].Center, Ctx.Rooms[r].RotationDeg, Ctx.Rooms[r].HalfExtent, Params.MinRoomGap * 0.5f);
				if (OBBOverlap(NewOBB, OBB))
				{
					bOverlap = true;
					break;
				}
			}
			// Also avoid rooms placed by other locations (keeps the whole cluster collision-free).
			if (!bOverlap)
			{
				for (const FOrganicRoom& O : Obstacles)
				{
					const FOBB2D OBB = MakeOBB(O.Center, O.RotationDeg, O.HalfExtent, Params.MinRoomGap * 0.5f);
					if (OBBOverlap(NewOBB, OBB))
					{
						bOverlap = true;
						break;
					}
				}
			}
			if (bOverlap)
			{
				++Ctx.StatRetries;
				continue;
			}

			// Commit: placement scatters the room AND records the spawn edge that grew it. Connectivity
			// (which corridors exist) is decided afterwards by ConnectRooms walking these spawn edges; no
			// corridor/doorway is consumed here. Recording reads only already-computed locals (SrcRoomIdx,
			// NewIdx, DoorIdx) so no extra FRandomStream draw happens — placement determinism is preserved.
			const int32 NewIdx = Ctx.Rooms.Add(MoveTemp(NewRoom));
			Ctx.SpawnEdges.Add(FOrgSpawnEdge{ SrcRoomIdx, NewIdx, DoorIdx });
			Ctx.OpenList.Add(NewIdx);
			++Ctx.QueueIdx;
			bPlaced = true;
		}

		if (!bPlaced)
		{
			// Cornered: drop this source so growth continues from an earlier room.
			++Ctx.StatBacktracks;
			Ctx.OpenList.RemoveAt(SrcListIdx);
		}
	}

	Ctx.PlacedCount = Ctx.Rooms.Num();

	// Debug: world AABB of what actually placed (so you can see where this subgraph landed and how big it is).
	FVector2D PlacedMin(FLT_MAX, FLT_MAX);
	FVector2D PlacedMax(-FLT_MAX, -FLT_MAX);
	for (const FOrganicRoom& R : Ctx.Rooms)
	{
		const float Rad = R.HalfExtent.Size();
		PlacedMin.X = FMath::Min(PlacedMin.X, R.Center.X - Rad);
		PlacedMin.Y = FMath::Min(PlacedMin.Y, R.Center.Y - Rad);
		PlacedMax.X = FMath::Max(PlacedMax.X, R.Center.X + Rad);
		PlacedMax.Y = FMath::Max(PlacedMax.Y, R.Center.Y + Rad);
	}
	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG]   Placed %d/%d rooms, bounds=(%.0f,%.0f)-(%.0f,%.0f) size=%.0fx%.0f retries=%d backtracks=%d"),
		Ctx.PlacedCount,
		RequestedRoomCount,
		PlacedMin.X,
		PlacedMin.Y,
		PlacedMax.X,
		PlacedMax.Y,
		PlacedMax.X - PlacedMin.X,
		PlacedMax.Y - PlacedMin.Y,
		Ctx.StatRetries,
		Ctx.StatBacktracks);

	if (Ctx.PlacedCount < RequestedRoomCount)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] Placement shortfall: placed %d / %d rooms (%d unplaced). Check footprint reach vs corridorLen above."),
			Ctx.PlacedCount,
			RequestedRoomCount,
			RequestedRoomCount - Ctx.PlacedCount);
	}
}

bool UOrganicDungeonGenerator2D::MakeRoomCorridor(FOrgSubgraphBuild& Ctx,
	const FOrganicDungeonResolvedParams&							 Params,
	int32															 A,
	int32															 B,
	bool															 bLoop,
	bool															 bSpine,
	float															 RadiusScale,
	int32															 AvoidPolicy)
{
	const ECorridorAvoidPolicy Policy = static_cast<ECorridorAvoidPolicy>(AvoidPolicy);

	// Thin wrapper: builds a corridor using the current subgraph's params (Waviness, thickness, style).
	const auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float InRadiusScale) -> FOrganicCorridor {
		const float MinR = Params.MinThickness * 0.5f;
		const float MaxR = Params.MaxWidth * 0.5f;
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, Params.WavinessControlPoints, InRadiusScale);
	};

	const FVector2D ToB = (Ctx.Rooms[B].Center - Ctx.Rooms[A].Center).GetSafeNormal();
	const int32		DA = PickDoorway(Ctx.Rooms, A, ToB);
	const int32		DB = PickDoorway(Ctx.Rooms, B, -ToB);
	if (DA == INDEX_NONE || DB == INDEX_NONE)
	{
		return false;
	}
	const FVector2D	 APos = Ctx.Rooms[A].Doorways[DA].Pos;
	const FVector2D	 ANrm = Ctx.Rooms[A].Doorways[DA].OutwardNormal;
	const FVector2D	 BPos = Ctx.Rooms[B].Doorways[DB].Pos;
	const FVector2D	 BNrm = Ctx.Rooms[B].Doorways[DB].OutwardNormal;
	FOrganicCorridor Cor = BuildCorridor(APos, ANrm, BPos, BNrm, RadiusScale);

	if (Policy != ECorridorAvoidPolicy::None)
	{
		// Collision must run on the SAMPLED curve, not the few stored control points, so a bow between two
		// control points can't clip a room undetected. Endpoint rooms A and B may only be entered along their
		// doorway approach span — never crossed through the body — so they are passed as endpoints (not just
		// allowed-room exemptions) to the unified check.
		const float				  ApproachDepth = FMath::Max(Params.MinThickness * 0.5f, static_cast<float>(GridSize));
		const int32				  Allowed[2] = { A, B };
		const FOrganicRoom* const EndA = &Ctx.Rooms[A];
		const FOrganicRoom* const EndB = &Ctx.Rooms[B];

		TArray<FVector2D> DenseCenter;
		SampleCorridorCurve(Cor.Centerline, ANrm, BNrm, static_cast<float>(GridSize) * 0.5f, DenseCenter);
		FCorridorRoomCheck Check = CheckCorridorAgainstRooms(Ctx.Rooms, DenseCenter, Allowed, EndA, APos, ANrm, EndB, BPos, BNrm, ApproachDepth);

		// An endpoint-body crossing is unfixable by a third-room waypoint — always reject regardless of policy.
		if (Check.bEndpointBodyCrossed)
		{
			return false;
		}

		if (Check.bThirdRoomHit)
		{
			if (Policy == ECorridorAvoidPolicy::Drop)
			{
				return false; // optional edge: drop silently (historic reject behavior)
			}

			// MANDATORY edge (Reroute): steer around the blocking room with ONE nearest-corner waypoint and
			// rebuild the curve as A->waypoint->B (two spliced few-point builds). Pure geometry — no FRandomStream
			// draws — so the global draw sequence is unperturbed. Retry the room test once; drop as a last resort.
			const float		ClearRadius = FMath::Max(Params.MaxWidth * 0.5f, static_cast<float>(GridSize)) + static_cast<float>(GridSize);
			const FVector2D Waypoint = ComputeRerouteWaypoint(Ctx.Rooms[Check.BlockingRoom], APos, BPos, ClearRadius);

			const FVector2D	 ToW = (Waypoint - APos).GetSafeNormal();
			const FVector2D	 FromW = (BPos - Waypoint).GetSafeNormal();
			FOrganicCorridor Leg1 = BuildCorridor(APos, ANrm, Waypoint, -ToW, RadiusScale);
			FOrganicCorridor Leg2 = BuildCorridor(Waypoint, FromW, BPos, BNrm, RadiusScale);

			// Splice: Leg1 centerline + Leg2 (drop Leg2's duplicate first point = the waypoint).
			FOrganicCorridor Rerouted;
			Rerouted.Centerline = Leg1.Centerline;
			Rerouted.Radii = Leg1.Radii;
			for (int32 i = 1; i < Leg2.Centerline.Num(); ++i)
			{
				Rerouted.Centerline.Add(Leg2.Centerline[i]);
				Rerouted.Radii.Add(Leg2.Radii.IsValidIndex(i) ? Leg2.Radii[i] : Rerouted.Radii.Last());
			}

			TArray<FVector2D> RerouteDense;
			SampleCorridorCurve(Rerouted.Centerline, ANrm, BNrm, static_cast<float>(GridSize) * 0.5f, RerouteDense);
			FCorridorRoomCheck RerouteCheck =
				CheckCorridorAgainstRooms(Ctx.Rooms, RerouteDense, Allowed, EndA, APos, ANrm, EndB, BPos, BNrm, ApproachDepth);
			if (RerouteCheck.bThirdRoomHit || RerouteCheck.bEndpointBodyCrossed)
			{
				// Last resort: a single waypoint could not clear a dense cluster. Drop and log a mandatory
				// shortfall so the e2e loop detects the lost connectivity (never silently drop a backbone).
				++Ctx.StatMandatoryDrops;
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[ORG] Mandatory corridor room[%d]->room[%d] dropped: single-waypoint reroute around room[%d] still crossed a room "
						 "(connectivity shortfall, generation NOT failed)."),
					A,
					B,
					Check.BlockingRoom);
				return false;
			}

			Cor = MoveTemp(Rerouted);
			++Ctx.StatReroutes;
		}
	}

	// Clamp corridor endpoint radii to half the declared opening width so the carved passage
	// never blasts through the wall geometry surrounding the doorway.
	if (Ctx.Rooms[A].Doorways[DA].bDeclared && Ctx.Rooms[A].Doorways[DA].Width > 0.0f && Cor.Radii.Num() > 0)
	{
		Cor.Radii[0] = FMath::Min(Cor.Radii[0], Ctx.Rooms[A].Doorways[DA].Width * 0.5f);
	}
	if (Ctx.Rooms[B].Doorways[DB].bDeclared && Ctx.Rooms[B].Doorways[DB].Width > 0.0f && Cor.Radii.Num() > 0)
	{
		Cor.Radii.Last() = FMath::Min(Cor.Radii.Last(), Ctx.Rooms[B].Doorways[DB].Width * 0.5f);
	}

	Ctx.Rooms[A].Doorways[DA].bUsed = true;
	Ctx.Rooms[B].Doorways[DB].bUsed = true;
	Cor.AnchorA = { EOrganicAnchorType::Room, A, APos, ANrm };
	Cor.AnchorB = { EOrganicAnchorType::Room, B, BPos, BNrm };
	Cor.bIsLoop = bLoop;
	Cor.bIsSpine = bSpine;
	Ctx.Corridors.Add(MoveTemp(Cor));
	return true;
}

void UOrganicDungeonGenerator2D::ConnectRooms(
	FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles)
{
	// --- Layer 1: connectivity — placement-spawn-tree backbone + budgeted loops, with a widened spine ---
	//
	// PlaceRooms already grew every non-seed room off a parent room's open doorway (Ctx.SpawnEdges), so those
	// edges form a connected spanning tree BY CONSTRUCTION. We carve one backbone corridor per spawn edge —
	// there is no geometric MST over room centers, and NO room is ever dropped: every placed room is a tree
	// node (>=2 backbone corridors = chain link, >2 = branch, 1 = leaf/cap). FindSpine is still used, but only
	// to pick the start/end (graph-diameter) endpoints over the spawn tree — never for connectivity.
	(void)Obstacles; // orientation-solve drop path removed; obstacles are only consulted during PlaceRooms now

	const int32 NumRooms = Ctx.Rooms.Num();
	Ctx.StartRoomIdx = (NumRooms > 0) ? 0 : -1; // graph-diameter endpoints (set during connectivity)
	Ctx.EndRoomIdx = -1;

	if (NumRooms >= 2)
	{
		// Build symmetric spawn-tree adjacency from the recorded spawn edges, then find the graph diameter
		// over it → start/end endpoints + spine edge set (used only to widen backbone corridors on the path).
		Ctx.SpawnAdj.Reset();
		Ctx.SpawnAdj.SetNum(NumRooms);
		for (const FOrgSpawnEdge& E : Ctx.SpawnEdges)
		{
			if (Ctx.SpawnAdj.IsValidIndex(E.Parent) && Ctx.SpawnAdj.IsValidIndex(E.Child))
			{
				Ctx.SpawnAdj[E.Parent].Add(E.Child);
				Ctx.SpawnAdj[E.Child].Add(E.Parent);
			}
		}
		FindSpine(Ctx.Rooms, Ctx.SpawnAdj, CenterPoint, Ctx.StartRoomIdx, Ctx.EndRoomIdx, Ctx.SpineEdges);

		// Helper: for declared-doorway rooms, returns true only if a free declared doorway
		// faces Toward within ~60°.  Legacy rooms always return true (no restriction).
		// Used to gate optional edges (loops, dead-ends, links) so they never silently
		// consume a declared opening that was intended for a mandatory corridor.
		const auto CanAttachOptional = [&](int32 RoomIdx, const FVector2D& Toward) -> bool {
			if (!Ctx.Rooms.IsValidIndex(RoomIdx))
			{
				return false;
			}
			const FOrganicResolvedRoomType& RT = Params.RoomTypes[Ctx.Rooms[RoomIdx].TypeIndex];
			if (RT.Doorways.Num() == 0)
			{
				return true; // legacy room: always eligible
			}
			constexpr float AlignDot = 0.5f; // cos(60°)
			for (const FOrganicDoorway& D : Ctx.Rooms[RoomIdx].Doorways)
			{
				if (!D.bUsed && FVector2D::DotProduct(D.OutwardNormal, Toward) >= AlignDot)
				{
					return true;
				}
			}
			return false;
		};

		// Backbone corridors: one per spawn edge (spine ones widened). MakeRoomCorridor anchors at the
		// best-facing PickDoorway-chosen doorways on each endpoint; the parent's actual spawn doorway is
		// usually that best-facing one (PlaceRooms grew the child off it), so the carve follows the spawn
		// geometry without needing a dedicated index-taking overload. A corridor can fail to carve when a
		// room's doorways are exhausted/misaligned (the all-single-door "star" case) — that is logged as a
		// best-effort shortfall and the room is NEVER dropped (junctions absorb the deficit in a later task).
		TSet<uint64> EdgeSet;
		for (const FOrgSpawnEdge& E : Ctx.SpawnEdges)
		{
			const bool	 bSpine = Ctx.SpineEdges.Contains(EdgeKey(E.Parent, E.Child));
			const uint64 Key = EdgeKey(E.Parent, E.Child);
			EdgeSet.Add(Key); // record even on failure so the loops stage never re-tries the same backbone pair
			// Backbone is MANDATORY: it must clear every non-endpoint room — reroute around a blocker, drop only
			// as a last resort (logged). This was previously untested (None), so layouts with crossing backbones
			// will change — that is the intended bug fix.
			if (MakeRoomCorridor(Ctx,
					Params,
					E.Parent,
					E.Child,
					false,
					bSpine,
					bSpine ? Params.SpineWidthScale : 1.0f,
					static_cast<int32>(ECorridorAvoidPolicy::Reroute)))
			{
				if (bSpine)
				{
					++Ctx.StatSpine;
				}
			}
			else
			{
				UE_LOG(LogRoguelikeGeometry,
					Verbose,
					TEXT("[ORG] Connectivity: backbone corridor parent[%d]->child[%d] could not carve (doorways exhausted/misaligned) — "
						 "room kept, connectivity shortfall logged (junctions absorb this in a later phase)"),
					E.Parent,
					E.Child);
			}
		}

		// Loops: add the shortest non-backbone edges within LoopMaxDistance, up to LoopCount.
		if (Params.LoopCount > 0)
		{
			const int32						CurrentRoomCount = Ctx.Rooms.Num();
			TArray<TPair<float, FIntPoint>> Cand;
			for (int32 a = 0; a < CurrentRoomCount; ++a)
			{
				for (int32 b = a + 1; b < CurrentRoomCount; ++b)
				{
					if (EdgeSet.Contains(EdgeKey(a, b)))
					{
						continue;
					}
					const float D = FVector2D::Distance(Ctx.Rooms[a].Center, Ctx.Rooms[b].Center);
					if (D <= Params.LoopMaxDistance)
					{
						Cand.Add({ D, FIntPoint(a, b) });
					}
				}
			}
			Cand.Sort([](const TPair<float, FIntPoint>& L, const TPair<float, FIntPoint>& R) { return L.Key < R.Key; });
			for (int32 i = 0; i < Cand.Num() && Ctx.StatLoops < Params.LoopCount; ++i)
			{
				const int32		a = Cand[i].Value.X;
				const int32		b = Cand[i].Value.Y;
				const FVector2D ToB = (Ctx.Rooms[b].Center - Ctx.Rooms[a].Center).GetSafeNormal();
				// Optional edge: skip if a declared-doorway room has no free aligned opening.
				if (!CanAttachOptional(a, ToB) || !CanAttachOptional(b, -ToB))
				{
					continue;
				}
				// Loops are OPTIONAL: drop silently if the curve crosses a room body it does not connect.
				if (MakeRoomCorridor(Ctx, Params, a, b, true, false, 1.0f, static_cast<int32>(ECorridorAvoidPolicy::Drop)))
				{
					EdgeSet.Add(EdgeKey(a, b));
					++Ctx.StatLoops;
				}
			}

			// Best-effort accounting: a loop can fall short of the request when no eligible non-backbone pair
			// is within LoopMaxDistance, or every candidate's declared doorways are exhausted. Generation never
			// fails on a loop shortfall — it is logged so the e2e dump can report achieved-vs-requested cycles.
			if (Ctx.StatLoops < Params.LoopCount)
			{
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[ORG] Loop shortfall: realized %d / %d loop(s) (%d short — no eligible spare-doorway cycle within LoopMaxDistance)."),
					Ctx.StatLoops,
					Params.LoopCount,
					Params.LoopCount - Ctx.StatLoops);
			}
		}
	}
}

FOrganicEndRoomAnchor UOrganicDungeonGenerator2D::ComputeChainLeaf(
	FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint)
{
	// --- Compute chain-leaf end-room anchor for inter-location stitching and the segment exit ---
	// The chain leaf is the spawn-tree diameter endpoint opposite the entrance. Its outward doorway position is
	// returned so GenerateInternal can seed the next location's anchor point. The doorway is NOT reserved
	// here — BuildInterLocationCorridor independently picks the best facing doorway when stitching.
	FOrganicEndRoomAnchor ChainLeaf;
	ChainLeaf.RoomIndex = Ctx.EndRoomIdx;
	if (Ctx.EndRoomIdx >= 0)
	{
		// Compute outward direction from dungeon centroid so the chain-leaf doorway faces away from the cluster.
		FVector2D Centroid(0.0f, 0.0f);
		for (const FOrganicRoom& R : Ctx.Rooms)
		{
			Centroid += R.Center;
		}
		Centroid /= static_cast<float>(FMath::Max(1, Ctx.Rooms.Num()));
		const FVector2D Outward = (Ctx.Rooms[Ctx.EndRoomIdx].Center - Centroid).GetSafeNormal();

		// Find the best outward-facing free doorway (prefer free so the chain-leaf has a clean attachment point).
		int32 BestFree = INDEX_NONE;
		float BestFreeDot = -FLT_MAX;
		int32 BestAny = INDEX_NONE;
		float BestAnyDot = -FLT_MAX;
		for (int32 d = 0; d < Ctx.Rooms[Ctx.EndRoomIdx].Doorways.Num(); ++d)
		{
			const FVector2D Dir = Outward.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Outward;
			const float		Dot = FVector2D::DotProduct(Ctx.Rooms[Ctx.EndRoomIdx].Doorways[d].OutwardNormal, Dir);
			if (Dot > BestAnyDot)
			{
				BestAnyDot = Dot;
				BestAny = d;
			}
			if (!Ctx.Rooms[Ctx.EndRoomIdx].Doorways[d].bUsed && Dot > BestFreeDot)
			{
				BestFreeDot = Dot;
				BestFree = d;
			}
		}
		const int32 ChainDoor = (BestFree != INDEX_NONE) ? BestFree : BestAny;
		if (ChainDoor != INDEX_NONE)
		{
			ChainLeaf.Pos = Ctx.Rooms[Ctx.EndRoomIdx].Doorways[ChainDoor].Pos;
			ChainLeaf.Normal = Ctx.Rooms[Ctx.EndRoomIdx].Doorways[ChainDoor].OutwardNormal;
		}
		else
		{
			ChainLeaf.Pos = Ctx.Rooms[Ctx.EndRoomIdx].Center;
			ChainLeaf.Normal = Outward.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Outward;
		}
	}
	else
	{
		ChainLeaf.Pos = CenterPoint;
		ChainLeaf.Normal = FVector2D(1.0f, 0.0f);
	}

	if (Ctx.StartRoomIdx >= 0 && Params.bHasStartRoom)
	{
		Ctx.Rooms[Ctx.StartRoomIdx].RoomLevel = Params.StartRoom.RoomLevel;
		Ctx.Rooms[Ctx.StartRoomIdx].FootprintCenterOffset = Params.StartRoom.FootprintCenter;
	}

	// The end-room prefab swap is applied once at cluster scope (GenerateInternal) on the cluster's single
	// exit room — the LAST segment's far endpoint — not per intermediate segment (whose end rooms are
	// internal hand-offs to the next location, not portals).

	return ChainLeaf;
}

void UOrganicDungeonGenerator2D::AddDeadEnds(FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params)
{
	// Thin wrapper: builds a corridor using the current subgraph's params (Waviness, thickness, style).
	const auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float InRadiusScale) -> FOrganicCorridor {
		const float MinR = Params.MinThickness * 0.5f;
		const float MaxR = Params.MaxWidth * 0.5f;
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, Params.WavinessControlPoints, InRadiusScale);
	};

	// --- Layer 1b: dead-end stubs ---
	for (int32 i = 0; i < Params.DeadEndCount; ++i)
	{
		// Find rooms with a free doorway.
		TArray<int32> Candidates;
		for (int32 r = 0; r < Ctx.Rooms.Num(); ++r)
		{
			for (const FOrganicDoorway& D : Ctx.Rooms[r].Doorways)
			{
				if (!D.bUsed)
				{
					Candidates.Add(r);
					break;
				}
			}
		}
		if (Candidates.Num() == 0)
		{
			break;
		}
		const int32	  RoomIdx = Candidates[RandomStream.RandRange(0, Candidates.Num() - 1)];
		TArray<int32> Free;
		for (int32 d = 0; d < Ctx.Rooms[RoomIdx].Doorways.Num(); ++d)
		{
			if (!Ctx.Rooms[RoomIdx].Doorways[d].bUsed)
			{
				Free.Add(d);
			}
		}
		const int32		DoorIdx = Free[RandomStream.RandRange(0, Free.Num() - 1)];
		const FVector2D DoorPos = Ctx.Rooms[RoomIdx].Doorways[DoorIdx].Pos;
		const FVector2D DoorNormal = Ctx.Rooms[RoomIdx].Doorways[DoorIdx].OutwardNormal;
		const FVector2D EndPos = DoorPos + DoorNormal * Params.DeadEndLength;

		// A dead-end stub is OPTIONAL: drop it if the straight stub would clip a room other than its own.
		// Wrap the segment as a 2-point centerline so the test runs through the unified room-clearance helper.
		const int32				StubAllowed[1] = { RoomIdx };
		const FVector2D			StubCenterline[2] = { DoorPos, EndPos };
		const TArray<FVector2D> StubPoly(StubCenterline, 2);
		int32					StubBlocker = INDEX_NONE;
		if (!CorridorClearsRooms(Ctx.Rooms, StubPoly, StubAllowed, StubBlocker))
		{
			continue;
		}

		Ctx.Rooms[RoomIdx].Doorways[DoorIdx].bUsed = true;
		// Stub: corridor from doorway to a point, ending in a small chamber (use BuildCorridor with an
		// outward end normal so the curve eases out).
		FOrganicCorridor Cor = BuildCorridor(DoorPos, DoorNormal, EndPos, -DoorNormal, 1.0f);
		Cor.AnchorA = { EOrganicAnchorType::Room, RoomIdx, DoorPos, DoorNormal };
		Cor.AnchorB = { EOrganicAnchorType::Free, -1, EndPos, -DoorNormal };
		Ctx.Corridors.Add(MoveTemp(Cor));
		++Ctx.StatDeadEnds;
	}
}

void UOrganicDungeonGenerator2D::AddCorridorLinks(FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params)
{
	// Thin wrapper: builds a corridor using the current subgraph's params (Waviness, thickness, style).
	const auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float InRadiusScale) -> FOrganicCorridor {
		const float MinR = Params.MinThickness * 0.5f;
		const float MaxR = Params.MaxWidth * 0.5f;
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, Params.WavinessControlPoints, InRadiusScale);
	};

	// --- Layer 1b: corridor links (a branch from a corridor to the nearest room doorway or other corridor) ---
	Ctx.Corridors.Reserve(Ctx.Corridors.Num() + Params.CorridorLinkCount); // keep Src reference valid across Add
	for (int32 i = 0; i < Params.CorridorLinkCount; ++i)
	{
		if (Ctx.Corridors.Num() == 0)
		{
			break;
		}
		const int32				SrcCorIdx = RandomStream.RandRange(0, Ctx.Corridors.Num() - 1);
		const FOrganicCorridor& Src = Ctx.Corridors[SrcCorIdx];

		// The stored centerline is few-point; sample it into a dense curve so the branch point/tangent come
		// from the actual curve (not the coarse control polyline). For a legacy straight (k=0) corridor this
		// dense array matches the old per-cell centerline, so the RandRange bounds below reproduce legacy
		// draws exactly. The two anchor normals seed the doorway-aligned curve endpoints.
		TArray<FVector2D> SrcDense;
		SampleCorridorCurve(Src.Centerline, Src.AnchorA.Normal, Src.AnchorB.Normal, static_cast<float>(GridSize) * 0.5f, SrcDense);
		const int32 N = SrcDense.Num();
		if (N < 4)
		{
			continue;
		}

		// Rooms the source corridor already touches — a link must not reconnect to these (neighbours).
		int32 SrcRoomA = INDEX_NONE;
		int32 SrcRoomB = INDEX_NONE;
		CorridorRooms(Src, SrcRoomA, SrcRoomB);

		// Branch point away from the ends, with a direction roughly perpendicular to the corridor.
		const int32		Margin = FMath::Max(1, N / 5);
		const int32		Bi = RandomStream.RandRange(Margin, N - 1 - Margin);
		const FVector2D P = SrcDense[Bi];
		const FVector2D Tangent = (SrcDense[Bi + 1] - SrcDense[Bi - 1]).GetSafeNormal();
		const FVector2D BranchDir = (RandomStream.FRand() < 0.5f) ? FVector2D(-Tangent.Y, Tangent.X) : FVector2D(Tangent.Y, -Tangent.X);

		// A link primarily springs corridor-to-corridor (nearest corridor point) so it forms independent of
		// room-doorway capacity. A free room doorway is only used as a fallback when no corridor target
		// exists. Each scan keeps its OWN best-distance accumulator so a nearer room never steals a valid
		// corridor target. Both scans are deterministic geometry — they draw no RandomStream values.
		FOrganicAnchor RoomTarget;
		bool		   bRoomFound = false;
		float		   BestRoomDist = Params.LinkMaxDistance;
		int32		   TargetRoom = INDEX_NONE;
		int32		   TargetDoor = INDEX_NONE;

		for (int32 r = 0; r < Ctx.Rooms.Num(); ++r)
		{
			if (r == SrcRoomA || r == SrcRoomB)
			{
				continue; // already a neighbour of the source corridor
			}
			for (int32 d = 0; d < Ctx.Rooms[r].Doorways.Num(); ++d)
			{
				if (Ctx.Rooms[r].Doorways[d].bUsed)
				{
					continue;
				}
				const FVector2D To = Ctx.Rooms[r].Doorways[d].Pos - P;
				const float		Dist = To.Size();
				if (Dist < 1.0f || Dist > BestRoomDist || FVector2D::DotProduct(To / Dist, BranchDir) < 0.2f)
				{
					continue;
				}
				BestRoomDist = Dist;
				RoomTarget = { EOrganicAnchorType::Room, r, Ctx.Rooms[r].Doorways[d].Pos, Ctx.Rooms[r].Doorways[d].OutwardNormal };
				TargetRoom = r;
				TargetDoor = d;
				bRoomFound = true;
			}
		}

		FOrganicAnchor CorTarget;
		bool		   bCorFound = false;
		float		   BestCorDist = Params.LinkMaxDistance;
		for (int32 c = 0; c < Ctx.Corridors.Num(); ++c)
		{
			if (c == SrcCorIdx)
			{
				continue;
			}
			int32 cA = INDEX_NONE;
			int32 cB = INDEX_NONE;
			CorridorRooms(Ctx.Corridors[c], cA, cB);
			const bool bShareRoom =
				(cA != INDEX_NONE && (cA == SrcRoomA || cA == SrcRoomB)) || (cB != INDEX_NONE && (cB == SrcRoomA || cB == SrcRoomB));
			if (bShareRoom)
			{
				continue; // corridors already joined through a shared room
			}
			// Run nearest-point against the SAMPLED candidate curve, not the coarse control polyline, so the
			// branch lands on the real corridor surface. Geometry-only — no RandomStream draws.
			TArray<FVector2D> CandDense;
			SampleCorridorCurve(Ctx.Corridors[c].Centerline,
				Ctx.Corridors[c].AnchorA.Normal,
				Ctx.Corridors[c].AnchorB.Normal,
				static_cast<float>(GridSize) * 0.5f,
				CandDense);
			float			D2 = FLT_MAX;
			const FVector2D Q = NearestOnPolyline(CandDense, P, D2);
			const float		Dist = FMath::Sqrt(D2);
			if (Dist < 1.0f || Dist > BestCorDist)
			{
				continue;
			}
			const FVector2D To = Q - P;
			if (FVector2D::DotProduct(To / Dist, BranchDir) < 0.2f)
			{
				continue;
			}
			BestCorDist = Dist;
			CorTarget = { EOrganicAnchorType::Corridor, c, Q, (P - Q).GetSafeNormal() };
			bCorFound = true;
		}

		// Prefer the corridor-to-corridor target so links never depend on a free room doorway; fall back to
		// a room doorway only when no corridor target was found.
		FOrganicAnchor Target;
		bool		   bUseRoomTarget = false;
		if (bCorFound)
		{
			Target = CorTarget;
		}
		else if (bRoomFound)
		{
			Target = RoomTarget;
			bUseRoomTarget = true;
		}
		else
		{
			continue;
		}

		// Build the actual curved link and reject it if its centerline clips a room other than the
		// source corridor's rooms or (for a room target) the target room. For a corridor target TargetRoom
		// is INDEX_NONE, so only the source corridor's rooms are exempt — genuine third-room crossings drop,
		// while a corridor-to-corridor link running near rooms it does not enter is kept.
		const int32		 RejectAllowedTarget = bUseRoomTarget ? TargetRoom : INDEX_NONE;
		FOrganicCorridor Link = BuildCorridor(P, BranchDir, Target.Pos, Target.Normal, 1.0f);
		// Reject on the SAMPLED link curve so a wavy bow can't clip a room undetected. A link is OPTIONAL —
		// drop it (don't reroute) if it crosses a room body it does not connect. Allowed = the source
		// corridor's rooms plus (for a room target) the target room; INDEX_NONE entries are filtered out.
		TArray<FVector2D> LinkDense;
		SampleCorridorCurve(Link.Centerline, BranchDir, Target.Normal, static_cast<float>(GridSize) * 0.5f, LinkDense);
		TArray<int32, TInlineAllocator<3>> LinkAllowed;
		if (SrcRoomA != INDEX_NONE)
		{
			LinkAllowed.Add(SrcRoomA);
		}
		if (SrcRoomB != INDEX_NONE)
		{
			LinkAllowed.Add(SrcRoomB);
		}
		if (RejectAllowedTarget != INDEX_NONE)
		{
			LinkAllowed.Add(RejectAllowedTarget);
		}
		int32 LinkBlocker = INDEX_NONE;
		if (!CorridorClearsRooms(Ctx.Rooms, LinkDense, LinkAllowed, LinkBlocker))
		{
			continue;
		}

		if (bUseRoomTarget)
		{
			Ctx.Rooms[TargetRoom].Doorways[TargetDoor].bUsed = true;
		}
		Link.AnchorA = { EOrganicAnchorType::Corridor, SrcCorIdx, P, BranchDir };
		Link.AnchorB = Target;
		Link.bIsLink = true;
		Ctx.Corridors.Add(MoveTemp(Link));
		++Ctx.StatLinks;
	}

	// Best-effort accounting: a link can fall short when no corridor/room target lies within LinkMaxDistance
	// in the branch direction, or every candidate curve would cross a room body and is rejected. Generation
	// never fails on a link shortfall — it is logged so the e2e dump can report achieved-vs-requested links.
	if (Ctx.StatLinks < Params.CorridorLinkCount)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] Link shortfall: realized %d / %d corridor link(s) (%d short — no in-range target or curve crossed a room)."),
			Ctx.StatLinks,
			Params.CorridorLinkCount,
			Params.CorridorLinkCount - Ctx.StatLinks);
	}
}

void UOrganicDungeonGenerator2D::AddJunctions(FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params)
{
	// Zero junctions requested → take NO RandomStream draws and add no entries, so the layout (and dump) is
	// byte-identical to the legacy pipeline. This early-out is the determinism guarantee for the whole feature.
	if (Params.JunctionCount <= 0 || Ctx.Rooms.Num() == 0)
	{
		return;
	}

	const float CellSizeVal = static_cast<float>(GridSize);

	// Thin wrapper mirroring AddCorridorLinks: builds a corridor using this subgraph's params.
	const auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float InRadiusScale) -> FOrganicCorridor {
		const float MinR = Params.MinThickness * 0.5f;
		const float MaxR = Params.MaxWidth * 0.5f;
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, Params.WavinessControlPoints, InRadiusScale);
	};

	Ctx.Junctions.Reserve(Params.JunctionCount);
	for (int32 i = 0; i < Params.JunctionCount; ++i)
	{
		// Anchor each junction off a random room's best outward doorway and grow it into open space, so the
		// junction reads as a hub a corridor melts into rather than a free-floating disc. Draw order: room
		// pick, doorway-radius pick (MakeJunction's diameter/jitter draws follow), preserving determinism.
		const int32			RoomIdx = RandomStream.RandRange(0, Ctx.Rooms.Num() - 1);
		const FOrganicRoom& Room = Ctx.Rooms[RoomIdx];
		if (Room.Doorways.Num() == 0)
		{
			continue;
		}

		// Prefer an unused doorway facing away from the room center; fall back to any doorway.
		int32 DoorIdx = INDEX_NONE;
		for (int32 d = 0; d < Room.Doorways.Num(); ++d)
		{
			if (!Room.Doorways[d].bUsed)
			{
				DoorIdx = d;
				break;
			}
		}
		if (DoorIdx == INDEX_NONE)
		{
			DoorIdx = 0;
		}

		const FVector2D DoorPos = Room.Doorways[DoorIdx].Pos;
		const FVector2D DoorNormal = Room.Doorways[DoorIdx].OutwardNormal;

		// Junction center sits a corridor-reach away along the doorway outward normal, into open space.
		const float		Reach = RandomStream.FRandRange(Params.CorridorLengthMin, Params.CorridorLengthMax);
		const FVector2D Center = DoorPos + DoorNormal * (Reach + CellSizeVal);

		// Reject (best-effort re-roll) if the junction disc center lands inside a room other than its own:
		// the doorway-to-center span may not pass through a third room's body, and the disc itself must not
		// overlap one. Mirrors the SHARED MODEL invariant enforced by AddDeadEnds / AddCorridorLinks.
		const int32				JAllowed[1] = { RoomIdx };
		const FVector2D			JSpan[2] = { DoorPos, Center };
		const TArray<FVector2D> JSpanPoly(JSpan, 2);
		int32					JSpanBlocker = INDEX_NONE;
		if (!CorridorClearsRooms(Ctx.Rooms, JSpanPoly, JAllowed, JSpanBlocker))
		{
			continue;
		}

		FOrganicJunction J = MakeJunction(Params, Center);
		J.LocationIndex = Room.LocationIndex;

		// Attach the connecting corridor at the junction perimeter point facing back toward the room doorway.
		FVector2D AttachPos;
		FVector2D AttachNormal;
		AttachCorridorToJunction(J, DoorPos - J.Center, AttachPos, AttachNormal);

		const int32		 JunctionIdx = Ctx.Junctions.Num();
		FOrganicCorridor Cor = BuildCorridor(DoorPos, DoorNormal, AttachPos, AttachNormal, 1.0f);

		// Reject the tap if its actual (sampled) curve crosses any room other than the originating room — a
		// junction tap must never breach a room body. The junction has no Room anchor, so only RoomIdx is
		// exempt; a genuine third-room crossing re-rolls. Routes through the unified CorridorClearsRooms helper.
		TArray<FVector2D> DenseCurve;
		SampleCorridorCurve(Cor.Centerline, DoorNormal, AttachNormal, static_cast<float>(GridSize) * 0.5f, DenseCurve);
		const int32 TapAllowed[1] = { RoomIdx };
		int32		TapBlocker = INDEX_NONE;
		if (!CorridorClearsRooms(Ctx.Rooms, DenseCurve, TapAllowed, TapBlocker))
		{
			continue;
		}

		Cor.AnchorA = { EOrganicAnchorType::Room, RoomIdx, DoorPos, DoorNormal };
		Cor.AnchorB = { EOrganicAnchorType::Junction, JunctionIdx, AttachPos, AttachNormal };
		Cor.bIsLink = true; // a junction tap is a connectivity branch, not part of the room backbone

		Ctx.Rooms[RoomIdx].Doorways[DoorIdx].bUsed = true;
		Ctx.Junctions.Add(MoveTemp(J));
		Ctx.Corridors.Add(MoveTemp(Cor));
		++Ctx.StatJunctions;
	}

	// Log any shortfall between requested and actually-placed junctions (rejections never fail generation).
	if (Ctx.StatJunctions < Params.JunctionCount)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] Junction shortfall: added %d / %d junctions (%d rejected for crossing a room)."),
			Ctx.StatJunctions,
			Params.JunctionCount,
			Params.JunctionCount - Ctx.StatJunctions);
	}
}

FOrganicLayout UOrganicDungeonGenerator2D::GenerateLocationSubgraph(
	const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles)
{
	// Build the placement queue, interleaving types so a high-count type doesn't form one long single-type
	// chain. See BuildRoomQueue for the full scheduling logic.
	TArray<int32> Queue;
	const int32	  RequestedRoomCount = BuildRoomQueue(Params, RandomStream, Queue);

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT(
			"[ORG] Generate() — GridSize=%d Seed='%s' RoomTypes=%d RequestedRooms=%d style=%d thickness=[%.0f,%.0f] wav=%.2f corridorLen=[%.0f,%.0f] "
			"branch=%.2f loops=%d spine=%.2f deadEnds=%d links=%d wall=%d"),
		GridSize,
		*Seed,
		Params.RoomTypes.Num(),
		RequestedRoomCount,
		static_cast<int32>(Params.CorridorStyle),
		Params.MinThickness,
		Params.MaxWidth,
		Params.Waviness,
		Params.CorridorLengthMin,
		Params.CorridorLengthMax,
		Params.BranchProbability,
		Params.LoopCount,
		Params.SpineWidthScale,
		Params.DeadEndCount,
		Params.CorridorLinkCount,
		Params.WallThickness);

	if (RequestedRoomCount == 0)
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[ORG] No room types with positive count — nothing to generate."));
		return FOrganicLayout{};
	}

	// Debug: anchor/obstacle context + actual room footprints. If a room's reach exceeds corridorLen,
	// rotated rooms can't clear the source and placement starves (the classic "1/N placed" shortfall).
	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG]   Subgraph anchor=(%.0f,%.0f) obstacles=%d corridorLen=[%.0f,%.0f] minGap=%.0f rotation=%s"),
		CenterPoint.X,
		CenterPoint.Y,
		Obstacles.Num(),
		Params.CorridorLengthMin,
		Params.CorridorLengthMax,
		Params.MinRoomGap,
		Params.bRandomRotation ? TEXT("on") : TEXT("off"));
	for (int32 t = 0; t < Params.RoomTypes.Num(); ++t)
	{
		const FOrganicResolvedRoomType& RT = Params.RoomTypes[t];
		const float						Reach = FVector2D(RT.FootprintWidth * 0.5f, RT.FootprintHeight * 0.5f).Size();
		UE_LOG(LogRoguelikeGeometry,
			Log,
			TEXT("[ORG]     roomType[%d] '%s' count=%d footprint=%.0fx%.0f reach=%.0f%s"),
			t,
			*RT.DisplayName.ToString(),
			RT.Count,
			RT.FootprintWidth,
			RT.FootprintHeight,
			Reach,
			(Reach > Params.CorridorLengthMax) ? TEXT("  [reach > corridorLenMax]") : TEXT(""));
	}
	if (Params.MinRoomGap > Params.CorridorLengthMax)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG]   MinRoomGap (%.0f) > CorridorLengthMax (%.0f): corridors are forced to ~MinRoomGap so rooms can place. "
				 "Lower MinRoomGap or raise CorridorLength for shorter corridors."),
			Params.MinRoomGap,
			Params.CorridorLengthMax);
	}

	// Drive the phases in the fixed order placement → connectivity → chain-leaf → dead-ends → links.
	// Each helper threads the shared working state through Build and preserves the FRandomStream draw
	// sequence exactly, so the generated layout is identical to the former monolithic body.
	FOrgSubgraphBuild Build;
	PlaceRooms(Build, Params, CenterPoint, Obstacles, Queue, RequestedRoomCount);
	ConnectRooms(Build, Params, CenterPoint, Obstacles);
	const FOrganicEndRoomAnchor ChainLeaf = ComputeChainLeaf(Build, Params, CenterPoint);
	AddDeadEnds(Build, Params);
	AddCorridorLinks(Build, Params);
	// Junctions are the LAST phase: count-gated so a zero-junction build draws no RandomStream values and
	// stays byte-identical to the legacy pipeline.
	AddJunctions(Build, Params);

	// Achieved-vs-requested constraint accounting (rooms/loops/links/junctions). Every figure is best-effort:
	// the pipeline NEVER fails on a shortfall, it only reports it here so the out-of-band e2e dump can verify
	// what the network actually realized against what the location asset requested.
	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG] Layout: rooms=%d/%d corridors=%d spine=%d loops=%d/%d deadEnds=%d links=%d/%d junctions=%d/%d | entrance=%d chainLeaf=%d | "
			 "placementRetries=%d backtracks=%d reroutes=%d mandatoryDrops=%d"),
		Build.PlacedCount,
		RequestedRoomCount,
		Build.Corridors.Num(),
		Build.StatSpine,
		Build.StatLoops,
		Params.LoopCount,
		Build.StatDeadEnds,
		Build.StatLinks,
		Params.CorridorLinkCount,
		Build.StatJunctions,
		Params.JunctionCount,
		Build.StartRoomIdx,
		ChainLeaf.RoomIndex,
		Build.StatRetries,
		Build.StatBacktracks,
		Build.StatReroutes,
		Build.StatMandatoryDrops);

	// Single explicit shortfall line (easy to grep in the e2e log) when ANY requested constraint was not fully
	// realized. Rooms shortfall is a placement issue (logged in PlaceRooms); here we surface the topology gaps.
	if (Build.PlacedCount < RequestedRoomCount || Build.StatLoops < Params.LoopCount || Build.StatLinks < Params.CorridorLinkCount
		|| Build.StatJunctions < Params.JunctionCount)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] Constraint shortfall (best-effort, generation NOT failed): rooms %d/%d loops %d/%d links %d/%d junctions %d/%d"),
			Build.PlacedCount,
			RequestedRoomCount,
			Build.StatLoops,
			Params.LoopCount,
			Build.StatLinks,
			Params.CorridorLinkCount,
			Build.StatJunctions,
			Params.JunctionCount);
	}

	FOrganicLayout OutLayout;
	OutLayout.Rooms = MoveTemp(Build.Rooms);
	OutLayout.Corridors = MoveTemp(Build.Corridors);
	OutLayout.Junctions = MoveTemp(Build.Junctions);
	OutLayout.StartRoomIdx = Build.StartRoomIdx;
	OutLayout.EndRoomIdx = ChainLeaf.RoomIndex; // far endpoint; GenerateInternal uses this for stitching + the segment exit
	OutLayout.RequestedRoomCount = RequestedRoomCount;
	OutLayout.RequestedLoopCount = Params.LoopCount;
	OutLayout.RequestedLinkCount = Params.CorridorLinkCount;
	OutLayout.RequestedDeadEndCount = Params.DeadEndCount;
	OutLayout.RequestedJunctionCount = Params.JunctionCount;
	OutLayout.PlacedCount = Build.PlacedCount;
	return OutLayout;
}

#pragma warning(pop)

FOrganicDungeonGridData UOrganicDungeonGenerator2D::RasterizeLayout(const FOrganicLayout& Layout)
{
	const double StartTime = FPlatformTime::Seconds();
	const float	 CellSizeVal = static_cast<float>(GridSize);

	const TArray<FOrganicRoom>&		Rooms = Layout.Rooms;
	const TArray<FOrganicCorridor>& Corridors = Layout.Corridors;
	const TArray<FOrganicJunction>& Junctions = Layout.Junctions;
	const int32						StartRoomIdx = Layout.StartRoomIdx;
	const int32						PlacedCount = Layout.PlacedCount;
	const int32						RequestedRoomCount = Layout.RequestedRoomCount;
	const int32						RequestedLoopCount = Layout.RequestedLoopCount;
	const int32						RequestedLinkCount = Layout.RequestedLinkCount;
	const int32						RequestedDeadEndCount = Layout.RequestedDeadEndCount;
	const int32						RequestedJunctionCount = Layout.RequestedJunctionCount;

	auto MakeEmpty = [&]() -> FOrganicDungeonGridData {
		FOrganicDungeonGridData R;
		R.CellSize = CellSizeVal;
		R.RequestedRoomCount = RequestedRoomCount;
		R.RequestedLoopCount = RequestedLoopCount;
		R.RequestedLinkCount = RequestedLinkCount;
		R.RequestedDeadEndCount = RequestedDeadEndCount;
		R.RequestedJunctionCount = RequestedJunctionCount;
		return R;
	};

	if (Rooms.Num() == 0)
	{
		return MakeEmpty();
	}

	// --- Layer 3: rasterize onto a fine grid ---
	FVector2D WorldMin(FLT_MAX, FLT_MAX);
	FVector2D WorldMax(-FLT_MAX, -FLT_MAX);
	auto	  Expand = [&](const FVector2D& P) {
		 WorldMin.X = FMath::Min(WorldMin.X, P.X);
		 WorldMin.Y = FMath::Min(WorldMin.Y, P.Y);
		 WorldMax.X = FMath::Max(WorldMax.X, P.X);
		 WorldMax.Y = FMath::Max(WorldMax.Y, P.Y);
	};
	for (const FOrganicRoom& Room : Rooms)
	{
		const FVector2D AxisX = RotateDeg(FVector2D(1, 0), Room.RotationDeg);
		const FVector2D AxisY = RotateDeg(FVector2D(0, 1), Room.RotationDeg);
		for (int32 sx = -1; sx <= 1; sx += 2)
		{
			for (int32 sy = -1; sy <= 1; sy += 2)
			{
				Expand(Room.Center + AxisX * (Room.HalfExtent.X * sx) + AxisY * (Room.HalfExtent.Y * sy));
			}
		}
	}
	// Densely sample a corridor's few-point centerline + interpolate its few stored radii along the curve.
	// The stored Centerline/Radii stay few-point; this transient dense array drives bounds + disc stamping
	// so the carved grid follows the smooth curve. Geometry-only — deterministic, no RandomStream draws.
	auto SampleCorridor = [&](const FOrganicCorridor& Cor, TArray<FVector2D>& OutPts, TArray<float>& OutRadii) {
		SampleCorridorCurve(Cor.Centerline, Cor.AnchorA.Normal, Cor.AnchorB.Normal, CellSizeVal * 0.5f, OutPts);
		OutRadii.Reset();
		OutRadii.Reserve(OutPts.Num());
		const int32 NumCP = Cor.Radii.Num();
		if (NumCP == 0 || OutPts.Num() == 0)
		{
			OutRadii.Init(CellSizeVal * 0.5f, OutPts.Num());
			return;
		}
		// Map each dense sample onto [0, NumCP-1] in control-point parameter space and lerp the few radii.
		for (int32 s = 0; s < OutPts.Num(); ++s)
		{
			const float U = (OutPts.Num() > 1) ? static_cast<float>(s) / static_cast<float>(OutPts.Num() - 1) : 0.0f;
			const float CParam = U * static_cast<float>(NumCP - 1);
			const int32 I0 = FMath::Clamp(FMath::FloorToInt(CParam), 0, NumCP - 1);
			const int32 I1 = FMath::Min(I0 + 1, NumCP - 1);
			const float Frac = CParam - static_cast<float>(I0);
			OutRadii.Add(FMath::Lerp(Cor.Radii[I0], Cor.Radii[I1], Frac));
		}
	};

	for (const FOrganicCorridor& Cor : Corridors)
	{
		TArray<FVector2D> DensePts;
		TArray<float>	  DenseRadii;
		SampleCorridor(Cor, DensePts, DenseRadii);
		for (int32 i = 0; i < DensePts.Num(); ++i)
		{
			const float R = DenseRadii[i];
			Expand(DensePts[i] + FVector2D(R, R));
			Expand(DensePts[i] - FVector2D(R, R));
		}
	}
	// Junction discs participate in bounds so a junction near the layout edge is never clipped by the pad guard.
	for (const FOrganicJunction& J : Junctions)
	{
		Expand(J.Center + FVector2D(J.Radius, J.Radius));
		Expand(J.Center - FVector2D(J.Radius, J.Radius));
	}

	const int32		Pad = FMath::Max(1, Params.WallThickness) + 1;
	const FVector2D GridOrigin = WorldMin - FVector2D(Pad * CellSizeVal, Pad * CellSizeVal);
	const int32		GWidth = FMath::CeilToInt((WorldMax.X - WorldMin.X) / CellSizeVal) + 2 * Pad;
	const int32		GHeight = FMath::CeilToInt((WorldMax.Y - WorldMin.Y) / CellSizeVal) + 2 * Pad;

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[ORG] Grid dimensions: %dx%d (%d total cells)"), GWidth, GHeight, GWidth * GHeight);

	if (GWidth <= 0 || GHeight <= 0 || (int64)GWidth * GHeight > 4'194'304)
	{
		UE_LOG(LogRoguelikeGeometry, Error, TEXT("[ORG] OOM guard triggered: %dx%d exceeds limit"), GWidth, GHeight);
		return MakeEmpty();
	}

	const int32	 TotalCells = GWidth * GHeight;
	TArray<bool> Grid;
	Grid.Init(false, TotalCells);
	TArray<uint8> CellType;
	CellType.Init(EOrganicCellType::Empty, TotalCells);

	auto CellCenterWorld = [&](int32 X, int32 Y) { return GridOrigin + FVector2D((X + 0.5f) * CellSizeVal, (Y + 0.5f) * CellSizeVal); };
	auto WorldToCellX = [&](float Wx) { return FMath::FloorToInt((Wx - GridOrigin.X) / CellSizeVal); };
	auto WorldToCellY = [&](float Wy) { return FMath::FloorToInt((Wy - GridOrigin.Y) / CellSizeVal); };

	// Rasterize rooms.
	TArray<TArray<FIntPoint>> RoomFootprintCells;
	RoomFootprintCells.SetNum(Rooms.Num());
	for (int32 r = 0; r < Rooms.Num(); ++r)
	{
		const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, 0.0f);
		const float	 Reach = Rooms[r].HalfExtent.Size();
		const int32	 MinX = FMath::Max(0, WorldToCellX(Rooms[r].Center.X - Reach));
		const int32	 MaxX = FMath::Min(GWidth - 1, WorldToCellX(Rooms[r].Center.X + Reach));
		const int32	 MinY = FMath::Max(0, WorldToCellY(Rooms[r].Center.Y - Reach));
		const int32	 MaxY = FMath::Min(GHeight - 1, WorldToCellY(Rooms[r].Center.Y + Reach));
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				if (PointInOBB(CellCenterWorld(X, Y), OBB))
				{
					const int32 Idx = Y * GWidth + X;
					Grid[Idx] = true;
					CellType[Idx] = EOrganicCellType::Room;
					RoomFootprintCells[r].Add(FIntPoint(X, Y));
				}
			}
		}
	}

	// Rasterize corridors (disc stamping); never overwrite room cells.
	auto StampDisc = [&](const FVector2D& P, float Radius) {
		const int32 MinX = FMath::Max(0, WorldToCellX(P.X - Radius));
		const int32 MaxX = FMath::Min(GWidth - 1, WorldToCellX(P.X + Radius));
		const int32 MinY = FMath::Max(0, WorldToCellY(P.Y - Radius));
		const int32 MaxY = FMath::Min(GHeight - 1, WorldToCellY(P.Y + Radius));
		const float R2 = Radius * Radius;
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				if (FVector2D::DistSquared(CellCenterWorld(X, Y), P) <= R2)
				{
					const int32 Idx = Y * GWidth + X;
					if (CellType[Idx] != EOrganicCellType::Room)
					{
						Grid[Idx] = true;
						CellType[Idx] = EOrganicCellType::Corridor;
					}
				}
			}
		}
	};
	for (const FOrganicCorridor& Cor : Corridors)
	{
		TArray<FVector2D> DensePts;
		TArray<float>	  DenseRadii;
		SampleCorridor(Cor, DensePts, DenseRadii);
		for (int32 i = 0; i < DensePts.Num(); ++i)
		{
			StampDisc(DensePts[i], FMath::Max(CellSizeVal * 0.5f, DenseRadii[i]));
		}
	}

	// Carve junction discs as walkable floor. StampDisc already guards Room cells, so a junction never
	// overwrites a room footprint; it shares the Corridor cell type (junctions are part of the floor network).
	for (const FOrganicJunction& J : Junctions)
	{
		StampDisc(J.Center, FMath::Max(CellSizeVal * 0.5f, J.Radius));
	}

	// Optional cave smoothing on corridor cells.
	if (Params.bSmoothCorridors)
	{
		const int32	  DXf[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
		const int32	  DYf[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
		TArray<uint8> Next; // hoisted: copy-assign below reuses the allocation instead of allocating per pass
		for (int32 Pass = 0; Pass < Params.SmoothIterations; ++Pass)
		{
			Next = CellType;
			for (int32 Y = 1; Y < GHeight - 1; ++Y)
			{
				for (int32 X = 1; X < GWidth - 1; ++X)
				{
					const int32 Idx = Y * GWidth + X;
					if (CellType[Idx] == EOrganicCellType::Room)
					{
						continue; // rooms are authoritative
					}
					int32 FloorN = 0;
					for (int32 d = 0; d < 8; ++d)
					{
						if (Grid[(Y + DYf[d]) * GWidth + (X + DXf[d])])
						{
							++FloorN;
						}
					}
					const bool bFloor = Grid[Idx];
					if (bFloor && FloorN < 3)
					{
						Next[Idx] = EOrganicCellType::Empty; // erode lone corridor cell
					}
					else if (!bFloor && FloorN >= 5)
					{
						Next[Idx] = EOrganicCellType::Corridor; // fill pockets
					}
				}
			}
			for (int32 Idx = 0; Idx < TotalCells; ++Idx)
			{
				if (CellType[Idx] == EOrganicCellType::Room)
				{
					continue;
				}
				CellType[Idx] = Next[Idx];
				Grid[Idx] = (Next[Idx] == EOrganicCellType::Corridor);
			}
		}
	}

	// Wall classification: non-floor within WallThickness of floor -> Wall, else Empty.
	// Clamp the band width: the inner loop scans a (2*WT+1)^2 neighbourhood per cell, so an unbounded
	// WallThickness would make this quadratic in WT on top of the full-grid scan.
	const int32 WT = FMath::Clamp(Params.WallThickness, 1, 16);
	for (int32 Y = 0; Y < GHeight; ++Y)
	{
		for (int32 X = 0; X < GWidth; ++X)
		{
			if (!Grid[Y * GWidth + X])
			{
				continue;
			}
			for (int32 dy = -WT; dy <= WT; ++dy)
			{
				const int32 NY = Y + dy;
				if (NY < 0 || NY >= GHeight)
				{
					continue;
				}
				for (int32 dx = -WT; dx <= WT; ++dx)
				{
					const int32 NX = X + dx;
					if (NX < 0 || NX >= GWidth)
					{
						continue;
					}
					const int32 NIdx = NY * GWidth + NX;
					if (!Grid[NIdx] && CellType[NIdx] == EOrganicCellType::Empty)
					{
						CellType[NIdx] = EOrganicCellType::Wall;
					}
				}
			}
		}
	}

	// Flood-fill connected floor regions.
	TArray<int32>			  RegionIds;
	TArray<TArray<FIntPoint>> Regions;
	int32					  CenterRegionId = -1;

	const int32 CenterX = FMath::Clamp(WorldToCellX(CenterPoint.X), 0, GWidth - 1);
	const int32 CenterY = FMath::Clamp(WorldToCellY(CenterPoint.Y), 0, GHeight - 1);

	FloodFillRegions(Grid, GWidth, GHeight, CenterX, CenterY, RegionIds, Regions, CenterRegionId);

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG] Rasterize: rooms=%d/%d corridors=%d junctions=%d | entrance=%d exit=%d | regions=%d"),
		PlacedCount,
		RequestedRoomCount,
		Corridors.Num(),
		Junctions.Num(),
		StartRoomIdx,
		Layout.EndRoomIdx,
		Regions.Num());

	// Position the diagram in world: cell (0,0) maps to GridOrigin, CellSize = GridSize.
	Bounds = FBox2D(GridOrigin, GridOrigin + FVector2D(GWidth * CellSizeVal, GHeight * CellSizeVal));
	FLayoutDiagram2D Diagram = ConvertGridToDiagram(Grid, GWidth, GHeight);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[ORG] Generate() complete: %d cells in %.2fms"), Diagram.Cells.Num(), ElapsedMs);

	FOrganicDungeonGridData Result;
	Result.Grid = MoveTemp(Grid);
	Result.CellType = MoveTemp(CellType);
	Result.RegionIds = MoveTemp(RegionIds);
	Result.Regions = MoveTemp(Regions);
	Result.CenterRegionId = CenterRegionId;
	Result.GridWidth = GWidth;
	Result.GridHeight = GHeight;
	Result.CellSize = CellSizeVal;
	Result.GridOriginWorld = GridOrigin;
	Result.Rooms = Rooms;
	Result.Corridors = Corridors;
	Result.Junctions = Junctions;
	Result.RoomFootprintCells = MoveTemp(RoomFootprintCells);
	Result.RequestedRoomCount = RequestedRoomCount;
	Result.RequestedLoopCount = RequestedLoopCount;
	Result.RequestedLinkCount = RequestedLinkCount;
	Result.RequestedDeadEndCount = RequestedDeadEndCount;
	Result.RequestedJunctionCount = RequestedJunctionCount;
	Result.StartRoomIndex = StartRoomIdx;
	Result.EndRoomIndex = Layout.EndRoomIdx;
	Result.LocationStartRoomIndex = Layout.LocationStartRoomIndex;
	Result.Diagram = MoveTemp(Diagram);

	// Floor/wall GEOMETRY is no longer built in C++: the runtime emits corridor-centerline and
	// room-boundary splines from this grid data (Corridors/Rooms) and PCG produces the floor + walls.

	return Result;
}

// ---------------------------------------------------------------------------
// Isolated algorithmic stages — stateless helpers with clean, testable interfaces.
// ---------------------------------------------------------------------------

int32 UOrganicDungeonGenerator2D::BuildRoomQueue(const FOrganicDungeonResolvedParams& Params, FRandomStream& RandomStream, TArray<int32>& OutQueue)
{
	// Accumulate per-type counts and sum for the total requested.
	const int32	  NumTypes = Params.RoomTypes.Num();
	TArray<int32> Remaining;
	Remaining.Reserve(NumTypes);
	int32 RequestedRoomCount = 0;
	for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
	{
		const int32 Count = FMath::Max(0, RT.Count);
		Remaining.Add(Count);
		RequestedRoomCount += Count;
	}

	OutQueue.Reset();
	OutQueue.Reserve(RequestedRoomCount);
	int32 PrevType = INDEX_NONE;

	for (int32 Placed = 0; Placed < RequestedRoomCount; ++Placed)
	{
		// Eligible = types with rooms left, excluding the previous type whenever any OTHER type still has
		// rooms (so two of the same type never sit adjacent until that type is genuinely the only one left).
		bool bHasNonPrev = false;
		for (int32 t = 0; t < NumTypes; ++t)
		{
			if (Remaining[t] > 0 && t != PrevType)
			{
				bHasNonPrev = true;
				break;
			}
		}

		int32 Chosen = INDEX_NONE;
		if (!bHasNonPrev)
		{
			Chosen = PrevType; // only the previous type remains — it has to repeat
		}
		else if (Params.bShuffleRoomOrder)
		{
			// Weighted-random by remaining count: a high-count type is picked more often but the
			// exact sequence is random, so there is no fixed repeating pattern.
			Chosen =
				ProceduralGeometry_PickWeightedType(Remaining, PrevType, [&](int32 MaxInclusive) { return RandomStream.RandRange(0, MaxInclusive); });
			if (Chosen == INDEX_NONE)
			{
				Chosen = PrevType; // fallback: only prev type has rooms
			}
		}
		else
		{
			// Deterministic spread: most-remaining non-prev type (ties go to lowest declared index).
			for (int32 t = 0; t < NumTypes; ++t)
			{
				if (Remaining[t] <= 0 || t == PrevType)
				{
					continue;
				}
				if (Chosen == INDEX_NONE || Remaining[t] > Remaining[Chosen])
				{
					Chosen = t;
				}
			}
		}

		OutQueue.Add(Chosen);
		--Remaining[Chosen];
		PrevType = Chosen;
	}
	return RequestedRoomCount;
}

void UOrganicDungeonGenerator2D::FindSpine(const TArray<FOrganicRoom>& Rooms,
	const TArray<TArray<int32>>&									   Adj,
	const FVector2D&												   CenterPoint,
	int32&															   OutStartRoomIdx,
	int32&															   OutEndRoomIdx,
	TSet<uint64>&													   OutSpineEdges)
{
	const int32 NumRooms = Rooms.Num();
	OutSpineEdges.Reset();

	// BFS from Src; returns the farthest reachable node and populates OutParent.
	auto BfsFarthest = [&](int32 Src, TArray<int32>& OutParent) -> int32 {
		TArray<int32> Dist;
		Dist.Init(-1, NumRooms);
		OutParent.Init(INDEX_NONE, NumRooms);
		TArray<int32> Q;
		Q.Add(Src);
		Dist[Src] = 0;
		int32 Head = 0;
		int32 Far = Src;
		while (Head < Q.Num())
		{
			const int32 U = Q[Head++];
			if (Dist[U] > Dist[Far])
			{
				Far = U;
			}
			for (int32 V : Adj[U])
			{
				if (Dist[V] < 0)
				{
					Dist[V] = Dist[U] + 1;
					OutParent[V] = U;
					Q.Add(V);
				}
			}
		}
		return Far;
	};

	// Two BFS passes give the graph diameter endpoints A and B.
	TArray<int32> P1;
	const int32	  A = BfsFarthest(0, P1);
	TArray<int32> P2;
	const int32	  B = BfsFarthest(A, P2); // P2 = parents rooted at A; B = far end

	// Orient: Start = diameter endpoint closer to CenterPoint (level entrance), End = farther.
	if (FVector2D::Distance(Rooms[B].Center, CenterPoint) < FVector2D::Distance(Rooms[A].Center, CenterPoint))
	{
		OutStartRoomIdx = B;
		OutEndRoomIdx = A;
	}
	else
	{
		OutStartRoomIdx = A;
		OutEndRoomIdx = B;
	}

	// Walk the A→B path using P2 (parents rooted at A); each step is a spine edge.
	for (int32 N = B; P2[N] != INDEX_NONE; N = P2[N])
	{
		OutSpineEdges.Add(EdgeKey(N, P2[N]));
	}
}

// Pipeline-unused (see header): retained for direct unit-test coverage of the orientation-assignment math.
// The connectivity pipeline now carves the backbone along the placement-spawn tree instead of solving
// orientation, so nothing in the generator calls this anymore.
bool UOrganicDungeonGenerator2D::SolveRoomOrientation(const TArray<FOrganicBakedDoorway>& Declared,
	const TArray<FVector2D>&															  MandatoryDirs,
	float																				  AngularToleranceDeg,
	TArrayView<const float>																  CandidateRotations,
	float&																				  OutRotationDeg,
	TArray<int32>&																		  OutDoorwayForConnection)
{
	// Trivially infeasible: more connections than declared doorways.
	if (MandatoryDirs.Num() > Declared.Num())
	{
		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[SolveRoomOrientation] infeasible: %d connections > %d doorways"),
			MandatoryDirs.Num(),
			Declared.Num());
		return false;
	}

	if (MandatoryDirs.Num() == 0)
	{
		// No connections to satisfy — any rotation is fine; use the first candidate or 0.
		OutRotationDeg = CandidateRotations.Num() > 0 ? CandidateRotations[0] : 0.0f;
		OutDoorwayForConnection.Reset();
		return true;
	}

	const float CosTol = FMath::Cos(FMath::DegreesToRadians(AngularToleranceDeg));

	// For each candidate rotation, check whether every mandatory direction can be matched to a
	// distinct declared doorway whose rotated outward dir is within tolerance.
	// Uses a greedy bipartite matching (sufficient for small counts typical in OD rooms).
	for (const float Theta : CandidateRotations)
	{
		// Pre-rotate all declared doorway directions by Theta.
		const float ThetaRad = FMath::DegreesToRadians(Theta);
		const float CosT = FMath::Cos(ThetaRad);
		const float SinT = FMath::Sin(ThetaRad);

		TArray<FVector2D> RotatedDirs;
		RotatedDirs.Reserve(Declared.Num());
		for (const FOrganicBakedDoorway& D : Declared)
		{
			RotatedDirs.Add(
				FVector2D(D.LocalOutwardDir.X * CosT - D.LocalOutwardDir.Y * SinT, D.LocalOutwardDir.X * SinT + D.LocalOutwardDir.Y * CosT)
					.GetSafeNormal());
		}

		// Greedy assignment: for each mandatory direction, pick the best-matching unassigned doorway.
		TArray<bool> Used;
		Used.Init(false, Declared.Num());
		TArray<int32> Assignment;
		Assignment.Reserve(MandatoryDirs.Num());
		bool bFeasible = true;

		for (const FVector2D& Dir : MandatoryDirs)
		{
			float BestDot = CosTol - KINDA_SMALL_NUMBER; // must exceed threshold
			int32 BestIdx = INDEX_NONE;

			for (int32 d = 0; d < Declared.Num(); ++d)
			{
				if (Used[d])
				{
					continue;
				}
				const float Dot = FVector2D::DotProduct(RotatedDirs[d], Dir);
				if (Dot > BestDot)
				{
					BestDot = Dot;
					BestIdx = d;
				}
			}

			if (BestIdx == INDEX_NONE)
			{
				bFeasible = false;
				break;
			}
			Used[BestIdx] = true;
			Assignment.Add(BestIdx);
		}

		if (bFeasible)
		{
			OutRotationDeg = Theta;
			OutDoorwayForConnection = MoveTemp(Assignment);
			UE_LOG(
				LogRoguelikeGeometry, Verbose, TEXT("[SolveRoomOrientation] feasible: rot=%.1f° for %d connection(s)"), Theta, MandatoryDirs.Num());
			return true;
		}
	}

	UE_LOG(LogRoguelikeGeometry,
		Verbose,
		TEXT("[SolveRoomOrientation] no candidate rotation satisfies %d connection(s) within %.0f°"),
		MandatoryDirs.Num(),
		AngularToleranceDeg);
	return false;
}
