#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Generators/OrganicDungeon2D/OrganicFloorBuilder.h"

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

	/** Packs two room indices into a uint64 edge key (order-independent). Used by BuildMST and FindSpine. */
	uint64 EdgeKey(int32 A, int32 B)
	{
		const int32 Lo = FMath::Min(A, B);
		const int32 Hi = FMath::Max(A, B);
		return (static_cast<uint64>(Lo) << 32) | static_cast<uint32>(Hi);
	}

	// --- Pure read-only helpers shared across GenerateLocationSubgraph phases ---
	// Extracted verbatim from the former in-function lambdas; they only read the room array (and a couple
	// of scalars), so they are free functions taking explicit parameters rather than member methods.

	// Does the straight segment AP->BP pass through any room other than the two endpoints' rooms?
	// Exact segment-vs-OBB: catches corner grazes and thin rooms that between-sample point tests skip over.
	bool SegmentHitsRoom(const TArray<FOrganicRoom>& Rooms, const FVector2D& AP, const FVector2D& BP, int32 RoomExclA, int32 RoomExclB)
	{
		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			if (r == RoomExclA || r == RoomExclB)
			{
				continue;
			}
			const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, 0.0f);
			if (SegmentHitsOBB(AP, BP, OBB))
			{
				return true;
			}
		}
		return false;
	}

	// True if a corridor's actual (curved) centerline enters any room not in AllowedRooms.
	// Walks each consecutive centerline segment with the exact segment-vs-OBB test so a room can never
	// be skipped over between samples.
	bool CorridorHitsRoom(const TArray<FOrganicRoom>& Rooms, const TArray<FVector2D>& Centerline, int32 AllowedA, int32 AllowedB, int32 AllowedC)
	{
		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			if (r == AllowedA || r == AllowedB || r == AllowedC)
			{
				continue;
			}
			const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, 0.0f);
			for (int32 i = 0; i + 1 < Centerline.Num(); ++i)
			{
				if (SegmentHitsOBB(Centerline[i], Centerline[i + 1], OBB))
				{
					return true;
				}
			}
		}
		return false;
	}

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

// Mutable working state for one GenerateLocationSubgraph build (see the forward declaration / doc comment
// in the header). Default member initializers reproduce the original stack-local initial values. The
// subgraph phase helpers move named stack locals into these identically-named fields, so statement and
// FRandomStream draw order are preserved verbatim across the extraction.
struct FOrgSubgraphBuild
{
	TArray<FOrganicRoom>		Rooms;
	TArray<FOrganicCorridor>	Corridors;
	TArray<int32>				OpenList;	  // room indices still eligible to grow from
	int32						QueueIdx = 1; // placement cursor (room 0 is seeded before placement)
	int32						StartRoomIdx = -1;
	int32						EndRoomIdx = -1;
	TArray<TArray<int32>>		MstAdj;
	TArray<TPair<int32, int32>> MstEdges;
	TSet<uint64>				SpineEdges;

	// Per-build stat counters (logged in the final layout summary).
	int32 StatRetries = 0;
	int32 StatBacktracks = 0;
	int32 StatLoops = 0;
	int32 StatSpine = 0;
	int32 StatDeadEnds = 0;
	int32 StatLinks = 0;
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

	// Offset a corridor anchor's room/corridor index into the merged arrays.
	auto OffsetAnchor = [](FOrganicAnchor& A, int32 RoomBase, int32 CorrBase) {
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
		if (Sub.Rooms.Num() == 0)
		{
			UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[ORG] Location %d produced no rooms — skipped."), s);
			continue;
		}

		const int32 RoomBase = Merged.Rooms.Num();
		const int32 CorrBase = Merged.Corridors.Num();

		for (FOrganicRoom& R : Sub.Rooms)
		{
			R.LocationIndex = s;
		}
		Merged.Rooms.Append(Sub.Rooms);
		for (FOrganicCorridor& C : Sub.Corridors)
		{
			OffsetAnchor(C.AnchorA, RoomBase, CorrBase);
			OffsetAnchor(C.AnchorB, RoomBase, CorrBase);
			Merged.Corridors.Add(MoveTemp(C));
		}

		const int32 ThisStartGlobal = (Sub.StartRoomIdx >= 0) ? RoomBase + Sub.StartRoomIdx : RoomBase;
		const int32 ThisEndGlobal = (Sub.EndRoomIdx >= 0) ? RoomBase + Sub.EndRoomIdx : RoomBase + Sub.Rooms.Num() - 1;

		Merged.LocationStartRoomIndex[s] = ThisStartGlobal;

		if (OverallStartIdx == INDEX_NONE)
		{
			OverallStartIdx = ThisStartGlobal;
		}

		// Stitch subgraphs into one graph: previous location's end room -> this location's start room.
		if (bHavePrev && PrevEndGlobalIdx >= 0)
		{
			FOrganicCorridor Link = BuildInterLocationCorridor(
				Merged.Rooms[PrevEndGlobalIdx], PrevEndGlobalIdx, Merged.Rooms[ThisStartGlobal], ThisStartGlobal, SegParams);
			Merged.Corridors.Add(MoveTemp(Link));
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

FOrganicCorridor UOrganicDungeonGenerator2D::BuildBezierCorridor(const FVector2D& AP,
	const FVector2D&															  AN,
	const FVector2D&															  BP,
	const FVector2D&															  BN,
	float																		  Waviness,
	float																		  MinRadius,
	float																		  MaxRadius,
	EOrganicCorridorStyle														  Style,
	float																		  RadiusScale)
{
	const float CellSizeVal = static_cast<float>(GridSize);

	FOrganicCorridor Cor;

	// Simple straight corridor: a linear centerline between the two doorway points at constant width.
	// Organic waviness and the visual floor/wall geometry are produced downstream by the PCG brush
	// (the config Waviness/width travel as spline metadata); the C++ centerline stays minimal.
	// AN/BN/Waviness/MaxRadius/Style are intentionally unused now (kept for signature compatibility).
	const FVector2D Chord = BP - AP;
	const float		Dist = FMath::Max(1.0f, Chord.Size());
	const int32		N = FMath::Clamp(FMath::CeilToInt(Dist / (CellSizeVal * 0.5f)), 2, 256);
	const float		HalfWidth = FMath::Max(1.0f, MinRadius * RadiusScale);

	Cor.Centerline.Reserve(N + 1);
	Cor.Radii.Reserve(N + 1);
	for (int32 i = 0; i <= N; ++i)
	{
		const float T = static_cast<float>(i) / N;
		Cor.Centerline.Add(FMath::Lerp(AP, BP, T));
		Cor.Radii.Add(HalfWidth);
	}
	return Cor;
}

FOrganicCorridor UOrganicDungeonGenerator2D::BuildInterLocationCorridor(
	FOrganicRoom& FromRoom, int32 FromIdx, FOrganicRoom& ToRoom, int32 ToIdx, const FOrganicDungeonResolvedParams& LinkParams)
{
	const float CellSizeVal = static_cast<float>(GridSize);

	// Pick the doorway on a room best facing Toward (prefer an unused one, else the best of any).
	auto PickDoor = [&](FOrganicRoom& Room, const FVector2D& Toward) -> int32 {
		int32 BestFree = INDEX_NONE;
		float BestFreeDot = -FLT_MAX;
		int32 BestAny = INDEX_NONE;
		float BestAnyDot = -FLT_MAX;
		for (int32 d = 0; d < Room.Doorways.Num(); ++d)
		{
			const float Dot = FVector2D::DotProduct(Room.Doorways[d].OutwardNormal, Toward);
			if (Dot > BestAnyDot)
			{
				BestAnyDot = Dot;
				BestAny = d;
			}
			if (!Room.Doorways[d].bUsed && Dot > BestFreeDot)
			{
				BestFreeDot = Dot;
				BestFree = d;
			}
		}
		return (BestFree != INDEX_NONE) ? BestFree : BestAny;
	};

	const FVector2D ToB = (ToRoom.Center - FromRoom.Center).GetSafeNormal();

	FVector2D	AP = FromRoom.Center;
	FVector2D	AN = ToB;
	const int32 FromDoor = PickDoor(FromRoom, ToB);
	if (FromRoom.Doorways.IsValidIndex(FromDoor))
	{
		AP = FromRoom.Doorways[FromDoor].Pos;
		AN = FromRoom.Doorways[FromDoor].OutwardNormal;
		FromRoom.Doorways[FromDoor].bUsed = true;
	}

	FVector2D	BP = ToRoom.Center;
	FVector2D	BN = -ToB;
	const int32 ToDoor = PickDoor(ToRoom, -ToB);
	if (ToRoom.Doorways.IsValidIndex(ToDoor))
	{
		BP = ToRoom.Doorways[ToDoor].Pos;
		BN = ToRoom.Doorways[ToDoor].OutwardNormal;
		ToRoom.Doorways[ToDoor].bUsed = true;
	}

	// Inter-location links clamp MinR to CellSize so the passage is never narrower than one grid cell.
	const float		 MinR = FMath::Max(CellSizeVal, LinkParams.MinThickness * 0.5f);
	const float		 MaxR = FMath::Max(MinR, LinkParams.MaxWidth * 0.5f);
	FOrganicCorridor Cor = BuildBezierCorridor(AP, AN, BP, BN, LinkParams.Waviness, MinR, MaxR, LinkParams.CorridorStyle);

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

			// Commit: placement only scatters the room. Connectivity (which corridors exist) is decided
			// afterwards by the proximity graph, so no corridor/doorway is consumed here.
			const int32 NewIdx = Ctx.Rooms.Add(MoveTemp(NewRoom));
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
	bool															 bRejectIfClipsRoom)
{
	// Thin wrapper: builds a corridor using the current subgraph's params (Waviness, thickness, style).
	const auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float InRadiusScale) -> FOrganicCorridor {
		const float MinR = Params.MinThickness * 0.5f;
		const float MaxR = Params.MaxWidth * 0.5f;
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, InRadiusScale);
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
	if (bRejectIfClipsRoom)
	{
		// Reject if the curve clips any THIRD room (exact swept test, no endpoint allowance).
		if (CorridorHitsRoom(Ctx.Rooms, Cor.Centerline, A, B, INDEX_NONE))
		{
			return false;
		}
		// Endpoint rooms A and B may only be entered along their doorway approach span — never crossed
		// through the body (e.g. a far-side doorway whose corridor traverses the room interior). Tolerate
		// penetration to ~one corridor radius past the doorway line so the legitimate doorway entry passes.
		const float ApproachDepth = FMath::Max(Params.MinThickness * 0.5f, static_cast<float>(GridSize));
		if (EndpointBodyCrossed(Ctx.Rooms[A], Cor.Centerline, APos, ANrm, ApproachDepth)
			|| EndpointBodyCrossed(Ctx.Rooms[B], Cor.Centerline, BPos, BNrm, ApproachDepth))
		{
			return false;
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
	// --- Layer 1: connectivity — proximity MST backbone + budgeted loops, with a widened spine ---
	const int32 NumRooms = Ctx.Rooms.Num();
	Ctx.StartRoomIdx = (NumRooms > 0) ? 0 : -1; // graph-diameter endpoints (set during connectivity)
	Ctx.EndRoomIdx = -1;

	if (NumRooms >= 2)
	{
		// Build MST (Prim's, O(N^2)) → backbone connectivity; find graph diameter → spine endpoints.
		BuildMST(Ctx.Rooms, Ctx.MstEdges, Ctx.MstAdj);
		FindSpine(Ctx.Rooms, Ctx.MstAdj, CenterPoint, Ctx.StartRoomIdx, Ctx.EndRoomIdx, Ctx.SpineEdges);

		// --- Orientation solve stage ---
		// For each declared-doorway room, choose a rotation that maps every mandatory MST connection
		// to a distinct declared doorway within angular tolerance.  Rooms that cannot be satisfied
		// are re-rolled to another pool entry or dropped.  Runs after MST/spine so mandatory
		// connections are known, before backbone carve so doorways are not yet consumed.

		// Angular tolerance for matching a rotated doorway direction to a mandatory connection.
		constexpr float SolveToleranceDeg = 45.0f;

		TArray<int32> DroppedRoomIndices; // collected in ascending order, removed in reverse

		for (int32 r = 0; r < Ctx.Rooms.Num(); ++r)
		{
			const FOrganicResolvedRoomType& RT = Params.RoomTypes[Ctx.Rooms[r].TypeIndex];
			if (RT.Doorways.Num() == 0)
			{
				continue; // legacy room — skip solve, synthetic doorways already generated
			}

			// Gather mandatory connection directions (toward each MST neighbour).
			TArray<FVector2D> MandatoryDirs;
			if (Ctx.MstAdj.IsValidIndex(r))
			{
				for (int32 Neighbor : Ctx.MstAdj[r])
				{
					MandatoryDirs.Add((Ctx.Rooms[Neighbor].Center - Ctx.Rooms[r].Center).GetSafeNormal());
				}
			}

			if (MandatoryDirs.Num() == 0)
			{
				// Isolated declared-doorway room — regenerate at current rotation, no solve needed.
				GenerateDoorways(Ctx.Rooms[r], Params);
				continue;
			}

			// Fast-reject: impossible if more connections than doorways.
			if (MandatoryDirs.Num() > RT.Doorways.Num())
			{
				UE_LOG(LogRoguelikeGeometry,
					Verbose,
					TEXT("[ORG] Orientation: room[%d] type '%s' has %d mandatory connections but only %d doorways — skip to re-roll"),
					r,
					*RT.DisplayName.ToString(),
					MandatoryDirs.Num(),
					RT.Doorways.Num());
				// Fall through to re-roll/drop logic below.
			}
			else
			{
				// Generate candidate rotations: align each declared doorway to the primary connection.
				const float	  PrimaryAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(MandatoryDirs[0].Y, MandatoryDirs[0].X));
				TArray<float> Candidates;
				Candidates.Reserve(RT.Doorways.Num());
				for (const FOrganicBakedDoorway& D : RT.Doorways)
				{
					const float DoorAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.LocalOutwardDir.Y, D.LocalOutwardDir.X));
					Candidates.Add(PrimaryAngleDeg - DoorAngleDeg);
				}

				float		  SolvedRot = 0.0f;
				TArray<int32> Assignment;
				const bool	  bSolved = SolveRoomOrientation(RT.Doorways,
					   MandatoryDirs,
					   SolveToleranceDeg,
					   TArrayView<const float>(Candidates.GetData(), Candidates.Num()),
					   SolvedRot,
					   Assignment);

				if (bSolved)
				{
					// Validate footprint overlap at the new rotation.
					const FOBB2D NewOBB = MakeOBB(Ctx.Rooms[r].Center, SolvedRot, Ctx.Rooms[r].HalfExtent, Params.MinRoomGap * 0.5f);
					bool		 bOverlap = false;
					for (int32 i = 0; i < Ctx.Rooms.Num() && !bOverlap; ++i)
					{
						if (i == r)
						{
							continue;
						}
						bOverlap = OBBOverlap(
							NewOBB, MakeOBB(Ctx.Rooms[i].Center, Ctx.Rooms[i].RotationDeg, Ctx.Rooms[i].HalfExtent, Params.MinRoomGap * 0.5f));
					}
					for (int32 i = 0; i < Obstacles.Num() && !bOverlap; ++i)
					{
						bOverlap = OBBOverlap(
							NewOBB, MakeOBB(Obstacles[i].Center, Obstacles[i].RotationDeg, Obstacles[i].HalfExtent, Params.MinRoomGap * 0.5f));
					}

					if (!bOverlap)
					{
						// Commit orientation: update rotation and regenerate doorways.
						Ctx.Rooms[r].RotationDeg = SolvedRot;
						GenerateDoorways(Ctx.Rooms[r], Params);
						UE_LOG(LogRoguelikeGeometry,
							Verbose,
							TEXT("[ORG] Orientation: room[%d] type '%s' solved rot=%.1f° (%d connections → %d doorways)"),
							r,
							*RT.DisplayName.ToString(),
							SolvedRot,
							MandatoryDirs.Num(),
							RT.Doorways.Num());
						continue; // success — move to next room
					}
					UE_LOG(LogRoguelikeGeometry,
						Verbose,
						TEXT("[ORG] Orientation: room[%d] solved rot=%.1f° overlaps another room — trying re-roll"),
						r,
						SolvedRot);
				}
				else
				{
					UE_LOG(LogRoguelikeGeometry,
						Verbose,
						TEXT("[ORG] Orientation: room[%d] type '%s' no rotation aligns %d mandatory connection(s) — trying re-roll"),
						r,
						*RT.DisplayName.ToString(),
						MandatoryDirs.Num());
				}
			}

			// Re-roll: try another room type from the pool with enough declared doorways.
			bool		bRerolled = false;
			const int32 OrigTypeIdx = Ctx.Rooms[r].TypeIndex;
			for (int32 t = 0; t < Params.RoomTypes.Num() && !bRerolled; ++t)
			{
				if (t == OrigTypeIdx)
				{
					continue;
				}
				const FOrganicResolvedRoomType& AltRT = Params.RoomTypes[t];
				if (AltRT.Doorways.Num() == 0 || AltRT.Doorways.Num() < MandatoryDirs.Num())
				{
					continue;
				}

				const float AltPrimaryAngleDeg =
					MandatoryDirs.Num() > 0 ? FMath::RadiansToDegrees(FMath::Atan2(MandatoryDirs[0].Y, MandatoryDirs[0].X)) : 0.0f;
				TArray<float> AltCandidates;
				AltCandidates.Reserve(AltRT.Doorways.Num());
				for (const FOrganicBakedDoorway& D : AltRT.Doorways)
				{
					const float DA = FMath::RadiansToDegrees(FMath::Atan2(D.LocalOutwardDir.Y, D.LocalOutwardDir.X));
					AltCandidates.Add(AltPrimaryAngleDeg - DA);
				}

				float		  AltRot = 0.0f;
				TArray<int32> AltAssignment;
				if (!SolveRoomOrientation(AltRT.Doorways,
						MandatoryDirs,
						SolveToleranceDeg,
						TArrayView<const float>(AltCandidates.GetData(), AltCandidates.Num()),
						AltRot,
						AltAssignment))
				{
					continue;
				}

				const FVector2D AltHalf(AltRT.FootprintWidth * 0.5f, AltRT.FootprintHeight * 0.5f);
				const FOBB2D	AltOBB = MakeOBB(Ctx.Rooms[r].Center, AltRot, AltHalf, Params.MinRoomGap * 0.5f);
				bool			bAltOverlap = false;
				for (int32 i = 0; i < Ctx.Rooms.Num() && !bAltOverlap; ++i)
				{
					if (i == r)
					{
						continue;
					}
					bAltOverlap =
						OBBOverlap(AltOBB, MakeOBB(Ctx.Rooms[i].Center, Ctx.Rooms[i].RotationDeg, Ctx.Rooms[i].HalfExtent, Params.MinRoomGap * 0.5f));
				}
				for (int32 i = 0; i < Obstacles.Num() && !bAltOverlap; ++i)
				{
					bAltOverlap =
						OBBOverlap(AltOBB, MakeOBB(Obstacles[i].Center, Obstacles[i].RotationDeg, Obstacles[i].HalfExtent, Params.MinRoomGap * 0.5f));
				}
				if (bAltOverlap)
				{
					continue;
				}

				// Commit re-roll.
				Ctx.Rooms[r].TypeIndex = t;
				Ctx.Rooms[r].RotationDeg = AltRot;
				Ctx.Rooms[r].HalfExtent = AltHalf;
				Ctx.Rooms[r].RoomLevel = AltRT.RoomLevel;
				Ctx.Rooms[r].FootprintCenterOffset = AltRT.FootprintCenter;
				GenerateDoorways(Ctx.Rooms[r], Params);
				bRerolled = true;

				UE_LOG(LogRoguelikeGeometry,
					Log,
					TEXT("[ORG] Orientation: room[%d] re-rolled from type '%s' to '%s' (rot=%.1f°)"),
					r,
					*Params.RoomTypes[OrigTypeIdx].DisplayName.ToString(),
					*AltRT.DisplayName.ToString(),
					AltRot);
			}

			if (!bRerolled)
			{
				// Cannot satisfy mandatory connections with any pool entry — schedule drop.
				DroppedRoomIndices.Add(r);
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT("[ORG] Orientation: room[%d] type '%s' dropped — no pool entry satisfies %d mandatory connection(s)"),
					r,
					*RT.DisplayName.ToString(),
					MandatoryDirs.Num());
			}
		}

		// Process drops: remove in reverse-index order (preserves remaining indices),
		// then rebuild MST + spine over the surviving rooms.
		if (DroppedRoomIndices.Num() > 0)
		{
			// Sort descending so RemoveAt doesn't invalidate earlier indices.
			DroppedRoomIndices.Sort([](int32 A, int32 B) { return A > B; });
			for (const int32 DropIdx : DroppedRoomIndices)
			{
				Ctx.Rooms.RemoveAt(DropIdx);
				// Keep start/end indices consistent.
				if (Ctx.StartRoomIdx == DropIdx)
				{
					Ctx.StartRoomIdx = Ctx.Rooms.Num() > 0 ? 0 : INDEX_NONE;
				}
				else if (Ctx.StartRoomIdx > DropIdx)
				{
					--Ctx.StartRoomIdx;
				}
				if (Ctx.EndRoomIdx == DropIdx)
				{
					Ctx.EndRoomIdx = INDEX_NONE;
				}
				else if (Ctx.EndRoomIdx > DropIdx)
				{
					--Ctx.EndRoomIdx;
				}
			}
			UE_LOG(LogRoguelikeGeometry,
				Warning,
				TEXT("[ORG] Orientation: dropped %d room(s); %d remain. Rebuilding MST."),
				DroppedRoomIndices.Num(),
				Ctx.Rooms.Num());

			// Rebuild MST + spine over survivors.
			Ctx.MstAdj.Empty();
			Ctx.MstEdges.Empty();
			Ctx.SpineEdges.Empty();
			if (Ctx.Rooms.Num() >= 2)
			{
				BuildMST(Ctx.Rooms, Ctx.MstEdges, Ctx.MstAdj);
				FindSpine(Ctx.Rooms, Ctx.MstAdj, CenterPoint, Ctx.StartRoomIdx, Ctx.EndRoomIdx, Ctx.SpineEdges);
			}
			else
			{
				Ctx.MstAdj.SetNum(Ctx.Rooms.Num());
				if (Ctx.Rooms.Num() == 1)
				{
					Ctx.StartRoomIdx = 0;
					Ctx.EndRoomIdx = 0;
				}
			}
		}
		// --- End orientation solve stage ---

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

		// Backbone corridors (spine ones widened).
		TSet<uint64> EdgeSet;
		for (const TPair<int32, int32>& E : Ctx.MstEdges)
		{
			const bool bSpine = Ctx.SpineEdges.Contains(EdgeKey(E.Key, E.Value));
			MakeRoomCorridor(Ctx, Params, E.Key, E.Value, false, bSpine, bSpine ? Params.SpineWidthScale : 1.0f, false);
			EdgeSet.Add(EdgeKey(E.Key, E.Value));
			if (bSpine)
			{
				++Ctx.StatSpine;
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
				if (MakeRoomCorridor(Ctx, Params, a, b, true, false, 1.0f, true))
				{
					EdgeSet.Add(EdgeKey(a, b));
					++Ctx.StatLoops;
				}
			}
		}
	}
}

FOrganicEndRoomAnchor UOrganicDungeonGenerator2D::ComputeChainLeaf(
	FOrgSubgraphBuild& Ctx, const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint)
{
	// --- Compute chain-leaf end-room anchor for inter-location stitching and the segment exit ---
	// The chain leaf is the MST diameter endpoint opposite the entrance. Its outward doorway position is
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
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, InRadiusScale);
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

		if (SegmentHitsRoom(Ctx.Rooms, DoorPos, EndPos, RoomIdx, INDEX_NONE))
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
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, InRadiusScale);
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
		const int32				N = Src.Centerline.Num();
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
		const FVector2D P = Src.Centerline[Bi];
		const FVector2D Tangent = (Src.Centerline[Bi + 1] - Src.Centerline[Bi - 1]).GetSafeNormal();
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
			float			D2 = FLT_MAX;
			const FVector2D Q = NearestOnPolyline(Ctx.Corridors[c].Centerline, P, D2);
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
		if (CorridorHitsRoom(Ctx.Rooms, Link.Centerline, SrcRoomA, SrcRoomB, RejectAllowedTarget))
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

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT(
			"[ORG] Layout: rooms=%d/%d corridors=%d spine=%d loops=%d deadEnds=%d links=%d | entrance=%d chainLeaf=%d | placementRetries=%d backtracks=%d"),
		Build.PlacedCount,
		RequestedRoomCount,
		Build.Corridors.Num(),
		Build.StatSpine,
		Build.StatLoops,
		Build.StatDeadEnds,
		Build.StatLinks,
		Build.StartRoomIdx,
		ChainLeaf.RoomIndex,
		Build.StatRetries,
		Build.StatBacktracks);

	FOrganicLayout OutLayout;
	OutLayout.Rooms = MoveTemp(Build.Rooms);
	OutLayout.Corridors = MoveTemp(Build.Corridors);
	OutLayout.StartRoomIdx = Build.StartRoomIdx;
	OutLayout.EndRoomIdx = ChainLeaf.RoomIndex; // far endpoint; GenerateInternal uses this for stitching + the segment exit
	OutLayout.RequestedRoomCount = RequestedRoomCount;
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
	const int32						StartRoomIdx = Layout.StartRoomIdx;
	const int32						PlacedCount = Layout.PlacedCount;
	const int32						RequestedRoomCount = Layout.RequestedRoomCount;

	auto MakeEmpty = [&]() -> FOrganicDungeonGridData {
		FOrganicDungeonGridData R;
		R.CellSize = CellSizeVal;
		R.RequestedRoomCount = RequestedRoomCount;
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
	for (const FOrganicCorridor& Cor : Corridors)
	{
		for (int32 i = 0; i < Cor.Centerline.Num(); ++i)
		{
			const float R = Cor.Radii[i];
			Expand(Cor.Centerline[i] + FVector2D(R, R));
			Expand(Cor.Centerline[i] - FVector2D(R, R));
		}
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
		for (int32 i = 0; i < Cor.Centerline.Num(); ++i)
		{
			StampDisc(Cor.Centerline[i], FMath::Max(CellSizeVal * 0.5f, Cor.Radii[i]));
		}
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
		TEXT("[ORG] Rasterize: rooms=%d/%d corridors=%d | entrance=%d exit=%d | regions=%d"),
		PlacedCount,
		RequestedRoomCount,
		Corridors.Num(),
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
	Result.RoomFootprintCells = MoveTemp(RoomFootprintCells);
	Result.RequestedRoomCount = RequestedRoomCount;
	Result.StartRoomIndex = StartRoomIdx;
	Result.EndRoomIndex = Layout.EndRoomIdx;
	Result.LocationStartRoomIndex = Layout.LocationStartRoomIndex;
	Result.Diagram = MoveTemp(Diagram);

	// ---- Floor/wall boundary preparation ----
	// The boundary geometry source is the VECTOR union of smoothed corridor ribbons and room
	// OBBs (not the rasterized grid trace).  The grid (Grid/CellType/RegionIds/Regions) is
	// retained above for flood-fill / region ids / cave smoothing; only the boundary contour
	// comes from the vector path.  This is a pure-data pass (no UObject/world access).
	const int32 SmoothIters = Params.bSmoothCorridors ? Params.SmoothIterations : 2;
	FOrganicFloorBuilder::BuildVectorContour(Result.Corridors, Result.Rooms, SmoothIters, Result.WalkableContour);

	// Carry wall thickness (in cells) through to the runtime foundation-mesh builder.
	Result.WallThicknessCells = FMath::Max(1, Params.WallThickness);

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

void UOrganicDungeonGenerator2D::BuildMST(const TArray<FOrganicRoom>& Rooms, TArray<TPair<int32, int32>>& OutEdges, TArray<TArray<int32>>& OutAdj)
{
	const int32 NumRooms = Rooms.Num();
	OutEdges.Reset();
	OutAdj.SetNum(NumRooms);

	TArray<bool>  InTree;
	TArray<float> MinD;
	TArray<int32> Parent;
	InTree.Init(false, NumRooms);
	MinD.Init(FLT_MAX, NumRooms);
	Parent.Init(INDEX_NONE, NumRooms);
	MinD[0] = 0.0f;

	// Prim's algorithm: O(N^2), appropriate for the small room counts (<= ~200) used here.
	for (int32 It = 0; It < NumRooms; ++It)
	{
		// Pick the unvisited node with the smallest tentative distance.
		int32 U = INDEX_NONE;
		float Best = FLT_MAX;
		for (int32 V = 0; V < NumRooms; ++V)
		{
			if (!InTree[V] && MinD[V] < Best)
			{
				Best = MinD[V];
				U = V;
			}
		}
		if (U == INDEX_NONE)
		{
			break; // disconnected (shouldn't happen for a fully-connected complete graph)
		}
		InTree[U] = true;
		if (Parent[U] != INDEX_NONE)
		{
			OutEdges.Add({ Parent[U], U });
			OutAdj[Parent[U]].Add(U);
			OutAdj[U].Add(Parent[U]);
		}
		// Relax neighbours.
		for (int32 V = 0; V < NumRooms; ++V)
		{
			if (!InTree[V])
			{
				const float D = FVector2D::Distance(Rooms[U].Center, Rooms[V].Center);
				if (D < MinD[V])
				{
					MinD[V] = D;
					Parent[V] = U;
				}
			}
		}
	}
}

void UOrganicDungeonGenerator2D::FindSpine(const TArray<FOrganicRoom>& Rooms,
	const TArray<TArray<int32>>&									   MstAdj,
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
			for (int32 V : MstAdj[U])
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
