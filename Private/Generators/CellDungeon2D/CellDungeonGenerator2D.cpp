#include "Generators/CellDungeon2D/CellDungeonGenerator2D.h"

#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "Generators/CellDungeon2D/CellDungeonConfig.h"

#include "Algo/Reverse.h"
#include "Containers/Queue.h"

DEFINE_LOG_CATEGORY_STATIC(LogCellDungeon, Log, All);

// ============================================================================
// CellDungeonGenerator2D.cpp
//
// Implements the CellDungeon generator: discretize space into Voronoi cells,
// mark room-occupied cells, then route corridors ONLY through Empty/Corridor
// cells via cell-graph Dijkstra. Rooms are positioned by their DOORWAY (never
// by their center), so a single-doorway room is never a connectivity bottleneck
// — corridors branch through empty/corridor cells acting as graph junctions.
//
// Pure CPU, fully deterministic via the seeded FRandomStream (Rng).
// ============================================================================

namespace
{
	// --- Tunables --------------------------------------------------------------

	// Minimum / maximum number of Voronoi sites used for the substrate.
	constexpr int32 MinSites = 16;
	constexpr int32 MaxSites = 4096;

	// Lloyd relaxation iterations for an even cell distribution.
	constexpr int32 RelaxIterations = 8;

	// Extra spacing between rooms, in CorridorSize multiples, ADDED to the room bounding diagonal when
	// spacing the coarse layout. Guarantees a clear lane of empty fine cells between adjacent rooms so
	// footprints never touch and a corridor (with a 1-cell wall clearance each side) always fits.
	constexpr float RoomGapCells = 4.0f;

	// Dijkstra edge costs by destination cell state (Corridor cheaper so paths
	// merge into a shared network).
	constexpr float CorridorCost = 0.2f;
	constexpr float EmptyCost = 1.0f;

	// Anchor-walk acceptance cone half-angle (~60deg -> cos ~= 0.5).
	constexpr float AnchorConeCos = 0.5f;

	// --- Geometry helpers ------------------------------------------------------

	// Rotate a 2D vector by RotationDeg (CCW, +Y up math convention; consistent
	// throughout so determinism is preserved regardless of handedness).
	FVector2D Rotate2D(const FVector2D& V, const float RotationDeg)
	{
		const float Rad = FMath::DegreesToRadians(RotationDeg);
		const float C = FMath::Cos(Rad);
		const float S = FMath::Sin(Rad);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}

	// World-space CCW corners of a rotated room rectangle.
	void RoomRectCorners(const FVector2D& Center, const float RotationDeg, const FVector2D& Footprint, TArray<FVector2D>& OutCorners)
	{
		const float HX = Footprint.X * 0.5f;
		const float HY = Footprint.Y * 0.5f;

		const FVector2D Local[4] = {
			FVector2D(-HX, -HY),
			FVector2D(HX, -HY),
			FVector2D(HX, HY),
			FVector2D(-HX, HY),
		};

		OutCorners.Reset(4);
		for (int32 i = 0; i < 4; ++i)
		{
			OutCorners.Add(Center + Rotate2D(Local[i], RotationDeg));
		}
	}

	// Axis-aligned bounding box of a point set.
	FBox2D PolyAABB(const TArray<FVector2D>& Poly)
	{
		FBox2D Box(ForceInit);
		for (const FVector2D& P : Poly)
		{
			Box += P;
		}
		return Box;
	}

	// Projects a convex polygon onto an axis; returns [min,max].
	void ProjectPoly(const TArray<FVector2D>& Poly, const FVector2D& Axis, float& OutMin, float& OutMax)
	{
		OutMin = TNumericLimits<float>::Max();
		OutMax = TNumericLimits<float>::Lowest();
		for (const FVector2D& P : Poly)
		{
			const float D = FVector2D::DotProduct(P, Axis);
			OutMin = FMath::Min(OutMin, D);
			OutMax = FMath::Max(OutMax, D);
		}
	}

	// Convex-vs-convex overlap via the Separating Axis Theorem. Touching counts
	// as overlap (conservative / round-UP: we never want to under-mark a cell).
	bool ConvexOverlapSAT(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
	{
		if (A.Num() < 3 || B.Num() < 3)
		{
			return false;
		}

		const TArray<FVector2D>* Polys[2] = { &A, &B };
		for (int32 Pi = 0; Pi < 2; ++Pi)
		{
			const TArray<FVector2D>& Poly = *Polys[Pi];
			const int32				 N = Poly.Num();
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D Edge = Poly[(i + 1) % N] - Poly[i];
				FVector2D		Axis(-Edge.Y, Edge.X);
				const float		Len = Axis.Size();
				if (Len <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				Axis /= Len;

				float MinA, MaxA, MinB, MaxB;
				ProjectPoly(A, Axis, MinA, MaxA);
				ProjectPoly(B, Axis, MinB, MaxB);

				// Strict gap on this axis => separated (touching is overlap).
				if (MaxA < MinB - KINDA_SMALL_NUMBER || MaxB < MinA - KINDA_SMALL_NUMBER)
				{
					return false;
				}
			}
		}
		return true;
	}
} // namespace

// ============================================================================
// FCellDungeonConfig::ResolveMiddleQueue
// ============================================================================
TArray<int32> FCellDungeonConfig::ResolveMiddleQueue(FRandomStream& Rng) const
{
	TArray<int32> Queue;

	const int32 SlotCount = FMath::Max(0, TargetRoomCount - 2);
	if (SlotCount <= 0 || MiddleRooms.Num() == 0)
	{
		return Queue;
	}

	// Track how many of each middle type we've committed (respect Max).
	TArray<int32> Placed;
	Placed.Init(0, MiddleRooms.Num());

	auto CanPlace = [&](const int32 Idx) -> bool {
		const FCellRoomType& T = MiddleRooms[Idx];
		return (T.Max <= 0) || (Placed[Idx] < T.Max);
	};

	// Phase 1: honor Min requirements first (in declaration order, deterministic).
	for (int32 Idx = 0; Idx < MiddleRooms.Num() && Queue.Num() < SlotCount; ++Idx)
	{
		const FCellRoomType& T = MiddleRooms[Idx];
		for (int32 c = 0; c < T.Min && Queue.Num() < SlotCount; ++c)
		{
			if (!CanPlace(Idx))
			{
				break;
			}
			Queue.Add(Idx);
			Placed[Idx]++;
		}
	}

	// Phase 2: fill remaining slots by weighted random selection (capped by Max).
	while (Queue.Num() < SlotCount)
	{
		// Build the eligible weighted pool.
		int32 TotalWeight = 0;
		for (int32 Idx = 0; Idx < MiddleRooms.Num(); ++Idx)
		{
			if (CanPlace(Idx))
			{
				TotalWeight += FMath::Max(0, MiddleRooms[Idx].Weight);
			}
		}

		if (TotalWeight <= 0)
		{
			// Nothing eligible (all maxed or zero-weight). Stop early.
			break;
		}

		int32 Roll = Rng.RandRange(0, TotalWeight - 1);
		int32 Chosen = INDEX_NONE;
		for (int32 Idx = 0; Idx < MiddleRooms.Num(); ++Idx)
		{
			if (!CanPlace(Idx))
			{
				continue;
			}
			const int32 W = FMath::Max(0, MiddleRooms[Idx].Weight);
			if (Roll < W)
			{
				Chosen = Idx;
				break;
			}
			Roll -= W;
		}

		if (Chosen == INDEX_NONE)
		{
			break;
		}

		Queue.Add(Chosen);
		Placed[Chosen]++;
	}

	return Queue;
}

// ============================================================================
// Builder setters
// ============================================================================
UCellDungeonGenerator2D* UCellDungeonGenerator2D::SetSeed(const FString& InSeed)
{
	Seed = InSeed;
	return this;
}

UCellDungeonGenerator2D* UCellDungeonGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Bounds = InBounds;
	return this;
}

UCellDungeonGenerator2D* UCellDungeonGenerator2D::SetConfig(const FCellDungeonConfig& InConfig)
{
	Config = InConfig;
	return this;
}

// ============================================================================
// Substrate construction
// ============================================================================
int32 UCellDungeonGenerator2D::EstimateSiteCount() const
{
	const FVector2D Size = Bounds.GetSize();
	const float		Area = FMath::Abs(Size.X * Size.Y);
	const float		Cell = FMath::Max(1.f, Config.CorridorSize);
	const int32		Estimate = FMath::RoundToInt(Area / (Cell * Cell));
	return FMath::Clamp(Estimate, MinSites, MaxSites);
}

FVoronoiDiagram2D UCellDungeonGenerator2D::BuildSubstrate() const
{
	UVoronoiGenerator2D* Gen = NewObject<UVoronoiGenerator2D>();
	const int32			 NumSites = EstimateSiteCount();

	Gen->SetBounds(Bounds)->SetSeed(Seed)->SetMinSiteDistance(Config.CorridorSize * 0.7f)->SetRelaxationIterations(RelaxIterations);

	return Gen->GenerateRelaxed(NumSites);
}

// ============================================================================
// Footprint -> occupied cells (conservative SAT overlap)
// ============================================================================
void UCellDungeonGenerator2D::MarkRoomCells(FCellDungeonResult& Result, FCellPlacedRoom& Room, const int32 RoomIndex) const
{
	// Caller commits state; this only records OccupiedCells and writes state/index.
	for (const int32 CellIdx : Room.OccupiedCells)
	{
		if (Result.CellState.IsValidIndex(CellIdx))
		{
			Result.CellState[CellIdx] = ECellState::RoomOccupied;
			Result.CellRoomIndex[CellIdx] = RoomIndex;
		}
	}
}

// Compute (without committing) the set of cells a room footprint overlaps.
// File-local because the header's helpers don't expose a pure "compute" variant.
static void ComputeFootprintCells(const FVoronoiDiagram2D& Diagram, const FCellPlacedRoom& Room, const float CellSize, TArray<int32>& OutCells)
{
	OutCells.Reset();

	TArray<FVector2D> Rect;
	RoomRectCorners(Room.Center, Room.RotationDeg, Room.Footprint, Rect);

	// Restrict the candidate set by AABB expanded by ~1.5 cell sizes (perf).
	FBox2D			RectBox = PolyAABB(Rect);
	const FVector2D Pad(CellSize * 1.5f, CellSize * 1.5f);
	RectBox = RectBox.ExpandBy(Pad);

	for (int32 i = 0; i < Diagram.Cells.Num(); ++i)
	{
		const FVoronoiCell2D& Cell = Diagram.Cells[i];
		if (!Cell.bIsValid || Cell.Vertices.Num() < 3)
		{
			continue;
		}
		if (!RectBox.IsInside(Cell.SiteLocation))
		{
			continue;
		}
		if (ConvexOverlapSAT(Rect, Cell.Vertices))
		{
			OutCells.Add(i);
		}
	}
}

bool UCellDungeonGenerator2D::CanPlaceRoom(const FCellDungeonResult& Result, const FCellPlacedRoom& Room) const
{
	if (Room.OccupiedCells.Num() == 0)
	{
		// A room that covers no cells is degenerate / off-grid — reject.
		return false;
	}

	for (const int32 CellIdx : Room.OccupiedCells)
	{
		if (!Result.CellState.IsValidIndex(CellIdx))
		{
			return false;
		}
		const ECellState S = Result.CellState[CellIdx];
		if (S == ECellState::RoomOccupied || S == ECellState::Blocked)
		{
			return false;
		}
	}
	return true;
}

// ============================================================================
// Doorway resolution + anchor walk
// ============================================================================

// Walk from the doorway's containing cell along the outward dir until we leave
// the room's footprint cells; the first non-room cell is the anchor.
// Returns INDEX_NONE on dead-end or if we step into ANOTHER room's cell.
static int32 AnchorWalk(const FVoronoiDiagram2D& Diagram,
	const TArray<ECellState>&					 CellState,
	const TArray<int32>&						 OccupiedCells,
	const FVector2D&							 DoorWorld,
	const FVector2D&							 OutwardDir)
{
	const FVector2D Dir = OutwardDir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return INDEX_NONE;
	}

	int32 Cur = Diagram.FindCellContainingPoint(DoorWorld);
	if (Cur == INDEX_NONE)
	{
		Cur = Diagram.FindClosestCellBySite(DoorWorld);
	}
	if (Cur == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const TSet<int32> RoomSet(OccupiedCells);

	// Bounded walk (cell count is a safe upper bound on steps).
	const int32 MaxSteps = Diagram.Cells.Num() + 1;
	for (int32 Step = 0; Step < MaxSteps; ++Step)
	{
		if (!RoomSet.Contains(Cur))
		{
			// First cell outside the footprint. Must be a usable non-room cell.
			if (!CellState.IsValidIndex(Cur))
			{
				return INDEX_NONE;
			}
			const ECellState S = CellState[Cur];
			if (S == ECellState::RoomOccupied || S == ECellState::Blocked)
			{
				return INDEX_NONE;
			}
			return Cur;
		}

		// Still inside the room — step to the neighbour best aligned with Dir.
		const FVoronoiCell2D& CurCell = Diagram.Cells[Cur];
		int32				  Best = INDEX_NONE;
		float				  BestDot = AnchorConeCos;
		for (const int32 Nb : CurCell.Neighbors)
		{
			if (!Diagram.Cells.IsValidIndex(Nb) || !Diagram.Cells[Nb].bIsValid)
			{
				continue;
			}
			FVector2D	ToNb = Diagram.Cells[Nb].SiteLocation - CurCell.SiteLocation;
			const float L = ToNb.Size();
			if (L <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			ToNb /= L;
			const float Dot = FVector2D::DotProduct(ToNb, Dir);
			// Deterministic tie-break by lower CellIndex.
			if (Dot > BestDot || (FMath::IsNearlyEqual(Dot, BestDot) && (Best == INDEX_NONE || Nb < Best)))
			{
				BestDot = Dot;
				Best = Nb;
			}
		}

		if (Best == INDEX_NONE)
		{
			return INDEX_NONE; // Dead-end / cone empty.
		}
		Cur = Best;
	}

	return INDEX_NONE;
}

void UCellDungeonGenerator2D::ResolveDoorways(const FCellDungeonResult& Result, FCellPlacedRoom& Room) const
{
	const int32 NumDoors = Room.DoorwayPos.Num();
	Room.DoorwayDir.SetNum(NumDoors);
	Room.DoorwayAnchorCell.SetNum(NumDoors);
	if (Room.DoorwayUsed.Num() != NumDoors)
	{
		Room.DoorwayUsed.Init(false, NumDoors);
	}

	for (int32 d = 0; d < NumDoors; ++d)
	{
		Room.DoorwayAnchorCell[d] = AnchorWalk(Result.Diagram, Result.CellState, Room.OccupiedCells, Room.DoorwayPos[d], Room.DoorwayDir[d]);
	}
}

// Build a FCellPlacedRoom for a room type at a given center/rotation, filling in
// world-space doorway pos/dir and computed footprint cells. (Anchors resolved
// separately via ResolveDoorways once the room's cells are known and committed.)
static FCellPlacedRoom MakeCandidateRoom(const FCellRoomType& Type,
	const FVector2D&										  Center,
	const float												  RotationDeg,
	const int32												  TypeIndex,
	const FVoronoiDiagram2D&								  Diagram,
	const float												  CellSize)
{
	FCellPlacedRoom Room;
	Room.Center = Center;
	Room.RotationDeg = RotationDeg;
	Room.Footprint = Type.Footprint;
	Room.TypeIndex = TypeIndex;

	const int32 NumDoors = Type.Doorways.Num();
	Room.DoorwayPos.SetNum(NumDoors);
	Room.DoorwayDir.SetNum(NumDoors);
	Room.DoorwayUsed.Init(false, NumDoors);
	for (int32 d = 0; d < NumDoors; ++d)
	{
		Room.DoorwayPos[d] = Center + Rotate2D(Type.Doorways[d].LocalPosition, RotationDeg);
		Room.DoorwayDir[d] = Rotate2D(Type.Doorways[d].LocalOutwardDir, RotationDeg).GetSafeNormal();
	}

	ComputeFootprintCells(Diagram, Room, CellSize, Room.OccupiedCells);
	return Room;
}

// ============================================================================
// Pathfinding (Dijkstra over the cell graph)
// ============================================================================

// Multi-target Dijkstra. Allowed traversal: Empty / Corridor cells; plus any
// cell explicitly listed in AllowedExtra (e.g. the source anchor that may be
// the start node). Forbidden: RoomOccupied / Blocked. Returns ordered path
// (start..nearest goal), or empty if unreachable. Deterministic tie-break by
// CellIndex.
static TArray<int32> DijkstraToSet(const FVoronoiDiagram2D& Diagram,
	const TArray<ECellState>&								CellState,
	const int32												StartCell,
	const TSet<int32>&										Goals,
	const TSet<int32>&										AllowedExtra)
{
	TArray<int32> Path;
	if (!Diagram.Cells.IsValidIndex(StartCell) || Goals.Num() == 0)
	{
		return Path;
	}

	const int32 N = Diagram.Cells.Num();

	auto IsTraversable = [&](const int32 Idx) -> bool {
		if (!CellState.IsValidIndex(Idx))
		{
			return false;
		}
		if (AllowedExtra.Contains(Idx))
		{
			return true;
		}
		const ECellState S = CellState[Idx];
		return S == ECellState::Empty || S == ECellState::Corridor;
	};

	auto EnterCost = [&](const int32 Idx) -> float {
		if (CellState.IsValidIndex(Idx) && CellState[Idx] == ECellState::Corridor)
		{
			return CorridorCost;
		}
		return EmptyCost;
	};

	if (!IsTraversable(StartCell))
	{
		return Path;
	}

	TArray<float> Dist;
	Dist.Init(TNumericLimits<float>::Max(), N);
	TArray<int32> Prev;
	Prev.Init(INDEX_NONE, N);
	TArray<bool> Visited;
	Visited.Init(false, N);

	Dist[StartCell] = 0.f;

	// Simple O(N^2) Dijkstra (cell counts are bounded and modest). Deterministic.
	for (int32 Iter = 0; Iter < N; ++Iter)
	{
		int32 U = INDEX_NONE;
		float Best = TNumericLimits<float>::Max();
		for (int32 i = 0; i < N; ++i)
		{
			if (!Visited[i] && Dist[i] < Best)
			{
				Best = Dist[i];
				U = i;
			}
		}

		if (U == INDEX_NONE)
		{
			break; // Remaining nodes unreachable.
		}

		Visited[U] = true;

		if (Goals.Contains(U))
		{
			// Reconstruct path U -> ... -> StartCell, then reverse.
			int32 Cur = U;
			while (Cur != INDEX_NONE)
			{
				Path.Add(Cur);
				Cur = Prev[Cur];
			}
			Algo::Reverse(Path);
			return Path;
		}

		const FVoronoiCell2D& Cell = Diagram.Cells[U];

		// Visit neighbours in CellIndex order for deterministic relaxation.
		TArray<int32> Neighbours = Cell.Neighbors;
		Neighbours.Sort();
		for (const int32 V : Neighbours)
		{
			if (!Diagram.Cells.IsValidIndex(V) || Visited[V])
			{
				continue;
			}
			// A goal cell is always reachable as a terminal even if it's a room
			// anchor; otherwise it must be traversable.
			if (!IsTraversable(V) && !Goals.Contains(V))
			{
				continue;
			}
			const float NewDist = Dist[U] + EnterCost(V);
			if (NewDist < Dist[V] - KINDA_SMALL_NUMBER)
			{
				Dist[V] = NewDist;
				Prev[V] = U;
			}
		}
	}

	return Path; // empty
}

TArray<int32> UCellDungeonGenerator2D::FindCellPath(const FCellDungeonResult& Result, const int32 StartCell, const int32 GoalCell) const
{
	TSet<int32> Goals;
	Goals.Add(GoalCell);
	TSet<int32> Extra;
	Extra.Add(StartCell);
	Extra.Add(GoalCell);
	return DijkstraToSet(Result.Diagram, Result.CellState, StartCell, Goals, Extra);
}

// BFS over the corridor/cell network to find graph distance (hop count) from a
// source cell to every reachable cell. Used to choose the graph-farthest exit
// for the end room.
static void BfsHopDistances(const FVoronoiDiagram2D& Diagram, const TArray<ECellState>& CellState, const int32 SourceCell, TArray<int32>& OutHops)
{
	const int32 N = Diagram.Cells.Num();
	OutHops.Init(INDEX_NONE, N);
	if (!Diagram.Cells.IsValidIndex(SourceCell))
	{
		return;
	}

	auto IsTraversable = [&](const int32 Idx) -> bool {
		if (!CellState.IsValidIndex(Idx))
		{
			return false;
		}
		const ECellState S = CellState[Idx];
		return S == ECellState::Empty || S == ECellState::Corridor;
	};

	TQueue<int32> Q;
	OutHops[SourceCell] = 0;
	Q.Enqueue(SourceCell);

	int32 Cur;
	while (Q.Dequeue(Cur))
	{
		TArray<int32> Neighbours = Diagram.Cells[Cur].Neighbors;
		Neighbours.Sort();
		for (const int32 V : Neighbours)
		{
			if (!Diagram.Cells.IsValidIndex(V) || OutHops[V] != INDEX_NONE)
			{
				continue;
			}
			if (!IsTraversable(V))
			{
				continue;
			}
			OutHops[V] = OutHops[Cur] + 1;
			Q.Enqueue(V);
		}
	}
}

// ============================================================================
// Room placement + corridor routing (driven from PlaceRooms / RouteCorridors,
// but the bulk of the work — being inherently coupled — lives in Generate()).
// ============================================================================

// PlaceRooms/RouteCorridors are provided to satisfy the header contract. The
// real placement+routing is interleaved (a room can only be accepted once its
// connecting corridor is found), so Generate() performs the combined pass and
// these two are thin wrappers used for clarity / future separation.
bool UCellDungeonGenerator2D::PlaceRooms(FCellDungeonResult& Result)
{
	// Placement is performed inline within Generate() (coupled with routing).
	// Returning true here keeps the contract; Generate() sets bValid itself.
	return Result.Rooms.Num() > 0;
}

bool UCellDungeonGenerator2D::RouteCorridors(FCellDungeonResult& Result)
{
	// Routing is performed inline within Generate() (coupled with placement).
	return Result.CorridorPaths.Num() > 0 || Result.Rooms.Num() <= 1;
}

// ============================================================================
// Generate
// ============================================================================
FCellDungeonResult UCellDungeonGenerator2D::Generate()
{
	FCellDungeonResult Result;
	Result.RequestedRoomCount = Config.TargetRoomCount;

	// Deterministic seeding.
	Rng.Initialize(static_cast<int32>(GetTypeHash(Seed)));

	const float CellSize = FMath::Max(1.f, Config.CorridorSize);

	// ------------------------------------------------------------------------
	// TWO-DIAGRAM pipeline:
	//   (1) a COARSE Voronoi lays out the room graph (one cell per room, spaced
	//       so footprints can never overlap at any rotation). It is discarded
	//       once room centers/rotations are decided.
	//   (2) the FINE Voronoi (BuildSubstrate) is the actual cell substrate the
	//       rooms rasterize onto and corridors route through.
	// Doorways are not used for connectivity; we connect corridor SEED cells.
	// ------------------------------------------------------------------------

	// A room assigned to a coarse blob cell, before fine rasterization.
	struct FPlannedRoom
	{
		FVector2D Center = FVector2D::ZeroVector; // coarse cell site
		float	  RotationDeg = 0.f;
		int32	  Marker = INDEX_NONE; // -2 start, -3 end, >=0 middle type index
	};

	// --- 1. COARSE placement diagram ----------------------------------------

	// Coarse cell size = the largest room BOUNDING DIAGONAL across all room types.
	// MinSiteDistance >= this diagonal guarantees that two adjacent room footprints
	// can never overlap, regardless of rotation.
	auto Diagonal = [](const FVector2D& Fp) -> float { return FMath::Sqrt(Fp.X * Fp.X + Fp.Y * Fp.Y); };

	float CoarseCell = FMath::Max(Diagonal(Config.StartRoom.Footprint), Diagonal(Config.EndRoom.Footprint));
	for (const FCellRoomType& MidType : Config.MiddleRooms)
	{
		CoarseCell = FMath::Max(CoarseCell, Diagonal(MidType.Footprint));
	}
	CoarseCell = FMath::Max(CoarseCell, CellSize);

	// Space sites by the diagonal PLUS a corridor-cell gap: adjacent footprints then never touch (the
	// conservative SAT would otherwise reject the second room) and a corridor lane always fits between them.
	const float CoarseSpacing = CoarseCell + RoomGapCells * CellSize;

	const FVector2D BoundsSize = Bounds.GetSize();
	const float		BoundsArea = BoundsSize.X * BoundsSize.Y;
	const int32		CoarseSites = FMath::Clamp(FMath::RoundToInt(FMath::Abs(BoundsArea) / (CoarseSpacing * CoarseSpacing)), MinSites, MaxSites);

	UVoronoiGenerator2D* CoarseGen = NewObject<UVoronoiGenerator2D>();
	CoarseGen->SetBounds(Bounds)->SetSeed(Seed + TEXT("_coarse"))->SetMinSiteDistance(CoarseSpacing)->SetRelaxationIterations(RelaxIterations);
	const FVoronoiDiagram2D Coarse = CoarseGen->GenerateRelaxed(CoarseSites);

	const int32 CoarseN = Coarse.Cells.Num();
	if (CoarseN == 0)
	{
		UE_LOG(LogCellDungeon, Warning, TEXT("CellDungeon: empty coarse Voronoi; aborting."));
		Result.bValid = false;
		return Result;
	}

	auto IsUsableCoarse = [&](const int32 Idx) -> bool {
		return Coarse.Cells.IsValidIndex(Idx) && Coarse.Cells[Idx].bIsValid && !Coarse.Cells[Idx].bIsBoundaryCell;
	};

	// Center usable cell.
	const FVector2D CoarseCenter = Bounds.GetCenter();
	int32			CenterCell = Coarse.FindCellContainingPoint(CoarseCenter);
	if (!IsUsableCoarse(CenterCell))
	{
		CenterCell = INDEX_NONE;
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 i = 0; i < CoarseN; ++i)
		{
			if (!IsUsableCoarse(i))
			{
				continue;
			}
			const float DistSq = FVector2D::DistSquared(Coarse.Cells[i].SiteLocation, CoarseCenter);
			// Deterministic tie-break by lower CellIndex (strictly-less keeps the first found).
			if (DistSq < BestDistSq - KINDA_SMALL_NUMBER)
			{
				BestDistSq = DistSq;
				CenterCell = i;
			}
		}
	}

	if (CenterCell == INDEX_NONE)
	{
		UE_LOG(LogCellDungeon, Warning, TEXT("CellDungeon: no usable coarse cell near center; aborting."));
		Result.bValid = false;
		return Result;
	}

	// DFS-grow a connected blob of up to TargetRoomCount usable cells from center.
	TArray<int32> Blob;
	{
		TSet<int32>	  InBlob;
		TArray<int32> Stack;
		Stack.Push(CenterCell);
		InBlob.Add(CenterCell);
		Blob.Add(CenterCell);

		while (Stack.Num() > 0 && Blob.Num() < Config.TargetRoomCount)
		{
			const int32			  Cur = Stack.Last();
			const FVoronoiCell2D& Cell = Coarse.Cells[Cur];

			// Gather usable, not-yet-in-blob neighbours; sort by CellIndex (determinism).
			TArray<int32> Candidates;
			for (const int32 Nb : Cell.Neighbors)
			{
				if (IsUsableCoarse(Nb) && !InBlob.Contains(Nb))
				{
					Candidates.Add(Nb);
				}
			}

			if (Candidates.Num() == 0)
			{
				Stack.Pop();
				continue;
			}

			Candidates.Sort();
			const int32 Pick = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
			InBlob.Add(Pick);
			Blob.Add(Pick);
			Stack.Push(Pick);
		}
	}

	// --- 2. ASSIGN rooms to blob cells --------------------------------------

	// Per-blob-cell marker; INDEX_NONE means unassigned (left out when queue runs short).
	TArray<int32> CellMarker;
	CellMarker.Init(INDEX_NONE, Blob.Num());

	// Map blob cell -> blob slot for neighbour lookups.
	TMap<int32, int32> CellToSlot;
	for (int32 s = 0; s < Blob.Num(); ++s)
	{
		CellToSlot.Add(Blob[s], s);
	}

	// Blob[0] => START.
	CellMarker[0] = -2;

	int32 EndSlot = INDEX_NONE;
	if (Blob.Num() >= 2)
	{
		// Build a coarse cell-state where blob cells are Empty and the rest Blocked,
		// then BFS hop distances from Blob[0] to find the graph-farthest blob cell.
		TArray<ECellState> CoarseState;
		CoarseState.Init(ECellState::Blocked, CoarseN);
		for (const int32 BCell : Blob)
		{
			if (CoarseState.IsValidIndex(BCell))
			{
				CoarseState[BCell] = ECellState::Empty;
			}
		}

		TArray<int32> Hops;
		BfsHopDistances(Coarse, CoarseState, Blob[0], Hops);

		int32 BestHops = -1;
		for (int32 s = 1; s < Blob.Num(); ++s)
		{
			const int32 H = Hops.IsValidIndex(Blob[s]) ? Hops[Blob[s]] : -1;
			// Max hops; tie -> lower CellIndex.
			if (H > BestHops || (H == BestHops && EndSlot != INDEX_NONE && Blob[s] < Blob[EndSlot]))
			{
				BestHops = H;
				EndSlot = s;
			}
		}

		if (EndSlot != INDEX_NONE)
		{
			CellMarker[EndSlot] = -3;
		}
	}

	// Remaining blob cells => middles, avoiding repeats in adjacent cells.
	{
		TArray<int32> MiddleQueue = Config.ResolveMiddleQueue(Rng);
		TArray<bool>  Used;
		Used.Init(false, MiddleQueue.Num());

		for (int32 s = 0; s < Blob.Num(); ++s)
		{
			if (CellMarker[s] != INDEX_NONE)
			{
				continue; // start / end already assigned
			}

			// Collect middle types of already-assigned coarse NEIGHBOURS.
			TSet<int32> NeighbourTypes;
			for (const int32 Nb : Coarse.Cells[Blob[s]].Neighbors)
			{
				const int32* NbSlot = CellToSlot.Find(Nb);
				if (NbSlot && CellMarker.IsValidIndex(*NbSlot) && CellMarker[*NbSlot] >= 0)
				{
					NeighbourTypes.Add(CellMarker[*NbSlot]);
				}
			}

			// First still-unused queue entry whose type differs from all neighbours.
			int32 ChosenQ = INDEX_NONE;
			for (int32 q = 0; q < MiddleQueue.Num(); ++q)
			{
				if (!Used[q] && !NeighbourTypes.Contains(MiddleQueue[q]))
				{
					ChosenQ = q;
					break;
				}
			}
			// If none qualifies, take the next unused entry.
			if (ChosenQ == INDEX_NONE)
			{
				for (int32 q = 0; q < MiddleQueue.Num(); ++q)
				{
					if (!Used[q])
					{
						ChosenQ = q;
						break;
					}
				}
			}

			if (ChosenQ == INDEX_NONE)
			{
				break; // queue exhausted; leave remaining blob cells unassigned
			}

			Used[ChosenQ] = true;
			CellMarker[s] = MiddleQueue[ChosenQ];
		}
	}

	// Resolve a marker to its room type description.
	auto MarkerToType = [&](const int32 Marker) -> const FCellRoomType& {
		if (Marker == -2)
		{
			return Config.StartRoom;
		}
		if (Marker == -3)
		{
			return Config.EndRoom;
		}
		return Config.MiddleRooms[Marker];
	};

	// --- 3. ROTATE each assigned room toward an occupied neighbour ----------
	TArray<FPlannedRoom> Planned;
	{
		TSet<int32> Processed; // coarse cells already given a rotation (= "occupied")
		for (int32 s = 0; s < Blob.Num(); ++s)
		{
			if (CellMarker[s] == INDEX_NONE)
			{
				continue;
			}

			const int32			 ThisCell = Blob[s];
			const FVector2D		 ThisSite = Coarse.Cells[ThisCell].SiteLocation;
			const FCellRoomType& Type = MarkerToType(CellMarker[s]);

			// Choose a target neighbour SITE direction.
			int32 ChosenNb = INDEX_NONE;		   // prefer assigned AND processed
			int32 FallbackAssignedNb = INDEX_NONE; // any assigned blob neighbour
			for (const int32 Nb : Coarse.Cells[ThisCell].Neighbors)
			{
				const int32* NbSlot = CellToSlot.Find(Nb);
				if (!NbSlot || !CellMarker.IsValidIndex(*NbSlot) || CellMarker[*NbSlot] == INDEX_NONE)
				{
					continue;
				}
				if (FallbackAssignedNb == INDEX_NONE || Nb < FallbackAssignedNb)
				{
					FallbackAssignedNb = Nb;
				}
				if (Processed.Contains(Nb) && (ChosenNb == INDEX_NONE || Nb < ChosenNb))
				{
					ChosenNb = Nb;
				}
			}
			if (ChosenNb == INDEX_NONE)
			{
				ChosenNb = FallbackAssignedNb;
			}

			FVector2D TargetDir;
			if (ChosenNb != INDEX_NONE)
			{
				TargetDir = (Coarse.Cells[ChosenNb].SiteLocation - ThisSite).GetSafeNormal();
			}
			else
			{
				TargetDir = (Bounds.GetCenter() - ThisSite).GetSafeNormal();
			}
			if (TargetDir.IsNearlyZero())
			{
				TargetDir = FVector2D(1.f, 0.f);
			}

			// Rotate so doorway 0's WORLD outward dir == TargetDir.
			float Rot = 0.f;
			if (Type.Doorways.Num() > 0)
			{
				const FVector2D Ld = Type.Doorways[0].LocalOutwardDir;
				const float		TargetAng = FMath::RadiansToDegrees(FMath::Atan2(TargetDir.Y, TargetDir.X));
				const float		LocalAng = FMath::RadiansToDegrees(FMath::Atan2(Ld.Y, Ld.X));
				Rot = TargetAng - LocalAng;
			}

			FPlannedRoom PR;
			PR.Center = ThisSite;
			PR.RotationDeg = Rot;
			PR.Marker = CellMarker[s];
			Planned.Add(PR);

			Processed.Add(ThisCell);
		}
	}

	// --- 4. FINE diagram + rasterize ----------------------------------------
	Result.Diagram = BuildSubstrate();
	const int32 N = Result.Diagram.Cells.Num();
	if (N == 0)
	{
		UE_LOG(LogCellDungeon, Warning, TEXT("CellDungeon: empty Voronoi substrate; aborting."));
		Result.bValid = false;
		return Result;
	}

	Result.CellState.Init(ECellState::Empty, N);
	Result.CellRoomIndex.Init(INDEX_NONE, N);
	for (int32 i = 0; i < N; ++i)
	{
		const FVoronoiCell2D& Cell = Result.Diagram.Cells[i];
		if (!Cell.bIsValid || Cell.bIsBoundaryCell)
		{
			Result.CellState[i] = ECellState::Blocked;
		}
	}

	// Commit rooms onto the fine grid (blob order).
	for (const FPlannedRoom& PR : Planned)
	{
		const FCellRoomType& Type = MarkerToType(PR.Marker);
		FCellPlacedRoom		 Cand = MakeCandidateRoom(Type, PR.Center, PR.RotationDeg, PR.Marker, Result.Diagram, CellSize);

		if (!CanPlaceRoom(Result, Cand))
		{
			UE_LOG(LogCellDungeon, Verbose, TEXT("CellDungeon: could not commit planned room (marker=%d); skipping."), PR.Marker);
			continue;
		}

		const int32 Idx = Result.Rooms.Num();
		MarkRoomCells(Result, Cand, Idx);
		Result.Rooms.Add(Cand);

		if (PR.Marker == -2)
		{
			Result.StartRoomIndex = Idx;
		}
		else if (PR.Marker == -3)
		{
			Result.EndRoomIndex = Idx;
		}
	}

	// Resolve doorway anchors against the FINAL committed state (second pass so a
	// later room's footprint can't invalidate an earlier room's anchor).
	for (int32 r = 0; r < Result.Rooms.Num(); ++r)
	{
		ResolveDoorways(Result, Result.Rooms[r]);
	}

	// --- 5. CONNECTIVITY via corridor seeds ---------------------------------

	// Pick a SEED cell per room (first doorway whose anchor is valid + Empty/Corridor),
	// mark it Corridor, and collect all seeds.
	TArray<int32> Seeds;
	for (int32 r = 0; r < Result.Rooms.Num(); ++r)
	{
		const FCellPlacedRoom& Room = Result.Rooms[r];
		int32				   SeedCell = INDEX_NONE;
		for (int32 d = 0; d < Room.DoorwayAnchorCell.Num(); ++d)
		{
			const int32 Anchor = Room.DoorwayAnchorCell[d];
			if (Anchor == INDEX_NONE || !Result.CellState.IsValidIndex(Anchor))
			{
				continue;
			}
			const ECellState S = Result.CellState[Anchor];
			if (S == ECellState::Empty || S == ECellState::Corridor)
			{
				SeedCell = Anchor;
				break;
			}
		}

		if (SeedCell == INDEX_NONE)
		{
			UE_LOG(LogCellDungeon, Verbose, TEXT("CellDungeon: room %d has no valid doorway anchor; may be unreachable."), r);
			continue;
		}

		Result.CellState[SeedCell] = ECellState::Corridor;
		Result.CorridorSeeds.Add(SeedCell);
		Seeds.Add(SeedCell);
	}

	// Enforce a 1-cell gap between corridors and room walls: a corridor may only TOUCH a room at its seed
	// cell. Temporarily Block every Empty cell adjacent to a room (the buffer ring) so routing keeps one
	// cell clear of every wall; seeds are already Corridor (not Empty) so they stay usable as endpoints.
	// Restored to Empty after routing (corridors never enter the ring, so nothing there becomes Corridor).
	TArray<int32> BufferCells;
	for (int32 i = 0; i < N; ++i)
	{
		if (Result.CellState[i] != ECellState::Empty)
		{
			continue;
		}
		for (const int32 Nb : Result.Diagram.Cells[i].Neighbors)
		{
			if (Result.CellState.IsValidIndex(Nb) && Result.CellState[Nb] == ECellState::RoomOccupied)
			{
				BufferCells.Add(i);
				Result.CellState[i] = ECellState::Blocked;
				break;
			}
		}
	}

	// Connect ALL seeds into one component. Sort for determinism.
	Seeds.Sort();

	TSet<int32> Net;
	if (Seeds.Num() > 0)
	{
		Net.Add(Seeds[0]);
	}

	for (int32 i = 1; i < Seeds.Num(); ++i)
	{
		const int32 Seed0 = Seeds[i];
		if (Net.Contains(Seed0))
		{
			continue; // already absorbed by an earlier path
		}

		TSet<int32> Extra = Net;
		Extra.Add(Seed0);

		TArray<int32> Path = DijkstraToSet(Result.Diagram, Result.CellState, Seed0, Net, Extra);
		if (Path.Num() == 0)
		{
			UE_LOG(LogCellDungeon, Verbose, TEXT("CellDungeon: seed cell %d unreachable from network."), Seed0);
			continue;
		}

		for (const int32 PCell : Path)
		{
			if (Result.CellState.IsValidIndex(PCell) && Result.CellState[PCell] == ECellState::Empty)
			{
				Result.CellState[PCell] = ECellState::Corridor;
			}
			Net.Add(PCell);
		}
		Result.CorridorPaths.Add(Path);
	}

	// Restore the buffer ring to Empty (corridors never entered it; seeds stayed Corridor).
	for (const int32 BufCell : BufferCells)
	{
		if (Result.CellState.IsValidIndex(BufCell) && Result.CellState[BufCell] == ECellState::Blocked)
		{
			Result.CellState[BufCell] = ECellState::Empty;
		}
	}

	// --- 6. Validity ---------------------------------------------------------
	// Thin wrappers (kept for the header contract); they don't change state.
	PlaceRooms(Result);
	RouteCorridors(Result);

	const int32 ExpectedRooms = Config.TargetRoomCount;
	const int32 ActualRooms = Result.Rooms.Num();
	const bool	bHaveStart = Result.StartRoomIndex != INDEX_NONE;
	const bool	bHaveEnd = Result.EndRoomIndex != INDEX_NONE;

	Result.bValid = bHaveStart && bHaveEnd && (ActualRooms >= ExpectedRooms);

	if (!Result.bValid)
	{
		UE_LOG(LogCellDungeon,
			Log,
			TEXT("CellDungeon: shortfall — placed %d/%d rooms (start=%d end=%d). Layout still returned for inspection."),
			ActualRooms,
			ExpectedRooms,
			bHaveStart ? 1 : 0,
			bHaveEnd ? 1 : 0);
	}
	else
	{
		UE_LOG(LogCellDungeon,
			Log,
			TEXT("CellDungeon: generated %d rooms, %d corridors on %d cells (seed='%s')."),
			ActualRooms,
			Result.CorridorPaths.Num(),
			N,
			*Seed);
	}

	return Result;
}
