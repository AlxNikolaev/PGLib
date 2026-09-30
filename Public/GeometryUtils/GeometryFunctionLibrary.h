#pragma once

#include "CoreMinimal.h"

class PROCEDURALGEOMETRY_API FGeometryUtils
{
public:
	/** Sorts at least three XY vertices by polar angle around their arithmetic centroid.
	 * Copies shorter inputs unchanged to OutSortedVertices
	 * and returns false; otherwise returns true. */
	static bool SortPlaneVerticesByAngle(const TArray<FVector2D>& InVertices, TArray<FVector2D>& OutSortedVertices);
	/** Clips OutPolygon in place to the nonpositive side of the plane normal.
	 * Returns true when at least three vertices survive; an empty input
	 * returns false unchanged. */
	static bool ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal);

	/** Clips in place with reusable Scratch to avoid allocation across successive calls.
	 * Scratch is cleared/swapped on nonempty input; true
	 * means at least three vertices survive. */
	static bool ClipPolygonByHalfPlane(
		TArray<FVector2D>& OutPolygon, TArray<FVector2D>& Scratch, const FVector2D& PlanePoint, const FVector2D& PlaneNormal);

	/** Winding-number containment query for a polygon of at least three vertices.
	 * Returns false for shorter inputs; no mutation or world access.
	 */
	static bool PointInPolygon(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point);
	/** Minimum Euclidean distance from Point to a polygon edge in the input's units.
	 * Returns zero for fewer than three vertices. */
	static float DistanceToPolygonBoundary(const TArray<FVector2D>& PolygonVertices, const FVector2D& Point);
	/** Approximates the largest inscribed circle using a bounded polylabel search.
	 * Returns false for degenerate or unrepresentable bounds;
	 * outputs are meaningful on success. */
	static bool MaxInscribedCircle(const TArray<FVector2D>& PolygonVertices, FVector2D& OutCenter, float& OutRadius, float Epsilon = 10.0f);
	/** Replaces OutPoints with up to MaxPoints sites inside the polygon, separated by Radius where possible.
	 * Uses the supplied RNG; invalid
	 * polygon, radius or budget produces an empty result. */
	static void PoissonDiskSampling(
		const TArray<FVector2D>& PolygonVertices, float Radius, int32 MaxPoints, FRandomStream& RandomStream, TArray<FVector2D>& OutPoints);
	/** Returns the area-weighted XY polygon centroid, with defined empty/one/two-point fallbacks.
	 * Pure value calculation; degenerate area uses
	 * the implementation's fallback. */
	static FVector2D GetPolygonCentroid(const TArray<FVector2D>& PolygonVertices);

	/** Chaikin's corner-cutting subdivision. Smooths a closed polygon in place. Each iteration ~doubles vertex count.
	 *  No-op if Vertices has fewer than 3 elements or Iterations <= 0.
	 */
	static void ChaikinSubdivide(TArray<FVector2D>& Vertices, int32 Iterations = 2);

	/** Rotate a 2D vector by RotationDeg (CCW, +Y-up math convention). */
	static FVector2D RotateVector(const FVector2D& V, float RotationDeg);

	/** World-space CCW corners of an axis-aligned rect (full Width,Height = Footprint) centered at Center, rotated by RotationDeg. */
	static void RotatedRectCorners(const FVector2D& Center, float RotationDeg, const FVector2D& Footprint, TArray<FVector2D>& OutCorners);

	/** Convex-vs-convex overlap via the Separating Axis Theorem. Touching counts as overlap (conservative). */
	static bool ConvexPolygonsOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B);

	/** Uniform Catmull-Rom sampling through control points (SamplesPerSegment per span). <3 points => copied through. */
	static void SampleCatmullRom(const TArray<FVector2D>& ControlPoints, int32 SamplesPerSegment, TArray<FVector2D>& OutPoints);

	/** Offset a polyline into a closed ribbon polygon of the given width (per-vertex normals). Empty if <2 points. */
	static void OffsetPolylineToRibbon(const TArray<FVector2D>& Polyline, float Width, TArray<FVector2D>& OutRibbon);

private:
	static float DistanceToLineSegment(const FVector2D& Point, const FVector2D& LineStart, const FVector2D& LineEnd);
	static void	 GetPolygonBounds(const TArray<FVector2D>& PolygonVertices, FVector2D& OutMin, FVector2D& OutMax);
};
