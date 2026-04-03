#include "Generators/LayoutGenerator.h"

ULayoutGenerator::ULayoutGenerator()
{
	Bounds = FBox2D();
	GridSize = 100;
	CenterPoint = FVector2D::ZeroVector;
}

ULayoutGenerator* ULayoutGenerator::SetBounds(const FBox2D& InBounds)
{
	Bounds = InBounds;
	if (CenterPoint == FVector2D::ZeroVector)
	{
		CenterPoint = Bounds.GetCenter();
	}
	return this;
}

ULayoutGenerator* ULayoutGenerator::SetCenter(const FVector2D& InCenter)
{
	CenterPoint = InCenter;
	return this;
}

ULayoutGenerator* ULayoutGenerator::SetSeed(const FString& InSeed)
{
	Seed = InSeed;
	InitializeRandomStream();
	return this;
}

ULayoutGenerator* ULayoutGenerator::SetGridSize(int32 InSize)
{
	GridSize = FMath::Max(10, InSize);
	return this;
}

void ULayoutGenerator::InitializeRandomStream()
{
	if (Seed.IsEmpty())
	{
		Seed = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
	RandomStream = FRandomStream(GetTypeHash(Seed));
}

FVector2D ULayoutGenerator::ClampToBounds(const FVector2D& Point) const
{
	return FVector2D(FMath::Clamp(Point.X, Bounds.Min.X, Bounds.Max.X), FMath::Clamp(Point.Y, Bounds.Min.Y, Bounds.Max.Y));
}

FLayoutDiagram2D ULayoutGenerator::ConvertGridToDiagram(const TArray<bool>& Grid, int32 GridWidth, int32 GridHeight) const
{
	FLayoutDiagram2D Diagram;
	Diagram.Bounds = Bounds;
	Diagram.Seed = Seed;
	Diagram.CenterPoint = CenterPoint;
	Diagram.CenterCellIndex = INDEX_NONE;

	const float CellSize = static_cast<float>(GridSize);
	const float MinX = Bounds.Min.X;
	const float MinY = Bounds.Min.Y;

	// Map from grid linear index to cell index
	TArray<int32> GridToCellIndex;
	GridToCellIndex.Init(INDEX_NONE, GridWidth * GridHeight);

	// First pass: create cells for carved grid positions
	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const int32 GridIndex = Y * GridWidth + X;
			if (!Grid[GridIndex])
			{
				continue;
			}

			const int32 CellIndex = Diagram.Cells.Num();
			GridToCellIndex[GridIndex] = CellIndex;

			FLayoutCell2D Cell;
			Cell.CellIndex = CellIndex;

			// CCW rectangle vertices
			const float X0 = MinX + X * CellSize;
			const float Y0 = MinY + Y * CellSize;
			const float X1 = MinX + (X + 1) * CellSize;
			const float Y1 = MinY + (Y + 1) * CellSize;

			Cell.Vertices.SetNum(4);
			Cell.Vertices[0] = FVector2D(X0, Y0); // bottom-left
			Cell.Vertices[1] = FVector2D(X1, Y0); // bottom-right
			Cell.Vertices[2] = FVector2D(X1, Y1); // top-right
			Cell.Vertices[3] = FVector2D(X0, Y1); // top-left

			Cell.Center = FVector2D(MinX + (X + 0.5f) * CellSize, MinY + (Y + 0.5f) * CellSize);

			// bIsExterior: on grid boundary or adjacent to an uncarved cell at grid edge
			Cell.bIsExterior = (X == 0 || X == GridWidth - 1 || Y == 0 || Y == GridHeight - 1);

			Diagram.Cells.Add(Cell);
		}
	}

	// Second pass: compute neighbors and find center cell
	float		BestCenterDistSq = FLT_MAX;
	const int32 DX[] = { 1, -1, 0, 0 };
	const int32 DY[] = { 0, 0, 1, -1 };

	for (int32 Y = 0; Y < GridHeight; ++Y)
	{
		for (int32 X = 0; X < GridWidth; ++X)
		{
			const int32 GridIndex = Y * GridWidth + X;
			const int32 CellIndex = GridToCellIndex[GridIndex];
			if (CellIndex == INDEX_NONE)
			{
				continue;
			}

			FLayoutCell2D& Cell = Diagram.Cells[CellIndex];

			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				const int32 NX = X + DX[Dir];
				const int32 NY = Y + DY[Dir];

				if (NX >= 0 && NX < GridWidth && NY >= 0 && NY < GridHeight)
				{
					const int32 NeighborCellIndex = GridToCellIndex[NY * GridWidth + NX];
					if (NeighborCellIndex != INDEX_NONE)
					{
						Cell.Neighbors.Add(NeighborCellIndex);
					}
				}
			}

			const float DistSq = FVector2D::DistSquared(Cell.Center, CenterPoint);
			if (DistSq < BestCenterDistSq)
			{
				BestCenterDistSq = DistSq;
				Diagram.CenterCellIndex = CellIndex;
			}
		}
	}

	return Diagram;
}