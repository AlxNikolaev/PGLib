#include "Generators/OrganicDungeon2D/OrganicDungeon2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "Generators/OrganicDungeon2D/OrganicDungeonGenerator2D.h"
#include "Generators/VisualizerCellMesh.h"
#include "LevelInstance/LevelInstanceActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrganicViz, Log, All);

namespace
{
	const FLinearColor WallLinearColor(0.016f, 0.016f, 0.018f);
	const FLinearColor CorridorLinearColor(0.28f, 0.30f, 0.38f);

	/** Distinct, saturated color per placed room. */
	FLinearColor GetRoomDistinctColor(int32 RoomIndex)
	{
		const float Hue = FMath::Fmod(RoomIndex * 0.618033988749895f + 0.15f, 1.0f) * 360.0f;
		return FLinearColor(Hue, 0.85f, 1.0f).HSVToLinearRGB();
	}

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

	if (!DebugMaterial)
	{
		DebugMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (!DebugMaterial)
		{
			UE_LOG(LogOrganicViz, Warning, TEXT("OnConstruction: could not load vertex color material; assign one in the Visualization category."));
		}
	}

	if (GridData.GridWidth <= 0 || GridData.GridHeight <= 0)
	{
		UE_LOG(LogOrganicViz, Warning, TEXT("OnConstruction: invalid grid (%dx%d), skipping."), GridData.GridWidth, GridData.GridHeight);
		return;
	}

	const float CS = GridData.CellSize;
	// Grid origin in world; mesh vertices are local (relative to actor).
	const FVector2D GridOriginLocal = GridData.GridOriginWorld - Center2D;
	int32			SectionIndex = 0;

	if (bShowGridCells)
	{
		// Walls (Z=0)
		{
			TArray<FIntPoint> WallCells;
			for (int32 i = 0; i < GridData.CellType.Num(); ++i)
			{
				if (GridData.CellType[i] == EOrganicCellType::Wall)
				{
					WallCells.Add(FIntPoint(i % GridData.GridWidth, i / GridData.GridWidth));
				}
			}
			if (WallCells.Num() > 0)
			{
				ProcGen_BuildCellMeshSection(GridMeshComponent, DebugMaterial, SectionIndex++, WallCells, CS, GridOriginLocal, WallLinearColor, 0.0f);
			}
		}

		// Corridors (Z=1)
		{
			TArray<FIntPoint> CorridorCells;
			for (int32 i = 0; i < GridData.CellType.Num(); ++i)
			{
				if (GridData.CellType[i] == EOrganicCellType::Corridor)
				{
					CorridorCells.Add(FIntPoint(i % GridData.GridWidth, i / GridData.GridWidth));
				}
			}
			if (CorridorCells.Num() > 0)
			{
				ProcGen_BuildCellMeshSection(
					GridMeshComponent, DebugMaterial, SectionIndex++, CorridorCells, CS, GridOriginLocal, CorridorLinearColor, 1.0f);
			}
		}

		// Rooms (Z=1.5) — Start = green, End = red, others distinct.
		for (int32 r = 0; r < GridData.RoomFootprintCells.Num(); ++r)
		{
			if (GridData.RoomFootprintCells[r].Num() == 0)
			{
				continue;
			}
			FLinearColor RoomColor = GetRoomDistinctColor(r);
			if (r == GridData.StartRoomIndex)
			{
				RoomColor = FLinearColor(0.05f, 0.9f, 0.1f); // entrance = green
			}
			else if (r == GridData.EndRoomIndex)
			{
				RoomColor = FLinearColor(0.9f, 0.05f, 0.05f); // exit room = red
			}
			ProcGen_BuildCellMeshSection(
				GridMeshComponent, DebugMaterial, SectionIndex++, GridData.RoomFootprintCells[r], CS, GridOriginLocal, RoomColor, 1.5f);
		}
	}

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
		FVector		Anchor(GridData.GridOriginWorld.X, GridData.GridOriginWorld.Y - LineSpacing, ActorLoc.Z + 15.0f);
		int32		LineIndex = 0;
		auto		DrawLine = [&](const FString& Text) {
			   DrawDebugString(GetWorld(), Anchor + FVector(0.0f, -LineSpacing * LineIndex, 0.0f), Text, nullptr, FColor::White, -1.0f, true);
			   ++LineIndex;
		};

		DrawLine(TEXT("[Organic Dungeon]"));
		DrawLine(FString::Printf(TEXT("Grid: %d x %d (cell %.0f)"), GridData.GridWidth, GridData.GridHeight, CS));
		DrawLine(FString::Printf(TEXT("Rooms: %d/%d | Corridors: %d (spine %d, loops %d, deadEnds %d, links %d)"),
			GridData.Rooms.Num(),
			GridData.RequestedRoomCount,
			GridData.Corridors.Num(),
			Spine,
			Loops,
			DeadEnds,
			Links));
		DrawLine(FString::Printf(TEXT("Regions: %d | Gen: %.2f ms"), GridData.Regions.Num(), GenMs));
		DrawLine(FString::Printf(TEXT("Seed: \"%s\""), *Seed));
	}

	UE_LOG(LogOrganicViz,
		Verbose,
		TEXT("OnConstruction: %dx%d grid, %d rooms, %d corridors, %d sections."),
		GridData.GridWidth,
		GridData.GridHeight,
		GridData.Rooms.Num(),
		GridData.Corridors.Num(),
		SectionIndex);
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
