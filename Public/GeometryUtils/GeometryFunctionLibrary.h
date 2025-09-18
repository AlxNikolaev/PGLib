#pragma once

class FGeometryUtils
{
public:
	static bool SortPlaneVerticesByAngle(const TArray<FVector2D>& InVertices, TArray<FVector2D>& OutSortedVertices);
	static bool ComputeBisector2D(const FVector2D& P1, const FVector2D& P2, const FVector2D& OutStart, const FVector2D& OutEnd);
	static bool ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal);
};
