#pragma once

class PROCEDURALGEOMETRY_API FGeometryUtils
{
public:
	static bool SortPlaneVerticesByAngle(const TArray<FVector2D>& InVertices, TArray<FVector2D>& OutSortedVertices);
	static bool ComputeBisector2D(const FVector2D& P1, const FVector2D& P2, const FVector2D& OutStart, const FVector2D& OutEnd);
	static bool ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal);

	// Polygon utilities for POI placement
	static bool	 PointInPolygon(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point);
	static float DistanceToPolygonBoundary(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point);
	static bool	 MaxInscribedCircle(const TArray<FVector2D>& PolygonVertices, FVector2D& OutCenter, float& OutRadius, float Epsilon = 10.0f);
	static void	 PoissonDiskSampling(
		 const TArray<FVector2D>& PolygonVertices, float Radius, int32 MaxPoints, FRandomStream& RandomStream, TArray<FVector2D>& OutPoints);
	static FVector2D GetPolygonCentroid(const TArray<FVector2D>& PolygonVertices);

private:
	// Helper functions for polygon operations
	static float DistanceToLineSegment(const FVector2D& Point, const FVector2D& LineStart, const FVector2D& LineEnd);
};
