#include "Generators/OrganicDungeon2D/OrganicDungeon2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrganicViz, Log, All);

namespace
{
	FVector2D RotateDeg2D(const FVector2D& V, float Deg)
	{
		const float R = FMath::DegreesToRadians(Deg);
		const float C = FMath::Cos(R);
		const float S = FMath::Sin(R);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}
} // namespace

AOrganicDungeon2DVisualizer::AOrganicDungeon2DVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;
	GridMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GridMeshComponent"));
	SetRootComponent(GridMeshComponent);
}

void AOrganicDungeon2DVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const FVector	ActorLoc = GetActorLocation();
	const FVector2D Center2D(ActorLoc.X, ActorLoc.Y);

	FOrganicDungeonResolvedParams ResolvedParams = Config.Resolve();

	UOrganicDungeonGenerator2D* Generator = NewObject<UOrganicDungeonGenerator2D>();
	Generator->SetSeed(Seed)->SetGridSize(GridSize)->SetCenter(Center2D)->ApplyResolvedParams(ResolvedParams);

	const double			StartTime = FPlatformTime::Seconds();
	FOrganicDungeonGridData GridData = Generator->GenerateWithGridData();
	const double			GenMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	FlushPersistentDebugLines(GetWorld());
	FlushDebugStrings(GetWorld());
	GridMeshComponent->ClearAllMeshSections();

	if (GridData.Rooms.Num() == 0)
	{
		UE_LOG(LogOrganicViz, Warning, TEXT("OnConstruction: empty layout (no rooms), skipping."));
		return;
	}

	const float CS = GridData.CellSize;

	// OrganicDungeon is gridless: there is no rasterized cell mesh. The room/junction floor is built by PCG
	// from the room-boundary + junction-perimeter splines. The in-editor debug draw below shows only the
	// smooth corridor centerlines, the room footprints/doorways, and the junction perimeters.

	// Corridor centerline splines (world space, Z=3)
	if (bShowCorridorSplines)
	{
		for (const FOrganicCorridor& Cor : GridData.Corridors)
		{
			FColor LineColor = FColor::Cyan;
			float  LineThickness = 2.0f;
			if (Cor.bIsSpine)
			{
				LineColor = FColor::White; // critical path
				LineThickness = 5.0f;
			}
			else if (Cor.bIsLink)
			{
				LineColor = FColor::Magenta;
			}
			else if (Cor.bIsLoop)
			{
				LineColor = FColor::Yellow;
			}
			else if (Cor.AnchorB.Type == EOrganicAnchorType::Free)
			{
				LineColor = FColor::Red;
			}
			for (int32 i = 0; i + 1 < Cor.Centerline.Num(); ++i)
			{
				const FVector P1(Cor.Centerline[i].X, Cor.Centerline[i].Y, ActorLoc.Z + 3.0f);
				const FVector P2(Cor.Centerline[i + 1].X, Cor.Centerline[i + 1].Y, ActorLoc.Z + 3.0f);
				DrawDebugLine(GetWorld(), P1, P2, LineColor, true, -1.f, 0, LineThickness);
			}
		}
	}

	// Junction hubs (deformed-circle network nodes, world space, Z=3) — magenta closed perimeter polylines.
	if (bShowCorridorSplines)
	{
		for (const FOrganicJunction& Junction : GridData.Junctions)
		{
			const int32 NumPts = Junction.Perimeter.Num();
			for (int32 i = 0; i < NumPts; ++i)
			{
				const FVector2D A = Junction.Perimeter[i];
				const FVector2D B = Junction.Perimeter[(i + 1) % NumPts];
				DrawDebugLine(GetWorld(),
					FVector(A.X, A.Y, ActorLoc.Z + 3.0f),
					FVector(B.X, B.Y, ActorLoc.Z + 3.0f),
					FColor(155, 89, 182),
					true,
					-1.f,
					0,
					3.0f);
			}
		}
	}

	// Doorways (world space, Z=4)
	// Declared doorways (bDeclared=true) are drawn in cyan with a width indicator so authors can
	// verify alignment between markers and the generated corridor endpoints.
	// Legacy synthetic doorways are drawn in green (unchanged).
	if (bShowDoorways)
	{
		for (const FOrganicRoom& Room : GridData.Rooms)
		{
			for (const FOrganicDoorway& D : Room.Doorways)
			{
				if (!D.bUsed)
				{
					continue;
				}
				const FVector Pos(D.Pos.X, D.Pos.Y, ActorLoc.Z + 4.0f);
				const FVector NEnd = Pos + FVector(D.OutwardNormal.X, D.OutwardNormal.Y, 0.0f) * (CS * 1.5f);
				const FColor  DoorColor = D.bDeclared ? FColor(0, 220, 220) : FColor::Green; // cyan vs green
				const float	  DotSize = D.bDeclared ? 16.0f : 12.0f;

				DrawDebugPoint(GetWorld(), Pos, DotSize, DoorColor, true);
				DrawDebugLine(GetWorld(), Pos, NEnd, DoorColor, true, -1.f, 0, D.bDeclared ? 3.0f : 1.5f);

				// For declared doorways draw a width bar perpendicular to the outward normal so
				// authors can verify the opening width against the prefab geometry.
				if (D.bDeclared && D.Width > 0.0f)
				{
					const FVector2D Perp(-D.OutwardNormal.Y, D.OutwardNormal.X);
					const float		HalfW = D.Width * 0.5f;
					const FVector	WA(D.Pos.X + Perp.X * HalfW, D.Pos.Y + Perp.Y * HalfW, ActorLoc.Z + 4.0f);
					const FVector	WB(D.Pos.X - Perp.X * HalfW, D.Pos.Y - Perp.Y * HalfW, ActorLoc.Z + 4.0f);
					DrawDebugLine(GetWorld(), WA, WB, FColor(0, 180, 180), true, -1.f, 0, 2.0f);
				}
			}

			// Show free (unused) declared doorways in a dimmer tint so authors can see which openings
			// have no corridor attached (potential wall-gen concern / content issue).
			for (const FOrganicDoorway& D : Room.Doorways)
			{
				if (D.bUsed || !D.bDeclared)
				{
					continue;
				}
				const FVector Pos(D.Pos.X, D.Pos.Y, ActorLoc.Z + 4.0f);
				DrawDebugPoint(GetWorld(), Pos, 10.0f, FColor(0, 120, 120), true); // dim cyan
			}
		}
	}

	// Room oriented footprint outlines + centers (world space, Z=5)
	if (bShowRoomBounds)
	{
		for (const FOrganicRoom& Room : GridData.Rooms)
		{
			const float		R = FMath::DegreesToRadians(Room.RotationDeg);
			const FVector2D AxisX(FMath::Cos(R), FMath::Sin(R));
			const FVector2D AxisY(-FMath::Sin(R), FMath::Cos(R));
			FVector2D		Corners[4];
			Corners[0] = Room.Center + AxisX * Room.HalfExtent.X + AxisY * Room.HalfExtent.Y;
			Corners[1] = Room.Center - AxisX * Room.HalfExtent.X + AxisY * Room.HalfExtent.Y;
			Corners[2] = Room.Center - AxisX * Room.HalfExtent.X - AxisY * Room.HalfExtent.Y;
			Corners[3] = Room.Center + AxisX * Room.HalfExtent.X - AxisY * Room.HalfExtent.Y;
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector2D A = Corners[i];
				const FVector2D B = Corners[(i + 1) % 4];
				DrawDebugLine(
					GetWorld(), FVector(A.X, A.Y, ActorLoc.Z + 5.0f), FVector(B.X, B.Y, ActorLoc.Z + 5.0f), FColor::White, true, -1.f, 0, 2.0f);
			}
			DrawDebugPoint(GetWorld(), FVector(Room.Center.X, Room.Center.Y, ActorLoc.Z + 5.0f), 8.0f, FColor::Orange, true);
		}

		// Exit room: draw a red marker at the single exit room center (graph-diameter far endpoint).
		if (GridData.Rooms.IsValidIndex(GridData.EndRoomIndex))
		{
			const FOrganicRoom& EndRoom = GridData.Rooms[GridData.EndRoomIndex];
			DrawDebugPoint(GetWorld(), FVector(EndRoom.Center.X, EndRoom.Center.Y, ActorLoc.Z + 6.0f), 14.0f, FColor::Red, true);
		}
	}

	// Metrics overlay
	if (bShowMetrics)
	{
		int32 Spine = 0;
		int32 Loops = 0;
		int32 DeadEnds = 0;
		int32 Links = 0;
		for (const FOrganicCorridor& Cor : GridData.Corridors)
		{
			if (Cor.bIsSpine)
			{
				++Spine;
			}
			else if (Cor.bIsLink)
			{
				++Links;
			}
			else if (Cor.bIsLoop)
			{
				++Loops;
			}
			else if (Cor.AnchorB.Type == EOrganicAnchorType::Free)
			{
				++DeadEnds;
			}
		}

		const float LineSpacing = CS * 1.5f;
		FVector		Anchor(ActorLoc.X, ActorLoc.Y - LineSpacing, ActorLoc.Z + 15.0f);
		int32		LineIndex = 0;
		auto		DrawLine = [&](const FString& Text) {
			   DrawDebugString(GetWorld(), Anchor + FVector(0.0f, -LineSpacing * LineIndex, 0.0f), Text, nullptr, FColor::White, -1.0f, true);
			   ++LineIndex;
		};

		DrawLine(TEXT("[Organic Dungeon]"));
		DrawLine(FString::Printf(TEXT("Cell: %.0f | Cells: %d"), CS, GridData.Diagram.Cells.Num()));
		DrawLine(FString::Printf(TEXT("Rooms: %d/%d | Corridors: %d (spine %d, loops %d, deadEnds %d, links %d)"),
			GridData.Rooms.Num(),
			GridData.RequestedRoomCount,
			GridData.Corridors.Num(),
			Spine,
			Loops,
			DeadEnds,
			Links));
		DrawLine(FString::Printf(TEXT("Junctions: %d | Gen: %.2f ms"), GridData.Junctions.Num(), GenMs));
		DrawLine(FString::Printf(TEXT("Seed: \"%s\""), *Seed));
	}

	UE_LOG(LogOrganicViz,
		Verbose,
		TEXT("OnConstruction: %d cells, %d rooms, %d corridors, %d junctions."),
		GridData.Diagram.Cells.Num(),
		GridData.Rooms.Num(),
		GridData.Corridors.Num(),
		GridData.Junctions.Num());
}

void AOrganicDungeon2DVisualizer::SpawnLevelInstances()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClearSpawnedInstances();

	const FVector	ActorLoc = GetActorLocation();
	const FVector2D Center2D(ActorLoc.X, ActorLoc.Y);

	FOrganicDungeonResolvedParams ResolvedParams = Config.Resolve();
	UOrganicDungeonGenerator2D*	  Generator = NewObject<UOrganicDungeonGenerator2D>();
	Generator->SetSeed(Seed)->SetGridSize(GridSize)->SetCenter(Center2D)->ApplyResolvedParams(ResolvedParams);

	const FOrganicDungeonGridData GridData = Generator->GenerateWithGridData();

	int32 SpawnedCount = 0;
	for (const FOrganicRoom& Room : GridData.Rooms)
	{
		if (Room.RoomLevel.IsNull())
		{
			continue;
		}
		// Position the level so its bounds-center lands on the room's footprint center.
		const FVector2D Offset = RotateDeg2D(Room.FootprintCenterOffset, Room.RotationDeg);
		const FVector	SpawnPos(Room.Center.X - Offset.X, Room.Center.Y - Offset.Y, ActorLoc.Z);
		const FRotator	SpawnRot(0.0f, Room.RotationDeg, 0.0f);

		ALevelInstance* Instance = World->SpawnActor<ALevelInstance>(ALevelInstance::StaticClass(), FTransform(SpawnRot, SpawnPos));
		if (Instance)
		{
			Instance->SetWorldAsset(Room.RoomLevel);
			Instance->LoadLevelInstance();
			SpawnedInstances.Add(Instance);
			++SpawnedCount;
		}
	}

	UE_LOG(LogOrganicViz, Log, TEXT("SpawnLevelInstances: spawned %d / %d rooms."), SpawnedCount, GridData.Rooms.Num());
}

void AOrganicDungeon2DVisualizer::ClearSpawnedInstances()
{
	for (AActor* Actor : SpawnedInstances)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	SpawnedInstances.Empty();
}
