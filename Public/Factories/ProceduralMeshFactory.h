// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/Object.h"
#include "ProceduralMeshComponent.h"
#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "ProceduralMeshFactory.generated.h"

/**
 * Call-scoped input block for CreatePrismMesh.
 *
 * Deliberately unreflected: the two fields that define the prism are views onto arrays the caller owns, and a
 * borrowed pointer is something neither a Blueprint value nor a serialized copy can carry. Reflecting the
 * scalars alone would advertise a value type that survives a Make-struct node or a save/load with its footprint
 * silently gone, which ValidateInput rejects as a missing mesh section far from wherever the copy was made.
 */
struct PROCEDURALGEOMETRY_API FMeshGenerationParams
{
	/** Convex, CCW polygon footprint. A VIEW, not a copy: the array it refers to must stay alive and
	 *  unmodified until CreatePrismMesh returns, so a merge loop can point it at each cell's own vertex
	 *  array instead of copying one polygon per cell. */
	TArrayView<const FVector2D> FoundationVertices;

	float Height = 100.0f;

	/** Z of the prism's bottom plane; the top plane lands at BaseZ + Height. Callers that seat a prism on a
	 *  non-zero base plane pass it here rather than translating the emitted vertices afterwards, so the mesh
	 *  and everything measured against its top surface share one origin. */
	double BaseZ = 0.0;

	float UVScale = 0.01f;

	/** NOTE: not consumed by the factory — the mesh data it produces carries no collision flag. Collision is
	 *  decided downstream by the consumer's UProceduralMeshComponent::CreateMeshSection (bCreateCollision) call.
	 *  This field is a hint passed through to that consumer, not an input to mesh generation here. */
	bool bGenerateCollision = true;

	FLinearColor MeshColor = FLinearColor::White;

	/** Per-edge skirt mask, indexed like FoundationVertices (edge i spans vertex i → i+1). When non-empty,
	 *  a false entry suppresses that side skirt — used to drop walls interior to a merged slab. An empty
	 *  view emits every skirt (closed solo prism). Same lifetime rule as FoundationVertices. */
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

	/** Empties every array while keeping its allocation, so one scratch mesh can be reused across a merge
	 *  loop without re-acquiring the same buffers on every cell. */
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
	 *  OutMeshData is reset on success, so it may be a scratch buffer reused across many calls; a rejected
	 *  input returns false and leaves it untouched.
	 *  Precondition: FoundationVertices must be convex and wound counter-clockwise.
	 *  Concave polygons produce broken cap geometry (fan triangulation from vertex 0 is only
	 *  correct for convex input). A warning is logged when a reflex vertex is detected. */
	static bool CreatePrismMesh(const FMeshGenerationParams& Params, FMeshData& OutMeshData);

	/** Builds a per-cell side-skirt mask for a set of cells merged into one slab. Output[c][e] is false when
	 *  edge e of SlabCells[c] is shared with another cell in the same slab (an interior wall to cull), true
	 *  when it lies on the slab's outer boundary. Indexing matches each cell's Vertices array. */
	static void BuildSlabSkirtMasks(const TArray<const FVoronoiCell2D*>& SlabCells, TArray<TArray<bool>>& OutMasks);
};
