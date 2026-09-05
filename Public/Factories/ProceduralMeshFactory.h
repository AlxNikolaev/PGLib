// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/Object.h"
#include "ProceduralMeshComponent.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "ProceduralMeshFactory.generated.h"

/**
 * Call-scoped input block for CreatePrismMesh. Deliberately unreflected: the fields that define the prism are
 * views onto caller-owned arrays, which neither a Blueprint value nor a serialized copy can carry.
 */
struct PROCEDURALGEOMETRY_API FMeshGenerationParams
{
	/** Convex, CCW polygon footprint. A view, not a copy: the array it refers to must stay alive and
	 *  unmodified until CreatePrismMesh returns. */
	TArrayView<const FVector2D> FoundationVertices;

	float Height = 100.0f;

	/** Z of the prism's bottom plane; the top plane lands at BaseZ + Height. Seating a prism on a non-zero base
	 *  plane goes through here rather than translating the emitted vertices afterwards. */
	double BaseZ = 0.0;

	float UVScale = 0.01f;

	/** Not consumed by the factory: a hint passed through to the consumer's
	 *  UProceduralMeshComponent::CreateMeshSection call. */
	bool bGenerateCollision = true;

	FLinearColor MeshColor = FLinearColor::White;

	/** Per-edge skirt mask, indexed like FoundationVertices (edge i spans vertex i → i+1); a false entry
	 *  suppresses that skirt, an empty view emits all of them. Same lifetime rule as FoundationVertices. */
	TArrayView<const bool> EmitSkirtPerEdge;
};

USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FMeshData
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

	/** Empties every array but keeps its allocation, so one scratch mesh can be reused across a merge loop. */
	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UVs.Reset();
		VertexColors.Reset();
		Tangents.Reset();
	}
};

UCLASS()
class PROCEDURALGEOMETRY_API UProceduralMeshFactory : public UObject
{
	GENERATED_BODY()

	static bool ValidateInput(const FMeshGenerationParams& Params);
	static void BuildVertices(const FMeshGenerationParams& Params, TArray<FVector>& BottomVerts, TArray<FVector>& TopVerts);
	static void ComposeFaceTriangles(TArrayView<const FVector2D> FoundationVertices, TArray<int32>& Triangles);
	static void BuildSideGeometry(
		const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, const FMeshGenerationParams& Params, FMeshData& MeshData);
	static void CalcNormals(const int VertexCount, FMeshData& MeshData);
	static void CalcUVs(const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, float UVScale, FMeshData& MeshData);
	static void CalcTangentsAndColors(int32 VertexCount, const FLinearColor& Color, FMeshData& MeshData);

public:
	/** Creates a prism mesh from a convex, CCW polygon footprint spanning Z in [BaseZ, BaseZ + Height].
	 *  OutMeshData is reset on success and left untouched when the input is rejected. Concave input produces
	 *  broken caps: the fan triangulation from vertex 0 is only correct for convex, counter-clockwise input. */
	static bool CreatePrismMesh(const FMeshGenerationParams& Params, FMeshData& OutMeshData);

	/** Builds a per-cell side-skirt mask for cells merged into one slab: Output[c][e] is false when edge e of
	 *  SlabCells[c] is interior to the slab. Indexing matches each cell's Vertices array. */
	static void BuildSlabSkirtMasks(const TArray<const FVoronoiCell2D*>& SlabCells, TArray<TArray<bool>>& OutMasks);
};
