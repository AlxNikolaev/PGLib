#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Generators/WeightedDistribute.h"
#include "ProceduralGeometry.h"

namespace
{
	constexpr float GDefaultFootprint = 600.0f;

	// Measuring a footprint loads the whole prefab UWorld synchronously, so cache the result per level path.
	// Resolve() runs on the game thread (sync pipeline + async Prepare), but guard the cache anyway.
	struct FCachedFootprint
	{
		float	  W = 0.0f;
		float	  H = 0.0f;
		FVector2D Center = FVector2D::ZeroVector;
		bool	  bValid = false;
	};
	FCriticalSection						GFootprintCacheLock;
	TMap<FSoftObjectPath, FCachedFootprint> GFootprintCache;

	/** Loads a level asset and measures its XY footprint (size + center offset) from its actors' bounds. Cached. */
	bool MeasureLevelFootprintXY(const TSoftObjectPtr<UWorld>& LevelRef, float& OutW, float& OutH, FVector2D& OutCenter)
	{
		if (LevelRef.IsNull())
		{
			return false;
		}

		const FSoftObjectPath Key = LevelRef.ToSoftObjectPath();
		{
			FScopeLock Lock(&GFootprintCacheLock);
			if (const FCachedFootprint* Cached = GFootprintCache.Find(Key))
			{
				OutW = Cached->W;
				OutH = Cached->H;
				OutCenter = Cached->Center;
				return Cached->bValid;
			}
		}

		FCachedFootprint Result;
		if (UWorld* World = LevelRef.LoadSynchronous(); World && World->PersistentLevel)
		{
			FBox Box(ForceInit);
			bool bAny = false;
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				if (!Actor)
				{
					continue;
				}
				const FBox ActorBox = Actor->GetComponentsBoundingBox(true);
				if (ActorBox.IsValid)
				{
					Box += ActorBox;
					bAny = true;
				}
			}
			if (bAny)
			{
				const FVector Size = Box.GetSize();
				const FVector Center = Box.GetCenter();
				Result.W = static_cast<float>(Size.X);
				Result.H = static_cast<float>(Size.Y);
				Result.Center = FVector2D(Center.X, Center.Y);
				Result.bValid = Result.W > 1.0f && Result.H > 1.0f;
			}
		}

		{
			FScopeLock Lock(&GFootprintCacheLock);
			GFootprintCache.Add(Key, Result);
		}

		OutW = Result.W;
		OutH = Result.H;
		OutCenter = Result.Center;
		return Result.bValid;
	}

	/** Resolves a room type's level ref, doorways, display name, footprint, and baked doorway declarations. */
	void ResolveRoomFootprint(const FOrganicRoomType& Type, FOrganicResolvedRoomType& Out)
	{
		Out.RoomLevel = Type.RoomLevel;
		Out.DisplayName = Type.RoomLevel.IsNull() ? FName(TEXT("<unset>")) : FName(*Type.RoomLevel.GetAssetName());

		// Copy baked doorway declarations. Non-empty → declared-doorway room (no synthetic generation).
		Out.Doorways = Type.BakedDoorways;

		float	  MW = 0.0f;
		float	  MH = 0.0f;
		FVector2D MCenter = FVector2D::ZeroVector;
		if (Type.FootprintOverride.X > 0.0f && Type.FootprintOverride.Y > 0.0f)
		{
			Out.FootprintWidth = Type.FootprintOverride.X;
			Out.FootprintHeight = Type.FootprintOverride.Y;
		}
		else if (MeasureLevelFootprintXY(Type.RoomLevel, MW, MH, MCenter))
		{
			Out.FootprintWidth = MW;
			Out.FootprintHeight = MH;
			Out.FootprintCenter = MCenter;
		}
		else
		{
			Out.FootprintWidth = GDefaultFootprint;
			Out.FootprintHeight = GDefaultFootprint;
			UE_LOG(LogRoguelikeGeometry,
				Warning,
				TEXT("[ORG] Room type '%s' has no measurable level bounds and no override — using %.0f x %.0f."),
				*Out.DisplayName.ToString(),
				GDefaultFootprint,
				GDefaultFootprint);
		}
	}
} // namespace

FOrganicDungeonResolvedParams FOrganicDungeonConfig::Resolve() const
{
	FOrganicDungeonResolvedParams Params;

	// Rooms — drop zero-weight; resolve footprint from level bounds (or override / fallback).
	// Weight / Min / Max are copied into the resolved struct for later use by ResolveForTotal().
	int32 TotalRooms = 0;
	Params.RoomTypes.Reserve(RoomTypes.Num());
	for (const FOrganicRoomType& Type : RoomTypes)
	{
		if (Type.Weight <= 0)
		{
			continue;
		}

		FOrganicResolvedRoomType Resolved;
		Resolved.Count = FMath::Max(0, Type.Weight); // Weight = absolute count in direct-resolve mode
		Resolved.Weight = FMath::Max(0, Type.Weight);
		Resolved.Min = FMath::Max(0, Type.Min);
		Resolved.Max = FMath::Max(0, Type.Max);
		ResolveRoomFootprint(Type, Resolved);

		Params.RoomTypes.Add(Resolved);
		TotalRooms += Resolved.Count;
	}

	// Special entrance room (optional): present if a level is assigned or a footprint override is set.
	auto HasRoom = [](const FOrganicRoomType& T) { return !T.RoomLevel.IsNull() || (T.FootprintOverride.X > 0.0f && T.FootprintOverride.Y > 0.0f); };
	Params.bHasStartRoom = HasRoom(StartRoom);
	if (Params.bHasStartRoom)
	{
		ResolveRoomFootprint(StartRoom, Params.StartRoom);
	}

	// Exit terminus: resolve the portal-room prefab (optional). All anchors fall back to PortalStub when null.
	Params.ExitTerminusForm = DefaultExitTerminusForm;
	Params.bHasExitPortalRoom = HasRoom(ExitPortalRoom);
	if (Params.bHasExitPortalRoom)
	{
		ResolveRoomFootprint(ExitPortalRoom, Params.ExitPortalRoom);
	}

	Params.MinRoomGap = FMath::Max(0.0f, MinRoomGap);
	Params.MaxPlacementAttempts = FMath::Max(1, MaxPlacementAttempts);
	Params.bRandomRotation = bRandomRotation;
	Params.BranchProbability = FMath::Clamp(BranchProbability, 0.0f, 1.0f);
	Params.bShuffleRoomOrder = bShuffleRoomOrder;

	Params.CorridorStyle = CorridorStyle;
	Params.MinThickness = FMath::Max(1.0f, MinThickness);
	Params.MaxWidth = FMath::Max(Params.MinThickness, MaxWidth);
	Params.Waviness = FMath::Clamp(Waviness, 0.0f, 1.0f);
	Params.CorridorLengthMin = FMath::Max(1.0f, CorridorLengthMin);
	Params.CorridorLengthMax = FMath::Max(Params.CorridorLengthMin, CorridorLengthMax);

	Params.LoopCount = FMath::Max(0, LoopCount);
	Params.LoopMaxDistance = FMath::Max(0.0f, LoopMaxDistance);
	Params.SpineWidthScale = FMath::Max(1.0f, SpineWidthScale);
	Params.DeadEndCount = FMath::Max(0, DeadEndCount);
	Params.DeadEndLength = FMath::Max(1.0f, DeadEndLength);
	Params.CorridorLinkCount = FMath::Max(0, CorridorLinkCount);
	Params.LinkMaxDistance = FMath::Max(1.0f, LinkMaxDistance);

	Params.WallThickness = FMath::Max(1, WallThickness);
	Params.bSmoothCorridors = bSmoothCorridors;
	Params.SmoothIterations = FMath::Clamp(SmoothIterations, 1, 6);

	UE_LOG(LogRoguelikeGeometry,
		Verbose,
		TEXT("[ORG] Resolve: %d room types (%d rooms), style=%d, thickness=[%.0f,%.0f], wav=%.2f, corridorLen=[%.0f,%.0f], branch=%.2f, "
			 "loops=%d/%.0f spine=%.2f deadEnds=%d links=%d wall=%d smooth=%s"),
		Params.RoomTypes.Num(),
		TotalRooms,
		static_cast<int32>(Params.CorridorStyle),
		Params.MinThickness,
		Params.MaxWidth,
		Params.Waviness,
		Params.CorridorLengthMin,
		Params.CorridorLengthMax,
		Params.BranchProbability,
		Params.LoopCount,
		Params.LoopMaxDistance,
		Params.SpineWidthScale,
		Params.DeadEndCount,
		Params.CorridorLinkCount,
		Params.WallThickness,
		Params.bSmoothCorridors ? TEXT("true") : TEXT("false"));

	return Params;
}

FOrganicDungeonResolvedParams FOrganicDungeonConfig::ResolveForTotal(const int32 TotalRooms) const
{
	// Resolve all params (including footprint measurement) first, then redistribute the regular room
	// counts using the pool-aware distribution: mandatory minimums first, then proportionally by
	// weight up to per-type maximums. Start/end special rooms are unaffected — always placed if set.
	FOrganicDungeonResolvedParams Params = Resolve();

	if (Params.RoomTypes.Num() == 0 || TotalRooms <= 0)
	{
		for (FOrganicResolvedRoomType& RT : Params.RoomTypes)
		{
			RT.Count = 0;
		}
		UE_LOG(LogRoguelikeGeometry, Verbose, TEXT("[ORG] ResolveForTotal(%d): no room types or zero total — empty queue"), TotalRooms);
		return Params;
	}

	// Build Weight / Min / Max arrays from resolved types (copied in from Resolve()).
	TArray<int32> Weights, Mins, Maxes;
	Weights.Reserve(Params.RoomTypes.Num());
	Mins.Reserve(Params.RoomTypes.Num());
	Maxes.Reserve(Params.RoomTypes.Num());
	int32 TotalWeight = 0;
	for (const FOrganicResolvedRoomType& RT : Params.RoomTypes)
	{
		const int32 W = FMath::Max(0, RT.Weight);
		Weights.Add(W);
		Mins.Add(FMath::Max(0, RT.Min));
		Maxes.Add(FMath::Max(0, RT.Max));
		TotalWeight += W;
	}

	// Distribute TotalRooms: mandatory minimums first, then proportionally by weight up to Max.
	TArray<int32> Counts;
	ProceduralGeometry_DistributePoolByWeight(Counts, Weights, Mins, Maxes, TotalRooms);
	for (int32 i = 0; i < Params.RoomTypes.Num(); ++i)
	{
		Params.RoomTypes[i].Count = Counts[i];
	}

	UE_LOG(LogRoguelikeGeometry,
		Verbose,
		TEXT("[ORG] ResolveForTotal(%d): %d room types, pool distribution applied (total weight was %d)"),
		TotalRooms,
		Params.RoomTypes.Num(),
		TotalWeight);

	return Params;
}
