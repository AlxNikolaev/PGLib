// Fill out your copyright notice in the Description page of Project Settings.

#include "ProceduralGeometry/Public/Factories/ProceduralMeshFactory.h"
#include "ProceduralMeshComponent.h"

bool UProceduralMeshFactory::CreatePrismMesh(const FMeshGenerationParams& Params, FMeshData& OutMeshData)
{
	if (!ValidateInput(Params))
	{
		return false;
	}

	OutMeshData.Clear();

	const int32 VertexCount = Params.FoundationVertices.Num();
	const int32 TotalVertices = VertexCount * 6;
	const int32 TotalTriangles = ((VertexCount - 2) * 2 + VertexCount * 2) * 3;

	OutMeshData.Reserve(TotalVertices, TotalTriangles);

	TArray<FVector> BottomVerts, TopVerts;
	BuildVertices(Params, BottomVerts, TopVerts);

	OutMeshData.Vertices.Append(BottomVerts);
	OutMeshData.Vertices.Append(TopVerts);

	// Order matters!!!
	ComposeFaceTriangles(VertexCount, OutMeshData.Triangles);
	CalcNormals(VertexCount, OutMeshData);
	CalcUVs(BottomVerts, TopVerts, Params.UVScale, OutMeshData);
	CalcTangentsAndColors(TotalVertices, Params.MeshColor, OutMeshData);
	BuildSideGeometry(BottomVerts, TopVerts, Params, OutMeshData);

	return true;
}

bool UProceduralMeshFactory::ValidateInput(const FMeshGenerationParams& Params)
{
	if (Params.FoundationVertices.Num() < 3)
	{
		UE_LOG(LogTemp,
			Warning,
			TEXT("ProceduralMeshGenerator: Invalid foundation vertices count (%d), minimum 3 required"),
			Params.FoundationVertices.Num());
		return false;
	}

	if (Params.Height <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("ProceduralMeshGenerator: Invalid height (%f), must be positive"), Params.Height);
		return false;
	}

	return true;
}

void UProceduralMeshFactory::BuildVertices(const FMeshGenerationParams& Params, TArray<FVector>& BottomVerts, TArray<FVector>& TopVerts)
{
	const int32 VertexCount = Params.FoundationVertices.Num();
	BottomVerts.Reserve(VertexCount);
	TopVerts.Reserve(VertexCount);

	for (const FVector2D& Vertex : Params.FoundationVertices)
	{
		BottomVerts.Emplace(Vertex, 0.0f);
		TopVerts.Emplace(Vertex, Params.Height);
	}
}

void UProceduralMeshFactory::ComposeFaceTriangles(const int VertexCount, TArray<int32>& Triangles)
{
	for (int32 i = 0; i < VertexCount - 2; ++i)
	{
		Triangles.Append({ 0, i + 1, i + 2 });
	}

	const int32 TopBase = VertexCount;
	for (int32 i = 0; i < VertexCount - 2; ++i)
	{
		Triangles.Append({ TopBase, TopBase + i + 2, TopBase + i + 1 });
	}
}

void UProceduralMeshFactory::BuildSideGeometry(
	const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, const FMeshGenerationParams& Params, FMeshData& MeshData)
{
	const int32 VertexCount = BottomVerts.Num();
	float		AccumulatedLength = 0.0f;
	int32		VertexOffset = VertexCount * 2;

	for (int32 i = 0; i < VertexCount; ++i)
	{
		const int32 NextIndex = (i + 1) % VertexCount;

		MeshData.Vertices.Append({ BottomVerts[i], BottomVerts[NextIndex], TopVerts[i], TopVerts[NextIndex] });

		const float EdgeLength = FVector2D::Distance(FVector2D(BottomVerts[i]), FVector2D(BottomVerts[NextIndex]));
		const float UStart = AccumulatedLength * Params.UVScale;
		const float UEnd = (AccumulatedLength + EdgeLength) * Params.UVScale;
		AccumulatedLength += EdgeLength;

		MeshData.UVs.Append({ FVector2D(UStart, 0.0f),
			FVector2D(UEnd, 0.0f),
			FVector2D(UStart, Params.Height * Params.UVScale),
			FVector2D(UEnd, Params.Height * Params.UVScale) });

		const FVector SideNormal = -FVector::CrossProduct(MeshData.Vertices[VertexOffset + 2] - MeshData.Vertices[VertexOffset],
			MeshData.Vertices[VertexOffset + 1] - MeshData.Vertices[VertexOffset])
										.GetSafeNormal();

		for (int32 j = 0; j < 4; ++j)
		{
			MeshData.Normals.Add(SideNormal);
		}

		MeshData.Triangles.Append({ VertexOffset, VertexOffset + 3, VertexOffset + 1, VertexOffset, VertexOffset + 2, VertexOffset + 3 });

		VertexOffset += 4;
	}
}

void UProceduralMeshFactory::CalcNormals(const int VertexCount, FMeshData& MeshData)
{
	for (int32 i = 0; i < VertexCount; ++i)
	{
		MeshData.Normals.Add(FVector::DownVector);
	}

	for (int32 i = 0; i < VertexCount; ++i)
	{
		MeshData.Normals.Add(FVector::UpVector);
	}
}

void UProceduralMeshFactory::CalcUVs(const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, const float UVScale, FMeshData& MeshData)
{
	for (const FVector& Vert : BottomVerts)
	{
		MeshData.UVs.Add(FVector2D(Vert.X * UVScale, Vert.Y * UVScale));
	}

	for (const FVector& Vert : TopVerts)
	{
		MeshData.UVs.Add(FVector2D(Vert.X * UVScale, Vert.Y * UVScale));
	}
}

void UProceduralMeshFactory::CalcTangentsAndColors(const int32 VertexCount, const FLinearColor& Color, FMeshData& MeshData)
{
	MeshData.Tangents.Init(FProcMeshTangent(1, 0, 0), VertexCount);
	MeshData.VertexColors.Init(Color, VertexCount);
}