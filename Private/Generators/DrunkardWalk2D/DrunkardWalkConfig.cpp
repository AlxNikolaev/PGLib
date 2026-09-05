#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"

#include "Generators/WeightedDistribute.h"
#include "ProceduralGeometry.h"

FDrunkardWalkResolvedParams FDrunkardWalkConfig::Resolve() const
{
	FDrunkardWalkResolvedParams Params;

	Params.CorridorLengthMin = FMath::Max(1, CorridorLengthMin);
	Params.CorridorLengthMax = FMath::Max(Params.CorridorLengthMin, CorridorLengthMax);
	Params.CorridorWidthMin = FMath::Max(1, CorridorWidthMin);
	Params.CorridorWidthMax = FMath::Max(Params.CorridorWidthMin, CorridorWidthMax);
	Params.CorridorTurnProbability = FMath::Clamp(CorridorTurnProbability, 0.0f, 1.0f);
	Params.CorridorBranchProbability = FMath::Clamp(CorridorBranchProbability, 0.0f, 1.0f);
	Params.RoomBorderMargin = FMath::Max(0, RoomBorderMargin);
	Params.WallThickness = FMath::Max(1, WallThickness);
	Params.MaxPlacementAttemptsPerExit = FMath::Max(1, MaxPlacementAttemptsPerExit);
	Params.bShuffleRoomOrder = bShuffleRoomOrder;
	Params.BranchProbability = FMath::Clamp(BranchProbability, 0.0f, 1.0f);

	// A type with Weight <= 0 but Min > 0 is a mandatory-count entry and must survive for ResolveForTotal; the counts
	// reported here are the authored weights, so a Min-only type comes back at zero.
	Params.RoomTypes.Reserve(RoomTypes.Num());
	int32 TotalRooms = 0;
	for (const FRoomTypeConfig& Type : RoomTypes)
	{
		if (Type.Weight <= 0 && Type.Min <= 0)
		{
			continue;
		}

		FRoomTypeConfig Clamped = Type;
		Clamped.FootprintWidthCells = FMath::Max(1, Type.FootprintWidthCells);
		Clamped.FootprintHeightCells = FMath::Max(1, Type.FootprintHeightCells);
		Clamped.Weight = FMath::Max(0, Type.Weight);
		Clamped.Min = FMath::Max(0, Type.Min);
		Clamped.Max = FMath::Max(0, Type.Max);
		Params.RoomTypes.Add(Clamped);
		TotalRooms += Clamped.Weight;
	}

	UE_LOG(LogRoguelikeGeometry,
		Verbose,
		TEXT("[DWConfig] Resolve: %d room types, %d total rooms, corridorLen=[%d,%d], width=[%d,%d], turn=%.2f, corridorBranch=%.2f, border=%d, "
			 "wall=%d, attempts=%d, shuffle=%s, branch=%.2f"),
		Params.RoomTypes.Num(),
		TotalRooms,
		Params.CorridorLengthMin,
		Params.CorridorLengthMax,
		Params.CorridorWidthMin,
		Params.CorridorWidthMax,
		Params.CorridorTurnProbability,
		Params.CorridorBranchProbability,
		Params.RoomBorderMargin,
		Params.WallThickness,
		Params.MaxPlacementAttemptsPerExit,
		Params.bShuffleRoomOrder ? TEXT("true") : TEXT("false"),
		Params.BranchProbability);

	return Params;
}

FDrunkardWalkResolvedParams FDrunkardWalkConfig::ResolveForTotal(const int32 TotalRooms) const
{
	FDrunkardWalkResolvedParams Params = Resolve();

	if (Params.RoomTypes.Num() == 0 || TotalRooms <= 0)
	{
		for (FRoomTypeConfig& Type : Params.RoomTypes)
		{
			Type.Weight = 0;
		}
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[DWConfig] ResolveForTotal(%d): no room types or zero total — empty queue"), TotalRooms);
		return Params;
	}

	TArray<int32> Weights, Mins, Maxes;
	Weights.Reserve(Params.RoomTypes.Num());
	Mins.Reserve(Params.RoomTypes.Num());
	Maxes.Reserve(Params.RoomTypes.Num());
	int32 TotalWeight = 0;
	for (const FRoomTypeConfig& Type : Params.RoomTypes)
	{
		const int32 W = FMath::Max(0, Type.Weight);
		Weights.Add(W);
		Mins.Add(FMath::Max(0, Type.Min));
		Maxes.Add(FMath::Max(0, Type.Max));
		TotalWeight += W;
	}

	TArray<int32> Counts;
	if (TotalWeight == 0)
	{
		// With no weights the distributor would split the budget equally, but a Min is a floor rather than a share of
		// the pool, so place exactly the minimums.
		Counts.SetNumZeroed(Params.RoomTypes.Num());
		int32 TotalMin = 0;
		for (int32 i = 0; i < Params.RoomTypes.Num(); ++i)
		{
			Counts[i] = (Maxes[i] > 0) ? FMath::Min(Mins[i], Maxes[i]) : Mins[i];
			TotalMin += Counts[i];
		}

		// Minimums that overrun the budget still have to fit inside it; the distributor scales them down.
		if (TotalMin > TotalRooms)
		{
			ProceduralGeometry_DistributePoolByWeight(Counts, Weights, Mins, Maxes, TotalRooms);
		}
	}
	else
	{
		ProceduralGeometry_DistributePoolByWeight(Counts, Weights, Mins, Maxes, TotalRooms);
	}

	for (int32 i = 0; i < Params.RoomTypes.Num(); ++i)
	{
		Params.RoomTypes[i].Weight = Counts[i];
	}

	UE_LOG(LogRoguelikeGeometry,
		Verbose,
		TEXT("[DWConfig] ResolveForTotal(%d): %d room types, pool distribution applied (total weight was %d)"),
		TotalRooms,
		Params.RoomTypes.Num(),
		TotalWeight);

	return Params;
}
