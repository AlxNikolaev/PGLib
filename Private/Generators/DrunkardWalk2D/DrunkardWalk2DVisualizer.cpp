#include "Generators/DrunkardWalk2D/DrunkardWalk2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"
#include "Generators/VisualizerCellMesh.h"
#include "GeometryUtils/GeometryFunctionLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWVisualizer, Log, All);

namespace
{
	// Wall color: dark charcoal
	const FLinearColor WallLinearColor(0.016f, 0.016f, 0.018f);

	// Grid line color
	const FColor GridLineColor(20, 20, 25);

	// Corridor color: muted blue-gray, distinct from the saturated room colors
	const FLinearColor CorridorLinearColor(0.28f, 0.30f, 0.38f);

	/** Distinct, saturated color per placed room (offset hue so rooms differ strongly from corridors). */
	FLinearColor GetRoomDistinctColor(int32 RoomIndex)
	{
		const float Hue = FMath::Fmod(RoomIndex * 0.618033988749895f + 0.15f, 1.0f) * 360.0f;
		return FLinearColor(Hue, 0.85f, 1.0f).HSVToLinearRGB();
	}

	/** Walker path color: higher saturation and full brightness for distinction from region colors. */
	FLinearColor GetWalkerLinearColor(int32 WalkerIndex)
	{
		const float Hue = FMath::Fmod(WalkerIndex * 0.618033988749895f, 1.0f) * 360.0f;
		return FLinearColor(Hue, 0.9f, 1.0f).HSVToLinearRGB();
	}
} // namespace

ADrunkardWalk2DVisualizer::ADrunkardWalk2DVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;

	GridMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GridMeshComponent"));
	SetRootComponent(GridMeshComponent);
}

void ADrunkardWalk2DVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!Bounds.bIsValid)
	{
		UE_LOG(LogDWVisualizer, Verbose, TEXT("OnConstruction: Bounds not valid, skipping generation."));
		return;
	}

	FDrunkardWalkResolvedParams Params = Config.Resolve();

	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(Bounds)->SetSeed(Seed)->SetGridSize(GridSize)->SetCenter(Bounds.GetCenter())->ApplyResolvedParams(Params);

	const double		  StartTime = FPlatformTime::Seconds();
	FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();
	const double		  GenerationTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	// Clear previous state — FlushDebugStrings is needed separately because
	// FlushPersistentDebugLines does not clear DrawDebugString text.
	FlushPersistentDebugLines(GetWorld());
	FlushDebugStrings(GetWorld());
	GridMeshComponent->ClearAllMeshSections();

	if (!DebugMaterial)
	{
		DebugMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (!DebugMaterial)
		{
			UE_LOG(LogDWVisualizer,
				Warning,
				TEXT("OnConstruction: Could not load vertex color material. Assign one manually in the Visualization category."));
		}
	}

	if (GridData.GridWidth <= 0 || GridData.GridHeight <= 0)
	{
		UE_LOG(
			LogDWVisualizer, Warning, TEXT("OnConstruction: Grid dimensions invalid (%d x %d), skipping."), GridData.GridWidth, GridData.GridHeight);
		return;
	}

	const float	 CS = GridData.CellSize;
	const FBox2D B = GridData.Diagram.Bounds;
	int32		 SectionIndex = 0;

	// ProceduralMeshComponent vertices are local-space; B is world-space, so rebase to the actor origin.
	const FVector ActorLoc = GetActorLocation();
	const FBox2D  LocalBounds(B.Min - FVector2D(ActorLoc.X, ActorLoc.Y), B.Max - FVector2D(ActorLoc.X, ActorLoc.Y));

	UE_LOG(LogDWVisualizer,
		Verbose,
		TEXT("OnConstruction: Grid %d x %d, CellSize=%.2f, Regions=%d"),
		GridData.GridWidth,
		GridData.GridHeight,
		CS,
		GridData.Regions.Num());

	// Layer 1: Cells via ProceduralMesh — only real walls (pruned to WallThickness around floor) and floor.
	// Empty cells are not drawn. Rooms get a distinct color each; corridors share a muted color.
	if (bShowGridCells)
	{
		// Walls (Z=0): only cells classified as Wall, drawn individually (no full background quad).
		{
			TArray<FIntPoint> WallCells;
			for (int32 i = 0; i < GridData.CellType.Num(); ++i)
			{
				if (GridData.CellType[i] == EDrunkardWalkCellType::Wall)
				{
					WallCells.Add(FIntPoint(i % GridData.GridWidth, i / GridData.GridWidth));
				}
			}
			if (WallCells.Num() > 0)
			{
				ProcGen_BuildCellMeshSection(GridMeshComponent, DebugMaterial, SectionIndex, WallCells, CS, LocalBounds.Min, WallLinearColor, 0.0f);
				++SectionIndex;
			}
		}

		// Corridor cells (Z=1): one muted section.
		{
			TArray<FIntPoint> CorridorCells;
			for (int32 i = 0; i < GridData.CellType.Num(); ++i)
			{
				if (GridData.CellType[i] == EDrunkardWalkCellType::Corridor)
				{
					CorridorCells.Add(FIntPoint(i % GridData.GridWidth, i / GridData.GridWidth));
				}
			}
			if (CorridorCells.Num() > 0)
			{
				ProcGen_BuildCellMeshSection(
					GridMeshComponent, DebugMaterial, SectionIndex, CorridorCells, CS, LocalBounds.Min, CorridorLinearColor, 1.0f);
				++SectionIndex;
			}
		}

		// Rooms (Z=1.5): one section per placed room, each a distinct color.
		for (int32 RoomId = 0; RoomId < GridData.PlacedRooms.Num(); ++RoomId)
		{
			const FDrunkardWalkPlacedRoom& Room = GridData.PlacedRooms[RoomId];
			TArray<FIntPoint>			   RoomCells;
			RoomCells.Reserve(Room.Width * Room.Height);
			for (int32 dy = 0; dy < Room.Height; ++dy)
			{
				for (int32 dx = 0; dx < Room.Width; ++dx)
				{
					RoomCells.Add(FIntPoint(Room.Min.X + dx, Room.Min.Y + dy));
				}
			}

			// Always color rooms distinctly so adjacent rooms are visually separable.
			const FLinearColor RoomColor = GetRoomDistinctColor(RoomId);
			ProcGen_BuildCellMeshSection(GridMeshComponent, DebugMaterial, SectionIndex, RoomCells, CS, LocalBounds.Min, RoomColor, 1.5f);
			++SectionIndex;
		}

		UE_LOG(LogDWVisualizer, Verbose, TEXT("OnConstruction: Created %d mesh sections (walls/corridors/rooms)."), SectionIndex);
	}

	// Layer 3 (Z=2): Grid lines (DrawDebugLine, world space)
	if (bShowGridLines)
	{
		const float MinX = B.Min.X;
		const float MinY = B.Min.Y;
		const float MaxX = MinX + GridData.GridWidth * CS;
		const float MaxY = MinY + GridData.GridHeight * CS;

		// Vertical lines
		for (int32 X = 0; X <= GridData.GridWidth; ++X)
		{
			const float PosX = MinX + X * CS;
			DrawDebugLine(GetWorld(), FVector(PosX, MinY, 2.0f), FVector(PosX, MaxY, 2.0f), GridLineColor, true, -1.f, 0, 0.5f);
		}

		// Horizontal lines
		for (int32 Y = 0; Y <= GridData.GridHeight; ++Y)
		{
			const float PosY = MinY + Y * CS;
			DrawDebugLine(GetWorld(), FVector(MinX, PosY, 2.0f), FVector(MaxX, PosY, 2.0f), GridLineColor, true, -1.f, 0, 0.5f);
		}
	}

	// Layer 4 (Z=3): Walker paths (DrawDebugLine, world space)
	if (bShowWalkerPaths)
	{
		for (int32 WalkerIdx = 0; WalkerIdx < GridData.WalkerPaths.Num(); ++WalkerIdx)
		{
			const TArray<FIntPoint>& Path = GridData.WalkerPaths[WalkerIdx];
			const FColor			 PathColor = GetWalkerLinearColor(WalkerIdx).ToFColor(true);

			for (int32 i = 0; i + 1 < Path.Num(); ++i)
			{
				const FVector P1(B.Min.X + (Path[i].X + 0.5f) * CS, B.Min.Y + (Path[i].Y + 0.5f) * CS, 3.0f);
				const FVector P2(B.Min.X + (Path[i + 1].X + 0.5f) * CS, B.Min.Y + (Path[i + 1].Y + 0.5f) * CS, 3.0f);

				DrawDebugLine(GetWorld(), P1, P2, PathColor, true, -1.f, 0, 2.0f);
			}
		}
	}

	// Layer 5 (Z=4): Region boundaries from Diagram (with optional Chaikin smoothing)
	if (bShowRegionBoundaries)
	{
		for (const FLayoutCell2D& Cell : GridData.Diagram.Cells)
		{
			TArray<FVector2D> Vertices = Cell.Vertices;

			if (bSmoothBoundaries && Vertices.Num() >= 3)
			{
				FGeometryUtils::ChaikinSubdivide(Vertices, SmoothingIterations);
			}

			for (int32 i = 0; i < Vertices.Num(); ++i)
			{
				const FVector2D& V1 = Vertices[i];
				const FVector2D& V2 = Vertices[(i + 1) % Vertices.Num()];

				DrawDebugLine(GetWorld(), FVector(V1.X, V1.Y, 4.0f), FVector(V2.X, V2.Y, 4.0f), FColor::White, true, -1.f, 0, 4.0f);
			}
		}
	}

	// Layer 6 (Z=5): Adjacency graph
	if (bShowAdjacencyGraph)
	{
		for (const FLayoutCell2D& Cell : GridData.Diagram.Cells)
		{
			for (int32 NeighborIdx : Cell.Neighbors)
			{
				// Only draw each edge once (lower index -> higher index)
				if (NeighborIdx > Cell.CellIndex)
				{
					const FVector2D& C1 = Cell.Center;
					const FVector2D& C2 = GridData.Diagram.Cells[NeighborIdx].Center;

					DrawDebugLine(GetWorld(), FVector(C1.X, C1.Y, 5.0f), FVector(C2.X, C2.Y, 5.0f), FColor::Cyan, true, -1.f, 0, 2.0f);
				}
			}
		}
	}

	// Layer 7 (Z=10): Center marker
	if (bShowCenterMarker && GridData.CenterRegionId >= 0 && GridData.CenterRegionId < GridData.Regions.Num())
	{
		// First room center if available, else grid center.
		const int32 CenterX = GridData.RoomCenters.Num() > 0 ? GridData.RoomCenters[0].X : GridData.GridWidth / 2;
		const int32 CenterY = GridData.RoomCenters.Num() > 0 ? GridData.RoomCenters[0].Y : GridData.GridHeight / 2;

		const FVector CenterPos(B.Min.X + (CenterX + 0.5f) * CS, B.Min.Y + (CenterY + 0.5f) * CS, 10.0f);
		const FVector MarkerExtent(CS * 1.0f, CS * 1.0f, 1.0f);

		DrawDebugBox(GetWorld(), CenterPos, MarkerExtent, FColor::Yellow, true, -1.f, 0, 3.0f);
		DrawDebugPoint(GetWorld(), CenterPos, 10.0f, FColor::Yellow, true);
	}

	// Layer 8 (Z=15): Metrics overlay
	if (bShowMetrics)
	{
		const float LineSpacing = CS * 1.5f;
		FVector		Anchor(B.Min.X, B.Min.Y - LineSpacing, 15.0f);
		int32		LineIndex = 0;

		auto DrawLine = [&](const FString& Text) {
			DrawDebugString(GetWorld(), Anchor + FVector(0.0f, -LineSpacing * LineIndex, 0.0f), Text, nullptr, FColor::White, -1.0f, true);
			++LineIndex;
		};

		const int32 TotalCells = GridData.GridWidth * GridData.GridHeight;
		DrawLine(TEXT("[DW Metrics]"));
		DrawLine(FString::Printf(TEXT("Grid: %d x %d (%d cells)"), GridData.GridWidth, GridData.GridHeight, TotalCells));

		// Floor/wall/room counts
		int32 FloorCount = 0;
		int32 RoomCount = 0;
		for (int32 i = 0; i < GridData.Grid.Num(); ++i)
		{
			if (GridData.Grid[i])
			{
				++FloorCount;
				if (GridData.CellType[i] == EDrunkardWalkCellType::Room)
				{
					++RoomCount;
				}
			}
		}
		const int32 WallCount = TotalCells - FloorCount;
		const float FloorPct = TotalCells > 0 ? (static_cast<float>(FloorCount) / TotalCells) * 100.0f : 0.0f;
		const float WallPct = TotalCells > 0 ? (static_cast<float>(WallCount) / TotalCells) * 100.0f : 0.0f;
		const float RoomPct = TotalCells > 0 ? (static_cast<float>(RoomCount) / TotalCells) * 100.0f : 0.0f;
		DrawLine(FString::Printf(
			TEXT("Floor: %d (%.1f%%) | Wall: %d (%.1f%%) | Room: %d (%.1f%%)"), FloorCount, FloorPct, WallCount, WallPct, RoomCount, RoomPct));

		// Region, room, and corridor counts
		DrawLine(FString::Printf(TEXT("Regions: %d | Rooms: %d/%d | Corridors: %d"),
			GridData.Regions.Num(),
			GridData.PlacedRooms.Num(),
			GridData.RequestedRoomCount,
			GridData.WalkerPaths.Num()));

		// Generation time
		DrawLine(FString::Printf(TEXT("Generation: %.2f ms"), GenerationTimeMs));

		// Seed
		DrawLine(FString::Printf(TEXT("Seed: \"%s\""), *Seed));

		UE_LOG(LogDWVisualizer, Verbose, TEXT("Metrics: drew %d lines at (%.1f, %.1f, %.1f)"), LineIndex, Anchor.X, Anchor.Y, Anchor.Z);
	}
}
