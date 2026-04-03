#include "Generators/DrunkardWalk2D/DrunkardWalkConfig.h"

#include "ProceduralGeometry.h"

namespace
{
	/** Base parameter set for a single EDungeonStyle entry. */
	struct FDungeonStylePreset
	{
		int32 BaseWalkLength;
		int32 BaseNumWalkers;
		float BaseBranchProb;
		int32 BaseCorridorWidth;
		float BaseRoomChance;
		int32 BaseRoomRadius;
		int32 BaseMinRoomSpacing;
		int32 BaseMaxRoomCount;
	};

	/** Returns the base parameters for the given dungeon style. */
	const FDungeonStylePreset& GetDungeonStylePreset(EDungeonStyle Style)
	{
		static const FDungeonStylePreset OpenRuins = { 1000, 3, 0.10f, 3, 0.15f, 3, 8, 6 };
		static const FDungeonStylePreset TightMaze = { 500, 1, 0.00f, 1, 0.00f, 1, 0, 0 };
		static const FDungeonStylePreset RoomHeavy = { 800, 2, 0.05f, 2, 0.30f, 4, 5, 12 };
		static const FDungeonStylePreset Labyrinth = { 600, 4, 0.30f, 1, 0.05f, 2, 10, 3 };
		static const FDungeonStylePreset Caverns = { 1200, 2, 0.15f, 4, 0.10f, 5, 6, 4 };

		switch (Style)
		{
			case EDungeonStyle::OpenRuins:
				return OpenRuins;
			case EDungeonStyle::TightMaze:
				return TightMaze;
			case EDungeonStyle::RoomHeavy:
				return RoomHeavy;
			case EDungeonStyle::Labyrinth:
				return Labyrinth;
			case EDungeonStyle::Caverns:
				return Caverns;
			default:
				return OpenRuins;
		}
	}

	/** Scale preset mapping for corridor width and room radius range. */
	struct FDungeonScalePreset
	{
		int32 CorridorWidth;
		int32 RoomRadiusMin;
		int32 RoomRadiusMax;
	};

	/** Returns the corridor width and room radius range for the given dungeon scale. */
	FDungeonScalePreset GetDungeonScalePreset(EDungeonScale Scale)
	{
		switch (Scale)
		{
			case EDungeonScale::Small:
				return { 1, 1, 2 };
			case EDungeonScale::Medium:
				return { 2, 2, 3 };
			case EDungeonScale::Large:
				return { 4, 4, 5 };
			default:
				return { 2, 2, 3 };
		}
	}

} // namespace

FDrunkardWalkResolvedParams FDrunkardWalkConfig::Resolve() const
{
	FDrunkardWalkResolvedParams Params;

	if (bUseAdvancedOverride)
	{
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[DWConfig] Resolve: using advanced override"));

		Params.WalkLength = FMath::Max(1, AdvancedWalkLength);
		Params.NumWalkers = FMath::Max(1, AdvancedNumWalkers);
		Params.BranchProbability = FMath::Clamp(AdvancedBranchProbability, 0.0f, 1.0f);
		Params.CorridorWidth = FMath::Max(1, AdvancedCorridorWidth);
		Params.RoomChance = FMath::Clamp(AdvancedRoomChance, 0.0f, 1.0f);
		Params.RoomRadiusMin = FMath::Max(1, AdvancedRoomRadiusMin);
		Params.RoomRadiusMax = FMath::Max(Params.RoomRadiusMin, FMath::Max(1, AdvancedRoomRadius));
		Params.MinRoomSpacing = FMath::Max(0, AdvancedMinRoomSpacing);
		Params.MaxRoomCount = FMath::Max(0, AdvancedMaxRoomCount);
		Params.DirectionalMomentum = FMath::Clamp(AdvancedDirectionalMomentum, 0.0f, 1.0f);
		Params.ExplorationBias = FMath::Clamp(AdvancedExplorationBias, 0.0f, 1.0f);
	}
	else
	{
		const FDungeonStylePreset& StylePreset = GetDungeonStylePreset(DungeonStyle);

		// Density modulation: scales WalkLength +/- 50% from base
		// At 0.0: base * 0.5, at 0.5: base * 1.0, at 1.0: base * 1.5
		Params.WalkLength = FMath::Max(1, FMath::RoundToInt(StylePreset.BaseWalkLength * (0.5f + Density)));

		// Complexity modulation: scales NumWalkers +/- 50% from base
		Params.NumWalkers = FMath::Max(1, FMath::RoundToInt(StylePreset.BaseNumWalkers * (0.5f + Complexity)));

		// Complexity modulation: BranchProbability linear from 0 to 2x base
		// At 0.0: 0, at 0.5: base, at 1.0: 2x base
		Params.BranchProbability = FMath::Clamp(StylePreset.BaseBranchProb * Complexity * 2.0f, 0.0f, 1.0f);

		// RoomFrequency modulation: RoomChance linear from 0 to 2x base
		Params.RoomChance = FMath::Clamp(StylePreset.BaseRoomChance * RoomFrequency * 2.0f, 0.0f, 1.0f);

		// Scale resolution: corridor width and room radius range from scale preset
		const FDungeonScalePreset ScalePreset = GetDungeonScalePreset(Scale);
		Params.CorridorWidth = ScalePreset.CorridorWidth;
		Params.RoomRadiusMin = ScalePreset.RoomRadiusMin;
		Params.RoomRadiusMax = ScalePreset.RoomRadiusMax;

		// Room spacing and count from style preset; MaxRoomCount modulated by RoomFrequency (0-2x)
		Params.MinRoomSpacing = StylePreset.BaseMinRoomSpacing;
		Params.MaxRoomCount =
			StylePreset.BaseMaxRoomCount > 0 ? FMath::Max(1, FMath::RoundToInt(StylePreset.BaseMaxRoomCount * RoomFrequency * 2.0f)) : 0;

		// Momentum and bias: direct pass-through (orthogonal to style presets)
		Params.DirectionalMomentum = FMath::Clamp(DirectionalMomentum, 0.0f, 1.0f);
		Params.ExplorationBias = FMath::Clamp(ExplorationBias, 0.0f, 1.0f);

		UE_LOG(LogRoguelikeGeometry,
			Verbose,
			TEXT("[DWConfig] Resolve: style=%d, density=%.2f, complexity=%.2f, roomFreq=%.2f, scale=%d -> walk=%d, walkers=%d, branch=%.2f, "
				 "corridor=%d, roomChance=%.2f, roomRadius=[%d,%d], spacing=%d, maxRooms=%d, momentum=%.2f, bias=%.2f"),
			static_cast<int32>(DungeonStyle),
			Density,
			Complexity,
			RoomFrequency,
			static_cast<int32>(Scale),
			Params.WalkLength,
			Params.NumWalkers,
			Params.BranchProbability,
			Params.CorridorWidth,
			Params.RoomChance,
			Params.RoomRadiusMin,
			Params.RoomRadiusMax,
			Params.MinRoomSpacing,
			Params.MaxRoomCount,
			Params.DirectionalMomentum,
			Params.ExplorationBias);
	}

	return Params;
}
