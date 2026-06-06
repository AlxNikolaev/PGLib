#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

/**
 * Shared mesh-section builder used by the three 2D procedural-geometry editor visualizers
 * (CellularAutomata, DrunkardWalk, OrganicDungeon).
 *
 * Emits one flat quad per cell (4 verts, 2 CCW tris) coloured uniformly by Color, sitting at
 * ZOffset. All three visualizers used an identical algorithm — this is the single implementation.
 *
 * @param MeshComponent    Target component; creates/overwrites the section at SectionIndex.
 * @param Material         Optional debug material applied after section creation.
 * @param SectionIndex     Section slot on the mesh component.
 * @param CellPositions    Grid-coordinate cells to rasterize (X = column, Y = row).
 * @param CellSize         World-space side length of one grid cell.
 * @param GridOriginLocal  World-space origin of cell (0,0) relative to the owning actor (local space).
 * @param Color            Uniform vertex colour for every quad in this section.
 * @param ZOffset          Z coordinate assigned to all generated vertices (used for layer separation).
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
