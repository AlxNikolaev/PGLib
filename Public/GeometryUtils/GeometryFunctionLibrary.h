#pragma once

#include "CoreMinimal.h"

class PROCEDURALGEOMETRY_API FGeometryUtils
{
public:
	static bool SortPlaneVerticesByAngle(const TArray<FVector2D>& InVertices, TArray<FVector2D>& OutSortedVertices);
	static bool ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal);

	// Polygon utilities for POI placement
	static bool	 PointInPolygon(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point);
	static float DistanceToPolygonBoundary(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point);
	static bool	 MaxInscribedCircle(const TArray<FVector2D>& PolygonVertices, FVector2D& OutCenter, float& OutRadius, float Epsilon = 10.0f);
	static void	 PoissonDiskSampling(
		 const TArray<FVector2D>& PolygonVertices, float Radius, int32 MaxPoints, FRandomStream& RandomStream, TArray<FVector2D>& OutPoints);
	static FVector2D GetPolygonCentroid(const TArray<FVector2D>& PolygonVertices);

	/** Chaikin's corner-cutting subdivision. Smooths a closed polygon in place. Each iteration ~doubles vertex count.
	 *  No-op if Vertices has fewer than 3 elements or Iterations <= 0.
	 */
	static void ChaikinSubdivide(TArray<FVector2D>& Vertices, int32 Iterations = 2);

private:
	// Helper functions for polygon operations
	static float DistanceToLineSegment(const FVector2D& Point, const FVector2D& LineStart, const FVector2D& LineEnd);
	static void	 GetPolygonBounds(const TArray<FVector2D>& PolygonVertices, FVector2D& OutMin, FVector2D& OutMax);
};
