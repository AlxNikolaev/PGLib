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

	// Copy and clamp room types; drop zero-weight or degenerate entries.
	Params.RoomTypes.Reserve(RoomTypes.Num());
	int32 TotalRooms = 0;
	for (const FRoomTypeConfig& Type : RoomTypes)
	{
		if (Type.Weight <= 0)
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
	// Validate all non-room params via the base Resolve() first, then redistribute room counts
	// using the pool-aware weighted distribution: mandatory minimums first, then proportionally
	// by weight up to per-type maximums.
	FDrunkardWalkResolvedParams Params = Resolve();

	if (Params.RoomTypes.Num() == 0 || TotalRooms <= 0)
	{
		// No types or no rooms requested — clear any absolute counts from Resolve() and return.
		for (FRoomTypeConfig& Type : Params.RoomTypes)
		{
			Type.Weight = 0;
		}
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[DWConfig] ResolveForTotal(%d): no room types or zero total — empty queue"), TotalRooms);
		return Params;
	}

	// Build Weight / Min / Max arrays from the resolved (clamped) types.
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

	// Distribute TotalRooms: mandatory minimums first, then proportionally by weight up to Max.
	// Falls back to equal distribution if all weights are zero.
	TArray<int32> Counts;
	ProceduralGeometry_DistributePoolByWeight(Counts, Weights, Mins, Maxes, TotalRooms);
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
