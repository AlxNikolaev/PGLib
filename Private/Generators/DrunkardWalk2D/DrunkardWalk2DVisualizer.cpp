#include "Generators/DrunkardWalk2D/DrunkardWalk2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

namespace
{
	// Wall color: dark charcoal
	const FColor WallColor(4, 4, 5);

	// Grid line color
	const FColor GridLineColor(20, 20, 25);

	// Default floor color when region coloring is off
	const FColor DefaultFloorColor(178, 178, 178);

	/** Golden ratio hue cycling for maximally distant region colors. */
	FColor GetRegionColor(int32 RegionIndex)
	{
		const float Hue = FMath::Fmod(RegionIndex * 0.618033988749895f, 1.0f);
		return FLinearColor(Hue, 0.7f, 0.85f).HSVToLinearRGB().ToFColor(true);
	}

	/** Walker path color: higher saturation and full brightness for distinction from region colors. */
	FColor GetWalkerColor(int32 WalkerIndex)
	{
		const float Hue = FMath::Fmod(WalkerIndex * 0.618033988749895f, 1.0f);
		return FLinearColor(Hue, 0.9f, 1.0f).HSVToLinearRGB().ToFColor(true);
	}

	/** Warm tint for room highlight overlay. Shifts the region color toward orange. */
	FColor GetRoomHighlightColor(int32 RegionIndex)
	{
		// Warm orange-shifted hue based on region, with moderate saturation
		const float RegionHue = FMath::Fmod(RegionIndex * 0.618033988749895f, 1.0f);
		// Blend toward warm (0.08 = orange) by averaging
		const float WarmHue = FMath::Fmod((RegionHue + 0.08f) * 0.5f + 0.04f, 1.0f);
		return FLinearColor(WarmHue, 0.8f, 0.95f).HSVToLinearRGB().ToFColor(true);
	}
} // namespace

ADrunkardWalk2DVisualizer::ADrunkardWalk2DVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADrunkardWalk2DVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!Bounds.bIsValid)
	{
		return;
	}

	UDrunkardWalkGenerator2D* Generator = NewObject<UDrunkardWalkGenerator2D>();
	Generator->SetBounds(Bounds)
		->SetSeed(Seed)
		->SetGridSize(GridSize)
		->SetWalkLength(WalkLength)
		->SetNumWalkers(NumWalkers)
		->SetBranchProbability(BranchProbability)
		->SetCorridorWidth(CorridorWidth)
		->SetRoomChance(RoomChance)
		->SetRoomRadius(RoomRadius);

	FDrunkardWalkGridData GridData = Generator->GenerateWithGridData();

	FlushPersistentDebugLines(GetWorld());

	if (GridData.GridWidth <= 0 || GridData.GridHeight <= 0)
	{
		return;
	}

	const float	  CS = GridData.CellSize;
	const FBox2D& B = Bounds;

	// Layer 1 (Z=0): Grid cells — wall/floor fill with optional region coloring
	if (bShowGridCells)
	{
		for (int32 Y = 0; Y < GridData.GridHeight; ++Y)
		{
			for (int32 X = 0; X < GridData.GridWidth; ++X)
			{
				const int32	  Index = Y * GridData.GridWidth + X;
				const FVector CellCenter(B.Min.X + (X + 0.5f) * CS, B.Min.Y + (Y + 0.5f) * CS, 0.0f);

				if (GridData.Grid[Index])
				{
					FColor Color;
					if (bShowRegionColors && GridData.RegionIds[Index] >= 0)
					{
						Color = GetRegionColor(GridData.RegionIds[Index]);
					}
					else
					{
						Color = DefaultFloorColor;
					}
					DrawDebugPoint(GetWorld(), CellCenter, CS * 0.9f, Color, true);
				}
				else
				{
					DrawDebugPoint(GetWorld(), CellCenter, CS * 0.9f, WallColor, true);
				}
			}
		}
	}

	// Layer 2 (Z=1): Room highlights — warm tint overlay for room cells
	if (bShowRoomHighlights)
	{
		for (int32 Y = 0; Y < GridData.GridHeight; ++Y)
		{
			for (int32 X = 0; X < GridData.GridWidth; ++X)
			{
				const int32 Index = Y * GridData.GridWidth + X;
				if (GridData.CellType[Index] == EDrunkardWalkCellType::Room)
				{
					const int32	  RegionId = GridData.RegionIds[Index];
					const FColor  HighlightColor = (RegionId >= 0) ? GetRoomHighlightColor(RegionId) : FColor(255, 140, 0);
					const FVector CellCenter(B.Min.X + (X + 0.5f) * CS, B.Min.Y + (Y + 0.5f) * CS, 1.0f);
					DrawDebugPoint(GetWorld(), CellCenter, CS * 0.7f, HighlightColor, true);
				}
			}
		}
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

	// Layer 4 (Z=3): Walker paths — colored lines per walker
	if (bShowWalkerPaths)
	{
		for (int32 WalkerIdx = 0; WalkerIdx < GridData.WalkerPaths.Num(); ++WalkerIdx)
		{
			const TArray<FIntPoint>& Path = GridData.WalkerPaths[WalkerIdx];
			const FColor			 PathColor = GetWalkerColor(WalkerIdx);

			for (int32 i = 0; i + 1 < Path.Num(); ++i)
			{
				const FVector P1(B.Min.X + (Path[i].X + 0.5f) * CS, B.Min.Y + (Path[i].Y + 0.5f) * CS, 3.0f);
				const FVector P2(B.Min.X + (Path[i + 1].X + 0.5f) * CS, B.Min.Y + (Path[i + 1].Y + 0.5f) * CS, 3.0f);

				DrawDebugLine(GetWorld(), P1, P2, PathColor, true, -1.f, 0, 2.0f);
			}
		}
	}

	// Layer 5 (Z=4): Region boundaries from Diagram
	if (bShowRegionBoundaries)
	{
		for (const FLayoutCell2D& Cell : GridData.Diagram.Cells)
		{
			for (int32 i = 0; i < Cell.Vertices.Num(); ++i)
			{
				const FVector2D& V1 = Cell.Vertices[i];
				const FVector2D& V2 = Cell.Vertices[(i + 1) % Cell.Vertices.Num()];

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
}
