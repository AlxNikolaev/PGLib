#include "Generators/DrunkardWalk2D/DrunkardWalk2DVisualizer.h"

#include "DrawDebugHelpers.h"
#include "Generators/DrunkardWalk2D/DrunkardWalkGenerator2D.h"

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
