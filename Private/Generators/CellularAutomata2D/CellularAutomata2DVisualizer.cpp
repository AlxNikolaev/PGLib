#include "Generators/CellularAutomata2D/CellularAutomata2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "Generators/CellularAutomata2D/CellularAutomataGenerator2D.h"

ACellularAutomata2DVisualizer::ACellularAutomata2DVisualizer()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACellularAutomata2DVisualizer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!Bounds.bIsValid)
	{
		return;
	}

	UCellularAutomataGenerator2D* Generator = NewObject<UCellularAutomataGenerator2D>();
	Generator->SetBounds(Bounds)
		->SetSeed(Seed)
		->SetGridSize(GridSize)
		->SetFillProbability(FillProbability)
		->SetIterations(Iterations)
		->SetBirthRule(BirthRule)
		->SetSurvivalRule(SurvivalRule)
		->SetMinRegionSize(MinRegionSize)
		->SetKeepCenterRegion(bKeepCenterRegion);

	FLayoutDiagram2D Diagram = Generator->Generate();

	FlushPersistentDebugLines(GetWorld());

	for (const FLayoutCell2D& Cell : Diagram.Cells)
	{
		const FColor Color = Cell.bIsExterior ? FColor::Red : FColor::Green;
		const float	 Height = Cell.bIsExterior ? -10.f : 10.f;

		for (int32 i = 0; i < Cell.Vertices.Num(); ++i)
		{
			const FVector2D& V1 = Cell.Vertices[i];
			const FVector2D& V2 = Cell.Vertices[(i + 1) % Cell.Vertices.Num()];

			DrawDebugLine(GetWorld(), FVector(V1.X, V1.Y, Height), FVector(V2.X, V2.Y, Height), Color, true, -1.f, 0, 3.0f);
		}

		DrawDebugPoint(GetWorld(), FVector(Cell.Center.X, Cell.Center.Y, Height), 5.0f, Color, true);
	}
}
