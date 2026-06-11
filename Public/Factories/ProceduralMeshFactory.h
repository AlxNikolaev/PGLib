// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ProceduralMeshComponent.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "ProceduralMeshFactory.generated.h"

USTRUCT(BlueprintType)
struct PROCEDURALGEOMETRY_API FMeshGenerationParams
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

	/** Per-edge skirt mask, indexed like FoundationVertices (edge i spans vertex i → i+1). When non-empty,
	 *  a false entry suppresses that side skirt — used to drop walls interior to a merged slab. An empty
	 *  array emits every skirt (closed solo prism). */
	TArray<bool> EmitSkirtPerEdge;
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
	/** Creates a prism mesh from a convex, CCW polygon footprint extruded by Height.
	 *  Precondition: FoundationVertices must be convex and wound counter-clockwise.
	 *  Concave polygons produce broken cap geometry (fan triangulation from vertex 0 is only
	 *  correct for convex input). A warning is logged when a reflex vertex is detected. */
	static bool CreatePrismMesh(const FMeshGenerationParams& Params, FMeshData& OutMeshData);

	/** Builds a per-cell side-skirt mask for a set of cells merged into one slab. Output[c][e] is false when
	 *  edge e of SlabCells[c] is shared with another cell in the same slab (an interior wall to cull), true
	 *  when it lies on the slab's outer boundary. Indexing matches each cell's Vertices array. */
	static void BuildSlabSkirtMasks(const TArray<const FVoronoiCell2D*>& SlabCells, TArray<TArray<bool>>& OutMasks);
};
