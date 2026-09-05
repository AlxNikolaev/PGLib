#pragma once

#include "CoreMinimal.h"

/**
 * Distributes Total across OutCounts proportionally to Weights, resizing OutCounts to Weights.Num().
 * All-zero weights give equal shares. The last element absorbs the rounding remainder, so sum(OutCounts)
 * is exactly Total and every count is >= 0.
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

	int32 TotalWeight = 0;
	for (int32 W : Weights)
	{
		TotalWeight += FMath::Max(0, W);
	}

	if (TotalWeight > 0)
	{
		// Round-to-nearest for every type except the last, which absorbs the remainder.
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
		// All weights zero: equal integer shares, with the last type absorbing the remainder.
		const int32 PerType = Total / Num;
		for (int32 i = 0; i < Num - 1; ++i)
		{
			OutCounts[i] = PerType;
			AllocatedSoFar += PerType;
		}
	}

	OutCounts[Num - 1] = FMath::Max(0, Total - AllocatedSoFar);
}

/**
 * Pool-aware weighted distribution: every type gets its Min first, then the remaining budget is spread by
 * Weight within each type's Max cap (Max == 0 means uncapped). All four arrays share Weights.Num() length,
 * and OutCounts is resized to it. sum(OutCounts) == Budget unless every cap is hit first.
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

	int32 TotalMin = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		OutCounts[i] = FMath::Max(0, Mins.IsValidIndex(i) ? Mins[i] : 0);
		TotalMin += OutCounts[i];
	}

	if (TotalMin >= Budget)
	{
		// Mins already fill the budget: scale them down proportionally, then clamp each to its Max cap.
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

	const int32 Remaining = Budget - TotalMin;

	// Headroom is the room between the current count and the cap; a cap of 0 means unlimited.
	TArray<int32> Headroom;
	Headroom.SetNumUninitialized(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const int32 MaxI = Maxes.IsValidIndex(i) ? Maxes[i] : 0;
		Headroom[i] = (MaxI <= 0) ? (Remaining + 1) : FMath::Max(0, MaxI - OutCounts[i]);
	}

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
		// No positive weight anywhere: distribute equally among the types that still have headroom.
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

	// The last eligible index absorbs the rounding remainder.
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

	const int32 LastAdd = FMath::Clamp(Remaining - Allocated, 0, Headroom[LastEligIdx]);
	OutCounts[LastEligIdx] += LastAdd;
}
