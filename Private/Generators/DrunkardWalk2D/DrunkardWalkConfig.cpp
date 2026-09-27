#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"

#include "SelectionRules.h"
#include "ProceduralGeometry.h"

FDrunkardWalkResolvedParams FDrunkardWalkConfig::ResolveForTotal(int32 TotalRooms) const
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

	TArray<VariatSelection::FCandidate> Candidates;
	for (const FRoomTypeConfig& Type : RoomTypes)
		Candidates.Add({ Type.SelectionWeight, Type.MinCount, Type.MaxCount, Type.bLimitCount });
	const auto Distribution = VariatSelection::AllocateProportionalBudget(Candidates, TotalRooms);
	if (!Distribution.Error.IsEmpty())
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("DW room budget: %s (%d unfilled)"), *Distribution.Error, Distribution.UnfilledCount);
	for (int32 I = 0; I < RoomTypes.Num(); ++I)
	{
		const auto& Type = RoomTypes[I];
		Params.RoomTypes.Add({ Type.Tag, FMath::Max(1, Type.FootprintWidthCells), FMath::Max(1, Type.FootprintHeightCells), Distribution.Counts[I] });
	}
	return Params;
}
