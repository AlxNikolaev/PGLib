#include "Generators/OrganicDungeon2D/OrganicDungeonConfig.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ProceduralGeometry.h"

namespace
{
	constexpr float GDefaultFootprint = 600.0f;

	/** Loads a level asset and measures its XY footprint (size + center offset) from its actors' bounds. */
	bool MeasureLevelFootprintXY(const TSoftObjectPtr<UWorld>& LevelRef, float& OutW, float& OutH, FVector2D& OutCenter)
	{
		if (LevelRef.IsNull())
		{
			return false;
		}
		UWorld* World = LevelRef.LoadSynchronous();
		if (!World || !World->PersistentLevel)
		{
			return false;
		}
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
		if (!bAny)
		{
			return false;
		}
		const FVector Size = Box.GetSize();
		const FVector Center = Box.GetCenter();
		OutW = static_cast<float>(Size.X);
		OutH = static_cast<float>(Size.Y);
		OutCenter = FVector2D(Center.X, Center.Y);
		return OutW > 1.0f && OutH > 1.0f;
	}

	/** Resolves a room type's level ref, doorways, display name, and footprint (override → measured → fallback). */
	void ResolveRoomFootprint(const FOrganicRoomType& Type, FOrganicResolvedRoomType& Out)
	{
		Out.RoomLevel = Type.RoomLevel;
		Out.DoorwaysPerEdge = FMath::Max(1, Type.DoorwaysPerEdge);
		Out.DisplayName = Type.RoomLevel.IsNull() ? FName(TEXT("<unset>")) : FName(*Type.RoomLevel.GetAssetName());

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

	// Rooms — drop zero-count; resolve footprint from level bounds (or override / fallback).
	int32 TotalRooms = 0;
	Params.RoomTypes.Reserve(RoomTypes.Num());
	for (const FOrganicRoomType& Type : RoomTypes)
	{
		if (Type.Count <= 0)
		{
			continue;
		}

		FOrganicResolvedRoomType Resolved;
		Resolved.Count = FMath::Max(0, Type.Count);
		ResolveRoomFootprint(Type, Resolved);

		Params.RoomTypes.Add(Resolved);
		TotalRooms += Resolved.Count;
	}

	// Special start/end rooms (optional): present if a level is assigned or a footprint override is set.
	auto HasRoom = [](const FOrganicRoomType& T) { return !T.RoomLevel.IsNull() || (T.FootprintOverride.X > 0.0f && T.FootprintOverride.Y > 0.0f); };
	Params.bHasStartRoom = HasRoom(StartRoom);
	if (Params.bHasStartRoom)
	{
		ResolveRoomFootprint(StartRoom, Params.StartRoom);
	}
	Params.bHasEndRoom = HasRoom(EndRoom);
	if (Params.bHasEndRoom)
	{
		ResolveRoomFootprint(EndRoom, Params.EndRoom);
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
