// Fill out your copyright notice in the Description page of Project Settings.

#include "Factories/ProceduralMeshFactory.h"
#include "ProceduralMeshComponent.h"

#include "ProceduralGeometry.h"

bool UProceduralMeshFactory::CreatePrismMesh(const FMeshGenerationParams& Params, FMeshData& OutMeshData)
{
	if (!ValidateInput(Params))
	{
		return false;
	}

	OutMeshData.Clear();

	const int32 VertexCount = Params.FoundationVertices.Num();

	int32 SkirtEdges = VertexCount;
	if (Params.EmitSkirtPerEdge.Num() == VertexCount)
	{
		SkirtEdges = 0;
		for (const bool bEmit : Params.EmitSkirtPerEdge)
		{
			SkirtEdges += bEmit ? 1 : 0;
		}
	}

	const int32 TotalVertices = VertexCount * 2 + SkirtEdges * 4;
	const int32 TotalTriangles = ((VertexCount - 2) * 2 + SkirtEdges * 2) * 3;

	OutMeshData.Reserve(TotalVertices, TotalTriangles);

	TArray<FVector> BottomVerts, TopVerts;
	BuildVertices(Params, BottomVerts, TopVerts);

	OutMeshData.Vertices.Append(BottomVerts);
	OutMeshData.Vertices.Append(TopVerts);

	// Order matters!!!
	ComposeFaceTriangles(Params.FoundationVertices, OutMeshData.Triangles);
	CalcNormals(VertexCount, OutMeshData);
	CalcUVs(BottomVerts, TopVerts, Params.UVScale, OutMeshData);
	BuildSideGeometry(BottomVerts, TopVerts, Params, OutMeshData);
	CalcTangentsAndColors(OutMeshData.Vertices.Num(), Params.MeshColor, OutMeshData);

	return true;
}

void UProceduralMeshFactory::BuildSlabSkirtMasks(const TArray<const FVoronoiCell2D*>& SlabCells, TArray<TArray<bool>>& OutMasks)
{
	OutMasks.Reset();
	OutMasks.SetNum(SlabCells.Num());

	constexpr float QuantScale = 1.0f; // 1cm weld tolerance — coincident cell edges share quantized endpoints

	auto QuantizePoint = [](const FVector2D& P) -> FIntPoint {
		return FIntPoint(FMath::RoundToInt(P.X * QuantScale), FMath::RoundToInt(P.Y * QuantScale));
	};
	auto EdgeKey = [&QuantizePoint](const FVector2D& A, const FVector2D& B) -> TPair<FIntPoint, FIntPoint> {
		const FIntPoint QA = QuantizePoint(A);
		const FIntPoint QB = QuantizePoint(B);
		const bool		bAFirst = QA.X < QB.X || (QA.X == QB.X && QA.Y <= QB.Y);
		return bAFirst ? TPair<FIntPoint, FIntPoint>(QA, QB) : TPair<FIntPoint, FIntPoint>(QB, QA);
	};

	TMap<TPair<FIntPoint, FIntPoint>, int32> EdgeUseCount;
	for (const FVoronoiCell2D* Cell : SlabCells)
	{
		if (!Cell || Cell->Vertices.Num() < 3)
		{
			continue;
		}
		const int32 N = Cell->Vertices.Num();
		for (int32 e = 0; e < N; ++e)
		{
			EdgeUseCount.FindOrAdd(EdgeKey(Cell->Vertices[e], Cell->Vertices[(e + 1) % N]), 0)++;
		}
	}

	for (int32 c = 0; c < SlabCells.Num(); ++c)
	{
		const FVoronoiCell2D* Cell = SlabCells[c];
		if (!Cell || Cell->Vertices.Num() < 3)
		{
			continue;
		}
		const int32 N = Cell->Vertices.Num();
		OutMasks[c].SetNumUninitialized(N);
		for (int32 e = 0; e < N; ++e)
		{
			const TPair<FIntPoint, FIntPoint> Key = EdgeKey(Cell->Vertices[e], Cell->Vertices[(e + 1) % N]);
			// A sub-tolerance (degenerate) edge gets no skirt — its quad would be zero-area.
			if (Key.Key == Key.Value)
			{
				OutMasks[c][e] = false;
				continue;
			}
			const int32* Count = EdgeUseCount.Find(Key);
			OutMasks[c][e] = !Count || *Count <= 1;
		}
	}
}

bool UProceduralMeshFactory::ValidateInput(const FMeshGenerationParams& Params)
{
	if (Params.FoundationVertices.Num() < 3)
	{
		UE_LOG(LogRoguelikeGeometry,
			Warning,
			TEXT("ProceduralMeshGenerator: Invalid foundation vertices count (%d), minimum 3 required"),
			Params.FoundationVertices.Num());
		return false;
	}

	if (Params.Height <= 0.0f)
	{
		UE_LOG(LogRoguelikeGeometry, Warning, TEXT("ProceduralMeshGenerator: Invalid height (%f), must be positive"), Params.Height);
		return false;
	}

	// Fan triangulation from vertex 0 produces broken geometry for concave polygons. Warn when a
	// reflex vertex (negative cross product in CCW winding) is detected.
	{
		const int32 N = Params.FoundationVertices.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Params.FoundationVertices[(i - 1 + N) % N];
			const FVector2D& B = Params.FoundationVertices[i];
			const FVector2D& C = Params.FoundationVertices[(i + 1) % N];
			const float		 Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
			if (Cross < -UE_KINDA_SMALL_NUMBER)
			{
				UE_LOG(LogRoguelikeGeometry,
					Warning,
					TEXT(
						"ProceduralMeshGenerator: CreatePrismMesh requires a convex CCW polygon; reflex vertex detected at index %d. Cap triangulation will be incorrect."),
					i);
				break;
			}
		}
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

void UProceduralMeshFactory::ComposeFaceTriangles(const TArray<FVector2D>& FoundationVertices, TArray<int32>& Triangles)
{
	const int32 VertexCount = FoundationVertices.Num();
	const int32 TopBase = VertexCount;

	for (int32 i = 0; i < VertexCount - 2; ++i)
	{
		// Voronoi clipping can emit coincident/collinear polygon vertices; their fan triangles are
		// degenerate (zero area) and pollute collision cooking — drop them, keep the shared vertices.
		const FVector2D E1 = FoundationVertices[i + 1] - FoundationVertices[0];
		const FVector2D E2 = FoundationVertices[i + 2] - FoundationVertices[0];
		if (FMath::Abs(E1 ^ E2) < 1.0f)
		{
			continue;
		}
		Triangles.Append({ 0, i + 1, i + 2 });
		Triangles.Append({ TopBase, TopBase + i + 2, TopBase + i + 1 });
	}
}

void UProceduralMeshFactory::BuildSideGeometry(
	const TArray<FVector>& BottomVerts, const TArray<FVector>& TopVerts, const FMeshGenerationParams& Params, FMeshData& MeshData)
{
	const int32 VertexCount = BottomVerts.Num();
	const bool	bMaskedSkirts = Params.EmitSkirtPerEdge.Num() == VertexCount;
	float		AccumulatedLength = 0.0f;
	int32		VertexOffset = VertexCount * 2;

	for (int32 i = 0; i < VertexCount; ++i)
	{
		const int32 NextIndex = (i + 1) % VertexCount;
		const float EdgeLength = FVector2D::Distance(FVector2D(BottomVerts[i]), FVector2D(BottomVerts[NextIndex]));

		const float UStart = AccumulatedLength * Params.UVScale;
		const float UEnd = (AccumulatedLength + EdgeLength) * Params.UVScale;
		AccumulatedLength += EdgeLength;

		if (bMaskedSkirts && !Params.EmitSkirtPerEdge[i])
		{
			continue;
		}

		// Degenerate edge (coincident vertices) — its skirt quad would be zero-area.
		if (EdgeLength < 0.5f)
		{
			continue;
		}

		MeshData.Vertices.Append({ BottomVerts[i], BottomVerts[NextIndex], TopVerts[i], TopVerts[NextIndex] });

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