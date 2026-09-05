// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

	#include "Generators/LayoutGenerator.h"
	#include "Generators/Voronoi2D/VoronoiGenerator2D.h"

/** Structural hashes over generator output for same-seed determinism tests; prefixed because unity builds merge these translation units. */
namespace PGTestHash
{
	/** 1/100 of a world unit: finer than any placement decision downstream, coarser than float round-off. */
	inline constexpr double QuantizationScale = 100.0;

	inline uint32 HashPoint(uint32 Hash, const FVector2D& Point)
	{
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt64(Point.X * QuantizationScale)));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt64(Point.Y * QuantizationScale)));
		return Hash;
	}

	/** Order-sensitive: winding and starting vertex are part of what has to stay put. */
	inline uint32 HashPolygon(uint32 Hash, const TArray<FVector2D>& Vertices)
	{
		Hash = HashCombine(Hash, GetTypeHash(Vertices.Num()));
		for (const FVector2D& Vertex : Vertices)
		{
			Hash = HashPoint(Hash, Vertex);
		}
		return Hash;
	}

	/** Sorted before hashing: neighbour lists come from a TSet, so their raw order is hash-derived rather than stable. */
	inline uint32 HashNeighbors(uint32 Hash, const TArray<int32>& Neighbors)
	{
		Hash = HashCombine(Hash, GetTypeHash(Neighbors.Num()));

		TArray<int32> Sorted = Neighbors;
		Sorted.Sort();
		for (const int32 Neighbor : Sorted)
		{
			Hash = HashCombine(Hash, GetTypeHash(Neighbor));
		}
		return Hash;
	}

	inline uint32 HashVoronoiDiagram2D(const FVoronoiDiagram2D& Diagram)
	{
		uint32 Hash = GetTypeHash(Diagram.Cells.Num());
		Hash = HashCombine(Hash, GetTypeHash(Diagram.Sites.Num()));

		for (const FVector2D& Site : Diagram.Sites)
		{
			Hash = HashPoint(Hash, Site);
		}

		for (int32 CellIdx = 0; CellIdx < Diagram.Cells.Num(); ++CellIdx)
		{
			const FVoronoiCell2D& Cell = Diagram.Cells[CellIdx];
			Hash = HashCombine(Hash, GetTypeHash(CellIdx));
			Hash = HashCombine(Hash, GetTypeHash(Cell.CellIndex));
			Hash = HashCombine(Hash, GetTypeHash(Cell.bIsValid ? 1 : 0));
			Hash = HashCombine(Hash, GetTypeHash(Cell.bIsBoundaryCell ? 1 : 0));
			Hash = HashPoint(Hash, Cell.SiteLocation);
			Hash = HashPolygon(Hash, Cell.Vertices);
			Hash = HashNeighbors(Hash, Cell.Neighbors);
		}

		return Hash;
	}

	inline uint32 HashLayoutDiagram2D(const FLayoutDiagram2D& Diagram)
	{
		uint32 Hash = GetTypeHash(Diagram.Cells.Num());
		Hash = HashCombine(Hash, GetTypeHash(Diagram.CenterCellIndex));
		Hash = HashPoint(Hash, Diagram.CenterPoint);

		for (int32 CellIdx = 0; CellIdx < Diagram.Cells.Num(); ++CellIdx)
		{
			const FLayoutCell2D& Cell = Diagram.Cells[CellIdx];
			Hash = HashCombine(Hash, GetTypeHash(CellIdx));
			Hash = HashCombine(Hash, GetTypeHash(Cell.CellIndex));
			Hash = HashCombine(Hash, GetTypeHash(Cell.bIsExterior ? 1 : 0));
			Hash = HashPoint(Hash, Cell.Center);
			Hash = HashPolygon(Hash, Cell.Vertices);
			Hash = HashNeighbors(Hash, Cell.Neighbors);
		}

		return Hash;
	}
} // namespace PGTestHash

#endif
