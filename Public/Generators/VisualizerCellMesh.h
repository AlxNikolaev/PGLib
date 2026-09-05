#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

/**
 * Shared mesh-section builder for the 2D procedural-geometry editor visualizers: one flat quad per cell,
 * coloured uniformly by Color, overwriting the section at SectionIndex. CellPositions are grid coordinates
 * (X = column, Y = row), GridOriginLocal places cell (0,0) in the owning actor's local space, and ZOffset
 * separates visualizer layers.
 */
inline void ProcGen_BuildCellMeshSection(UProceduralMeshComponent* MeshComponent,
	UMaterialInterface*											   Material,
	int32														   SectionIndex,
	const TArray<FIntPoint>&									   CellPositions,
	float														   CellSize,
	const FVector2D&											   GridOriginLocal,
	const FLinearColor&											   Color,
	float														   ZOffset)
{
	if (!MeshComponent || CellPositions.Num() == 0)
	{
		return;
	}

	const int32 CellCount = CellPositions.Num();
	const int32 VertexCount = CellCount * 4;
	const int32 TriangleCount = CellCount * 6;

	TArray<FVector>			 Vertices;
	TArray<int32>			 Triangles;
	TArray<FVector>			 Normals;
	TArray<FVector2D>		 UVs;
	TArray<FLinearColor>	 Colors;
	TArray<FProcMeshTangent> Tangents; // empty — not needed for vertex-colour debug rendering

	Vertices.Reserve(VertexCount);
	Triangles.Reserve(TriangleCount);
	Normals.Reserve(VertexCount);
	UVs.Reserve(VertexCount);
	Colors.Reserve(VertexCount);

	for (const FIntPoint& Cell : CellPositions)
	{
		const float X0 = GridOriginLocal.X + Cell.X * CellSize;
		const float Y0 = GridOriginLocal.Y + Cell.Y * CellSize;
		const float X1 = X0 + CellSize;
		const float Y1 = Y0 + CellSize;

		const int32 Base = Vertices.Num();
		Vertices.Add(FVector(X0, Y0, ZOffset));
		Vertices.Add(FVector(X1, Y0, ZOffset));
		Vertices.Add(FVector(X1, Y1, ZOffset));
		Vertices.Add(FVector(X0, Y1, ZOffset));

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

	MeshComponent->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs, Colors, Tangents, true);
	if (Material)
	{
		MeshComponent->SetMaterial(SectionIndex, Material);
	}
}
