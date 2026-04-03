#include "Generators/DrunkardWalk2D/DrunkardWalk2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWVisualizer, Log, All);

namespace
{
	// Wall color: dark charcoal
	const FLinearColor WallLinearColor(0.016f, 0.016f, 0.018f);

	// Grid line color
	const FColor GridLineColor(20, 20, 25);

	// Default floor color when region coloring is off
	const FLinearColor DefaultFloorLinearColor(0.7f, 0.7f, 0.7f);

	/** Golden ratio hue cycling for maximally distant region colors. HSVToLinearRGB expects H in [0,360]. */
	FLinearColor GetRegionLinearColor(int32 RegionIndex)
	{
		const float Hue = FMath::Fmod(RegionIndex * 0.618033988749895f, 1.0f) * 360.0f;
		return FLinearColor(Hue, 0.7f, 0.85f).HSVToLinearRGB();
	}

	/** Walker path color: higher saturation and full brightness for distinction from region colors. */
	FLinearColor GetWalkerLinearColor(int32 WalkerIndex)
	{
		const float Hue = FMath::Fmod(WalkerIndex * 0.618033988749895f, 1.0f) * 360.0f;
		return FLinearColor(Hue, 0.9f, 1.0f).HSVToLinearRGB();
	}

	/** Warm tint for room highlight overlay. Shifts the region color toward orange. */
	FLinearColor GetRoomHighlightLinearColor(int32 RegionIndex)
	{
		const float RegionHue = FMath::Fmod(RegionIndex * 0.618033988749895f, 1.0f);
		// Blend toward warm (0.08 = orange) by averaging
		const float WarmHue = FMath::Fmod((RegionHue + 0.08f) * 0.5f + 0.04f, 1.0f) * 360.0f;
		return FLinearColor(WarmHue, 0.8f, 0.95f).HSVToLinearRGB();
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
	Generator->SetBounds(Bounds)
		->SetSeed(Seed)
		->SetGridSize(GridSize)
		->SetWalkLength(Params.WalkLength)
		->SetNumWalkers(Params.NumWalkers)
		->SetBranchProbability(Params.BranchProbability)
		->SetCorridorWidth(Params.CorridorWidth)
		->SetRoomChance(Params.RoomChance)
		->SetRoomRadiusRange(Params.RoomRadiusMin, Params.RoomRadiusMax)
		->SetMinRoomSpacing(Params.MinRoomSpacing)
		->SetMaxRoomCount(Params.MaxRoomCount);

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

	const float	  CS = GridData.CellSize;
	const FBox2D& B = Bounds;
	int32		  SectionIndex = 0;

	// ProceduralMeshComponent vertices are in LOCAL space (relative to actor).
	// Bounds are in world space, so offset all mesh vertices by the actor's position.
	const FVector ActorLoc = GetActorLocation();
	const FBox2D  LocalBounds(B.Min - FVector2D(ActorLoc.X, ActorLoc.Y), B.Max - FVector2D(ActorLoc.X, ActorLoc.Y));

	UE_LOG(LogDWVisualizer,
		Log,
		TEXT("OnConstruction: Grid %d x %d, CellSize=%.2f, Regions=%d"),
		GridData.GridWidth,
		GridData.GridHeight,
		CS,
		GridData.Regions.Num());

	// Layer 1 (Z=0..1): Grid cells via ProceduralMesh
	if (bShowGridCells)
	{
		// Section 0: Wall background quad spanning full grid bounds at Z=0
		{
			const float MinX = LocalBounds.Min.X;
			const float MinY = LocalBounds.Min.Y;
			const float MaxX = MinX + GridData.GridWidth * CS;
			const float MaxY = MinY + GridData.GridHeight * CS;

			TArray<FVector> Vertices = { FVector(MinX, MinY, 0.0f), FVector(MaxX, MinY, 0.0f), FVector(MaxX, MaxY, 0.0f), FVector(MinX, MaxY, 0.0f) };
			TArray<int32>	Triangles = { 0, 2, 1, 0, 3, 2 };
			TArray<FVector> Normals = { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
			TArray<FVector2D>		 UVs = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
			TArray<FLinearColor>	 Colors = { WallLinearColor, WallLinearColor, WallLinearColor, WallLinearColor };
			TArray<FProcMeshTangent> Tangents;

			GridMeshComponent->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
			if (DebugMaterial)
			{
				GridMeshComponent->SetMaterial(SectionIndex, DebugMaterial);
			}
			++SectionIndex;
		}

		// One section per region at Z=1
		for (int32 RegionId = 0; RegionId < GridData.Regions.Num(); ++RegionId)
		{
			if (GridData.Regions[RegionId].Num() == 0)
			{
				continue;
			}

			FLinearColor RegionColor;
			if (bShowRegionColors)
			{
				RegionColor = GetRegionLinearColor(RegionId);
			}
			else
			{
				RegionColor = DefaultFloorLinearColor;
			}

			BuildCellMeshSection(SectionIndex, GridData.Regions[RegionId], CS, LocalBounds, RegionColor, 1.0f);
			++SectionIndex;
		}

		UE_LOG(LogDWVisualizer, Log, TEXT("OnConstruction: Created %d mesh sections for grid cells."), SectionIndex);
	}

	// Layer 2 (Z=1.5): Room highlights via ProceduralMesh
	if (bShowRoomHighlights)
	{
		for (int32 RegionId = 0; RegionId < GridData.Regions.Num(); ++RegionId)
		{
			// Collect only room-type cells for this region
			TArray<FIntPoint> RoomCells;
			for (const FIntPoint& Cell : GridData.Regions[RegionId])
			{
				const int32 Index = Cell.Y * GridData.GridWidth + Cell.X;
				if (GridData.CellType[Index] == EDrunkardWalkCellType::Room)
				{
					RoomCells.Add(Cell);
				}
			}

			if (RoomCells.Num() == 0)
			{
				continue;
			}

			const FLinearColor HighlightColor = GetRoomHighlightLinearColor(RegionId);
			BuildCellMeshSection(SectionIndex, RoomCells, CS, LocalBounds, HighlightColor, 1.5f);
			++SectionIndex;
		}
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
		const int32 CenterX = GridData.GridWidth / 2;
		const int32 CenterY = GridData.GridHeight / 2;

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

		// Region and walker counts
		DrawLine(FString::Printf(TEXT("Regions: %d | Walkers: %d"), GridData.Regions.Num(), GridData.WalkerPaths.Num()));

		// Generation time
		DrawLine(FString::Printf(TEXT("Generation: %.2f ms"), GenerationTimeMs));

		// Seed
		DrawLine(FString::Printf(TEXT("Seed: \"%s\""), *Seed));

		UE_LOG(LogDWVisualizer, Log, TEXT("Metrics: drew %d lines at (%.1f, %.1f, %.1f)"), LineIndex, Anchor.X, Anchor.Y, Anchor.Z);
	}
}

void ADrunkardWalk2DVisualizer::BuildCellMeshSection(
	int32 SectionIndex, const TArray<FIntPoint>& CellPositions, float CellSize, const FBox2D& GridBounds, const FLinearColor& Color, float ZOffset)
{
	const int32 CellCount = CellPositions.Num();
	if (CellCount == 0)
	{
		return;
	}

	const int32 VertexCount = CellCount * 4;
	const int32 TriangleCount = CellCount * 6;

	TArray<FVector>			 Vertices;
	TArray<int32>			 Triangles;
	TArray<FVector>			 Normals;
	TArray<FVector2D>		 UVs;
	TArray<FLinearColor>	 Colors;
	TArray<FProcMeshTangent> Tangents;

	Vertices.Reserve(VertexCount);
	Triangles.Reserve(TriangleCount);
	Normals.Reserve(VertexCount);
	UVs.Reserve(VertexCount);
	Colors.Reserve(VertexCount);

	for (const FIntPoint& Cell : CellPositions)
	{
		const float WorldX = GridBounds.Min.X + Cell.X * CellSize;
		const float WorldY = GridBounds.Min.Y + Cell.Y * CellSize;

		const int32 Base = Vertices.Num();

		Vertices.Add(FVector(WorldX, WorldY, ZOffset));
		Vertices.Add(FVector(WorldX + CellSize, WorldY, ZOffset));
		Vertices.Add(FVector(WorldX + CellSize, WorldY + CellSize, ZOffset));
		Vertices.Add(FVector(WorldX, WorldY + CellSize, ZOffset));

		// CCW winding from above
		Triangles.Add(Base);
		Triangles.Add(Base + 2);
		Triangles.Add(Base + 1);
		Triangles.Add(Base);
		Triangles.Add(Base + 3);
		Triangles.Add(Base + 2);

		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);

		UVs.Add(FVector2D(0, 0));
		UVs.Add(FVector2D(1, 0));
		UVs.Add(FVector2D(1, 1));
		UVs.Add(FVector2D(0, 1));

		Colors.Add(Color);
		Colors.Add(Color);
		Colors.Add(Color);
		Colors.Add(Color);
	}

	GridMeshComponent->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
	if (DebugMaterial)
	{
		GridMeshComponent->SetMaterial(SectionIndex, DebugMaterial);
	}
}
