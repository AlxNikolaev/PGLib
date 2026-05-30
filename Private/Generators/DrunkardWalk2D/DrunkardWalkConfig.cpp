#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"

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

	// Copy and clamp room types; drop zero-count or degenerate entries.
	Params.RoomTypes.Reserve(RoomTypes.Num());
	int32 TotalRooms = 0;
	for (const FRoomTypeConfig& Type : RoomTypes)
	{
		if (Type.Count <= 0)
		{
			continue;
		}

		FRoomTypeConfig Clamped = Type;
		Clamped.FootprintWidthCells = FMath::Max(1, Type.FootprintWidthCells);
		Clamped.FootprintHeightCells = FMath::Max(1, Type.FootprintHeightCells);
		Clamped.Count = FMath::Max(0, Type.Count);
		Params.RoomTypes.Add(Clamped);
		TotalRooms += Clamped.Count;
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
