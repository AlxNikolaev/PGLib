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

UOrganicDungeonGenerator2D* UOrganicDungeonGenerator2D::SetRequiredExitAnchors(int32 InCount)
{
	RequiredExitAnchors = FMath::Max(1, InCount);
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

		const int32				  ThisStartGlobal = (Sub.StartRoomIdx >= 0) ? RoomBase + Sub.StartRoomIdx : RoomBase;
		const FOrganicExitAnchor* ChainLeaf = Sub.ExitAnchors.Num() > 0 ? &Sub.ExitAnchors[0] : nullptr;
		const int32 ThisEndGlobal = (ChainLeaf && ChainLeaf->RoomIndex >= 0) ? RoomBase + ChainLeaf->RoomIndex : RoomBase + Sub.Rooms.Num() - 1;

		Merged.LocationStartRoomIndex[s] = ThisStartGlobal;

		if (OverallStartIdx == INDEX_NONE)
		{
			OverallStartIdx = ThisStartGlobal;
		}

		// Stitch subgraphs into one graph: previous location's chain-leaf room -> this location's start room.
		if (bHavePrev && PrevEndGlobalIdx >= 0)
		{
			FOrganicCorridor Link = BuildInterLocationCorridor(
				Merged.Rooms[PrevEndGlobalIdx], PrevEndGlobalIdx, Merged.Rooms[ThisStartGlobal], ThisStartGlobal, SegParams);
			Merged.Corridors.Add(MoveTemp(Link));
		}

		PrevEndGlobalIdx = ThisEndGlobal;
		PrevEndPos = (ChainLeaf && ChainLeaf->RoomIndex >= 0) ? ChainLeaf->Pos : Merged.Rooms[ThisEndGlobal].Center;
		PrevEndNormal = (ChainLeaf && ChainLeaf->RoomIndex >= 0) ? ChainLeaf->Normal : FVector2D(1.0f, 0.0f);
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

	// Build merged MST adjacency and select cluster-scope exit anchors.
	// SelectExitAnchors BFS-ranks MST leaves by graph distance from the entrance and picks
	// RequiredExitAnchors distinct far-leaf anchors. Falls back gracefully when supply is low.
	TArray<TArray<int32>>		MergedMstAdj;
	TArray<TPair<int32, int32>> MergedMstEdges;
	if (Merged.Rooms.Num() >= 2)
	{
		BuildMST(Merged.Rooms, MergedMstEdges, MergedMstAdj);
	}
	else
	{
		MergedMstAdj.SetNum(Merged.Rooms.Num());
	}

	const EOrganicTerminusForm DefaultForm = (Segs.Num() > 0) ? Segs[0].ExitTerminusForm : EOrganicTerminusForm::PortalStub;
	int32					   Shortfall = 0;
	SelectExitAnchors(Merged.Rooms, MergedMstAdj, Merged.StartRoomIdx, RequiredExitAnchors, DefaultForm, Merged.ExitAnchors, Shortfall);
	if (Shortfall > 0)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] Exit-anchor shortfall: required=%d produced=%d. Cross-cluster out-edges may share anchor or fall back to random cells."),
			RequiredExitAnchors,
			RequiredExitAnchors - Shortfall);
	}

	// Swap in portal-room prefab for PortalRoomPrefab anchors (mirrors old end-room swap).
	if (Segs.Num() > 0 && Segs[0].bHasExitPortalRoom)
	{
		for (FOrganicExitAnchor& Anchor : Merged.ExitAnchors)
		{
			if (Anchor.Form == EOrganicTerminusForm::PortalRoomPrefab && Merged.Rooms.IsValidIndex(Anchor.RoomIndex))
			{
				Merged.Rooms[Anchor.RoomIndex].RoomLevel = Segs[0].ExitPortalRoom.RoomLevel;
				Merged.Rooms[Anchor.RoomIndex].FootprintCenterOffset = Segs[0].ExitPortalRoom.FootprintCenter;
				UE_LOG(LogRoguelikeGeometry,
					Verbose,
					TEXT("[ORG] Swapped portal-room prefab at anchor room[%d] '%s'"),
					Anchor.RoomIndex,
					*Segs[0].ExitPortalRoom.DisplayName.ToString());
			}
		}
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[ORG] Cluster graph: %d location(s) -> rooms=%d corridors=%d entrance=%d exitAnchors=%d"),
		Segs.Num(),
		Merged.Rooms.Num(),
		Merged.Corridors.Num(),
		Merged.StartRoomIdx,
		Merged.ExitAnchors.Num());

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
	const FVector2D	 Chord = BP - AP;
	const float		 Dist = FMath::Max(1.0f, Chord.Size());
	const FVector2D	 Perp = FVector2D(-Chord.Y, Chord.X).GetSafeNormal();
	const float		 Off = Waviness * Dist * 0.5f;
	const FVector2D	 C1 = AP + AN * (Dist / 3.0f) + Perp * RandomStream.FRandRange(-Off, Off);
	const FVector2D	 C2 = BP + BN * (Dist / 3.0f) + Perp * RandomStream.FRandRange(-Off, Off);

	const int32 N = FMath::Clamp(FMath::CeilToInt(Dist / (CellSizeVal * 0.5f)), 8, 256);

	// Pre-generate radius control points for cave variation.
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
		if (Style == EOrganicCorridorStyle::Clean)
		{
			Radius = MinRadius;
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
			Radius = FMath::Lerp(MinRadius, MaxRadius, Noise);
		}
		Cor.Radii.Add(Radius * RadiusScale);
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

// The Params/CenterPoint parameters intentionally shadow the equally-named members so each location's
// subgraph generates against its own resolved params + anchor without rewriting the whole body.
#pragma warning(push)
#pragma warning(disable : 4458)
FOrganicLayout UOrganicDungeonGenerator2D::GenerateLocationSubgraph(
	const FOrganicDungeonResolvedParams& Params, const FVector2D& CenterPoint, const TArray<FOrganicRoom>& Obstacles)
{
	const float CellSizeVal = static_cast<float>(GridSize);

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

	// --- Helpers bound to this generation ---
	auto HalfExtentOf = [&](int32 TypeIdx) {
		return FVector2D(Params.RoomTypes[TypeIdx].FootprintWidth * 0.5f, Params.RoomTypes[TypeIdx].FootprintHeight * 0.5f);
	};

	// Generates doorways for a room.
	// For declared-doorway rooms (Doorways.Num() > 0 on the resolved type) the designer-baked
	// openings are transformed local→world.  Legacy rooms fall back to the synthetic per-edge
	// punch-points.  Room.RotationDeg must be set before calling.
	auto GenerateDoorways = [&](FOrganicRoom& Room) {
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

	auto RandomRotation = [&]() { return Params.bRandomRotation ? RandomStream.FRandRange(0.0f, 360.0f) : 0.0f; };

	// Thin wrapper: builds a corridor using the current subgraph's params (Waviness, thickness, style).
	// RadiusScale is forwarded for spine widening (MakeRoomCorridor passes SpineWidthScale there).
	auto BuildCorridor =
		[&](const FVector2D& AP, const FVector2D& AN, const FVector2D& BP, const FVector2D& BN, float RadiusScale = 1.0f) -> FOrganicCorridor {
		const float MinR = Params.MinThickness * 0.5f;
		const float MaxR = Params.MaxWidth * 0.5f;
		return BuildBezierCorridor(AP, AN, BP, BN, Params.Waviness, MinR, MaxR, Params.CorridorStyle, RadiusScale);
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

		// Clamp corridor endpoint radii to half the declared opening width so the carved passage
		// never blasts through the wall geometry surrounding the doorway.
		if (Rooms[A].Doorways[DA].bDeclared && Rooms[A].Doorways[DA].Width > 0.0f && Cor.Radii.Num() > 0)
		{
			Cor.Radii[0] = FMath::Min(Cor.Radii[0], Rooms[A].Doorways[DA].Width * 0.5f);
		}
		if (Rooms[B].Doorways[DB].bDeclared && Rooms[B].Doorways[DB].Width > 0.0f && Cor.Radii.Num() > 0)
		{
			Cor.Radii.Last() = FMath::Min(Cor.Radii.Last(), Rooms[B].Doorways[DB].Width * 0.5f);
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

	// MST adjacency/edges and spine set — declared outside the if-block so the orientation solve
	// and drop logic can rebuild them if rooms are dropped.
	TArray<TArray<int32>>		MstAdj;
	TArray<TPair<int32, int32>> MstEdges;
	TSet<uint64>				SpineEdges;

	if (NumRooms >= 2)
	{
		// Build MST (Prim's, O(N^2)) → backbone connectivity; find graph diameter → spine endpoints.
		BuildMST(Rooms, MstEdges, MstAdj);
		FindSpine(Rooms, MstAdj, CenterPoint, StartRoomIdx, EndRoomIdx, SpineEdges);

		// --- Orientation solve stage ---
		// For each declared-doorway room, choose a rotation that maps every mandatory MST connection
		// to a distinct declared doorway within angular tolerance.  Rooms that cannot be satisfied
		// are re-rolled to another pool entry or dropped.  Runs after MST/spine so mandatory
		// connections are known, before backbone carve so doorways are not yet consumed.

		// Angular tolerance for matching a rotated doorway direction to a mandatory connection.
		constexpr float SolveToleranceDeg = 45.0f;

		TArray<int32> DroppedRoomIndices; // collected in ascending order, removed in reverse

		for (int32 r = 0; r < Rooms.Num(); ++r)
		{
			const FOrganicResolvedRoomType& RT = Params.RoomTypes[Rooms[r].TypeIndex];
			if (RT.Doorways.Num() == 0)
			{
				continue; // legacy room — skip solve, synthetic doorways already generated
			}

			// Gather mandatory connection directions (toward each MST neighbour).
			TArray<FVector2D> MandatoryDirs;
			if (MstAdj.IsValidIndex(r))
			{
				for (int32 Neighbor : MstAdj[r])
				{
					MandatoryDirs.Add((Rooms[Neighbor].Center - Rooms[r].Center).GetSafeNormal());
				}
			}

			if (MandatoryDirs.Num() == 0)
			{
				// Isolated declared-doorway room — regenerate at current rotation, no solve needed.
				GenerateDoorways(Rooms[r]);
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
					const FOBB2D NewOBB = MakeOBB(Rooms[r].Center, SolvedRot, Rooms[r].HalfExtent, Params.MinRoomGap * 0.5f);
					bool		 bOverlap = false;
					for (int32 i = 0; i < Rooms.Num() && !bOverlap; ++i)
					{
						if (i == r)
						{
							continue;
						}
						bOverlap = OBBOverlap(NewOBB, MakeOBB(Rooms[i].Center, Rooms[i].RotationDeg, Rooms[i].HalfExtent, Params.MinRoomGap * 0.5f));
					}
					for (int32 i = 0; i < Obstacles.Num() && !bOverlap; ++i)
					{
						bOverlap = OBBOverlap(
							NewOBB, MakeOBB(Obstacles[i].Center, Obstacles[i].RotationDeg, Obstacles[i].HalfExtent, Params.MinRoomGap * 0.5f));
					}

					if (!bOverlap)
					{
						// Commit orientation: update rotation and regenerate doorways.
						Rooms[r].RotationDeg = SolvedRot;
						GenerateDoorways(Rooms[r]);
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
			const int32 OrigTypeIdx = Rooms[r].TypeIndex;
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
				const FOBB2D	AltOBB = MakeOBB(Rooms[r].Center, AltRot, AltHalf, Params.MinRoomGap * 0.5f);
				bool			bAltOverlap = false;
				for (int32 i = 0; i < Rooms.Num() && !bAltOverlap; ++i)
				{
					if (i == r)
					{
						continue;
					}
					bAltOverlap = OBBOverlap(AltOBB, MakeOBB(Rooms[i].Center, Rooms[i].RotationDeg, Rooms[i].HalfExtent, Params.MinRoomGap * 0.5f));
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
				Rooms[r].TypeIndex = t;
				Rooms[r].RotationDeg = AltRot;
				Rooms[r].HalfExtent = AltHalf;
				Rooms[r].RoomLevel = AltRT.RoomLevel;
				Rooms[r].FootprintCenterOffset = AltRT.FootprintCenter;
				GenerateDoorways(Rooms[r]);
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
				Rooms.RemoveAt(DropIdx);
				// Keep start/end indices consistent.
				if (StartRoomIdx == DropIdx)
				{
					StartRoomIdx = Rooms.Num() > 0 ? 0 : INDEX_NONE;
				}
				else if (StartRoomIdx > DropIdx)
				{
					--StartRoomIdx;
				}
				if (EndRoomIdx == DropIdx)
				{
					EndRoomIdx = INDEX_NONE;
				}
				else if (EndRoomIdx > DropIdx)
				{
					--EndRoomIdx;
				}
			}
			UE_LOG(LogRoguelikeGeometry,
				Warning,
				TEXT("[ORG] Orientation: dropped %d room(s); %d remain. Rebuilding MST."),
				DroppedRoomIndices.Num(),
				Rooms.Num());

			// Rebuild MST + spine over survivors.
			MstAdj.Empty();
			MstEdges.Empty();
			SpineEdges.Empty();
			if (Rooms.Num() >= 2)
			{
				BuildMST(Rooms, MstEdges, MstAdj);
				FindSpine(Rooms, MstAdj, CenterPoint, StartRoomIdx, EndRoomIdx, SpineEdges);
			}
			else
			{
				MstAdj.SetNum(Rooms.Num());
				if (Rooms.Num() == 1)
				{
					StartRoomIdx = 0;
					EndRoomIdx = 0;
				}
			}
		}
		// --- End orientation solve stage ---

		// Helper: for declared-doorway rooms, returns true only if a free declared doorway
		// faces Toward within ~60°.  Legacy rooms always return true (no restriction).
		// Used to gate optional edges (loops, dead-ends, links) so they never silently
		// consume a declared opening that was intended for a mandatory corridor.
		auto CanAttachOptional = [&](int32 RoomIdx, const FVector2D& Toward) -> bool {
			if (!Rooms.IsValidIndex(RoomIdx))
			{
				return false;
			}
			const FOrganicResolvedRoomType& RT = Params.RoomTypes[Rooms[RoomIdx].TypeIndex];
			if (RT.Doorways.Num() == 0)
			{
				return true; // legacy room: always eligible
			}
			constexpr float AlignDot = 0.5f; // cos(60°)
			for (const FOrganicDoorway& D : Rooms[RoomIdx].Doorways)
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
			const int32						CurrentRoomCount = Rooms.Num();
			TArray<TPair<float, FIntPoint>> Cand;
			for (int32 a = 0; a < CurrentRoomCount; ++a)
			{
				for (int32 b = a + 1; b < CurrentRoomCount; ++b)
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
				const int32		a = Cand[i].Value.X;
				const int32		b = Cand[i].Value.Y;
				const FVector2D ToB = (Rooms[b].Center - Rooms[a].Center).GetSafeNormal();
				// Optional edge: skip if a declared-doorway room has no free aligned opening.
				if (!CanAttachOptional(a, ToB) || !CanAttachOptional(b, -ToB))
				{
					continue;
				}
				if (MakeRoomCorridor(a, b, true, false, 1.0f, true))
				{
					EdgeSet.Add(EdgeKey(a, b));
					++StatLoops;
				}
			}
		}
	}

	// --- Compute chain-leaf anchor for inter-location stitching and entrance prefab swap ---
	// The chain leaf is the MST diameter endpoint opposite the entrance. Its outward doorway position
	// is stored in ExitAnchors[0] so GenerateInternal can seed the next location's anchor point.
	// The doorway is NOT reserved here — BuildInterLocationCorridor will independently pick the best
	// facing doorway when stitching, and cluster-scope SelectExitAnchors runs after all carving.
	FOrganicExitAnchor ChainLeaf;
	ChainLeaf.RoomIndex = EndRoomIdx;
	ChainLeaf.Form = EOrganicTerminusForm::PortalStub; // placeholder; overridden by SelectExitAnchors
	if (EndRoomIdx >= 0)
	{
		// Compute outward direction from dungeon centroid so the chain-leaf doorway faces away from the cluster.
		FVector2D Centroid(0.0f, 0.0f);
		for (const FOrganicRoom& R : Rooms)
		{
			Centroid += R.Center;
		}
		Centroid /= static_cast<float>(FMath::Max(1, Rooms.Num()));
		const FVector2D Outward = (Rooms[EndRoomIdx].Center - Centroid).GetSafeNormal();

		// Find the best outward-facing free doorway (prefer free so the chain-leaf has a clean attachment point).
		int32 BestFree = INDEX_NONE;
		float BestFreeDot = -FLT_MAX;
		int32 BestAny = INDEX_NONE;
		float BestAnyDot = -FLT_MAX;
		for (int32 d = 0; d < Rooms[EndRoomIdx].Doorways.Num(); ++d)
		{
			const FVector2D Dir = Outward.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Outward;
			const float		Dot = FVector2D::DotProduct(Rooms[EndRoomIdx].Doorways[d].OutwardNormal, Dir);
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
		const int32 ChainDoor = (BestFree != INDEX_NONE) ? BestFree : BestAny;
		if (ChainDoor != INDEX_NONE)
		{
			ChainLeaf.Pos = Rooms[EndRoomIdx].Doorways[ChainDoor].Pos;
			ChainLeaf.Normal = Rooms[EndRoomIdx].Doorways[ChainDoor].OutwardNormal;
		}
		else
		{
			ChainLeaf.Pos = Rooms[EndRoomIdx].Center;
			ChainLeaf.Normal = Outward.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Outward;
		}
	}
	else
	{
		ChainLeaf.Pos = CenterPoint;
		ChainLeaf.Normal = FVector2D(1.0f, 0.0f);
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
		TEXT(
			"[ORG] Layout: rooms=%d/%d corridors=%d spine=%d loops=%d deadEnds=%d links=%d | entrance=%d chainLeaf=%d | placementRetries=%d backtracks=%d"),
		PlacedCount,
		RequestedRoomCount,
		Corridors.Num(),
		StatSpine,
		StatLoops,
		StatDeadEnds,
		StatLinks,
		StartRoomIdx,
		ChainLeaf.RoomIndex,
		StatRetries,
		StatBacktracks);

	FOrganicLayout OutLayout;
	OutLayout.Rooms = MoveTemp(Rooms);
	OutLayout.Corridors = MoveTemp(Corridors);
	OutLayout.StartRoomIdx = StartRoomIdx;
	OutLayout.ExitAnchors.Add(ChainLeaf); // [0] = chain leaf; GenerateInternal uses this for inter-location stitching
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
		TEXT("[ORG] Rasterize: rooms=%d/%d corridors=%d | entrance=%d exitAnchors=%d | regions=%d"),
		PlacedCount,
		RequestedRoomCount,
		Corridors.Num(),
		StartRoomIdx,
		Layout.ExitAnchors.Num(),
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
	Result.ExitAnchors = Layout.ExitAnchors;
	Result.LocationStartRoomIndex = Layout.LocationStartRoomIndex;
	Result.Diagram = MoveTemp(Diagram);

	// ---- Floor mesh preparation ----
	// Select the floor-mesh strategy based on corridor style and smoothing.
	// Cave-style corridors (variable-width disc-stamped) and smoothed corridors both produce
	// floor shapes that diverge from the centerline ribbon — use grid-contour for those.
	// Clean-style corridors with no smoothing are well-approximated by ribbon extrusion.
	Result.bUseGridContourFloor = Params.bSmoothCorridors || (Params.CorridorStyle == EOrganicCorridorStyle::Cave);

	// Extract the walkable boundary contour from the completed floor grid.
	// This is a pure-data pass (no UObject/world access) — safe to do inline here.
	FOrganicFloorBuilder::ComputeWalkableContour(Result, Result.WalkableContour);

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

void UOrganicDungeonGenerator2D::SelectExitAnchors(TArray<FOrganicRoom>& Rooms,
	const TArray<TArray<int32>>&										 MstAdj,
	int32																 EntranceRoomIdx,
	int32																 RequiredCount,
	EOrganicTerminusForm												 DefaultForm,
	TArray<FOrganicExitAnchor>&											 OutAnchors,
	int32&																 OutShortfall)
{
	OutAnchors.Reset();
	OutShortfall = 0;

	const int32 NumRooms = Rooms.Num();
	if (NumRooms == 0 || RequiredCount <= 0)
	{
		return;
	}

	// Validate adjacency and entrance index.
	if (MstAdj.Num() != NumRooms || !MstAdj.IsValidIndex(EntranceRoomIdx))
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[ORG] SelectExitAnchors: degenerate MST (rooms=%d adj=%d entrance=%d) — falling back to single stub."),
			NumRooms,
			MstAdj.Num(),
			EntranceRoomIdx);
		if (NumRooms > 0)
		{
			FOrganicExitAnchor Stub;
			Stub.RoomIndex = 0;
			Stub.Pos = Rooms[0].Center;
			Stub.Normal = FVector2D(1.0f, 0.0f);
			Stub.GraphDistance = 0;
			Stub.Form = DefaultForm;
			Stub.bIsFallbackStub = true;
			OutAnchors.Add(Stub);
		}
		OutShortfall = FMath::Max(0, RequiredCount - OutAnchors.Num());
		return;
	}

	// BFS from entrance to compute per-room hop distances.
	TArray<int32> Dist;
	Dist.Init(-1, NumRooms);
	Dist[EntranceRoomIdx] = 0;
	TArray<int32> Queue;
	Queue.Reserve(NumRooms);
	Queue.Add(EntranceRoomIdx);
	int32 Head = 0;
	while (Head < Queue.Num())
	{
		const int32 U = Queue[Head++];
		for (int32 V : MstAdj[U])
		{
			if (Dist[V] < 0)
			{
				Dist[V] = Dist[U] + 1;
				Queue.Add(V);
			}
		}
	}

	const FVector2D EntrancePos = Rooms[EntranceRoomIdx].Center;

	// Centroid used for outward-doorway selection (doorway facing away from cluster interior).
	FVector2D Centroid(0.0f, 0.0f);
	for (const FOrganicRoom& R : Rooms)
	{
		Centroid += R.Center;
	}
	Centroid /= static_cast<float>(FMath::Max(1, NumRooms));

	// Returns the best free outward-facing doorway index on a room, or INDEX_NONE.
	// Falls back to the best of any doorway when all are used.
	auto PickBestFreeDoorway = [&](int32 RoomIdx) -> int32 {
		const FVector2D Outward = (Rooms[RoomIdx].Center - Centroid).GetSafeNormal();
		const FVector2D Dir = Outward.IsNearlyZero() ? FVector2D(1.0f, 0.0f) : Outward;

		int32 BestFree = INDEX_NONE;
		float BestFreeDot = -FLT_MAX;
		int32 BestAny = INDEX_NONE;
		float BestAnyDot = -FLT_MAX;
		for (int32 d = 0; d < Rooms[RoomIdx].Doorways.Num(); ++d)
		{
			const float Dot = FVector2D::DotProduct(Rooms[RoomIdx].Doorways[d].OutwardNormal, Dir);
			if (!Rooms[RoomIdx].Doorways[d].bUsed && Dot > BestFreeDot)
			{
				BestFreeDot = Dot;
				BestFree = d;
			}
			if (Dot > BestAnyDot)
			{
				BestAnyDot = Dot;
				BestAny = d;
			}
		}
		return (BestFree != INDEX_NONE) ? BestFree : INDEX_NONE; // only free doorways for exit anchors
	};

	// Builds an FOrganicExitAnchor from a room index and marks its selected doorway used.
	auto MakeAnchor = [&](int32 RoomIdx, int32 GraphDist, bool bFallback) -> FOrganicExitAnchor {
		FOrganicExitAnchor A;
		A.RoomIndex = RoomIdx;
		A.GraphDistance = GraphDist;
		A.Form = DefaultForm;
		A.bIsFallbackStub = bFallback;

		const int32 DoorIdx = PickBestFreeDoorway(RoomIdx);
		if (DoorIdx != INDEX_NONE)
		{
			A.Pos = Rooms[RoomIdx].Doorways[DoorIdx].Pos;
			A.Normal = Rooms[RoomIdx].Doorways[DoorIdx].OutwardNormal;
			Rooms[RoomIdx].Doorways[DoorIdx].bUsed = true;
		}
		else
		{
			// No free doorway — synthesize a position from the room center + outward direction.
			const FVector2D Outward = (Rooms[RoomIdx].Center - Centroid).GetSafeNormal();
			A.Pos = Rooms[RoomIdx].Center;
			A.Normal = Outward.IsNearlyZero() ? (Rooms[RoomIdx].Center - EntrancePos).GetSafeNormal() : Outward;
			if (A.Normal.IsNearlyZero())
			{
				A.Normal = FVector2D(1.0f, 0.0f);
			}
		}
		return A;
	};

	// Deterministic sort comparator: farthest graph distance first; then farthest Euclidean from entrance; then lowest room index.
	auto SortFarFirst = [&](int32 RA, int32 RB) -> bool {
		const int32 DA = Dist.IsValidIndex(RA) ? Dist[RA] : -1;
		const int32 DB = Dist.IsValidIndex(RB) ? Dist[RB] : -1;
		if (DA != DB)
		{
			return DA > DB;
		}
		const float EA = FVector2D::Distance(Rooms[RA].Center, EntrancePos);
		const float EB = FVector2D::Distance(Rooms[RB].Center, EntrancePos);
		if (!FMath::IsNearlyEqual(EA, EB, 1.0f))
		{
			return EA > EB;
		}
		return RA < RB;
	};

	// --- Pass 1: MST leaves (degree 1) excluding entrance, sorted far→near ---
	TSet<int32> UsedRooms;
	UsedRooms.Add(EntranceRoomIdx);

	TArray<int32> Leaves;
	for (int32 r = 0; r < NumRooms; ++r)
	{
		if (r == EntranceRoomIdx)
		{
			continue;
		}
		const int32 Degree = MstAdj[r].Num();
		const int32 GraphDist = Dist[r];
		if (Degree == 1 && GraphDist > 0) // must be reachable and not the entrance
		{
			Leaves.Add(r);
		}
	}
	Leaves.Sort([&](int32 A, int32 B) { return SortFarFirst(A, B); });

	for (int32 r : Leaves)
	{
		if (OutAnchors.Num() >= RequiredCount)
		{
			break;
		}
		if (UsedRooms.Contains(r))
		{
			continue;
		}
		// Only select leaves that have a free doorway (PickBestFreeDoorway returns INDEX_NONE when all are used).
		if (PickBestFreeDoorway(r) == INDEX_NONE)
		{
			continue;
		}
		OutAnchors.Add(MakeAnchor(r, Dist[r], false));
		UsedRooms.Add(r);

		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[ORG] SelectExitAnchors: leaf anchor[%d] room=%d graphDist=%d pos=(%.0f,%.0f)"),
			OutAnchors.Num() - 1,
			r,
			Dist[r],
			OutAnchors.Last().Pos.X,
			OutAnchors.Last().Pos.Y);
	}

	// --- Pass 2: fallback — any non-entrance room with a free doorway, sorted far→near ---
	if (OutAnchors.Num() < RequiredCount)
	{
		TArray<int32> FallbackRooms;
		for (int32 r = 0; r < NumRooms; ++r)
		{
			if (UsedRooms.Contains(r))
			{
				continue;
			}
			if (Dist[r] <= 0) // must be farther than entrance
			{
				continue;
			}
			if (PickBestFreeDoorway(r) == INDEX_NONE)
			{
				continue;
			}
			FallbackRooms.Add(r);
		}
		FallbackRooms.Sort([&](int32 A, int32 B) { return SortFarFirst(A, B); });

		for (int32 r : FallbackRooms)
		{
			if (OutAnchors.Num() >= RequiredCount)
			{
				break;
			}
			if (UsedRooms.Contains(r))
			{
				continue;
			}
			OutAnchors.Add(MakeAnchor(r, Dist[r], true));
			UsedRooms.Add(r);

			UE_LOG(LogRoguelikeGeometry,
				Verbose,
				TEXT("[ORG] SelectExitAnchors: fallback anchor[%d] room=%d graphDist=%d pos=(%.0f,%.0f)"),
				OutAnchors.Num() - 1,
				r,
				Dist[r],
				OutAnchors.Last().Pos.X,
				OutAnchors.Last().Pos.Y);
		}
	}

	OutShortfall = FMath::Max(0, RequiredCount - OutAnchors.Num());
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
