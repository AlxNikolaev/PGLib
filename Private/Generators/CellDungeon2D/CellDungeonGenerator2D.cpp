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

	// Doorway-relative placement: starting gap (in CorridorSize multiples) and
	// how far we widen it on retry, plus retry budget.
	constexpr float StartGapMul = 1.5f;
	constexpr float GapStepMul = 1.0f;
	constexpr int32 MaxGapTries = 6;

	// Dijkstra edge costs by destination cell state (Corridor cheaper so paths
	// merge into a shared network).
	constexpr float CorridorCost = 0.2f;
	constexpr float EmptyCost = 1.0f;

	// Reject corridors that wander far relative to straight-line distance.
	constexpr float MaxPathDetour = 2.5f;

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
// Open-exit bookkeeping
// ============================================================================
namespace
{
	// One unused doorway available as a corridor attachment point.
	struct FOpenExit
	{
		int32	  RoomIndex = INDEX_NONE;
		int32	  DoorwayIndex = INDEX_NONE;
		FVector2D Dir = FVector2D::ZeroVector;			// world outward
		FVector2D AnchorCenter = FVector2D::ZeroVector; // anchor cell site
		int32	  AnchorCell = INDEX_NONE;
	};
} // namespace

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

	// --- 1. Substrate --------------------------------------------------------
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

	// --- 2. Resolve placement plan ------------------------------------------
	const TArray<int32> MiddleQueue = Config.ResolveMiddleQueue(Rng);

	// Helper: commit a fully-validated room into the result, returning its index.
	auto CommitRoom = [&](FCellPlacedRoom& Room) -> int32 {
		const int32 RoomIdx = Result.Rooms.Num();
		MarkRoomCells(Result, Room, RoomIdx);
		ResolveDoorways(Result, Room);
		Result.Rooms.Add(Room);
		return RoomIdx;
	};

	// Helper: push a room's unused, anchor-valid doorways onto the open-exit list.
	TArray<FOpenExit> OpenExits;
	auto			  PushExits = [&](const int32 RoomIdx) {
		 const FCellPlacedRoom& Room = Result.Rooms[RoomIdx];
		 for (int32 d = 0; d < Room.DoorwayDir.Num(); ++d)
		 {
			 if (Room.DoorwayUsed.IsValidIndex(d) && Room.DoorwayUsed[d])
			 {
				 continue;
			 }
			 const int32 Anchor = Room.DoorwayAnchorCell.IsValidIndex(d) ? Room.DoorwayAnchorCell[d] : INDEX_NONE;
			 if (Anchor == INDEX_NONE || !Result.CellState.IsValidIndex(Anchor))
			 {
				 continue;
			 }
			 if (Result.CellState[Anchor] != ECellState::Empty && Result.CellState[Anchor] != ECellState::Corridor)
			 {
				 continue;
			 }
			 FOpenExit Exit;
			 Exit.RoomIndex = RoomIdx;
			 Exit.DoorwayIndex = d;
			 Exit.Dir = Room.DoorwayDir[d];
			 Exit.AnchorCell = Anchor;
			 Exit.AnchorCenter = Result.Diagram.Cells[Anchor].SiteLocation;
			 OpenExits.Add(Exit);
		 }
	};

	// Build corridor-cell frontiers: every Corridor cell adjacent to an Empty cell is a branch point the
	// network can grow a new room off of. This is what lets single-doorway rooms keep connecting after their
	// host rooms' doors are consumed — corridors have unlimited branch capacity, so the frontier never dries
	// up while empty space remains next to the network. Deterministic (cell-index order).
	auto AppendCorridorFrontiers = [&](TArray<FOpenExit>& Out) {
		for (int32 c = 0; c < N; ++c)
		{
			if (Result.CellState[c] != ECellState::Corridor)
			{
				continue;
			}
			const FVoronoiCell2D& Cell = Result.Diagram.Cells[c];
			for (const int32 Nb : Cell.Neighbors)
			{
				if (!Result.CellState.IsValidIndex(Nb) || Result.CellState[Nb] != ECellState::Empty)
				{
					continue;
				}
				FOpenExit F;
				F.RoomIndex = INDEX_NONE;
				F.DoorwayIndex = INDEX_NONE;
				F.AnchorCell = c;
				F.AnchorCenter = Cell.SiteLocation;
				F.Dir = (Result.Diagram.Cells[Nb].SiteLocation - Cell.SiteLocation).GetSafeNormal();
				Out.Add(F);
			}
		}
	};

	// Remove a consumed room open-exit (matched by room + doorway) from the persistent OpenExits list.
	// Corridor frontiers carry RoomIndex == INDEX_NONE and own no exit, so they are a no-op here.
	auto ConsumeRoomExit = [&](const FOpenExit& Used) {
		if (Used.RoomIndex == INDEX_NONE)
		{
			return;
		}
		OpenExits.RemoveAll([&](const FOpenExit& E) { return E.RoomIndex == Used.RoomIndex && E.DoorwayIndex == Used.DoorwayIndex; });
	};

	// --- 3. Place START at the center ---------------------------------------
	{
		const FVector2D Center = Bounds.GetCenter();

		bool			bPlaced = false;
		FCellPlacedRoom Best;
		const float		Rotations[4] = { 0.f, 90.f, 180.f, 270.f };

		for (const float Rot : Rotations)
		{
			FCellPlacedRoom Cand = MakeCandidateRoom(Config.StartRoom, Center, Rot, -2, Result.Diagram, CellSize);
			if (!CanPlaceRoom(Result, Cand))
			{
				continue;
			}
			// Need at least one doorway whose anchor walk lands in an Empty cell.
			// Temporarily resolve anchors against current state.
			TArray<int32> Anchors;
			Anchors.SetNum(Cand.DoorwayPos.Num());
			bool bAnyAnchor = false;
			for (int32 d = 0; d < Cand.DoorwayPos.Num(); ++d)
			{
				Anchors[d] = AnchorWalk(Result.Diagram, Result.CellState, Cand.OccupiedCells, Cand.DoorwayPos[d], Cand.DoorwayDir[d]);
				if (Anchors[d] != INDEX_NONE && Result.CellState.IsValidIndex(Anchors[d]) && Result.CellState[Anchors[d]] == ECellState::Empty)
				{
					bAnyAnchor = true;
				}
			}
			if (bAnyAnchor)
			{
				Best = Cand;
				bPlaced = true;
				break;
			}
		}

		if (!bPlaced)
		{
			UE_LOG(LogCellDungeon, Warning, TEXT("CellDungeon: failed to place START room; aborting."));
			Result.bValid = false;
			return Result;
		}

		const int32 StartIdx = CommitRoom(Best);
		Result.StartRoomIndex = StartIdx;
		PushExits(StartIdx);
	}

	// --- 4. GROW: place each middle room, doorway-relative, then connect -----
	int32 PlacedMiddles = 0;
	for (const int32 MiddleTypeIdx : MiddleQueue)
	{
		if (!Config.MiddleRooms.IsValidIndex(MiddleTypeIdx))
		{
			continue;
		}
		const FCellRoomType& Type = Config.MiddleRooms[MiddleTypeIdx];

		bool bRoomPlaced = false;

		// Frontiers = room open-exits FIRST (prefer growing off real doorways), then corridor-cell branch
		// points (so single-doorway networks keep growing). Rebuilt per room because corridors grow.
		TArray<FOpenExit> Sources = OpenExits;
		AppendCorridorFrontiers(Sources);

		const int32 ExitCount = Sources.Num();
		for (int32 eOff = 0; eOff < ExitCount && !bRoomPlaced; ++eOff)
		{
			const FOpenExit Source = Sources[eOff]; // deterministic order

			if (Source.AnchorCell == INDEX_NONE || !Result.CellState.IsValidIndex(Source.AnchorCell))
			{
				continue;
			}

			// Choose the candidate rotation whose entry doorway best opposes the
			// source exit direction (world dot ~= -Source.Dir).
			const float Rotations[4] = { 0.f, 90.f, 180.f, 270.f };

			for (int32 GapTry = 0; GapTry < MaxGapTries && !bRoomPlaced; ++GapTry)
			{
				const float		Gap = (StartGapMul + GapStepMul * GapTry) * CellSize;
				const FVector2D Target = Source.AnchorCenter + Source.Dir * Gap;

				// Evaluate every rotation, pick the best-opposed entry doorway.
				float			BestScore = -2.f;
				FCellPlacedRoom BestCand;
				int32			BestEntryDoor = INDEX_NONE;
				bool			bHaveCand = false;

				for (const float Rot : Rotations)
				{
					// Find the entry doorway for this rotation (best opposition).
					int32 EntryDoor = INDEX_NONE;
					float EntryScore = -2.f;
					for (int32 d = 0; d < Type.Doorways.Num(); ++d)
					{
						const FVector2D WorldDir = Rotate2D(Type.Doorways[d].LocalOutwardDir, Rot).GetSafeNormal();
						const float		Score = FVector2D::DotProduct(WorldDir, -Source.Dir);
						if (Score > EntryScore)
						{
							EntryScore = Score;
							EntryDoor = d;
						}
					}
					if (EntryDoor == INDEX_NONE)
					{
						continue;
					}

					// Position room by its DOORWAY: Center = Target - Rotate(localPos).
					const FVector2D EntryLocal = Type.Doorways[EntryDoor].LocalPosition;
					const FVector2D RoomCenter = Target - Rotate2D(EntryLocal, Rot);

					FCellPlacedRoom Cand = MakeCandidateRoom(Type, RoomCenter, Rot, MiddleTypeIdx, Result.Diagram, CellSize);

					if (!CanPlaceRoom(Result, Cand))
					{
						continue;
					}

					if (EntryScore > BestScore)
					{
						BestScore = EntryScore;
						BestCand = Cand;
						BestEntryDoor = EntryDoor;
						bHaveCand = true;
					}
				}

				if (!bHaveCand)
				{
					continue; // try a larger gap
				}

				// Resolve the candidate entry anchor; must be Empty.
				const int32 EntryAnchor = AnchorWalk(
					Result.Diagram, Result.CellState, BestCand.OccupiedCells, BestCand.DoorwayPos[BestEntryDoor], BestCand.DoorwayDir[BestEntryDoor]);
				if (EntryAnchor == INDEX_NONE || !Result.CellState.IsValidIndex(EntryAnchor) || Result.CellState[EntryAnchor] != ECellState::Empty)
				{
					continue;
				}

				// CONNECT: Dijkstra from entry anchor to the connected target set =
				// {all Corridor cells} UNION {Source.AnchorCell}.
				TSet<int32> Goals;
				Goals.Add(Source.AnchorCell);
				for (int32 c = 0; c < N; ++c)
				{
					if (Result.CellState[c] == ECellState::Corridor)
					{
						Goals.Add(c);
					}
				}

				TSet<int32> Extra;
				Extra.Add(EntryAnchor);
				Extra.Add(Source.AnchorCell);

				TArray<int32> Path = DijkstraToSet(Result.Diagram, Result.CellState, EntryAnchor, Goals, Extra);
				if (Path.Num() == 0)
				{
					continue; // unreachable; try larger gap
				}

				// Optional detour rejection.
				{
					const FVector2D A = Result.Diagram.Cells[Path[0]].SiteLocation;
					const FVector2D B = Result.Diagram.Cells[Path.Last()].SiteLocation;
					const float		Straight = FVector2D::Distance(A, B);
					float			PathLen = 0.f;
					for (int32 p = 1; p < Path.Num(); ++p)
					{
						PathLen += FVector2D::Distance(Result.Diagram.Cells[Path[p]].SiteLocation, Result.Diagram.Cells[Path[p - 1]].SiteLocation);
					}
					if (Straight > KINDA_SMALL_NUMBER && PathLen > Straight * MaxPathDetour)
					{
						continue; // too winding; try larger gap
					}
				}

				// COMMIT the room and the corridor.
				const int32 NewRoomIdx = CommitRoom(BestCand);

				for (const int32 PCell : Path)
				{
					if (Result.CellState.IsValidIndex(PCell) && Result.CellState[PCell] != ECellState::RoomOccupied
						&& Result.CellState[PCell] != ECellState::Blocked)
					{
						Result.CellState[PCell] = ECellState::Corridor;
					}
				}
				Result.CorridorPaths.Add(Path);

				// Mark source + entry doorways used.
				if (Result.Rooms.IsValidIndex(Source.RoomIndex) && Result.Rooms[Source.RoomIndex].DoorwayUsed.IsValidIndex(Source.DoorwayIndex))
				{
					Result.Rooms[Source.RoomIndex].DoorwayUsed[Source.DoorwayIndex] = true;
				}
				if (Result.Rooms[NewRoomIdx].DoorwayUsed.IsValidIndex(BestEntryDoor))
				{
					Result.Rooms[NewRoomIdx].DoorwayUsed[BestEntryDoor] = true;
				}

				// The source exit is consumed; remove it from the open list (corridor frontiers own no exit).
				ConsumeRoomExit(Source);

				// Push the new room's remaining doorways as open exits.
				PushExits(NewRoomIdx);

				PlacedMiddles++;
				bRoomPlaced = true;
			}
		}

		if (!bRoomPlaced)
		{
			UE_LOG(LogCellDungeon,
				Warning,
				TEXT("CellDungeon: could not place middle room type %d (placed %d/%d middles)."),
				MiddleTypeIdx,
				PlacedMiddles,
				MiddleQueue.Num());
		}
	}

	// --- 5. PLACE END last, at the graph-farthest open exit ------------------
	{
		// Compute BFS hop distances from the start room's first valid anchor.
		int32 StartAnchor = INDEX_NONE;
		if (Result.Rooms.IsValidIndex(Result.StartRoomIndex))
		{
			const FCellPlacedRoom& StartRoom = Result.Rooms[Result.StartRoomIndex];
			for (int32 d = 0; d < StartRoom.DoorwayAnchorCell.Num(); ++d)
			{
				if (StartRoom.DoorwayAnchorCell[d] != INDEX_NONE)
				{
					StartAnchor = StartRoom.DoorwayAnchorCell[d];
					break;
				}
			}
		}

		TArray<int32> Hops;
		if (StartAnchor != INDEX_NONE)
		{
			BfsHopDistances(Result.Diagram, Result.CellState, StartAnchor, Hops);
		}

		// Order candidate exits by graph distance (farthest first); fall back to
		// insertion order when hops are unknown. Deterministic.
		TArray<FOpenExit> Sources = OpenExits;
		AppendCorridorFrontiers(Sources);

		TArray<int32> ExitOrder;
		for (int32 i = 0; i < Sources.Num(); ++i)
		{
			ExitOrder.Add(i);
		}
		ExitOrder.Sort([&](const int32 A, const int32 B) {
			const int32 HA = (Hops.IsValidIndex(Sources[A].AnchorCell)) ? Hops[Sources[A].AnchorCell] : -1;
			const int32 HB = (Hops.IsValidIndex(Sources[B].AnchorCell)) ? Hops[Sources[B].AnchorCell] : -1;
			if (HA != HB)
			{
				return HA > HB; // farther first
			}
			return A < B; // deterministic tie-break by insertion order
		});

		bool bEndPlaced = false;
		for (const int32 ExitIdx : ExitOrder)
		{
			if (!Sources.IsValidIndex(ExitIdx))
			{
				continue;
			}
			const FOpenExit Source = Sources[ExitIdx];
			if (Source.AnchorCell == INDEX_NONE || !Result.CellState.IsValidIndex(Source.AnchorCell))
			{
				continue;
			}

			const float Rotations[4] = { 0.f, 90.f, 180.f, 270.f };

			for (int32 GapTry = 0; GapTry < MaxGapTries && !bEndPlaced; ++GapTry)
			{
				const float		Gap = (StartGapMul + GapStepMul * GapTry) * CellSize;
				const FVector2D Target = Source.AnchorCenter + Source.Dir * Gap;

				float			BestScore = -2.f;
				FCellPlacedRoom BestCand;
				int32			BestEntryDoor = INDEX_NONE;
				bool			bHaveCand = false;

				for (const float Rot : Rotations)
				{
					int32 EntryDoor = INDEX_NONE;
					float EntryScore = -2.f;
					for (int32 d = 0; d < Config.EndRoom.Doorways.Num(); ++d)
					{
						const FVector2D WorldDir = Rotate2D(Config.EndRoom.Doorways[d].LocalOutwardDir, Rot).GetSafeNormal();
						const float		Score = FVector2D::DotProduct(WorldDir, -Source.Dir);
						if (Score > EntryScore)
						{
							EntryScore = Score;
							EntryDoor = d;
						}
					}
					if (EntryDoor == INDEX_NONE)
					{
						continue;
					}

					const FVector2D EntryLocal = Config.EndRoom.Doorways[EntryDoor].LocalPosition;
					const FVector2D RoomCenter = Target - Rotate2D(EntryLocal, Rot);

					FCellPlacedRoom Cand = MakeCandidateRoom(Config.EndRoom, RoomCenter, Rot, -3, Result.Diagram, CellSize);

					if (!CanPlaceRoom(Result, Cand))
					{
						continue;
					}
					if (EntryScore > BestScore)
					{
						BestScore = EntryScore;
						BestCand = Cand;
						BestEntryDoor = EntryDoor;
						bHaveCand = true;
					}
				}

				if (!bHaveCand)
				{
					continue;
				}

				const int32 EntryAnchor = AnchorWalk(
					Result.Diagram, Result.CellState, BestCand.OccupiedCells, BestCand.DoorwayPos[BestEntryDoor], BestCand.DoorwayDir[BestEntryDoor]);
				if (EntryAnchor == INDEX_NONE || !Result.CellState.IsValidIndex(EntryAnchor) || Result.CellState[EntryAnchor] != ECellState::Empty)
				{
					continue;
				}

				TSet<int32> Goals;
				Goals.Add(Source.AnchorCell);
				for (int32 c = 0; c < N; ++c)
				{
					if (Result.CellState[c] == ECellState::Corridor)
					{
						Goals.Add(c);
					}
				}
				TSet<int32> Extra;
				Extra.Add(EntryAnchor);
				Extra.Add(Source.AnchorCell);

				TArray<int32> Path = DijkstraToSet(Result.Diagram, Result.CellState, EntryAnchor, Goals, Extra);
				if (Path.Num() == 0)
				{
					continue;
				}

				const int32 EndIdx = CommitRoom(BestCand);
				for (const int32 PCell : Path)
				{
					if (Result.CellState.IsValidIndex(PCell) && Result.CellState[PCell] != ECellState::RoomOccupied
						&& Result.CellState[PCell] != ECellState::Blocked)
					{
						Result.CellState[PCell] = ECellState::Corridor;
					}
				}
				Result.CorridorPaths.Add(Path);

				if (Result.Rooms.IsValidIndex(Source.RoomIndex) && Result.Rooms[Source.RoomIndex].DoorwayUsed.IsValidIndex(Source.DoorwayIndex))
				{
					Result.Rooms[Source.RoomIndex].DoorwayUsed[Source.DoorwayIndex] = true;
				}
				if (Result.Rooms[EndIdx].DoorwayUsed.IsValidIndex(BestEntryDoor))
				{
					Result.Rooms[EndIdx].DoorwayUsed[BestEntryDoor] = true;
				}

				Result.EndRoomIndex = EndIdx;
				ConsumeRoomExit(Source);
				PushExits(EndIdx);
				bEndPlaced = true;
			}

			if (bEndPlaced)
			{
				break;
			}
		}

		if (!bEndPlaced)
		{
			UE_LOG(LogCellDungeon, Warning, TEXT("CellDungeon: failed to place END room."));
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
