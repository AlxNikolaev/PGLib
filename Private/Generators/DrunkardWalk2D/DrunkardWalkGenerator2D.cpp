#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

#include "GridBudget.h"
#include "ProceduralGeometry.h"

namespace
{
	// Side indices: 0=+X, 1=-X, 2=+Y, 3=-Y; the opposite side is side ^ 1.
	const FIntPoint GDirVec[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	/** A room under construction during the walk, in signed grid coordinates. */
	struct FWalkRoom
	{
		FIntPoint	  Min;	 // footprint min corner (inclusive)
		int32		  W = 0; // footprint width  (X extent)
		int32		  H = 0; // footprint height (Y extent)
		int32		  TypeIndex = -1;
		int32		  PlacedIndex = -1; // index into PlacedRoomsSigned / PlacedRooms
		TArray<int32> AvailableSides;	// exit sides not yet attempted (entry side excluded)
	};

	FORCEINLINE bool InRect(const FIntPoint& P, const FIntPoint& Min, int32 W, int32 H)
	{
		return P.X >= Min.X && P.X < Min.X + W && P.Y >= Min.Y && P.Y < Min.Y + H;
	}

	/** Left-hand perpendicular of a cardinal direction. */
	FORCEINLINE FIntPoint PerpOf(const FIntPoint& D)
	{
		return FIntPoint(-D.Y, D.X);
	}

	/** Rotates a cardinal direction 90 degrees (left = CCW, right = CW). Never reverses. */
	FORCEINLINE FIntPoint TurnDir(const FIntPoint& D, bool bLeft)
	{
		return bLeft ? FIntPoint(-D.Y, D.X) : FIntPoint(D.Y, -D.X);
	}

	/** Maps a cardinal direction to a side index (0=+X, 1=-X, 2=+Y, 3=-Y). */
	FORCEINLINE int32 SideFromDir(const FIntPoint& D)
	{
		if (D.X > 0)
		{
			return 0;
		}
		if (D.X < 0)
		{
			return 1;
		}
		if (D.Y > 0)
		{
			return 2;
		}
		return 3;
	}
} // namespace

UDrunkardWalkGenerator2D::UDrunkardWalkGenerator2D()
{
	Bounds = FBox2D(FVector2D(-500, -500), FVector2D(500, 500));
	CorridorLengthMin = 3;
	CorridorLengthMax = 8;
	CorridorWidthMin = 1;
	CorridorWidthMax = 1;
	CorridorTurnProbability = 0.0f;
	CorridorBranchProbability = 0.0f;
	RoomBorderMargin = 1;
	WallThickness = 1;
	MaxPlacementAttemptsPerExit = 8;
	bShuffleRoomOrder = true;
	BranchProbability = 0.0f;
	InitializeRandomStream();
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetBounds(const FBox2D& InBounds)
{
	Super::SetBounds(InBounds);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetSeed(const FString& InSeed)
{
	Super::SetSeed(InSeed);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetGridSize(int32 InSize)
{
	Super::SetGridSize(InSize);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCenter(const FVector2D& InCenter)
{
	Super::SetCenter(InCenter);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetRoomTypes(const TArray<FRoomTypeConfig>& InRoomTypes)
{
	RoomTypes = InRoomTypes;
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCorridorLengthRange(int32 InMin, int32 InMax)
{
	CorridorLengthMin = FMath::Max(1, InMin);
	CorridorLengthMax = FMath::Max(CorridorLengthMin, InMax);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCorridorWidth(int32 InWidth)
{
	CorridorWidthMin = FMath::Max(1, InWidth);
	CorridorWidthMax = CorridorWidthMin;
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCorridorWidthRange(int32 InMin, int32 InMax)
{
	CorridorWidthMin = FMath::Max(1, InMin);
	CorridorWidthMax = FMath::Max(CorridorWidthMin, InMax);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCorridorTurnProbability(float InProbability)
{
	CorridorTurnProbability = FMath::Clamp(InProbability, 0.0f, 1.0f);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetCorridorBranchProbability(float InProbability)
{
	CorridorBranchProbability = FMath::Clamp(InProbability, 0.0f, 1.0f);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetRoomBorderMargin(int32 InMargin)
{
	RoomBorderMargin = FMath::Max(0, InMargin);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetWallThickness(int32 InThickness)
{
	WallThickness = FMath::Max(1, InThickness);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetMaxPlacementAttemptsPerExit(int32 InAttempts)
{
	MaxPlacementAttemptsPerExit = FMath::Max(1, InAttempts);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetShuffleRoomOrder(bool bInShuffle)
{
	bShuffleRoomOrder = bInShuffle;
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::SetBranchProbability(float InProbability)
{
	BranchProbability = FMath::Clamp(InProbability, 0.0f, 1.0f);
	return this;
}

UDrunkardWalkGenerator2D* UDrunkardWalkGenerator2D::ApplyResolvedParams(const FDrunkardWalkResolvedParams& Params)
{
	RoomTypes = Params.RoomTypes;
	CorridorLengthMin = Params.CorridorLengthMin;
	CorridorLengthMax = Params.CorridorLengthMax;
	CorridorWidthMin = Params.CorridorWidthMin;
	CorridorWidthMax = Params.CorridorWidthMax;
	CorridorTurnProbability = Params.CorridorTurnProbability;
	CorridorBranchProbability = Params.CorridorBranchProbability;
	RoomBorderMargin = Params.RoomBorderMargin;
	WallThickness = Params.WallThickness;
	MaxPlacementAttemptsPerExit = Params.MaxPlacementAttemptsPerExit;
	bShuffleRoomOrder = Params.bShuffleRoomOrder;
	BranchProbability = Params.BranchProbability;
	return this;
}

FLayoutDiagram2D UDrunkardWalkGenerator2D::Generate()
{
	return GenerateInternal().Diagram;
}

FDrunkardWalkGridData UDrunkardWalkGenerator2D::GenerateWithGridData()
{
	return GenerateInternal();
}

FDrunkardWalkGridData UDrunkardWalkGenerator2D::GenerateInternal()
{
	const double StartTime = FPlatformTime::Seconds();

	// Re-seed so a fixed seed yields identical output regardless of any prior Generate() call on this instance.
	InitializeRandomStream();

	TArray<int32> Queue = BuildRoomQueue(RoomTypes, bShuffleRoomOrder, RandomStream);
	const int32	  RequestedRoomCount = Queue.Num();

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[DW] Generate() — GridSize=%d Seed='%s' RoomTypes=%d RequestedRooms=%d CorridorLen=[%d,%d] Width=[%d,%d] Turn=%.2f CorridorBranch=%.2f "
			 "Border=%d Wall=%d Attempts=%d Shuffle=%s Branch=%.2f"),
		GridSize,
		*Seed,
		RoomTypes.Num(),
		RequestedRoomCount,
		CorridorLengthMin,
		CorridorLengthMax,
		CorridorWidthMin,
		CorridorWidthMax,
		CorridorTurnProbability,
		CorridorBranchProbability,
		RoomBorderMargin,
		WallThickness,
		MaxPlacementAttemptsPerExit,
		bShuffleRoomOrder ? TEXT("true") : TEXT("false"),
		BranchProbability);

	float CellSizeVal = static_cast<float>(GridSize);

	auto MakeEmptyResult = [&]() -> FDrunkardWalkGridData {
		FDrunkardWalkGridData EmptyResult;
		EmptyResult.CenterRegionId = -1;
		EmptyResult.RequestedRoomCount = RequestedRoomCount;
		EmptyResult.GridWidth = 0;
		EmptyResult.GridHeight = 0;
		EmptyResult.CellSize = CellSizeVal;
		return EmptyResult;
	};

	if (RequestedRoomCount == 0)
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[DW] No room types with positive count — nothing to generate."));
		return MakeEmptyResult();
	}

	// The walk runs on an unbounded signed integer grid.

	// Floor cells (corridor + room) keyed by signed position.
	TMap<FIntPoint, uint8> CellTypeMap;
	TMap<FIntPoint, int32> CellRoomTypeMap;

	auto FootprintW = [&](int32 TypeIdx) { return FMath::Max(1, RoomTypes[TypeIdx].FootprintWidthCells); };
	auto FootprintH = [&](int32 TypeIdx) { return FMath::Max(1, RoomTypes[TypeIdx].FootprintHeightCells); };

	auto ShuffledSidesExcluding = [&](int32 ExcludeSide) -> TArray<int32> {
		TArray<int32> Sides;
		for (int32 s = 0; s < 4; ++s)
		{
			if (s != ExcludeSide)
			{
				Sides.Add(s);
			}
		}
		for (int32 i = Sides.Num() - 1; i > 0; --i)
		{
			const int32 j = RandomStream.RandRange(0, i);
			Sides.Swap(i, j);
		}
		return Sides;
	};

	auto StampRoom = [&](const FWalkRoom& R) {
		for (int32 dy = 0; dy < R.H; ++dy)
		{
			for (int32 dx = 0; dx < R.W; ++dx)
			{
				const FIntPoint P(R.Min.X + dx, R.Min.Y + dy);
				CellTypeMap.Add(P, EDrunkardWalkCellType::Room);
				CellRoomTypeMap.Add(P, R.TypeIndex);
			}
		}
	};

	// Open rooms still have an untried exit side. The next room grows from the most recent one, or from a random one
	// at BranchProbability. Reserved so element references stay stable across Add.
	TArray<FWalkRoom> OpenRooms;
	OpenRooms.Reserve(RequestedRoomCount + 1);

	TArray<FWalkRoom>		  PlacedRoomsSigned;
	TArray<TArray<FIntPoint>> CorridorPolylines;  // center rail per corridor segment (signed)
	TArray<int32>			  CorridorSourceRoom; // PlacedRoomsSigned index each corridor starts from
	TArray<int32>			  CorridorTargetRoom; // PlacedRoomsSigned index each corridor leads to

	// Place the first room centered on the origin.
	{
		FWalkRoom First;
		First.TypeIndex = Queue[0];
		First.W = FootprintW(First.TypeIndex);
		First.H = FootprintH(First.TypeIndex);
		First.Min = FIntPoint(-First.W / 2, -First.H / 2);
		First.PlacedIndex = 0;
		First.AvailableSides = ShuffledSidesExcluding(-1);
		StampRoom(First);
		PlacedRoomsSigned.Add(First);
		OpenRooms.Add(First);
	}

	// A corridor system (main corridor plus forks) is traced into these accumulators, validated for clearance, then
	// committed atomically or discarded and retried.
	struct FPendingRoom
	{
		FIntPoint Min;
		int32	  W = 0;
		int32	  H = 0;
		int32	  TypeIndex = -1;
		int32	  EntrySide = -1;
		int32	  PlacedIndex = -1;
	};
	struct FPendingRail
	{
		TArray<FIntPoint> Rail;
		int32			  SourcePlaced = -1;
		int32			  TargetPending = -1;
	};

	TArray<FPendingRoom> PendingRooms;
	TArray<FPendingRail> PendingRails;
	TArray<FIntPoint>	 PendingCorridorCells;
	TSet<FIntPoint>		 PendingCellSet; // corridor + room cells laid this attempt (collision/clearance)
	int32				 LocalQueueCursor = 0;

	auto ResetPending = [&]() {
		PendingRooms.Reset();
		PendingRails.Reset();
		PendingCorridorCells.Reset();
		PendingCellSet.Reset();
	};

	int32 StatTraceCalls = 0;	   // TraceOne invocations
	int32 StatRejectSelfTouch = 0; // corridor would fold onto itself
	int32 StatRejectRoomFit = 0;   // room edge couldn't cover the corridor end
	int32 StatRejectClearance = 0; // candidate touched committed geometry
	int32 StatTurns = 0;		   // total corridor bends
	int32 StatForkSeeds = 0;	   // fork opportunities raised
	int32 StatForksPlaced = 0;	   // forks that became rooms in a committed attempt
	int32 StatBacktracks = 0;	   // open-room drops (cornered sources)

	// Cells of the most recent successful trace, so a fork can be handed the rail it grows off as ExemptCells.
	TSet<FIntPoint>		  LastTraceCorridorCells;
	const TSet<FIntPoint> NoExemptCells;

	// Traces one corridor from StartOutside heading InitialDir and places the queue next room at its terminal,
	// appending to the pending accumulators. ExemptCells are ignored by the clearance test; a false return appends
	// nothing.
	auto TraceOne = [&](FIntPoint							 StartOutside,
						FIntPoint							 InitialDir,
						int32								 InitialWidth,
						int32								 SourcePlacedForGraph,
						const TSet<FIntPoint>&				 ExemptCells,
						bool								 bCollectForks,
						TArray<TPair<FIntPoint, FIntPoint>>& OutForkSeeds) -> bool {
		if (LocalQueueCursor >= Queue.Num())
		{
			return false;
		}
		++StatTraceCalls;
		const int32 MySlot = LocalQueueCursor;
		const int32 MyType = Queue[MySlot];
		const int32 MyW = FootprintW(MyType);
		const int32 MyH = FootprintH(MyType);

		const int32 Len = RandomStream.RandRange(CorridorLengthMin, CorridorLengthMax);

		FIntPoint Dir = InitialDir;
		FIntPoint Cur = StartOutside;
		int32	  Width = FMath::Clamp(InitialWidth, CorridorWidthMin, CorridorWidthMax);

		TArray<FIntPoint>					Rail;
		TArray<FIntPoint>					MyCells;
		TArray<FIntPoint>					EndBand;
		TArray<TPair<FIntPoint, FIntPoint>> ForkSeeds;
		Rail.Reserve(Len);

		// A corridor may never fold back onto itself: a new band may only touch the immediately previous band.
		TSet<FIntPoint> SelfSet;
		TSet<FIntPoint> PrevBandSet;

		// Bends stay at least MinSegment cells apart so corridors read as hallways rather than per-step jitter.
		const int32 MinSegment = FMath::Max(2, CorridorWidthMax + 1);
		int32		StepsSinceTurn = 0;

		for (int32 k = 0; k < Len; ++k)
		{
			if (k > 0 && CorridorTurnProbability > 0.0f && StepsSinceTurn >= MinSegment && RandomStream.FRand() < CorridorTurnProbability)
			{
				Dir = TurnDir(Dir, RandomStream.FRand() < 0.5f);
				StepsSinceTurn = 0;
				++StatTurns;
			}
			else
			{
				++StepsSinceTurn;
			}

			if (CorridorWidthMax > CorridorWidthMin && RandomStream.FRand() < 0.5f)
			{
				Width = FMath::Clamp(Width + (RandomStream.FRand() < 0.5f ? -1 : 1), CorridorWidthMin, CorridorWidthMax);
			}

			const FIntPoint	  Perp = PerpOf(Dir);
			TArray<FIntPoint> Band;
			TSet<FIntPoint>	  CurBandSet;
			Band.Reserve(Width);
			for (int32 j = 0; j < Width; ++j)
			{
				const FIntPoint C = Cur + Perp * (j - Width / 2);
				Band.Add(C);
				CurBandSet.Add(C);
			}

			// Reject a band that folds onto itself or comes within one cell of this attempt's pending geometry; the
			// previous and current bands are exempt because contiguity is expected.
			for (const FIntPoint& C : Band)
			{
				for (int32 ny = -1; ny <= 1; ++ny)
				{
					for (int32 nx = -1; nx <= 1; ++nx)
					{
						const FIntPoint N(C.X + nx, C.Y + ny);
						if (CurBandSet.Contains(N) || PrevBandSet.Contains(N))
						{
							continue;
						}
						// A fork is seeded just off the parent rail, so the exemption holds only for the first two
						// rings; further along it would let a turning fork run back over its parent.
						if (k <= 1 && ExemptCells.Contains(N))
						{
							continue;
						}
						if (SelfSet.Contains(N))
						{
							++StatRejectSelfTouch;
							return false;
						}
						if (PendingCellSet.Contains(N))
						{
							++StatRejectClearance;
							return false;
						}
					}
				}
			}

			for (const FIntPoint& C : Band)
			{
				SelfSet.Add(C);
				MyCells.Add(C);
			}
			Rail.Add(Cur);
			if (k == Len - 1)
			{
				EndBand = Band;
			}

			if (bCollectForks && k > 0 && k < Len - 1 && CorridorBranchProbability > 0.0f && RandomStream.FRand() < CorridorBranchProbability)
			{
				// Seed clear of this band, so a fork starts beside the parent instead of inside it.
				const FIntPoint ForkDir = TurnDir(Dir, RandomStream.FRand() < 0.5f);
				ForkSeeds.Add(TPair<FIntPoint, FIntPoint>(Cur + ForkDir * (Width / 2 + 1), ForkDir));
				++StatForkSeeds;
			}

			PrevBandSet = MoveTemp(CurBandSet);
			Cur += Dir;
		}

		const FIntPoint FinalDir = Dir;

		// End-band bounding box (robust to turns / perp sign).
		FIntPoint BMin(MAX_int32, MAX_int32);
		FIntPoint BMax(MIN_int32, MIN_int32);
		for (const FIntPoint& C : EndBand)
		{
			BMin.X = FMath::Min(BMin.X, C.X);
			BMin.Y = FMath::Min(BMin.Y, C.Y);
			BMax.X = FMath::Max(BMax.X, C.X);
			BMax.Y = FMath::Max(BMax.Y, C.Y);
		}

		const bool	bHorizontal = (FinalDir.Y == 0);
		const int32 EndSpan = bHorizontal ? (BMax.Y - BMin.Y + 1) : (BMax.X - BMin.X + 1);
		const int32 RoomPerpDim = bHorizontal ? MyH : MyW;
		if (EndSpan > RoomPerpDim)
		{
			++StatRejectRoomFit;
			return false; // room entry edge can't cover the corridor end
		}
		const int32 Q = RandomStream.RandRange(0, RoomPerpDim - EndSpan);

		// Place the room on the far side of the end band, along FinalDir.
		FIntPoint MyMin;
		if (FinalDir.X > 0)
		{
			MyMin = FIntPoint(BMax.X + 1, BMin.Y - Q);
		}
		else if (FinalDir.X < 0)
		{
			MyMin = FIntPoint(BMin.X - MyW, BMin.Y - Q);
		}
		else if (FinalDir.Y > 0)
		{
			MyMin = FIntPoint(BMin.X - Q, BMax.Y + 1);
		}
		else
		{
			MyMin = FIntPoint(BMin.X - Q, BMin.Y - MyH);
		}

		// Reject if the footprint collides with cells already laid this attempt.
		for (int32 dy = 0; dy < MyH; ++dy)
		{
			for (int32 dx = 0; dx < MyW; ++dx)
			{
				if (PendingCellSet.Contains(FIntPoint(MyMin.X + dx, MyMin.Y + dy)))
				{
					++StatRejectRoomFit;
					return false;
				}
			}
		}

		LocalQueueCursor = MySlot + 1;
		for (const FIntPoint& C : MyCells)
		{
			PendingCorridorCells.Add(C);
			PendingCellSet.Add(C);
		}
		LastTraceCorridorCells = TSet<FIntPoint>(MyCells);
		for (int32 dy = 0; dy < MyH; ++dy)
		{
			for (int32 dx = 0; dx < MyW; ++dx)
			{
				PendingCellSet.Add(FIntPoint(MyMin.X + dx, MyMin.Y + dy));
			}
		}

		FPendingRoom PR;
		PR.Min = MyMin;
		PR.W = MyW;
		PR.H = MyH;
		PR.TypeIndex = MyType;
		PR.EntrySide = SideFromDir(FinalDir) ^ 1; // entry side faces back along the corridor
		const int32 PendingIdx = PendingRooms.Add(PR);

		FPendingRail Prail;
		Prail.Rail = MoveTemp(Rail);
		Prail.SourcePlaced = SourcePlacedForGraph;
		Prail.TargetPending = PendingIdx;
		PendingRails.Add(MoveTemp(Prail));

		if (bCollectForks)
		{
			OutForkSeeds.Append(ForkSeeds);
		}
		return true;
	};

	int32 QueueIdx = 1;

	while (QueueIdx < Queue.Num() && OpenRooms.Num() > 0)
	{
		// Choose which open room to grow from: a random one when branching, else the most recent (DFS path).
		int32 SourceIdx;
		if (BranchProbability > 0.0f && OpenRooms.Num() > 1 && RandomStream.FRand() < BranchProbability)
		{
			SourceIdx = RandomStream.RandRange(0, OpenRooms.Num() - 1);
		}
		else
		{
			SourceIdx = OpenRooms.Num() - 1;
		}

		// Captured up front so nothing depends on the OpenRooms element reference after the later Add.
		const FIntPoint CurMin = OpenRooms[SourceIdx].Min;
		const int32		CurW = OpenRooms[SourceIdx].W;
		const int32		CurH = OpenRooms[SourceIdx].H;
		const int32		SourcePlacedIndex = OpenRooms[SourceIdx].PlacedIndex;

		bool bPlaced = false;

		while (OpenRooms[SourceIdx].AvailableSides.Num() > 0 && !bPlaced)
		{
			const int32		Side = OpenRooms[SourceIdx].AvailableSides.Pop();
			const FIntPoint Dir = GDirVec[Side];
			const FIntPoint Perp = PerpOf(Dir); // TraceOne lays its bands out along PerpOf(Dir); the band offset must use the same sign
			const int32		EdgeLen = (Side <= 1) ? CurH : CurW; // length of the source edge along Perp

			// The starting band must fit on the source edge.
			if (CorridorWidthMin > EdgeLen)
			{
				continue;
			}

			// Outside-adjacent edge cell at perpendicular index 0; which end that is flips with the sign of
			// PerpOf(Dir), so sides 1 and 2 start from the far corner or the band walks off the edge.
			FIntPoint O;
			switch (Side)
			{
				case 0:
					O = FIntPoint(CurMin.X + CurW, CurMin.Y);
					break;
				case 1:
					O = FIntPoint(CurMin.X - 1, CurMin.Y + CurH - 1);
					break;
				case 2:
					O = FIntPoint(CurMin.X + CurW - 1, CurMin.Y + CurH);
					break;
				default:
					O = FIntPoint(CurMin.X, CurMin.Y - 1);
					break;
			}

			for (int32 Attempt = 0; Attempt < MaxPlacementAttemptsPerExit && !bPlaced; ++Attempt)
			{
				ResetPending();
				LocalQueueCursor = QueueIdx;

				const int32		StartWidth = RandomStream.RandRange(CorridorWidthMin, FMath::Min(CorridorWidthMax, EdgeLen));
				const int32		P0 = RandomStream.RandRange(0, EdgeLen - StartWidth);
				const FIntPoint StartOutside = O + Perp * (P0 + StartWidth / 2);

				TArray<TPair<FIntPoint, FIntPoint>> ForkSeeds;
				if (!TraceOne(StartOutside, Dir, StartWidth, SourcePlacedIndex, NoExemptCells, true, ForkSeeds))
				{
					continue; // main room didn't fit this attempt
				}

				// Only the parent corridor stays exempt, so two forks can never plow through each other.
				const TSet<FIntPoint> ParentCorridorCells = LastTraceCorridorCells;

				// Forks go one level deep and each connects back to the same source room.
				int32 AttemptForksPlaced = 0;
				for (const TPair<FIntPoint, FIntPoint>& ForkSeed : ForkSeeds)
				{
					if (LocalQueueCursor >= Queue.Num())
					{
						break;
					}
					const int32							ForkWidth = RandomStream.RandRange(CorridorWidthMin, CorridorWidthMax);
					TArray<TPair<FIntPoint, FIntPoint>> Unused;
					if (TraceOne(ForkSeed.Key, ForkSeed.Value, ForkWidth, SourcePlacedIndex, ParentCorridorCells, false, Unused))
					{
						++AttemptForksPlaced;
					}
				}

				// Every pending cell keeps a RoomBorderMargin gap from committed floor, except the source room's own
				// cells, which the door is allowed to touch.
				bool bClear = true;
				for (const FIntPoint& C : PendingCellSet)
				{
					// The overlap test stands outside the margin ring, which exempts every pending cell: at margin 0
					// the ring would exempt C itself and rooms would be stamped over committed ones.
					if (CellTypeMap.Contains(C) && !InRect(C, CurMin, CurW, CurH))
					{
						bClear = false;
						break;
					}

					if (RoomBorderMargin <= 0)
					{
						continue;
					}

					for (int32 ny = -RoomBorderMargin; ny <= RoomBorderMargin && bClear; ++ny)
					{
						for (int32 nx = -RoomBorderMargin; nx <= RoomBorderMargin; ++nx)
						{
							const FIntPoint N(C.X + nx, C.Y + ny);
							if (PendingCellSet.Contains(N) || InRect(N, CurMin, CurW, CurH))
							{
								continue;
							}
							if (CellTypeMap.Contains(N))
							{
								bClear = false;
								break;
							}
						}
					}
					if (!bClear)
					{
						break;
					}
				}

				if (!bClear)
				{
					++StatRejectClearance;
					continue;
				}

				// Commit: rooms first (assign placed indices in order), then corridors (rooms win), then rails.
				for (FPendingRoom& PR : PendingRooms)
				{
					FWalkRoom NewRoom;
					NewRoom.TypeIndex = PR.TypeIndex;
					NewRoom.W = PR.W;
					NewRoom.H = PR.H;
					NewRoom.Min = PR.Min;
					NewRoom.PlacedIndex = PlacedRoomsSigned.Num();
					NewRoom.AvailableSides = ShuffledSidesExcluding(PR.EntrySide);
					PR.PlacedIndex = NewRoom.PlacedIndex;
					StampRoom(NewRoom);
					PlacedRoomsSigned.Add(NewRoom);
					OpenRooms.Add(NewRoom); // Reserve prevents realloc — SourceIdx stays valid
				}
				for (const FIntPoint& C : PendingCorridorCells)
				{
					if (!CellTypeMap.Contains(C)) // never overwrite a room cell
					{
						CellTypeMap.Add(C, EDrunkardWalkCellType::Corridor);
					}
				}
				for (const FPendingRail& PRail : PendingRails)
				{
					CorridorPolylines.Add(PRail.Rail);
					CorridorSourceRoom.Add(PRail.SourcePlaced);
					CorridorTargetRoom.Add(PendingRooms[PRail.TargetPending].PlacedIndex);
				}

				// Counted after the clearance test so a discarded attempt's forks never reach the layout count.
				StatForksPlaced += AttemptForksPlaced;

				QueueIdx = LocalQueueCursor;
				bPlaced = true;
			}
		}

		if (!bPlaced)
		{
			// No exit fits the next room, so drop the source and fall back to the previous open room.
			++StatBacktracks;
			OpenRooms.RemoveAt(SourceIdx);
		}
		else if (OpenRooms[SourceIdx].AvailableSides.Num() == 0)
		{
			OpenRooms.RemoveAt(SourceIdx);
		}
	}

	const int32 PlacedCount = PlacedRoomsSigned.Num();
	if (PlacedCount < RequestedRoomCount)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("[DW] Placement shortfall: placed %d / %d rooms (open set exhausted, %d unplaced)."),
			PlacedCount,
			RequestedRoomCount,
			RequestedRoomCount - PlacedCount);
	}

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[DW] Stats: traceCalls=%d turns=%d forkSeeds=%d forksPlaced=%d | rejects: selfTouch=%d roomFit=%d clearance=%d | backtracks=%d | "
			 "corridors=%d"),
		StatTraceCalls,
		StatTurns,
		StatForkSeeds,
		StatForksPlaced,
		StatRejectSelfTouch,
		StatRejectRoomFit,
		StatRejectClearance,
		StatBacktracks,
		CorridorPolylines.Num());

	// Rasterize the signed cells into a grid sized to the actual extents.

	auto ComputeExtents = [&](FIntPoint& OutMin, FIntPoint& OutMax) {
		OutMin = FIntPoint(MAX_int32, MAX_int32);
		OutMax = FIntPoint(MIN_int32, MIN_int32);
		for (const TPair<FIntPoint, uint8>& Pair : CellTypeMap)
		{
			OutMin.X = FMath::Min(OutMin.X, Pair.Key.X);
			OutMin.Y = FMath::Min(OutMin.Y, Pair.Key.Y);
			OutMax.X = FMath::Max(OutMax.X, Pair.Key.X);
			OutMax.Y = FMath::Max(OutMax.Y, Pair.Key.Y);
		}
	};

	FIntPoint MinExtent;
	FIntPoint MaxExtent;
	ComputeExtents(MinExtent, MaxExtent);

	if (CellTypeMap.Num() == 0)
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("[DW] No floor cells produced — nothing to generate."));
		return MakeEmptyResult();
	}

	bool bDegradedResolution = false;

	// The walk footprint is intrinsic, so the cell budget is honoured by merging S*S signed cells into one and
	// enlarging the physical cell size by the same factor, preserving world extents at a coarser resolution.
	{
		const int32 Pad = FMath::Max(1, WallThickness);
		const int64 RawWidth = (MaxExtent.X - MinExtent.X + 1) + 2 * Pad;
		const int64 RawHeight = (MaxExtent.Y - MinExtent.Y + 1) + 2 * Pad;
		if (RawWidth * RawHeight > PGGrid::MaxGridCells)
		{
			const int32 DownsampleFactor = FMath::CeilToInt(
				FMath::Sqrt(static_cast<double>(RawWidth) * static_cast<double>(RawHeight) / static_cast<double>(PGGrid::MaxGridCells)));

			auto CoarsenCoord = [DownsampleFactor](int32 V) { return FMath::FloorToInt(static_cast<float>(V) / DownsampleFactor); };

			TMap<FIntPoint, uint8> CoarseCellTypeMap;
			CoarseCellTypeMap.Reserve(CellTypeMap.Num());
			for (const TPair<FIntPoint, uint8>& Pair : CellTypeMap)
			{
				const FIntPoint Coarse(CoarsenCoord(Pair.Key.X), CoarsenCoord(Pair.Key.Y));
				uint8&			Existing = CoarseCellTypeMap.FindOrAdd(Coarse, Pair.Value);
				if (Pair.Value == EDrunkardWalkCellType::Room)
				{
					Existing = EDrunkardWalkCellType::Room; // Room beats Corridor where they collapse together.
				}
			}
			CellTypeMap = MoveTemp(CoarseCellTypeMap);

			for (FWalkRoom& R : PlacedRoomsSigned)
			{
				const int32 MaxX = R.Min.X + R.W - 1;
				const int32 MaxY = R.Min.Y + R.H - 1;
				R.Min = FIntPoint(CoarsenCoord(R.Min.X), CoarsenCoord(R.Min.Y));
				R.W = CoarsenCoord(MaxX) - R.Min.X + 1;
				R.H = CoarsenCoord(MaxY) - R.Min.Y + 1;
			}

			for (TArray<FIntPoint>& Poly : CorridorPolylines)
			{
				for (FIntPoint& P : Poly)
				{
					P = FIntPoint(CoarsenCoord(P.X), CoarsenCoord(P.Y));
				}
			}

			CellSizeVal *= DownsampleFactor;
			bDegradedResolution = true;
			ComputeExtents(MinExtent, MaxExtent);

			UE_LOG(LogRoguelikeGeometry,
				Warning,
				TEXT("[DW] Cell budget exceeded: %lldx%lld would exceed %lld cells; degrading by %dx (CellSize=%.1f)."),
				RawWidth,
				RawHeight,
				PGGrid::MaxGridCells,
				DownsampleFactor,
				CellSizeVal);
		}
	}

	const int32		Pad = FMath::Max(1, WallThickness); // outer ring wide enough to hold the walls
	const FIntPoint Offset(Pad - MinExtent.X, Pad - MinExtent.Y);
	const int32		GWidth = (MaxExtent.X - MinExtent.X + 1) + 2 * Pad;
	const int32		GHeight = (MaxExtent.Y - MinExtent.Y + 1) + 2 * Pad;

	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Grid dimensions: %dx%d (%d total cells)"), GWidth, GHeight, GWidth * GHeight);

	if (GWidth <= 0 || GHeight <= 0)
	{
		UE_LOG(LogRoguelikeGeometry, Error, TEXT("[DW] Invalid grid dimensions: %dx%d"), GWidth, GHeight);
		return MakeEmptyResult();
	}

	const int32 TotalCells = GWidth * GHeight;

	TArray<bool> Grid;
	Grid.Init(false, TotalCells);
	TArray<uint8> CellType;
	CellType.Init(EDrunkardWalkCellType::Empty, TotalCells); // non-floor defaults to Empty; walls added below

	for (const TPair<FIntPoint, uint8>& Pair : CellTypeMap)
	{
		const int32 AX = Pair.Key.X + Offset.X;
		const int32 AY = Pair.Key.Y + Offset.Y;
		const int32 Index = AY * GWidth + AX;
		Grid[Index] = true;
		CellType[Index] = Pair.Value;
	}

	// Only non-floor cells within WallThickness (Chebyshev) of a floor cell become walls; the rest stay Empty.
	const int32 WT = FMath::Max(1, WallThickness);
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
					if (!Grid[NIdx] && CellType[NIdx] == EDrunkardWalkCellType::Empty)
					{
						CellType[NIdx] = EDrunkardWalkCellType::Wall;
					}
				}
			}
		}
	}

	// Convert placed rooms / corridor polylines to grid-array coordinates.
	TArray<FDrunkardWalkPlacedRoom> PlacedRooms;
	TArray<FIntPoint>				RoomCenters;
	PlacedRooms.Reserve(PlacedRoomsSigned.Num());
	RoomCenters.Reserve(PlacedRoomsSigned.Num());
	for (const FWalkRoom& R : PlacedRoomsSigned)
	{
		FDrunkardWalkPlacedRoom PR;
		PR.Min = FIntPoint(R.Min.X + Offset.X, R.Min.Y + Offset.Y);
		PR.Width = R.W;
		PR.Height = R.H;
		PR.TypeIndex = R.TypeIndex;
		PlacedRooms.Add(PR);
		RoomCenters.Add(FIntPoint(PR.Min.X + R.W / 2, PR.Min.Y + R.H / 2));
	}

	TArray<TArray<FIntPoint>> WalkerPaths;
	WalkerPaths.Reserve(CorridorPolylines.Num());
	for (const TArray<FIntPoint>& Poly : CorridorPolylines)
	{
		TArray<FIntPoint>& Out = WalkerPaths.AddDefaulted_GetRef();
		Out.Reserve(Poly.Num());
		for (const FIntPoint& P : Poly)
		{
			Out.Add(FIntPoint(P.X + Offset.X, P.Y + Offset.Y));
		}
	}

	// First room center (array coords) — used as the layout center for region detection / world placement.
	const int32 CenterX = (PlacedRoomsSigned.Num() > 0) ? RoomCenters[0].X : GWidth / 2;
	const int32 CenterY = (PlacedRoomsSigned.Num() > 0) ? RoomCenters[0].Y : GHeight / 2;

	TArray<int32>			  RegionIds;
	TArray<TArray<FIntPoint>> Regions;
	int32					  CenterRegionId = -1;

	FloodFillRegions(Grid, GWidth, GHeight, CenterX, CenterY, RegionIds, Regions, CenterRegionId);

	UE_LOG(LogRoguelikeGeometry,
		Log,
		TEXT("[DW] Walk complete: %d/%d rooms placed, %d corridor segments, %d floor cells, %d regions, center region=%d"),
		PlacedCount,
		RequestedRoomCount,
		CorridorPolylines.Num(),
		CellTypeMap.Num(),
		Regions.Num(),
		CenterRegionId);

	const float	 MinXWorld = CenterPoint.X - (CenterX + 0.5f) * CellSizeVal;
	const float	 MinYWorld = CenterPoint.Y - (CenterY + 0.5f) * CellSizeVal;
	const FBox2D OutputBounds(FVector2D(MinXWorld, MinYWorld), FVector2D(MinXWorld + GWidth * CellSizeVal, MinYWorld + GHeight * CellSizeVal));

	// ConvertGridToDiagram reads the inherited Bounds, so swap in the output frame and restore it afterwards.
	const FBox2D SavedBounds = Bounds;
	Bounds = OutputBounds;
	FLayoutDiagram2D Diagram = ConvertGridToDiagram(Grid, GWidth, GHeight);
	Bounds = SavedBounds;

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	UE_LOG(LogRoguelikeGeometry, Log, TEXT("[DW] Generate() complete: %d cells in %.2fms"), Diagram.Cells.Num(), ElapsedMs);

	FDrunkardWalkGridData Result;
	Result.Grid = MoveTemp(Grid);
	Result.CellType = MoveTemp(CellType);
	Result.RegionIds = MoveTemp(RegionIds);
	Result.Regions = MoveTemp(Regions);
	Result.CenterRegionId = CenterRegionId;
	Result.WalkerPaths = MoveTemp(WalkerPaths);
	Result.CorridorSourceRoom = MoveTemp(CorridorSourceRoom);
	Result.CorridorTargetRoom = MoveTemp(CorridorTargetRoom);
	Result.RoomCenters = MoveTemp(RoomCenters);
	Result.PlacedRooms = MoveTemp(PlacedRooms);
	Result.RequestedRoomCount = RequestedRoomCount;
	Result.ForksPlaced = StatForksPlaced;
	Result.GridWidth = GWidth;
	Result.GridHeight = GHeight;
	Result.CellSize = CellSizeVal;
	Result.bDegradedResolution = bDegradedResolution;
	Result.Diagram = MoveTemp(Diagram);

	return Result;
}

TArray<int32> UDrunkardWalkGenerator2D::BuildRoomQueue(const TArray<FRoomTypeConfig>& RoomTypes, bool bShuffle, FRandomStream& RandomStream)
{
	// Expand each type by its Weight (resolved absolute count after Resolve()/ResolveForTotal()).
	TArray<int32> Queue;
	for (int32 TypeIdx = 0; TypeIdx < RoomTypes.Num(); ++TypeIdx)
	{
		const int32 Count = FMath::Max(0, RoomTypes[TypeIdx].Weight);
		for (int32 i = 0; i < Count; ++i)
		{
			Queue.Add(TypeIdx);
		}
	}

	// Fisher-Yates in-place shuffle (seeded) so room order varies per seed.
	if (bShuffle)
	{
		for (int32 i = Queue.Num() - 1; i > 0; --i)
		{
			const int32 j = RandomStream.RandRange(0, i);
			Queue.Swap(i, j);
		}
	}

	return Queue;
}
