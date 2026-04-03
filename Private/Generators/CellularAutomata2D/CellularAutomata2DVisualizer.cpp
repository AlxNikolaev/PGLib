#include "Generators/CellularAutomata2D/CellularAutomata2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCAVisualizer, Log, All);

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

	/** Same hue but desaturated for culled regions. */
	FLinearColor GetCulledRegionLinearColor(int32 RegionIndex)
	{
		const float Hue = FMath::Fmod(RegionIndex * 0.618033988749895f, 1.0f) * 360.0f;
		return FLinearColor(Hue, 0.2f, 0.35f).HSVToLinearRGB();
	}

	/** Builds a human-readable summary of the CA config for the metrics overlay. */
	FString BuildConfigSummary(const FCellularAutomataConfig& Config)
	{
		if (Config.bUseAdvancedOverride)
		{
			FString Notation = Config.AdvancedRuleNotation.IsEmpty() ? TEXT("(default)") : Config.AdvancedRuleNotation;
			return FString::Printf(
				TEXT("Advanced Override | %s | Fill=%.2f | Iter=%d"), *Notation, Config.AdvancedFillProbability, Config.AdvancedIterations);
		}

		const FString StyleName = UEnum::GetDisplayValueAsText(Config.CaveStyle).ToString();
		const FString ScaleName = UEnum::GetDisplayValueAsText(Config.RegionScale).ToString();
		return FString::Printf(TEXT("%s | Openness=%.2f | Smoothness=%d | %s"), *StyleName, Config.Openness, Config.Smoothness, *ScaleName);
	}
} // namespace

ACellularAutomata2DVisualizer::ACellularAutomata2DVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;

	GridMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GridMeshComponent"));
	SetRootComponent(GridMeshComponent);
}

void ACellularAutomata2DVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!Bounds.bIsValid)
	{
		UE_LOG(LogCAVisualizer, Verbose, TEXT("OnConstruction: Bounds not valid, skipping generation."));
		return;
	}

	FCellularAutomataResolvedParams Params = CaveConfig.Resolve();

	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(Bounds)
		->SetSeed(Seed)
		->SetGridSize(GridSize)
		->SetFillProbability(Params.FillProbability)
		->SetIterations(Params.Iterations)
		->SetBirthRule(Params.BirthRule)
		->SetSurvivalRule(Params.SurvivalRule)
		->SetMinRegionSize(Params.MinRegionSize)
		->SetKeepCenterRegion(Params.bKeepCenterRegion);

	const double			  StartTime = FPlatformTime::Seconds();
	FCellularAutomataGridData GridData = Generator->GenerateWithGridData();
	const double			  GenerationTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	// Clear previous state — FlushDebugStrings is needed separately because
	// FlushPersistentDebugLines does not clear DrawDebugString text.
	FlushPersistentDebugLines(GetWorld());
	FlushDebugStrings(GetWorld());
	GridMeshComponent->ClearAllMeshSections();

	if (!DebugMaterial)
	{
		DebugMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
		if (!DebugMaterial)
		{
			UE_LOG(LogCAVisualizer, Warning,
				TEXT("OnConstruction: Could not load vertex color material. Assign one manually in the Visualization category."));
		}
	}

	if (GridData.GridWidth <= 0 || GridData.GridHeight <= 0)
	{
		UE_LOG(
			LogCAVisualizer, Warning, TEXT("OnConstruction: Grid dimensions invalid (%d x %d), skipping."), GridData.GridWidth, GridData.GridHeight);
		return;
	}

	const float	  CS = GridData.CellSize;
	const FBox2D& B = Bounds;
	int32		  SectionIndex = 0;

	// ProceduralMeshComponent vertices are in LOCAL space (relative to actor).
	// Bounds are in world space, so offset all mesh vertices by the actor's position.
	const FVector ActorLoc = GetActorLocation();
	const FBox2D  LocalBounds(B.Min - FVector2D(ActorLoc.X, ActorLoc.Y), B.Max - FVector2D(ActorLoc.X, ActorLoc.Y));

	UE_LOG(LogCAVisualizer,
		Log,
		TEXT("OnConstruction: Grid %d x %d, CellSize=%.2f, Regions=%d"),
		GridData.GridWidth,
		GridData.GridHeight,
		CS,
		GridData.Regions.Num());

	UE_LOG(LogCAVisualizer, Log, TEXT("OnConstruction: bShowGridCells=%s, SurvivingRegions=%d, GridMeshComponent=%s"),
		bShowGridCells ? TEXT("true") : TEXT("false"),
		GridData.SurvivingRegions.Num(),
		GridMeshComponent ? TEXT("valid") : TEXT("NULL"));

	// Layer 1 (Z=0..1): Grid cells via ProceduralMesh
	if (bShowGridCells)
	{
		// Section 0: Wall background quad spanning full grid bounds at Z=0
		{
			const float MinX = LocalBounds.Min.X;
			const float MinY = LocalBounds.Min.Y;
			const float MaxX = MinX + GridData.GridWidth * CS;
			const float MaxY = MinY + GridData.GridHeight * CS;

			UE_LOG(LogCAVisualizer, Log, TEXT("  Wall quad: (%.1f,%.1f)-(%.1f,%.1f) Z=0 [local space]"), MinX, MinY, MaxX, MaxY);

			TArray<FVector> Vertices = { FVector(MinX, MinY, 0.0f), FVector(MaxX, MinY, 0.0f), FVector(MaxX, MaxY, 0.0f), FVector(MinX, MaxY, 0.0f) };
			TArray<int32>	Triangles = { 0, 2, 1, 0, 3, 2 };
			TArray<FVector> Normals = { FVector::UpVector, FVector::UpVector, FVector::UpVector, FVector::UpVector };
			TArray<FVector2D>		 UVs = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
			TArray<FLinearColor>	 Colors = { WallLinearColor, WallLinearColor, WallLinearColor, WallLinearColor };
			TArray<FProcMeshTangent> Tangents;

			GridMeshComponent->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
			if (DebugMaterial)
			{
				GridMeshComponent->SetMaterial(SectionIndex, DebugMaterial);
			}
			UE_LOG(LogCAVisualizer, Log, TEXT("  Section %d: wall quad created, material=%s"), SectionIndex,
				DebugMaterial ? *DebugMaterial->GetName() : TEXT("NULL"));
			++SectionIndex;
		}

		// One section per surviving region at Z=1
		for (int32 RegionId = 0; RegionId < GridData.Regions.Num(); ++RegionId)
		{
			if (!GridData.SurvivingRegions[RegionId])
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

			UE_LOG(LogCAVisualizer, Log, TEXT("  Section %d: Region %d, %d cells, color=(%.2f,%.2f,%.2f)"),
				SectionIndex, RegionId, GridData.Regions[RegionId].Num(), RegionColor.R, RegionColor.G, RegionColor.B);
			BuildCellMeshSection(SectionIndex, GridData.Regions[RegionId], CS, LocalBounds, RegionColor, 1.0f);
			++SectionIndex;
		}

		// Culled regions at Z=1 (desaturated)
		if (bShowCulledRegions)
		{
			for (int32 RegionId = 0; RegionId < GridData.Regions.Num(); ++RegionId)
			{
				if (GridData.SurvivingRegions[RegionId])
				{
					continue;
				}

				const FLinearColor CulledColor = GetCulledRegionLinearColor(RegionId);
				BuildCellMeshSection(SectionIndex, GridData.Regions[RegionId], CS, LocalBounds, CulledColor, 1.0f);
				++SectionIndex;
			}
		}

		UE_LOG(LogCAVisualizer,
			Log,
			TEXT("OnConstruction: Created %d mesh sections for grid cells. DebugMaterial=%s"),
			SectionIndex,
			DebugMaterial ? *DebugMaterial->GetName() : TEXT("NULL"));
	}

	// Layer 3 (Z=2): Grid lines
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

	// Layer 4 (Z=3): Region boundaries from diagram (with optional Chaikin smoothing)
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

				DrawDebugLine(GetWorld(), FVector(V1.X, V1.Y, 3.0f), FVector(V2.X, V2.Y, 3.0f), FColor::White, true, -1.f, 0, 4.0f);
			}
		}
	}

	// Layer 5 (Z=5): Adjacency graph
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

	// Layer 6 (Z=10): Center marker
	if (bShowCenterMarker && GridData.CenterRegionId >= 0 && GridData.CenterRegionId < GridData.Regions.Num())
	{
		const int32 CenterX = GridData.GridWidth / 2;
		const int32 CenterY = GridData.GridHeight / 2;

		const FVector CenterPos(B.Min.X + (CenterX + 0.5f) * CS, B.Min.Y + (CenterY + 0.5f) * CS, 10.0f);
		const FVector MarkerExtent(CS * 1.0f, CS * 1.0f, 1.0f);

		DrawDebugBox(GetWorld(), CenterPos, MarkerExtent, FColor::Yellow, true, -1.f, 0, 3.0f);
		DrawDebugPoint(GetWorld(), CenterPos, 10.0f, FColor::Yellow, true);
	}

	// Layer 7: Metrics overlay
	if (bShowMetrics)
	{
		const float LineSpacing = CS * 2.0f;
		FVector		Anchor(B.Min.X, B.Min.Y - LineSpacing, 15.0f);
		int32		LineIndex = 0;

		auto DrawLine = [&](const FString& Text) {
			DrawDebugString(GetWorld(), Anchor + FVector(0.0f, -LineSpacing * LineIndex, 0.0f), Text, nullptr, FColor::White, -1.0f, true);
			++LineIndex;
		};

		// Grid dimensions
		const int32 TotalCells = GridData.GridWidth * GridData.GridHeight;
		DrawLine(TEXT("[CA Metrics]"));
		DrawLine(FString::Printf(TEXT("Grid: %d x %d (%d cells)"), GridData.GridWidth, GridData.GridHeight, TotalCells));

		// Floor/wall counts
		int32 FloorCount = 0;
		for (bool bIsFloor : GridData.Grid)
		{
			if (bIsFloor)
			{
				++FloorCount;
			}
		}
		const int32 WallCount = TotalCells - FloorCount;
		const float FloorPct = TotalCells > 0 ? (static_cast<float>(FloorCount) / TotalCells) * 100.0f : 0.0f;
		const float WallPct = TotalCells > 0 ? (static_cast<float>(WallCount) / TotalCells) * 100.0f : 0.0f;
		DrawLine(FString::Printf(TEXT("Floor: %d (%.1f%%) | Wall: %d (%.1f%%)"), FloorCount, FloorPct, WallCount, WallPct));

		// Region counts
		const int32 TotalRegions = GridData.Regions.Num();
		int32		SurvivingCount = 0;
		for (bool bSurvived : GridData.SurvivingRegions)
		{
			if (bSurvived)
			{
				++SurvivingCount;
			}
		}
		const int32 CulledCount = TotalRegions - SurvivingCount;
		DrawLine(FString::Printf(TEXT("Regions: %d total, %d surviving, %d culled"), TotalRegions, SurvivingCount, CulledCount));

		// Largest/smallest surviving region, center region
		int32 LargestSize = 0;
		int32 SmallestSize = TotalCells;
		int32 CenterSize = 0;

		for (int32 i = 0; i < TotalRegions; ++i)
		{
			if (!GridData.SurvivingRegions[i])
			{
				continue;
			}
			const int32 RegionCellCount = GridData.Regions[i].Num();
			LargestSize = FMath::Max(LargestSize, RegionCellCount);
			SmallestSize = FMath::Min(SmallestSize, RegionCellCount);
		}
		if (SurvivingCount == 0)
		{
			SmallestSize = 0;
		}

		if (GridData.CenterRegionId >= 0 && GridData.CenterRegionId < TotalRegions)
		{
			CenterSize = GridData.Regions[GridData.CenterRegionId].Num();
		}
		DrawLine(FString::Printf(TEXT("Largest: %d cells | Smallest: %d cells | Center: %d cells"), LargestSize, SmallestSize, CenterSize));

		// Generation time
		DrawLine(FString::Printf(TEXT("Generation: %.2f ms"), GenerationTimeMs));

		// Config summary
		DrawLine(FString::Printf(TEXT("Config: %s"), *BuildConfigSummary(CaveConfig)));

		// Seed
		DrawLine(FString::Printf(TEXT("Seed: \"%s\""), *Seed));

		UE_LOG(LogCAVisualizer, Verbose, TEXT("OnConstruction: Metrics overlay drawn with %d lines."), LineIndex);
	}
}

void ACellularAutomata2DVisualizer::BuildCellMeshSection(
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

	UE_LOG(LogCAVisualizer, Log, TEXT("  BuildCellMeshSection: section=%d, verts=%d, tris=%d, cells=%d"),
		SectionIndex, Vertices.Num(), Triangles.Num(), CellCount);

	GridMeshComponent->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
	if (DebugMaterial)
	{
		GridMeshComponent->SetMaterial(SectionIndex, DebugMaterial);
	}

	const int32 NumSections = GridMeshComponent->GetNumSections();
	UE_LOG(LogCAVisualizer, Log, TEXT("  BuildCellMeshSection: after create, component has %d sections, visible=%s"),
		NumSections, GridMeshComponent->IsVisible() ? TEXT("true") : TEXT("false"));
}
