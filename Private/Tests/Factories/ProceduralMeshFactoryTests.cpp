// Fill out your copyright notice in the Description page of Project Settings.
//
// Helpers carry a file-specific prefix rather than an anonymous namespace: unity builds can merge these test
// translation units and a plain name would collide.

#include "Factories/ProceduralMeshFactory.h"
#include "../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

/** A regular convex CCW polygon of SideCount vertices, centred on Center. */
static TArray<FVector2D> MeshFactoryTests_MakePolygon(const int32 SideCount, const double Radius, const FVector2D& Center)
{
	TArray<FVector2D> Vertices;
	Vertices.Reserve(SideCount);
	for (int32 i = 0; i < SideCount; ++i)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(i) / static_cast<double>(SideCount);
		Vertices.Emplace(Center.X + Radius * FMath::Cos(Angle), Center.Y + Radius * FMath::Sin(Angle));
	}
	return Vertices;
}

/** Exact element-for-element comparison, no tolerance: identical inputs must produce identical floats. */
static bool MeshFactoryTests_MeshesIdentical(const FMeshData& A, const FMeshData& B, FString& OutMismatch)
{
	if (A.Vertices.Num() != B.Vertices.Num() || A.Triangles.Num() != B.Triangles.Num() || A.Normals.Num() != B.Normals.Num()
		|| A.UVs.Num() != B.UVs.Num() || A.VertexColors.Num() != B.VertexColors.Num() || A.Tangents.Num() != B.Tangents.Num())
	{
		OutMismatch = FString::Printf(TEXT("array lengths differ (verts %d/%d, tris %d/%d, normals %d/%d, uvs %d/%d, colors %d/%d, tangents %d/%d)"),
			A.Vertices.Num(),
			B.Vertices.Num(),
			A.Triangles.Num(),
			B.Triangles.Num(),
			A.Normals.Num(),
			B.Normals.Num(),
			A.UVs.Num(),
			B.UVs.Num(),
			A.VertexColors.Num(),
			B.VertexColors.Num(),
			A.Tangents.Num(),
			B.Tangents.Num());
		return false;
	}

	for (int32 i = 0; i < A.Vertices.Num(); ++i)
	{
		if (A.Vertices[i] != B.Vertices[i])
		{
			OutMismatch = FString::Printf(TEXT("vertex %d differs (%s vs %s)"), i, *A.Vertices[i].ToString(), *B.Vertices[i].ToString());
			return false;
		}
		if (A.Normals[i] != B.Normals[i])
		{
			OutMismatch = FString::Printf(TEXT("normal %d differs"), i);
			return false;
		}
		if (A.UVs[i] != B.UVs[i])
		{
			OutMismatch = FString::Printf(TEXT("UV %d differs"), i);
			return false;
		}
		if (A.VertexColors[i] != B.VertexColors[i])
		{
			OutMismatch = FString::Printf(TEXT("vertex color %d differs"), i);
			return false;
		}
		if (A.Tangents[i].TangentX != B.Tangents[i].TangentX || A.Tangents[i].bFlipTangentY != B.Tangents[i].bFlipTangentY)
		{
			OutMismatch = FString::Printf(TEXT("tangent %d differs"), i);
			return false;
		}
	}

	for (int32 i = 0; i < A.Triangles.Num(); ++i)
	{
		if (A.Triangles[i] != B.Triangles[i])
		{
			OutMismatch = FString::Printf(TEXT("triangle index %d differs (%d vs %d)"), i, A.Triangles[i], B.Triangles[i]);
			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FProceduralMeshFactoryScratchReuseTest, "ProceduralGeometry.MeshFactory.PrismScratchReuseIsIdentical", DefaultTestFlags)

bool FProceduralMeshFactoryScratchReuseTest::RunTest(const FString& /*Parameters*/)
{
	// Footprints differ in vertex count, position, height, colour and skirt mask, so anything the buffer keeps shows up.
	const TArray<FVector2D> Triangle = MeshFactoryTests_MakePolygon(/*SideCount=*/3, /*Radius=*/250.0, FVector2D(0.0, 0.0));
	const TArray<FVector2D> Heptagon = MeshFactoryTests_MakePolygon(/*SideCount=*/7, /*Radius=*/900.0, FVector2D(1500.0, -400.0));

	const TArray<bool> AllSkirts = { true, true, true };
	const TArray<bool> PartialSkirts = { true, false, true, true, false, true, false };

	FMeshGenerationParams FirstParams;
	FirstParams.FoundationVertices = Triangle;
	FirstParams.EmitSkirtPerEdge = AllSkirts;
	FirstParams.Height = 120.f;
	FirstParams.BaseZ = -50.0;
	FirstParams.UVScale = 0.02f;
	FirstParams.MeshColor = FLinearColor(0.25f, 0.5f, 0.75f, 1.f);

	FMeshGenerationParams SecondParams;
	SecondParams.FoundationVertices = Heptagon;
	SecondParams.EmitSkirtPerEdge = PartialSkirts;
	SecondParams.Height = 340.f;
	SecondParams.BaseZ = 210.0;
	SecondParams.UVScale = 0.005f;
	SecondParams.MeshColor = FLinearColor(1.f, 0.f, 0.25f, 1.f);

	// Reference: a fresh buffer per call.
	FMeshData  FreshFirst;
	FMeshData  FreshSecond;
	const bool bFreshFirstBuilt = UProceduralMeshFactory::CreatePrismMesh(FirstParams, FreshFirst);
	const bool bFreshSecondBuilt = UProceduralMeshFactory::CreatePrismMesh(SecondParams, FreshSecond);
	TestTrue(TEXT("ScratchReuse: reference prism 1 builds"), bFreshFirstBuilt);
	TestTrue(TEXT("ScratchReuse: reference prism 2 builds"), bFreshSecondBuilt);
	TestTrue(TEXT("ScratchReuse: the two reference prisms are actually different meshes"), FreshFirst.Vertices != FreshSecond.Vertices);

	// Under test: one buffer, reused.
	FMeshData Scratch;
	TestTrue(TEXT("ScratchReuse: scratch prism 1 builds"), UProceduralMeshFactory::CreatePrismMesh(FirstParams, Scratch));

	FString Mismatch;
	if (!MeshFactoryTests_MeshesIdentical(Scratch, FreshFirst, Mismatch))
	{
		AddError(FString::Printf(TEXT("ScratchReuse: first prism differs from the fresh-buffer build — %s"), *Mismatch));
		return false;
	}

	TestTrue(TEXT("ScratchReuse: scratch prism 2 builds"), UProceduralMeshFactory::CreatePrismMesh(SecondParams, Scratch));
	if (!MeshFactoryTests_MeshesIdentical(Scratch, FreshSecond, Mismatch))
	{
		AddError(FString::Printf(TEXT("ScratchReuse: second prism differs from the fresh-buffer build — %s"), *Mismatch));
		return false;
	}

	// A scratch that has held a larger mesh must shrink back rather than trail the previous cell's geometry.
	TestTrue(TEXT("ScratchReuse: scratch prism 3 builds"), UProceduralMeshFactory::CreatePrismMesh(FirstParams, Scratch));
	if (!MeshFactoryTests_MeshesIdentical(Scratch, FreshFirst, Mismatch))
	{
		AddError(FString::Printf(TEXT("ScratchReuse: reusing a grown scratch for a smaller prism differs — %s"), *Mismatch));
		return false;
	}

	return true;
}

#endif
