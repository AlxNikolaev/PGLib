// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ProceduralMeshFactory.generated.h"

struct FProcMeshTangent;

USTRUCT(BlueprintType)
struct FMeshGenerationParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FVector2D> FoundationVertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Height = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float UVScale = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bGenerateCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor MeshColor = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct FMeshData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector> Vertices;

	UPROPERTY()
	TArray<int32> Triangles;

	UPROPERTY()
	TArray<FVector> Normals;

	UPROPERTY()
	TArray<FVector2D> UVs;

	UPROPERTY()
	TArray<FLinearColor> VertexColors;

	UPROPERTY()
	TArray<FProcMeshTangent> Tangents;

	void Reserve(const int32 VertexCount, const int32 TriangleCount)
	{
		Vertices.Reserve(VertexCount);
		Normals.Reserve(VertexCount);
		UVs.Reserve(VertexCount);
		VertexColors.Reserve(VertexCount);
		Tangents.Reserve(VertexCount);
		Triangles.Reserve(TriangleCount);
	}

	void Clear()
	{
		Vertices.Empty();
		Triangles.Empty();
		Normals.Empty();
		UVs.Empty();
		VertexColors.Empty();
		Tangents.Empty();
	}
};

UCLASS()
class PROCEDURALGEOMETRY_API UProceduralMeshFactory : public UObject
{
	GENERATED_BODY()

	static bool ValidateInput(const FMeshGenerationParams& Params);
	static void BuildVertices(const FMeshGenerationParams& Params, TArray<FVector>& BottomVerts, TArray<FVector>& TopVerts);
	static void ComposeFaceTriangles(const int VertexCount, TArray<int32>& Triangles);
	static void BuildSideGeometry(
		const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, const FMeshGenerationParams& Params, FMeshData& MeshData);
	static void CalcNormals(const int VertexCount, FMeshData& MeshData);
	static void CalcUVs(const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, float UVScale, FMeshData& MeshData);
	static void CalcTangentsAndColors(int32 VertexCount, const FLinearColor& Color, FMeshData& MeshData);

public:
	static bool CreatePrismMesh(const FMeshGenerationParams& Params, FMeshData& OutMeshData);
};
