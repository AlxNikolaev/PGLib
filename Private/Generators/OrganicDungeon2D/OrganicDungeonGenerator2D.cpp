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
} // namespace

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
	FVector2D	   LastExitPos = CenterPoint;
	FVector2D	   LastExitNormal = FVector2D(1.0f, 0.0f);
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

		PrevEndGlobalIdx = ThisEndGlobal;
		PrevEndPos = Merged.Rooms[ThisEndGlobal].Center;
		PrevEndNormal = Sub.ExitAnchorNormal;
		LastExitPos = Sub.ExitAnchorPos;
		LastExitNormal = Sub.ExitAnchorNormal;
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
	Merged.EndRoomIdx = PrevEndGlobalIdx;
	Merged.ExitAnchorPos = LastExitPos;
	Merged.ExitAnchorNormal = LastExitNormal;
	Merged.PlacedCount = Merged.Rooms.Num();

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG] Cluster graph: %d location(s) -> rooms=%d corridors=%d start=%d end=%d"),
		Segs.Num(),
		Merged.Rooms.Num(),
		Merged.Corridors.Num(),
		Merged.StartRoomIdx,
		Merged.EndRoomIdx);

	return RasterizeLayout(Merged);
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

	FOrganicCorridor Cor;
	const FVector2D	 Chord = BP - AP;
	const float		 Dist = FMath::Max(1.0f, Chord.Size());
	const FVector2D	 Perp = FVector2D(-Chord.Y, Chord.X).GetSafeNormal();
	const float		 Off = LinkParams.Waviness * Dist * 0.5f;
	const FVector2D	 C1 = AP + AN * (Dist / 3.0f) + Perp * RandomStream.FRandRange(-Off, Off);
	const FVector2D	 C2 = BP + BN * (Dist / 3.0f) + Perp * RandomStream.FRandRange(-Off, Off);

	const int32 N = FMath::Clamp(FMath::CeilToInt(Dist / (CellSizeVal * 0.5f)), 8, 256);

	const float	  MinR = FMath::Max(CellSizeVal, LinkParams.MinThickness * 0.5f);
	const float	  MaxR = FMath::Max(MinR, LinkParams.MaxWidth * 0.5f);
	const int32	  NumCtrl = FMath::Clamp(FMath::CeilToInt(Dist / 200.0f) + 2, 3, 16);
	TArray<float> RadCtrl;
	RadCtrl.Reserve(NumCtrl);
	for (int32 i = 0; i < NumCtrl; ++i)
	{
		RadCtrl.Add(RandomStream.FRand());
	}

	Cor.Centerline.Reserve(N + 1);
	Cor.Radii.Reserve(N + 1);
	for (int32 i = 0; i <= N; ++i)
	{
		const float		T = static_cast<float>(i) / N;
		const FVector2D Pt = CubicBezier(AP, C1, C2, BP, T);
		Cor.Centerline.Add(Pt);

		float Radius;
		if (LinkParams.CorridorStyle == EOrganicCorridorStyle::Clean)
		{
			Radius = MinR;
		}
		else
		{
			const float Fp = T * (NumCtrl - 1);
			const int32 I0 = FMath::Clamp(FMath::FloorToInt(Fp), 0, NumCtrl - 1);
			const int32 I1 = FMath::Min(I0 + 1, NumCtrl - 1);
			float		Frac = FMath::Clamp(Fp - I0, 0.0f, 1.0f);
			Frac = Frac * Frac * (3.0f - 2.0f * Frac);
			const float Noise = FMath::Lerp(RadCtrl[I0], RadCtrl[I1], Frac);
			Radius = FMath::Lerp(MinR, MaxR, Noise);
		}
		Cor.Radii.Add(Radius);
	}

	Cor.AnchorA = { EOrganicAnchorType::Room, FromIdx, AP, AN };
	Cor.AnchorB = { EOrganicAnchorType::Room, ToIdx, BP, BN };
	Cor.bIsSpine = true; // inter-location link is part of the main route
	return Cor;
}

// The Params/CenterPoint parameters intentionally shadow the equally-named members so each location's
// subgraph generates against its own resolved params + anchor without rewriting the whole body.
#pragma warning(push)
#pragma warning(disable : 4458)
FOrganicLayout UOrganicDungeonGenerator2D::GenerateLocationSubgraph(
	const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles)
{
	const float CellSizeVal = static_cast<float>(GridSize);

	// Build the room queue, interleaving types so a high-count type doesn't form one long single-type chain.
	// Greedy: at each step place the type with the most rooms remaining that DIFFERS from the previously
	// placed type (when an alternative exists); ties break by a seeded type order. This spreads scarce types
	// out and only repeats a type once it is the only one left — more variety / a more natural layout.
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

	TArray<int32> Queue;
	Queue.Reserve(RequestedRoomCount);
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
			// Weighted-random by remaining count: a high-count type is picked more often (never starves the
			// scarce ones) but the exact sequence is random, so there is no fixed repeating pattern.
			// Uses the shared ProceduralGeometry_PickWeightedType utility (extracted from this loop).
			Chosen =
				ProceduralGeometry_PickWeightedType(Remaining, PrevType, [&](int32 MaxInclusive) { return RandomStream.RandRange(0, MaxInclusive); });
			if (Chosen == INDEX_NONE)
			{
				Chosen = PrevType; // fallback: only prev type has rooms
			}
		}
		else
		{
			// Deterministic spread: most-remaining non-prev type (ties go to the lowest declared index).
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

		Queue.Add(Chosen);
		--Remaining[Chosen];
		PrevType = Chosen;
	}

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

	// --- Helpers bound to this generation ---
	auto HalfExtentOf = [&](int32 TypeIdx) {
		return FVector2D(Params.RoomTypes[TypeIdx].FootprintWidth * 0.5f, Params.RoomTypes[TypeIdx].FootprintHeight * 0.5f);
	};

	auto GenerateDoorways = [&](FOrganicRoom& Room) {
		const int32		PerEdge = FMath::Max(1, Params.RoomTypes[Room.TypeIndex].DoorwaysPerEdge);
		const FVector2D AxisX = RotateDeg(FVector2D(1, 0), Room.RotationDeg);
		const FVector2D AxisY = RotateDeg(FVector2D(0, 1), Room.RotationDeg);
		// 4 edges: +X, -X, +Y, -Y (local normals)
		const FVector2D LocalNormals[4] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
		for (int32 e = 0; e < 4; ++e)
		{
			const FVector2D N = LocalNormals[e];
			const FVector2D WorldN = (AxisX * N.X + AxisY * N.Y).GetSafeNormal();
			// Tangent along the edge (perpendicular to N)
			const FVector2D LocalT(-N.Y, N.X);
			const float		EdgeHalf = (e < 2) ? Room.HalfExtent.Y : Room.HalfExtent.X; // edge length/2 along its tangent
			const float		Push = (e < 2) ? Room.HalfExtent.X : Room.HalfExtent.Y;		// distance from center to edge
			for (int32 i = 0; i < PerEdge; ++i)
			{
				const float		Frac = (PerEdge == 1) ? 0.0f : (((i + 1.0f) / (PerEdge + 1.0f)) * 2.0f - 1.0f); // [-1,1]
				const FVector2D LocalPos = N * Push + LocalT * (Frac * EdgeHalf);
				const FVector2D WorldPos = Room.Center + AxisX * LocalPos.X + AxisY * LocalPos.Y;
				FOrganicDoorway D;
				D.Pos = WorldPos;
				D.OutwardNormal = WorldN;
				Room.Doorways.Add(D);
			}
		}
	};

	auto MakeRoom = [&](int32 TypeIdx, const FVector2D& Center, float RotDeg) -> FOrganicRoom {
		FOrganicRoom Room;
		Room.TypeIndex = TypeIdx;
		Room.Center = Center;
		Room.RotationDeg = RotDeg;
		Room.HalfExtent = HalfExtentOf(TypeIdx);
		Room.RoomLevel = Params.RoomTypes[TypeIdx].RoomLevel;
		Room.FootprintCenterOffset = Params.RoomTypes[TypeIdx].FootprintCenter;
		GenerateDoorways(Room);
		return Room;
	};

	TArray<FOrganicRoom> Rooms;
	Rooms.Reserve(RequestedRoomCount);
	TArray<FOrganicCorridor> Corridors;
	TArray<int32>			 OpenList; // room indices still eligible to grow from

	auto EdgeKey = [](int32 A, int32 B) -> uint64 {
		const int32 Lo = FMath::Min(A, B);
		const int32 Hi = FMath::Max(A, B);
		return (static_cast<uint64>(Lo) << 32) | static_cast<uint32>(Hi);
	};

	auto RandomRotation = [&]() { return Params.bRandomRotation ? RandomStream.FRandRange(0.0f, 360.0f) : 0.0f; };

	// Builds a corridor centerline+radii between two doorway endpoints (B endpoint optional for stubs).
	auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float RadiusScale = 1.0f) -> FOrganicCorridor {
		FOrganicCorridor Cor;
		const FVector2D	 Chord = BP - AP;
		const float		 Dist = FMath::Max(1.0f, Chord.Size());
		const FVector2D	 Perp = FVector2D(-Chord.Y, Chord.X).GetSafeNormal();
		const float		 Off = Params.Waviness * Dist * 0.5f;
		const FVector2D	 C1 = AP + AN * (Dist / 3.0f) + Perp * RandomStream.FRandRange(-Off, Off);
		const FVector2D	 C2 = BP + BN * (Dist / 3.0f) + Perp * RandomStream.FRandRange(-Off, Off);

		const int32 N = FMath::Clamp(FMath::CeilToInt(Dist / (CellSizeVal * 0.5f)), 8, 256);

		// Pre-generate radius control points for cave variation.
		const float	  MinR = Params.MinThickness * 0.5f;
		const float	  MaxR = Params.MaxWidth * 0.5f;
		TArray<float> RadCtrl;
		const int32	  NumCtrl = FMath::Clamp(FMath::CeilToInt(Dist / 200.0f) + 2, 3, 16);
		for (int32 i = 0; i < NumCtrl; ++i)
		{
			RadCtrl.Add(RandomStream.FRand());
		}

		Cor.Centerline.Reserve(N + 1);
		Cor.Radii.Reserve(N + 1);
		for (int32 i = 0; i <= N; ++i)
		{
			const float		T = static_cast<float>(i) / N;
			const FVector2D Pt = CubicBezier(AP, C1, C2, BP, T);
			Cor.Centerline.Add(Pt);

			float Radius;
			if (Params.CorridorStyle == EOrganicCorridorStyle::Clean)
			{
				Radius = MinR;
			}
			else
			{
				// Smoothstep-interpolated value noise across the control points.
				const float Fp = T * (NumCtrl - 1);
				const int32 I0 = FMath::Clamp(FMath::FloorToInt(Fp), 0, NumCtrl - 1);
				const int32 I1 = FMath::Min(I0 + 1, NumCtrl - 1);
				float		Frac = FMath::Clamp(Fp - I0, 0.0f, 1.0f);
				Frac = Frac * Frac * (3.0f - 2.0f * Frac); // smoothstep
				const float Noise = FMath::Lerp(RadCtrl[I0], RadCtrl[I1], Frac);
				Radius = FMath::Lerp(MinR, MaxR, Noise);
			}
			Cor.Radii.Add(Radius * RadiusScale);
		}
		return Cor;
	};

	// Does the straight segment AP->BP pass through any room other than the two endpoints' rooms?
	auto SegmentHitsRoom = [&](const FVector2D& AP, const FVector2D& BP, int32 RoomExclA, int32 RoomExclB) -> bool {
		const int32 Steps = 16;
		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			if (r == RoomExclA || r == RoomExclB)
			{
				continue;
			}
			const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, 0.0f);
			for (int32 s = 0; s <= Steps; ++s)
			{
				const FVector2D P = FMath::Lerp(AP, BP, static_cast<float>(s) / Steps);
				if (PointInOBB(P, OBB))
				{
					return true;
				}
			}
		}
		return false;
	};

	// True if a corridor's actual (curved) centerline enters any room not in AllowedRooms.
	auto CorridorHitsRoom = [&](const TArray<FVector2D>& Centerline, int32 AllowedA, int32 AllowedB, int32 AllowedC) -> bool {
		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			if (r == AllowedA || r == AllowedB || r == AllowedC)
			{
				continue;
			}
			const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, 0.0f);
			for (const FVector2D& P : Centerline)
			{
				if (PointInOBB(P, OBB))
				{
					return true;
				}
			}
		}
		return false;
	};

	// The room indices a corridor connects (its Room-typed anchors); writes up to two, -1 if none.
	auto CorridorRooms = [&](const FOrganicCorridor& Cor, int32& OutA, int32& OutB) {
		OutA = (Cor.AnchorA.Type == EOrganicAnchorType::Room) ? Cor.AnchorA.Index : INDEX_NONE;
		OutB = (Cor.AnchorB.Type == EOrganicAnchorType::Room) ? Cor.AnchorB.Index : INDEX_NONE;
	};

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
	Rooms.Add(MakeRoom(Queue[0], FirstCenter, RandomRotation()));
	OpenList.Add(0);

	int32 QueueIdx = 1;
	int32 StatRetries = 0;
	int32 StatBacktracks = 0;

	while (QueueIdx < Queue.Num() && OpenList.Num() > 0)
	{
		int32 SrcListIdx;
		if (Params.BranchProbability > 0.0f && OpenList.Num() > 1 && RandomStream.FRand() < Params.BranchProbability)
		{
			SrcListIdx = RandomStream.RandRange(0, OpenList.Num() - 1);
		}
		else
		{
			SrcListIdx = OpenList.Num() - 1;
		}
		const int32 SrcRoomIdx = OpenList[SrcListIdx];

		const int32		NextType = Queue[QueueIdx];
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
			for (int32 d = 0; d < Rooms[SrcRoomIdx].Doorways.Num(); ++d)
			{
				if (!Rooms[SrcRoomIdx].Doorways[d].bUsed)
				{
					UnusedDoors.Add(d);
				}
			}
			if (UnusedDoors.Num() == 0)
			{
				break; // source exhausted
			}
			const int32		DoorIdx = UnusedDoors[RandomStream.RandRange(0, UnusedDoors.Num() - 1)];
			const FVector2D DoorPos = Rooms[SrcRoomIdx].Doorways[DoorIdx].Pos;
			const FVector2D DoorNormal = Rooms[SrcRoomIdx].Doorways[DoorIdx].OutwardNormal;

			const float L = RandomStream.FRandRange(Params.CorridorLengthMin, Params.CorridorLengthMax);
			// A room-to-room gap can't be smaller than MinRoomGap, so the corridor is at least that long.
			// If MinRoomGap > the requested corridor length, the new room would always overlap the source
			// (inflated by MinRoomGap/2 each) and the location would starve to a single room — clamp up.
			const float		Spacing = FMath::Max(L, Params.MinRoomGap + CellSizeVal);
			const FVector2D NewCenter = DoorPos + DoorNormal * (Spacing + NextReach);
			FOrganicRoom	NewRoom = MakeRoom(NextType, NewCenter, RandomRotation());

			// Overlap test (with gap) against all placed rooms.
			const FOBB2D NewOBB = MakeOBB(NewRoom.Center, NewRoom.RotationDeg, NewRoom.HalfExtent, Params.MinRoomGap * 0.5f);
			bool		 bOverlap = false;
			for (int32 r = 0; r < Rooms.Num(); ++r)
			{
				const FOBB2D OBB = MakeOBB(Rooms[r].Center, Rooms[r].RotationDeg, Rooms[r].HalfExtent, Params.MinRoomGap * 0.5f);
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
				++StatRetries;
				continue;
			}

			// Commit: placement only scatters the room. Connectivity (which corridors exist) is decided
			// afterwards by the proximity graph, so no corridor/doorway is consumed here.
			const int32 NewIdx = Rooms.Add(MoveTemp(NewRoom));
			OpenList.Add(NewIdx);
			++QueueIdx;
			bPlaced = true;
		}

		if (!bPlaced)
		{
			// Cornered: drop this source so growth continues from an earlier room.
			++StatBacktracks;
			OpenList.RemoveAt(SrcListIdx);
		}
	}

	const int32 PlacedCount = Rooms.Num();

	// Debug: world AABB of what actually placed (so you can see where this subgraph landed and how big it is).
	FVector2D PlacedMin(FLT_MAX, FLT_MAX);
	FVector2D PlacedMax(-FLT_MAX, -FLT_MAX);
	for (const FOrganicRoom& R : Rooms)
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
		PlacedCount,
		RequestedRoomCount,
		PlacedMin.X,
		PlacedMin.Y,
		PlacedMax.X,
		PlacedMax.Y,
		PlacedMax.X - PlacedMin.X,
		PlacedMax.Y - PlacedMin.Y,
		StatRetries,
		StatBacktracks);

	if (PlacedCount < RequestedRoomCount)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] Placement shortfall: placed %d / %d rooms (%d unplaced). Check footprint reach vs corridorLen above."),
			PlacedCount,
			RequestedRoomCount,
			RequestedRoomCount - PlacedCount);
	}

	// --- Layer 1: connectivity — proximity MST backbone + budgeted loops, with a widened spine ---
	int32		StatLoops = 0;
	int32		StatSpine = 0;
	const int32 NumRooms = Rooms.Num();
	int32		StartRoomIdx = (NumRooms > 0) ? 0 : -1; // graph-diameter endpoints (set during connectivity)
	int32		EndRoomIdx = -1;

	// Picks the doorway on RoomIdx best facing Toward — preferring an unused one, else the best of any.
	auto PickDoorway = [&](int32 RoomIdx, const FVector2D& Toward) -> int32 {
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
	};

	// Carves a corridor between two rooms via their best-facing doorways. When bRejectIfClipsRoom is set
	// (used for optional loops), the corridor is discarded if its curve would clip a third room; the
	// mandatory backbone passes false so connectivity is always guaranteed. Returns true if carved.
	auto MakeRoomCorridor = [&](int32 A, int32 B, bool bLoop, bool bSpine, float RadiusScale, bool bRejectIfClipsRoom) -> bool {
		const FVector2D ToB = (Rooms[B].Center - Rooms[A].Center).GetSafeNormal();
		const int32		DA = PickDoorway(A, ToB);
		const int32		DB = PickDoorway(B, -ToB);
		if (DA == INDEX_NONE || DB == INDEX_NONE)
		{
			return false;
		}
		const FVector2D	 APos = Rooms[A].Doorways[DA].Pos;
		const FVector2D	 ANrm = Rooms[A].Doorways[DA].OutwardNormal;
		const FVector2D	 BPos = Rooms[B].Doorways[DB].Pos;
		const FVector2D	 BNrm = Rooms[B].Doorways[DB].OutwardNormal;
		FOrganicCorridor Cor = BuildCorridor(APos, ANrm, BPos, BNrm, RadiusScale);
		if (bRejectIfClipsRoom && CorridorHitsRoom(Cor.Centerline, A, B, INDEX_NONE))
		{
			return false;
		}
		Rooms[A].Doorways[DA].bUsed = true;
		Rooms[B].Doorways[DB].bUsed = true;
		Cor.AnchorA = { EOrganicAnchorType::Room, A, APos, ANrm };
		Cor.AnchorB = { EOrganicAnchorType::Room, B, BPos, BNrm };
		Cor.bIsLoop = bLoop;
		Cor.bIsSpine = bSpine;
		Corridors.Add(MoveTemp(Cor));
		return true;
	};

	if (NumRooms >= 2)
	{
		// Prim's MST over the complete graph (weight = room-center distance) → the connecting backbone.
		TArray<bool>  InTree;
		TArray<float> MinD;
		TArray<int32> Parent;
		InTree.Init(false, NumRooms);
		MinD.Init(FLT_MAX, NumRooms);
		Parent.Init(INDEX_NONE, NumRooms);
		MinD[0] = 0.0f;

		TArray<TArray<int32>> MstAdj;
		MstAdj.SetNum(NumRooms);
		TArray<TPair<int32, int32>> MstEdges;

		for (int32 It = 0; It < NumRooms; ++It)
		{
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
				break;
			}
			InTree[U] = true;
			if (Parent[U] != INDEX_NONE)
			{
				MstEdges.Add({ Parent[U], U });
				MstAdj[Parent[U]].Add(U);
				MstAdj[U].Add(Parent[U]);
			}
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

		// Diameter: two BFS passes over the MST give the two farthest-apart rooms (Start/End endpoints).
		// The path between them is the spine. Start = endpoint nearer CenterPoint (entrance), End = other.
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

		TArray<int32> P1;
		const int32	  A = BfsFarthest(0, P1);
		TArray<int32> P2;
		const int32	  B = BfsFarthest(A, P2); // P2 = parents rooted at A; B = far end

		// Orient: Start is the endpoint closer to CenterPoint (the level entrance), End the farther.
		if (FVector2D::Distance(Rooms[B].Center, CenterPoint) < FVector2D::Distance(Rooms[A].Center, CenterPoint))
		{
			StartRoomIdx = B;
			EndRoomIdx = A;
		}
		else
		{
			StartRoomIdx = A;
			EndRoomIdx = B;
		}

		// Spine edges = the A..B path (walk B up to A via P2).
		TSet<uint64> SpineEdges;
		for (int32 N = B; P2[N] != INDEX_NONE; N = P2[N])
		{
			SpineEdges.Add(EdgeKey(N, P2[N]));
		}

		// Backbone corridors (spine ones widened).
		TSet<uint64> EdgeSet;
		for (const TPair<int32, int32>& E : MstEdges)
		{
			const bool bSpine = SpineEdges.Contains(EdgeKey(E.Key, E.Value));
			MakeRoomCorridor(E.Key, E.Value, false, bSpine, bSpine ? Params.SpineWidthScale : 1.0f, false);
			EdgeSet.Add(EdgeKey(E.Key, E.Value));
			if (bSpine)
			{
				++StatSpine;
			}
		}

		// Loops: add the shortest non-backbone edges within LoopMaxDistance, up to LoopCount.
		if (Params.LoopCount > 0)
		{
			TArray<TPair<float, FIntPoint>> Cand;
			for (int32 a = 0; a < NumRooms; ++a)
			{
				for (int32 b = a + 1; b < NumRooms; ++b)
				{
					if (EdgeSet.Contains(EdgeKey(a, b)))
					{
						continue;
					}
					const float D = FVector2D::Distance(Rooms[a].Center, Rooms[b].Center);
					if (D <= Params.LoopMaxDistance)
					{
						Cand.Add({ D, FIntPoint(a, b) });
					}
				}
			}
			Cand.Sort([](const TPair<float, FIntPoint>& L, const TPair<float, FIntPoint>& R) { return L.Key < R.Key; });
			for (int32 i = 0; i < Cand.Num() && StatLoops < Params.LoopCount; ++i)
			{
				const int32 a = Cand[i].Value.X;
				const int32 b = Cand[i].Value.Y;
				if (MakeRoomCorridor(a, b, true, false, 1.0f, true))
				{
					EdgeSet.Add(EdgeKey(a, b));
					++StatLoops;
				}
			}
		}
	}

	// --- Designate start/end prefabs at the diameter endpoints and compute the exit hand-off anchor ---
	FVector2D ExitAnchorPos = (EndRoomIdx >= 0) ? Rooms[EndRoomIdx].Center : CenterPoint;
	FVector2D ExitAnchorNormal = FVector2D(1.0f, 0.0f);
	if (EndRoomIdx >= 0)
	{
		// Dungeon centroid — used to point the exit outward, away from the rest of the level.
		FVector2D Centroid(0.0f, 0.0f);
		for (const FOrganicRoom& R : Rooms)
		{
			Centroid += R.Center;
		}
		Centroid /= FMath::Max(1, Rooms.Num());
		const FVector2D Outward = (Rooms[EndRoomIdx].Center - Centroid).GetSafeNormal();

		// End room's doorway best facing outward (prefer free); reserve it so corridors don't take it.
		int32 BestFree = INDEX_NONE;
		float BestFreeDot = -FLT_MAX;
		int32 BestAny = INDEX_NONE;
		float BestAnyDot = -FLT_MAX;
		for (int32 d = 0; d < Rooms[EndRoomIdx].Doorways.Num(); ++d)
		{
			const float Dot = FVector2D::DotProduct(Rooms[EndRoomIdx].Doorways[d].OutwardNormal, Outward);
			if (Dot > BestAnyDot)
			{
				BestAnyDot = Dot;
				BestAny = d;
			}
			if (!Rooms[EndRoomIdx].Doorways[d].bUsed && Dot > BestFreeDot)
			{
				BestFreeDot = Dot;
				BestFree = d;
			}
		}
		const int32 ExitDoor = (BestFree != INDEX_NONE) ? BestFree : BestAny;
		if (ExitDoor != INDEX_NONE)
		{
			Rooms[EndRoomIdx].Doorways[ExitDoor].bUsed = true; // reserve from dead-ends/links
			ExitAnchorPos = Rooms[EndRoomIdx].Doorways[ExitDoor].Pos;
			ExitAnchorNormal = Rooms[EndRoomIdx].Doorways[ExitDoor].OutwardNormal;
		}

		// Swap in the End prefab (level ref + bounds-center offset); keep the placed footprint so the
		// already-carved corridors stay attached to its edges.
		if (Params.bHasEndRoom)
		{
			Rooms[EndRoomIdx].RoomLevel = Params.EndRoom.RoomLevel;
			Rooms[EndRoomIdx].FootprintCenterOffset = Params.EndRoom.FootprintCenter;
		}
	}
	if (StartRoomIdx >= 0 && Params.bHasStartRoom)
	{
		Rooms[StartRoomIdx].RoomLevel = Params.StartRoom.RoomLevel;
		Rooms[StartRoomIdx].FootprintCenterOffset = Params.StartRoom.FootprintCenter;
	}

	// --- Layer 1b: dead-end stubs ---
	int32 StatDeadEnds = 0;
	for (int32 i = 0; i < Params.DeadEndCount; ++i)
	{
		// Find rooms with a free doorway.
		TArray<int32> Candidates;
		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			for (const FOrganicDoorway& D : Rooms[r].Doorways)
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
		for (int32 d = 0; d < Rooms[RoomIdx].Doorways.Num(); ++d)
		{
			if (!Rooms[RoomIdx].Doorways[d].bUsed)
			{
				Free.Add(d);
			}
		}
		const int32		DoorIdx = Free[RandomStream.RandRange(0, Free.Num() - 1)];
		const FVector2D DoorPos = Rooms[RoomIdx].Doorways[DoorIdx].Pos;
		const FVector2D DoorNormal = Rooms[RoomIdx].Doorways[DoorIdx].OutwardNormal;
		const FVector2D EndPos = DoorPos + DoorNormal * Params.DeadEndLength;

		if (SegmentHitsRoom(DoorPos, EndPos, RoomIdx, INDEX_NONE))
		{
			continue;
		}

		Rooms[RoomIdx].Doorways[DoorIdx].bUsed = true;
		// Stub: corridor from doorway to a point, ending in a small chamber (use BuildCorridor with an
		// outward end normal so the curve eases out).
		FOrganicCorridor Cor = BuildCorridor(DoorPos, DoorNormal, EndPos, -DoorNormal);
		Cor.AnchorA = { EOrganicAnchorType::Room, RoomIdx, DoorPos, DoorNormal };
		Cor.AnchorB = { EOrganicAnchorType::Free, -1, EndPos, -DoorNormal };
		Corridors.Add(MoveTemp(Cor));
		++StatDeadEnds;
	}

	// --- Layer 1b: corridor links (a branch from a corridor to the nearest room doorway or other corridor) ---
	int32 StatLinks = 0;
	Corridors.Reserve(Corridors.Num() + Params.CorridorLinkCount); // keep Src reference valid across Add
	for (int32 i = 0; i < Params.CorridorLinkCount; ++i)
	{
		if (Corridors.Num() == 0)
		{
			break;
		}
		const int32				SrcCorIdx = RandomStream.RandRange(0, Corridors.Num() - 1);
		const FOrganicCorridor& Src = Corridors[SrcCorIdx];
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

		// Find the nearest valid target in the branch direction: a free room doorway or another corridor.
		float		   BestDist = Params.LinkMaxDistance;
		FOrganicAnchor Target;
		bool		   bFound = false;
		int32		   TargetRoom = INDEX_NONE;
		int32		   TargetDoor = INDEX_NONE;

		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			if (r == SrcRoomA || r == SrcRoomB)
			{
				continue; // already a neighbour of the source corridor
			}
			for (int32 d = 0; d < Rooms[r].Doorways.Num(); ++d)
			{
				if (Rooms[r].Doorways[d].bUsed)
				{
					continue;
				}
				const FVector2D To = Rooms[r].Doorways[d].Pos - P;
				const float		Dist = To.Size();
				if (Dist < 1.0f || Dist > BestDist || FVector2D::DotProduct(To / Dist, BranchDir) < 0.2f)
				{
					continue;
				}
				BestDist = Dist;
				Target = { EOrganicAnchorType::Room, r, Rooms[r].Doorways[d].Pos, Rooms[r].Doorways[d].OutwardNormal };
				TargetRoom = r;
				TargetDoor = d;
				bFound = true;
			}
		}
		for (int32 c = 0; c < Corridors.Num(); ++c)
		{
			if (c == SrcCorIdx)
			{
				continue;
			}
			int32 cA = INDEX_NONE;
			int32 cB = INDEX_NONE;
			CorridorRooms(Corridors[c], cA, cB);
			const bool bShareRoom =
				(cA != INDEX_NONE && (cA == SrcRoomA || cA == SrcRoomB)) || (cB != INDEX_NONE && (cB == SrcRoomA || cB == SrcRoomB));
			if (bShareRoom)
			{
				continue; // corridors already joined through a shared room
			}
			float			D2 = FLT_MAX;
			const FVector2D Q = NearestOnPolyline(Corridors[c].Centerline, P, D2);
			const float		Dist = FMath::Sqrt(D2);
			if (Dist < 1.0f || Dist > BestDist)
			{
				continue;
			}
			const FVector2D To = Q - P;
			if (FVector2D::DotProduct(To / Dist, BranchDir) < 0.2f)
			{
				continue;
			}
			BestDist = Dist;
			Target = { EOrganicAnchorType::Corridor, c, Q, (P - Q).GetSafeNormal() };
			TargetRoom = INDEX_NONE;
			TargetDoor = INDEX_NONE;
			bFound = true;
		}

		if (!bFound)
		{
			continue;
		}

		// Build the actual curved link and reject it if its centerline clips a room other than the
		// source corridor's rooms or the target room (the straight segment alone misses curved bows).
		FOrganicCorridor Link = BuildCorridor(P, BranchDir, Target.Pos, Target.Normal);
		if (CorridorHitsRoom(Link.Centerline, SrcRoomA, SrcRoomB, TargetRoom))
		{
			continue;
		}

		if (TargetRoom != INDEX_NONE)
		{
			Rooms[TargetRoom].Doorways[TargetDoor].bUsed = true;
		}
		Link.AnchorA = { EOrganicAnchorType::Corridor, SrcCorIdx, P, BranchDir };
		Link.AnchorB = Target;
		Link.bIsLink = true;
		Corridors.Add(MoveTemp(Link));
		++StatLinks;
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG] Layout: rooms=%d/%d corridors=%d spine=%d loops=%d deadEnds=%d links=%d | start=%d end=%d | placementRetries=%d backtracks=%d"),
		PlacedCount,
		RequestedRoomCount,
		Corridors.Num(),
		StatSpine,
		StatLoops,
		StatDeadEnds,
		StatLinks,
		StartRoomIdx,
		EndRoomIdx,
		StatRetries,
		StatBacktracks);

	FOrganicLayout OutLayout;
	OutLayout.Rooms = MoveTemp(Rooms);
	OutLayout.Corridors = MoveTemp(Corridors);
	OutLayout.StartRoomIdx = StartRoomIdx;
	OutLayout.EndRoomIdx = EndRoomIdx;
	OutLayout.ExitAnchorPos = ExitAnchorPos;
	OutLayout.ExitAnchorNormal = ExitAnchorNormal;
	OutLayout.RequestedRoomCount = RequestedRoomCount;
	OutLayout.PlacedCount = PlacedCount;
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
	const int32						EndRoomIdx = Layout.EndRoomIdx;
	const FVector2D					ExitAnchorPos = Layout.ExitAnchorPos;
	const FVector2D					ExitAnchorNormal = Layout.ExitAnchorNormal;
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
		TEXT("[ORG] Rasterize: rooms=%d/%d corridors=%d | start=%d end=%d | regions=%d"),
		PlacedCount,
		RequestedRoomCount,
		Corridors.Num(),
		StartRoomIdx,
		EndRoomIdx,
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
	Result.EndRoomIndex = EndRoomIdx;
	Result.ExitAnchorPos = ExitAnchorPos;
	Result.ExitAnchorNormal = ExitAnchorNormal;
	Result.LocationStartRoomIndex = Layout.LocationStartRoomIndex;
	Result.Diagram = MoveTemp(Diagram);
	return Result;
}
