#pragma once

#include "CoreMinimal.h"

/**
 * Distributes Total count across OutCounts proportionally to Weights.
 *
 * Rules:
 *   - If Total <= 0 or Weights is empty, OutCounts is filled with zeros.
 *   - If all weights are zero, distributes equal shares (integer division per type;
 *     last element absorbs the remainder so the sum is exactly Total).
 *   - Otherwise, distributes proportionally using round-to-nearest; last element
 *     absorbs the rounding remainder, guaranteeing sum(OutCounts) == Total exactly.
 *   - All output counts are >= 0.
 *
 * OutCounts is resized to Weights.Num() on return.
 *
 * Shared by FDrunkardWalkConfig::ResolveForTotal() and FOrganicDungeonConfig::ResolveForTotal().
 * Defined as an inline free function to avoid a translation-unit dependency.
 */
inline void ProceduralGeometry_DistributeCountsByWeight(TArray<int32>& OutCounts, const TArray<int32>& Weights, int32 Total)
{
	const int32 Num = Weights.Num();
	OutCounts.SetNumZeroed(Num);

	if (Num == 0 || Total <= 0)
	{
		return;
	}

	int32 AllocatedSoFar = 0;

	// Compute total weight.
	int32 TotalWeight = 0;
	for (int32 W : Weights)
	{
		TotalWeight += FMath::Max(0, W);
	}

	if (TotalWeight > 0)
	{
		// Proportional distribution: round-to-nearest for all types except the last.
		// The last type absorbs the rounding remainder so sum == Total exactly.
		const float Scale = static_cast<float>(Total) / static_cast<float>(TotalWeight);
		for (int32 i = 0; i < Num - 1; ++i)
		{
			const int32 Count = FMath::Max(0, FMath::RoundToInt(static_cast<float>(FMath::Max(0, Weights[i])) * Scale));
			OutCounts[i] = Count;
			AllocatedSoFar += Count;
		}
	}
	else
	{
		// All weights zero — distribute equally (integer division per type).
		// Last type absorbs the remainder.
		const int32 PerType = Total / Num;
		for (int32 i = 0; i < Num - 1; ++i)
		{
			OutCounts[i] = PerType;
			AllocatedSoFar += PerType;
		}
	}

	// Last element absorbs remainder to guarantee sum(OutCounts) == Total.
	OutCounts[Num - 1] = FMath::Max(0, Total - AllocatedSoFar);
}

/**
 * Pool-aware weighted distribution with per-type minimum guarantees and maximum caps.
 *
 * Algorithm:
 *   1. Assigns each type its mandatory Min count.
 *   2. If sum(Mins) >= Budget, scales Mins down proportionally so sum(OutCounts) == Budget.
 *   3. Otherwise distributes the remaining budget (Budget - sum(Mins)) proportionally by
 *      Weight, respecting each type's Max cap (Max == 0 means uncapped). The last eligible
 *      type absorbs the rounding remainder (within its cap).
 *
 * All four arrays must have the same length (Weights.Num()).
 * OutCounts is resized to Weights.Num() on return.
 * sum(OutCounts) == Budget when possible; may be less if all caps are hit before Budget
 * is fully distributed.
 *
 * Used by FDrunkardWalkConfig::ResolveForTotal() and FOrganicDungeonConfig::ResolveForTotal()
 * to implement the two-phase room-count distribution (mandatory minimums first, then
 * weight-proportional fill up to optional per-type maximums).
 */
inline void ProceduralGeometry_DistributePoolByWeight(
	TArray<int32>& OutCounts, const TArray<int32>& Weights, const TArray<int32>& Mins, const TArray<int32>& Maxes, int32 Budget)
{
	const int32 Num = Weights.Num();
	OutCounts.SetNumZeroed(Num);

	if (Num == 0 || Budget <= 0)
	{
		return;
	}

	// Step 1: assign mandatory minimums.
	int32 TotalMin = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		OutCounts[i] = FMath::Max(0, Mins.IsValidIndex(i) ? Mins[i] : 0);
		TotalMin += OutCounts[i];
	}

	if (TotalMin >= Budget)
	{
		// Mins already fill (or exceed) the budget — scale them down proportionally so
		// sum(OutCounts) == Budget, then clamp each to its Max cap.
		TArray<int32> MinWeights;
		MinWeights.SetNumZeroed(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			MinWeights[i] = FMath::Max(0, Mins.IsValidIndex(i) ? Mins[i] : 0);
		}
		ProceduralGeometry_DistributeCountsByWeight(OutCounts, MinWeights, Budget);
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 MaxI = Maxes.IsValidIndex(i) ? Maxes[i] : 0;
			if (MaxI > 0)
			{
				OutCounts[i] = FMath::Min(OutCounts[i], MaxI);
			}
		}
		return;
	}

	// Step 2: distribute the remaining budget by Weight, respecting per-type Max caps.
	const int32 Remaining = Budget - TotalMin;

	// Per-type headroom = room between current count and cap (0 cap → effectively unlimited).
	TArray<int32> Headroom;
	Headroom.SetNumUninitialized(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 MaxI = Maxes.IsValidIndex(i) ? Maxes[i] : 0;
		Headroom[i] = (MaxI <= 0) ? (Remaining + 1) : FMath::Max(0, MaxI - OutCounts[i]);
	}

	// Eligible weights: types with positive weight AND positive headroom.
	TArray<int32> EligWeights;
	EligWeights.SetNumZeroed(Num);
	int32 TotalEligWeight = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		if (Headroom[i] > 0)
		{
			EligWeights[i] = FMath::Max(0, Weights.IsValidIndex(i) ? Weights[i] : 0);
			TotalEligWeight += EligWeights[i];
		}
	}

	if (TotalEligWeight <= 0)
	{
		// No eligible types with positive weight — distribute equally among types with headroom.
		int32 EligCount = 0;
		for (int32 i = 0; i < Num; ++i)
		{
			if (Headroom[i] > 0)
			{
				++EligCount;
			}
		}
		if (EligCount == 0)
		{
			return;
		}
		const int32 PerType = Remaining / EligCount;
		int32		Allocated = 0;
		int32		LastWithHeadroom = INDEX_NONE;
		for (int32 i = 0; i < Num; ++i)
		{
			if (Headroom[i] > 0)
			{
				const int32 Add = FMath::Min(PerType, Headroom[i]);
				OutCounts[i] += Add;
				Allocated += Add;
				LastWithHeadroom = i;
			}
		}
		if (LastWithHeadroom != INDEX_NONE)
		{
			OutCounts[LastWithHeadroom] += FMath::Min(Remaining - Allocated, Headroom[LastWithHeadroom]);
		}
		return;
	}

	// Find the last eligible index (it absorbs the rounding remainder).
	int32 LastEligIdx = INDEX_NONE;
	for (int32 i = Num - 1; i >= 0; --i)
	{
		if (EligWeights[i] > 0)
		{
			LastEligIdx = i;
			break;
		}
	}
	if (LastEligIdx == INDEX_NONE)
	{
		return;
	}

	const float Scale = static_cast<float>(Remaining) / static_cast<float>(TotalEligWeight);
	int32		Allocated = 0;

	for (int32 i = 0; i < Num; ++i)
	{
		if (EligWeights[i] <= 0 || i == LastEligIdx)
		{
			continue;
		}
		const int32 Raw = FMath::RoundToInt(static_cast<float>(EligWeights[i]) * Scale);
		const int32 Add = FMath::Clamp(Raw, 0, Headroom[i]);
		OutCounts[i] += Add;
		Allocated += Add;
	}

	// Last eligible absorbs remainder (capped at its headroom).
	const int32 LastAdd = FMath::Clamp(Remaining - Allocated, 0, Headroom[LastEligIdx]);
	OutCounts[LastEligIdx] += LastAdd;
}

/**
 * Weighted-random pick from a Remaining counts array, excluding ExcludedIdx (-1 = no exclusion).
 * Uses Remaining[i] as the probability weight for each entry — higher remaining count means
 * higher selection probability. Returns INDEX_NONE if no eligible entry has a positive count.
 *
 * RandRange(MaxInclusive) must return a uniformly distributed integer in [0, MaxInclusive].
 *
 * Extracted from UOrganicDungeonGenerator2D queue-building (bShuffleRoomOrder branch) so the
 * same weighted-selection logic can be shared with UDrunkardWalkGenerator2D and future generators.
 */
template <typename FRandRangeFunc>
inline int32 ProceduralGeometry_PickWeightedType(const TArray<int32>& Remaining, int32 ExcludedIdx, FRandRangeFunc&& RandRange)
{
	int32 TotalWeight = 0;
	for (int32 i = 0; i < Remaining.Num(); ++i)
	{
		if (Remaining[i] > 0 && i != ExcludedIdx)
		{
			TotalWeight += Remaining[i];
		}
	}
	if (TotalWeight <= 0)
	{
		return INDEX_NONE;
	}

	int32 Roll = RandRange(TotalWeight - 1);
	for (int32 i = 0; i < Remaining.Num(); ++i)
	{
		if (Remaining[i] <= 0 || i == ExcludedIdx)
		{
			continue;
		}
		Roll -= Remaining[i];
		if (Roll < 0)
		{
			return i;
		}
	}
	return INDEX_NONE; // should not reach here
}
