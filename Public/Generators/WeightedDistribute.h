#pragma once
#include "SelectionRules.h"

/** Deterministic proportional allocation. All-zero weights produce no extra instances. */
inline void ProceduralGeometry_DistributeCountsByWeight(TArray<int32>& OutCounts, const TArray<int32>& Weights, int32 Total)
{
	TArray<VariatSelection::FCandidate> Candidates;
	for (int32 Weight : Weights)
		Candidates.Add({ static_cast<float>(Weight), 0, 0, false });
	OutCounts = VariatSelection::AllocateProportionalBudget(Candidates, Total).Counts;
}

/** Legacy C++ adapter: zero Max in these arrays is an uncapped bound, never an amount. */
inline void ProceduralGeometry_DistributePoolByWeight(
	TArray<int32>& OutCounts, const TArray<int32>& Weights, const TArray<int32>& Mins, const TArray<int32>& Maxes, int32 Budget)
{
	TArray<VariatSelection::FCandidate> Candidates;
	for (int32 I = 0; I < Weights.Num(); ++I)
		Candidates.Add({ static_cast<float>(Weights[I]),
			Mins.IsValidIndex(I) ? Mins[I] : 0,
			Maxes.IsValidIndex(I) ? Maxes[I] : 0,
			Maxes.IsValidIndex(I) && Maxes[I] != 0 });
	OutCounts = VariatSelection::AllocateProportionalBudget(Candidates, Budget).Counts;
}
