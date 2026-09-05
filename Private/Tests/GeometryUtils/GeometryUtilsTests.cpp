#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Unit square [0,0]-[100,100] in CCW order */
	TArray<FVector2D> MakeSquare100()
	{
		return { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };
	}

	/**
	 * Reference Poisson sampler: same RNG draws and same candidate acceptance as FGeometryUtils::PoissonDiskSampling,
	 * but the rejection test scans every accepted point instead of a bucket ring. The library's ring width is only an
	 * acceleration claim, so any ring that is too narrow shows up as a divergence from this sequence.
	 */
	void PoissonRef_SampleWithLinearRejection(
		const TArray<FVector2D>& PolygonVertices, float Radius, int32 MaxPoints, FRandomStream& RandomStream, TArray<FVector2D>& OutPoints)
	{
		OutPoints.Empty();

		if (PolygonVertices.Num() < 3 || Radius < UE_KINDA_SMALL_NUMBER || MaxPoints <= 0)
		{
			return;
		}

		FVector2D MinBounds(FLT_MAX, FLT_MAX);
		FVector2D MaxBounds(-FLT_MAX, -FLT_MAX);
		for (const FVector2D& Vertex : PolygonVertices)
		{
			MinBounds.X = FMath::Min(MinBounds.X, Vertex.X);
			MinBounds.Y = FMath::Min(MinBounds.Y, Vertex.Y);
			MaxBounds.X = FMath::Max(MaxBounds.X, Vertex.X);
			MaxBounds.Y = FMath::Max(MaxBounds.Y, Vertex.Y);
		}

		FVector2D InitialPoint;
		bool	  bFoundInitial = false;
		for (int32 Attempts = 0; Attempts < 100 && !bFoundInitial; ++Attempts)
		{
			InitialPoint.X = RandomStream.FRandRange(MinBounds.X, MaxBounds.X);
			InitialPoint.Y = RandomStream.FRandRange(MinBounds.Y, MaxBounds.Y);
			bFoundInitial = FGeometryUtils::PointInPolygon(PolygonVertices, InitialPoint);
		}

		if (!bFoundInitial)
		{
			return;
		}

		OutPoints.Add(InitialPoint);

		TArray<int32> ActivePoints;
		ActivePoints.Add(0);

		while (ActivePoints.Num() > 0 && OutPoints.Num() < MaxPoints)
		{
			const int32		 ActiveIndex = RandomStream.RandRange(0, ActivePoints.Num() - 1);
			const FVector2D& ActivePoint = OutPoints[ActivePoints[ActiveIndex]];

			bool bFoundValidCandidate = false;
			for (int32 Candidate = 0; Candidate < 30; ++Candidate)
			{
				const float Angle = RandomStream.FRandRange(0, 2.0f * PI);
				const float Distance = RandomStream.FRandRange(Radius, 2.0f * Radius);

				const FVector2D CandidatePoint = ActivePoint + FVector2D(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance);
				if (!FGeometryUtils::PointInPolygon(PolygonVertices, CandidatePoint))
				{
					continue;
				}

				bool bTooClose = false;
				for (const FVector2D& Existing : OutPoints)
				{
					if (FVector2D::DistSquared(CandidatePoint, Existing) < Radius * Radius)
					{
						bTooClose = true;
						break;
					}
				}

				if (!bTooClose)
				{
					ActivePoints.Add(OutPoints.Add(CandidatePoint));
					bFoundValidCandidate = true;
					break;
				}
			}

			if (!bFoundValidCandidate)
			{
				ActivePoints.RemoveAt(ActiveIndex);
			}
		}
	}

	/** Check all vertices are finite (no NaN/Inf) */
	bool AllVerticesFinite(const TArray<FVector2D>& Vertices)
	{
		for (const FVector2D& V : Vertices)
		{
			if (!FMath::IsFinite(V.X) || !FMath::IsFinite(V.Y))
			{
				return false;
			}
		}
		return true;
	}
} // namespace

// ============================================================
// ClipPolygonByHalfPlane
// ============================================================

// Test 1: All-inside — polygon entirely on kept side → unchanged
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClipAllInsideTest, "ProceduralGeometry.GeometryUtils.Clip.AllInside", DefaultTestFlags)

bool FClipAllInsideTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Polygon = MakeSquare100();

	// PlanePoint=(200,0), PlaneNormal=(1,0) → kept side: X ≤ 200
	// All square vertices have X ≤ 100, so all inside
	bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(200, 0), FVector2D(1, 0));

	TestTrue("Should return true (polygon intact)", bResult);
	TestEqual("Should still have 4 vertices", Polygon.Num(), 4);

	return true;
}

// Test 2: All-outside — polygon entirely on clipped side → empty, returns false
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClipAllOutsideTest, "ProceduralGeometry.GeometryUtils.Clip.AllOutside", DefaultTestFlags)

bool FClipAllOutsideTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Polygon = MakeSquare100();

	// PlanePoint=(-10,0), PlaneNormal=(1,0) → kept side: dot ≤ 0, i.e. X ≤ -10
	// All square vertices have X ≥ 0, so all outside
	bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(-10, 0), FVector2D(1, 0));

	TestFalse("Should return false (nothing left)", bResult);
	TestTrue("Should have < 3 vertices", Polygon.Num() < 3);

	return true;
}

// Test 3: Clip square by horizontal plane → bottom rectangle
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClipSquareHorizontalTest, "ProceduralGeometry.GeometryUtils.Clip.HorizontalClip", DefaultTestFlags)

bool FClipSquareHorizontalTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Polygon = MakeSquare100();

	// PlanePoint=(0,50), PlaneNormal=(0,1) → kept side: Y ≤ 50
	bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(0, 50), FVector2D(0, 1));

	TestTrue("Should return true (clipped polygon)", bResult);
	TestEqual("Clipped polygon should have 4 vertices", Polygon.Num(), 4);

	// All resulting Y values should be ≤ 50
	for (int32 i = 0; i < Polygon.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Vertex %d Y should be <= 50"), i), Polygon[i].Y <= 50.0f + UE_KINDA_SMALL_NUMBER);
	}

	return true;
}

// Test 4: Vertex exactly on boundary → handled without NaN
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClipVertexOnBoundaryTest, "ProceduralGeometry.GeometryUtils.Clip.VertexOnBoundary", DefaultTestFlags)

bool FClipVertexOnBoundaryTest::RunTest(const FString& Parameters)
{
	// Triangle with one vertex exactly on the clip plane
	TArray<FVector2D> Polygon = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(50, 100) };

	// PlanePoint=(50,0), PlaneNormal=(1,0) → kept: X ≤ 50
	// (0,0) inside, (100,0) outside, (50,100) exactly on boundary (dot = 0)
	bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(50, 0), FVector2D(1, 0));

	TestTrue("Should return true", bResult);
	TestTrue("All vertices should be finite (no NaN)", AllVerticesFinite(Polygon));
	TestTrue("Should have >= 3 vertices", Polygon.Num() >= 3);

	// All X values should be ≤ 50
	for (int32 i = 0; i < Polygon.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Vertex %d X should be <= 50"), i), Polygon[i].X <= 50.0f + UE_KINDA_SMALL_NUMBER);
	}

	return true;
}

// Test 5: Near-tangential clip — SafeAlpha fires, no Inf/NaN
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClipNearTangentialTest, "ProceduralGeometry.GeometryUtils.Clip.NearTangential", DefaultTestFlags)

bool FClipNearTangentialTest::RunTest(const FString& Parameters)
{
	// Bottom edge nearly tangential to clip plane:
	// Two bottom vertices straddle Y=0 by ~1e-12
	TArray<FVector2D> Polygon = { FVector2D(0, 1e-12), FVector2D(100, -1e-12), FVector2D(100, 100), FVector2D(0, 100) };

	// PlanePoint=(0,0), PlaneNormal=(0,1) → kept: Y ≤ 0
	// (0,1e-12) barely outside, (100,-1e-12) barely inside, (100,100) and (0,100) outside
	// SafeAlpha fires on the edge between the two near-zero vertices
	bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(0, 0), FVector2D(0, 1));

	// May or may not produce a valid polygon, but must not produce NaN/Inf
	TestTrue("All result vertices should be finite (no NaN/Inf)", AllVerticesFinite(Polygon));

	return true;
}

// Test 5b: A clip plane running exactly through two polygon vertices emits each of them once.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClipPlaneCoincidentVertexTest, "ProceduralGeometry.GeometryUtils.Clip.PlaneCoincidentVertexEmittedOnce", DefaultTestFlags)

bool FClipPlaneCoincidentVertexTest::RunTest(const FString& Parameters)
{
	// Counts cyclically consecutive vertex pairs that sit on top of each other.
	const auto CountCoincidentNeighbours = [](const TArray<FVector2D>& Vertices) -> int32 {
		int32 Count = 0;
		for (int32 i = 0; i < Vertices.Num(); ++i)
		{
			if (FVector2D::Distance(Vertices[i], Vertices[(i + 1) % Vertices.Num()]) < UE_KINDA_SMALL_NUMBER)
			{
				++Count;
			}
		}
		return Count;
	};

	// The diagonal through (0,0) and (100,100) touches two square corners exactly. Walking the square
	// CCW hits both degenerate cases in one pass: the crossing at (0,0) lands on the edge's Prev end and
	// the crossing at (100,100) lands on its Curr end, and both endpoints are appended in their own right.
	{
		TArray<FVector2D> Polygon = MakeSquare100();

		// PlanePoint=(0,0), PlaneNormal=(1,-1) → kept side: X - Y <= 0 (the upper-left triangle).
		const bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(0, 0), FVector2D(1, -1));

		TestTrue("Upper-left half: should return true", bResult);
		TestEqual("Upper-left half: triangle has exactly 3 vertices", Polygon.Num(), 3);
		TestEqual("Upper-left half: no consecutive coincident vertices", CountCoincidentNeighbours(Polygon), 0);
		for (int32 i = 0; i < Polygon.Num(); ++i)
		{
			TestTrue(
				FString::Printf(TEXT("Upper-left half: vertex %d satisfies X - Y <= 0"), i), Polygon[i].X - Polygon[i].Y <= UE_KINDA_SMALL_NUMBER);
		}
	}

	// The mirrored half-plane keeps the same two degenerate branches but hits them at the opposite corners
	// and in the opposite order, so this half confirms independently that neither endpoint is emitted twice.
	{
		TArray<FVector2D> Polygon = MakeSquare100();

		// PlanePoint=(0,0), PlaneNormal=(-1,1) → kept side: Y - X <= 0 (the lower-right triangle).
		const bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(0, 0), FVector2D(-1, 1));

		TestTrue("Lower-right half: should return true", bResult);
		TestEqual("Lower-right half: triangle has exactly 3 vertices", Polygon.Num(), 3);
		TestEqual("Lower-right half: no consecutive coincident vertices", CountCoincidentNeighbours(Polygon), 0);
		for (int32 i = 0; i < Polygon.Num(); ++i)
		{
			TestTrue(
				FString::Printf(TEXT("Lower-right half: vertex %d satisfies Y - X <= 0"), i), Polygon[i].Y - Polygon[i].X <= UE_KINDA_SMALL_NUMBER);
		}
	}

	// A triangle whose clip plane runs through two of its corners survives as those two corners alone.
	// Emitting each of them once means the result is a two-vertex sliver and the clip reports failure, so
	// the caller drops a zero-area cell instead of accepting a "triangle" made of a duplicated point.
	{
		TArray<FVector2D> Polygon = { FVector2D(0, 0), FVector2D(100, 100), FVector2D(100, 0) };

		// PlanePoint=(0,0), PlaneNormal=(1,-1) → kept side: X - Y <= 0, which only (0,0) and (100,100) meet.
		const bool bResult = FGeometryUtils::ClipPolygonByHalfPlane(Polygon, FVector2D(0, 0), FVector2D(1, -1));

		TestFalse("Degenerate sliver: clip reports failure", bResult);
		TestEqual("Degenerate sliver: exactly the two plane-coincident corners remain", Polygon.Num(), 2);
	}

	return true;
}

// ============================================================
// SortPlaneVerticesByAngle
// ============================================================

// Test 6: Known shuffled vertices → CCW order
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSortVerticesCCWTest, "ProceduralGeometry.GeometryUtils.Sort.ShuffledToCCW", DefaultTestFlags)

bool FSortVerticesCCWTest::RunTest(const FString& Parameters)
{
	// Shuffled square vertices
	TArray<FVector2D> Input = { FVector2D(100, 0), FVector2D(0, 100), FVector2D(0, 0), FVector2D(100, 100) };
	TArray<FVector2D> Output;

	bool bResult = FGeometryUtils::SortPlaneVerticesByAngle(Input, Output);

	TestTrue("Should return true for >= 3 vertices", bResult);
	TestEqual("Should have 4 sorted vertices", Output.Num(), 4);

	// Verify CCW ordering: each angle from centroid should be increasing
	FVector2D Centroid(50, 50);
	for (int32 i = 0; i < Output.Num() - 1; ++i)
	{
		float AngleCurr = FMath::Atan2(Output[i].Y - Centroid.Y, Output[i].X - Centroid.X);
		float AngleNext = FMath::Atan2(Output[i + 1].Y - Centroid.Y, Output[i + 1].X - Centroid.X);
		TestTrue(FString::Printf(TEXT("Angle %d < angle %d (CCW order)"), i, i + 1), AngleCurr < AngleNext);
	}

	return true;
}

// Test 7: Already-sorted input → unchanged
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSortVerticesAlreadySortedTest, "ProceduralGeometry.GeometryUtils.Sort.AlreadySorted", DefaultTestFlags)

bool FSortVerticesAlreadySortedTest::RunTest(const FString& Parameters)
{
	// Square in CCW order (angles: -135, -45, 45, 135)
	TArray<FVector2D> Input = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };
	TArray<FVector2D> Output;

	FGeometryUtils::SortPlaneVerticesByAngle(Input, Output);

	TestEqual("Should have 4 vertices", Output.Num(), 4);
	for (int32 i = 0; i < 4; ++i)
	{
		TestEqual(FString::Printf(TEXT("Vertex %d X unchanged"), i), static_cast<float>(Output[i].X), static_cast<float>(Input[i].X), 0.01f);
		TestEqual(FString::Printf(TEXT("Vertex %d Y unchanged"), i), static_cast<float>(Output[i].Y), static_cast<float>(Input[i].Y), 0.01f);
	}

	return true;
}

// Test 8: Single and two points → returned as-is, returns false
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSortVerticesDegenerateTest, "ProceduralGeometry.GeometryUtils.Sort.Degenerate", DefaultTestFlags)

bool FSortVerticesDegenerateTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Output;

	// Single point
	{
		TArray<FVector2D> Single = { FVector2D(42, 42) };
		bool			  bResult = FGeometryUtils::SortPlaneVerticesByAngle(Single, Output);
		TestFalse("Single point should return false", bResult);
		TestEqual("Single point should be returned as-is", Output.Num(), 1);
	}

	// Two points
	{
		TArray<FVector2D> Two = { FVector2D(0, 0), FVector2D(100, 100) };
		bool			  bResult = FGeometryUtils::SortPlaneVerticesByAngle(Two, Output);
		TestFalse("Two points should return false", bResult);
		TestEqual("Two points should be returned as-is", Output.Num(), 2);
	}

	return true;
}

// ============================================================
// PointInPolygon
// ============================================================

// Test 9: Center of square → inside
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPointInPolygonCenterTest, "ProceduralGeometry.GeometryUtils.PointInPolygon.CenterInside", DefaultTestFlags)

bool FPointInPolygonCenterTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = MakeSquare100();

	TestTrue("Center should be inside", FGeometryUtils::PointInPolygon(Square, FVector2D(50, 50)));
	TestTrue("Off-center interior point should be inside", FGeometryUtils::PointInPolygon(Square, FVector2D(10, 90)));

	return true;
}

// Test 10: Far outside → not inside
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPointInPolygonOutsideTest, "ProceduralGeometry.GeometryUtils.PointInPolygon.FarOutside", DefaultTestFlags)

bool FPointInPolygonOutsideTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = MakeSquare100();

	TestFalse("Point far right should be outside", FGeometryUtils::PointInPolygon(Square, FVector2D(200, 50)));
	TestFalse("Point far above should be outside", FGeometryUtils::PointInPolygon(Square, FVector2D(50, 200)));
	TestFalse("Negative point should be outside", FGeometryUtils::PointInPolygon(Square, FVector2D(-50, -50)));

	return true;
}

// Test 11: Point on edge → document behavior (winding number may vary)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPointInPolygonEdgeTest, "ProceduralGeometry.GeometryUtils.PointInPolygon.OnEdge", DefaultTestFlags)

bool FPointInPolygonEdgeTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = MakeSquare100();

	// Winding number for exact-edge points is implementation-defined.
	// Just verify it doesn't crash and returns a bool consistently.
	bool bOnBottomEdge = FGeometryUtils::PointInPolygon(Square, FVector2D(50, 0));
	bool bOnLeftEdge = FGeometryUtils::PointInPolygon(Square, FVector2D(0, 50));

	// Verify consistency: same point returns same result
	TestEqual("On-edge result should be consistent", bOnBottomEdge, FGeometryUtils::PointInPolygon(Square, FVector2D(50, 0)));
	TestEqual("On-edge result should be consistent", bOnLeftEdge, FGeometryUtils::PointInPolygon(Square, FVector2D(0, 50)));

	// Degenerate polygon
	TArray<FVector2D> Line = { FVector2D(0, 0), FVector2D(100, 0) };
	TestFalse("< 3 vertices should return false", FGeometryUtils::PointInPolygon(Line, FVector2D(50, 0)));

	return true;
}

// ============================================================
// GetPolygonCentroid
// ============================================================

// Test 12: Regular square → center (50,50)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCentroidSquareTest, "ProceduralGeometry.GeometryUtils.Centroid.RegularSquare", DefaultTestFlags)

bool FCentroidSquareTest::RunTest(const FString& Parameters)
{
	FVector2D Centroid = FGeometryUtils::GetPolygonCentroid(MakeSquare100());

	TestEqual("Square centroid X", static_cast<float>(Centroid.X), 50.0f, 0.01f);
	TestEqual("Square centroid Y", static_cast<float>(Centroid.Y), 50.0f, 0.01f);

	return true;
}

// Test 13: L-shaped polygon → centroid differs from vertex average
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCentroidLShapeTest, "ProceduralGeometry.GeometryUtils.Centroid.LShape", DefaultTestFlags)

bool FCentroidLShapeTest::RunTest(const FString& Parameters)
{
	// L-shaped polygon
	TArray<FVector2D> LShape = {
		FVector2D(0, 0), FVector2D(200, 0), FVector2D(200, 100), FVector2D(100, 100), FVector2D(100, 200), FVector2D(0, 200)
	};

	FVector2D Centroid = FGeometryUtils::GetPolygonCentroid(LShape);

	// Vertex average = (100, 100)
	// Computed centroid ≈ (83.33, 83.33) — pulled toward the L's mass
	TestEqual("L-shape centroid X", static_cast<float>(Centroid.X), 83.33f, 1.0f);
	TestEqual("L-shape centroid Y", static_cast<float>(Centroid.Y), 83.33f, 1.0f);

	// Should differ from vertex average
	FVector2D VertexAvg(100, 100);
	float	  Difference = FVector2D::Distance(Centroid, VertexAvg);
	TestTrue("Centroid should differ from vertex average", Difference > 10.0f);

	return true;
}

// Test 14: Degenerate (< 3 vertices) → fallback to average
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCentroidDegenerateTest, "ProceduralGeometry.GeometryUtils.Centroid.Degenerate", DefaultTestFlags)

bool FCentroidDegenerateTest::RunTest(const FString& Parameters)
{
	// Empty
	{
		FVector2D C = FGeometryUtils::GetPolygonCentroid({});
		TestEqual("Empty → zero X", static_cast<float>(C.X), 0.0f, 0.01f);
		TestEqual("Empty → zero Y", static_cast<float>(C.Y), 0.0f, 0.01f);
	}

	// Single vertex
	{
		FVector2D C = FGeometryUtils::GetPolygonCentroid({ FVector2D(42, 77) });
		TestEqual("Single vertex X", static_cast<float>(C.X), 42.0f, 0.01f);
		TestEqual("Single vertex Y", static_cast<float>(C.Y), 77.0f, 0.01f);
	}

	// Two vertices → midpoint
	{
		FVector2D C = FGeometryUtils::GetPolygonCentroid({ FVector2D(0, 0), FVector2D(100, 100) });
		TestEqual("Two vertices → midpoint X", static_cast<float>(C.X), 50.0f, 0.01f);
		TestEqual("Two vertices → midpoint Y", static_cast<float>(C.Y), 50.0f, 0.01f);
	}

	return true;
}

// ============================================================
// PoissonDiskSampling
// ============================================================

// Test 15: All output points inside polygon
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoissonInsidePolygonTest, "ProceduralGeometry.GeometryUtils.Poisson.AllPointsInside", DefaultTestFlags)

bool FPoissonInsidePolygonTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000) };
	FRandomStream	  Stream(12345);
	TArray<FVector2D> Points;

	FGeometryUtils::PoissonDiskSampling(Square, 50.0f, 100, Stream, Points);

	TestTrue("Should produce points", Points.Num() > 0);

	for (int32 i = 0; i < Points.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Point %d should be inside polygon"), i), FGeometryUtils::PointInPolygon(Square, Points[i]));
	}

	return true;
}

// Test 16: All pairwise distances >= Radius
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoissonMinDistanceTest, "ProceduralGeometry.GeometryUtils.Poisson.MinDistance", DefaultTestFlags)

bool FPoissonMinDistanceTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000) };
	FRandomStream	  Stream(42);
	TArray<FVector2D> Points;
	const float		  Radius = 80.0f;

	FGeometryUtils::PoissonDiskSampling(Square, Radius, 50, Stream, Points);

	TestTrue("Should produce points", Points.Num() > 1);

	for (int32 i = 0; i < Points.Num(); ++i)
	{
		for (int32 j = i + 1; j < Points.Num(); ++j)
		{
			float Dist = FVector2D::Distance(Points[i], Points[j]);
			TestTrue(FString::Printf(TEXT("Points %d and %d distance (%.2f) >= Radius (%.2f)"), i, j, Dist, Radius), Dist >= Radius - 0.01f);
		}
	}

	return true;
}

// Test 17: Deterministic with seeded stream
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoissonDeterminismTest, "ProceduralGeometry.GeometryUtils.Poisson.Determinism", DefaultTestFlags)

bool FPoissonDeterminismTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 500), FVector2D(0, 500) };

	FRandomStream	  Stream1(99999);
	TArray<FVector2D> Points1;
	FGeometryUtils::PoissonDiskSampling(Square, 40.0f, 50, Stream1, Points1);

	FRandomStream	  Stream2(99999);
	TArray<FVector2D> Points2;
	FGeometryUtils::PoissonDiskSampling(Square, 40.0f, 50, Stream2, Points2);

	TestEqual("Same point count", Points1.Num(), Points2.Num());

	for (int32 i = 0; i < FMath::Min(Points1.Num(), Points2.Num()); ++i)
	{
		TestEqual(FString::Printf(TEXT("Point %d X match"), i), static_cast<float>(Points1[i].X), static_cast<float>(Points2[i].X), 0.01f);
		TestEqual(FString::Printf(TEXT("Point %d Y match"), i), static_cast<float>(Points1[i].Y), static_cast<float>(Points2[i].Y), 0.01f);
	}

	return true;
}

// Test 18: Empty polygon (< 3 verts) → empty result
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoissonEmptyPolygonTest, "ProceduralGeometry.GeometryUtils.Poisson.EmptyPolygon", DefaultTestFlags)

bool FPoissonEmptyPolygonTest::RunTest(const FString& Parameters)
{
	FRandomStream	  Stream(1);
	TArray<FVector2D> Points;

	// Empty polygon
	FGeometryUtils::PoissonDiskSampling({}, 50.0f, 100, Stream, Points);
	TestEqual("Empty polygon → 0 points", Points.Num(), 0);

	// Two-vertex polygon
	TArray<FVector2D> Line = { FVector2D(0, 0), FVector2D(100, 0) };
	FGeometryUtils::PoissonDiskSampling(Line, 50.0f, 100, Stream, Points);
	TestEqual("2-vertex polygon → 0 points", Points.Num(), 0);

	return true;
}

// ============================================================
// MaxInscribedCircle
// ============================================================

// Test 19: Unit square → center near half-side, radius near half-side
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMaxInscribedCircleSquareTest, "ProceduralGeometry.GeometryUtils.MaxInscribedCircle.Square", DefaultTestFlags)

bool FMaxInscribedCircleSquareTest::RunTest(const FString& Parameters)
{
	// Use 1000x1000 square for better precision (default Epsilon=10)
	TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000) };
	FVector2D		  Center;
	float			  Radius;

	bool bResult = FGeometryUtils::MaxInscribedCircle(Square, Center, Radius);

	TestTrue("Should find inscribed circle", bResult);
	TestEqual("Center X ≈ 500", static_cast<float>(Center.X), 500.0f, 15.0f);
	TestEqual("Center Y ≈ 500", static_cast<float>(Center.Y), 500.0f, 15.0f);
	TestEqual("Radius ≈ 500", Radius, 500.0f, 15.0f);

	return true;
}

// Test 20: Degenerate polygon → returns false
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxInscribedCircleDegenerateTest, "ProceduralGeometry.GeometryUtils.MaxInscribedCircle.Degenerate", DefaultTestFlags)

bool FMaxInscribedCircleDegenerateTest::RunTest(const FString& Parameters)
{
	FVector2D Center;
	float	  Radius;

	// Two vertices (not a polygon)
	TArray<FVector2D> Line = { FVector2D(0, 0), FVector2D(100, 0) };
	bool			  bResult = FGeometryUtils::MaxInscribedCircle(Line, Center, Radius);
	TestFalse("< 3 vertices should return false", bResult);

	// Empty
	bResult = FGeometryUtils::MaxInscribedCircle({}, Center, Radius);
	TestFalse("Empty should return false", bResult);

	return true;
}

// ============================================================
// DistanceToPolygonBoundary
// ============================================================

// Test 21: Center of square → distance = half side
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDistanceToBoundaryCenterTest, "ProceduralGeometry.GeometryUtils.DistanceToBoundary.Center", DefaultTestFlags)

bool FDistanceToBoundaryCenterTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = MakeSquare100();

	float Distance = FGeometryUtils::DistanceToPolygonBoundary(Square, FVector2D(50, 50));
	TestEqual("Center of 100x100 square → distance 50", Distance, 50.0f, 0.01f);

	// Off-center: (20, 50) → min distance is 20 (to left edge)
	float DistOffCenter = FGeometryUtils::DistanceToPolygonBoundary(Square, FVector2D(20, 50));
	TestEqual("(20,50) → distance 20 to left edge", DistOffCenter, 20.0f, 0.01f);

	return true;
}

// Test 22: Point on edge → distance ≈ 0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDistanceToBoundaryEdgeTest, "ProceduralGeometry.GeometryUtils.DistanceToBoundary.OnEdge", DefaultTestFlags)

bool FDistanceToBoundaryEdgeTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = MakeSquare100();

	// Point on bottom edge
	float DistBottom = FGeometryUtils::DistanceToPolygonBoundary(Square, FVector2D(50, 0));
	TestEqual("Point on bottom edge → distance ≈ 0", DistBottom, 0.0f, 0.01f);

	// Point on left edge
	float DistLeft = FGeometryUtils::DistanceToPolygonBoundary(Square, FVector2D(0, 50));
	TestEqual("Point on left edge → distance ≈ 0", DistLeft, 0.0f, 0.01f);

	// Point at corner vertex
	float DistCorner = FGeometryUtils::DistanceToPolygonBoundary(Square, FVector2D(0, 0));
	TestEqual("Point at corner → distance ≈ 0", DistCorner, 0.0f, 0.01f);

	// Degenerate
	float DistDegen = FGeometryUtils::DistanceToPolygonBoundary({}, FVector2D(50, 50));
	TestEqual("Degenerate polygon → distance 0", DistDegen, 0.0f, 0.01f);

	return true;
}

// ============================================================
// PoissonDiskSampling / MaxInscribedCircle robustness
// ============================================================

// Test 25: A kilometre-scale bounds asks for billions of acceleration cells to hold a handful of points; the sampler
// must bound that grid instead of overflowing the cell-count product.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPoissonHugeBoundsTest, "ProceduralGeometry.GeometryUtils.PoissonDiskSampling.HugeBoundsIsBounded", DefaultTestFlags)

bool FPoissonHugeBoundsTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Huge = { FVector2D(0, 0), FVector2D(500000, 0), FVector2D(500000, 500000), FVector2D(0, 500000) };
	constexpr float			Radius = 10.0f;
	constexpr int32			MaxPoints = 50;

	FRandomStream	  Stream(20260905);
	TArray<FVector2D> Points;
	FGeometryUtils::PoissonDiskSampling(Huge, Radius, MaxPoints, Stream, Points);

	TestTrue(TEXT("Huge bounds still produce points"), Points.Num() > 0);
	TestTrue(TEXT("Huge bounds never exceed MaxPoints"), Points.Num() <= MaxPoints);

	for (int32 i = 0; i < Points.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Point %d is inside the polygon"), i), FGeometryUtils::PointInPolygon(Huge, Points[i]));

		for (int32 j = i + 1; j < Points.Num(); ++j)
		{
			const float Dist = FVector2D::Distance(Points[i], Points[j]);
			TestTrue(FString::Printf(TEXT("Points %d and %d are at least Radius apart (%.3f)"), i, j, Dist), Dist >= Radius - 0.01f);
		}
	}

	return true;
}

// Test 26: For inputs that already fit the budget the bucket ring must still see every point within Radius, so the
// accepted sequence has to match a sampler that rejects against every accepted point.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPoissonMatchesLinearRejectionTest, "ProceduralGeometry.GeometryUtils.PoissonDiskSampling.UnchangedForNormalInputs", DefaultTestFlags)

bool FPoissonMatchesLinearRejectionTest::RunTest(const FString& Parameters)
{
	struct FCase
	{
		TArray<FVector2D> Polygon;
		float			  Radius;
		int32			  MaxPoints;
		int32			  Seed;
	};

	const TArray<FVector2D> Square1000 = { FVector2D(0, 0), FVector2D(1000, 0), FVector2D(1000, 1000), FVector2D(0, 1000) };
	const TArray<FVector2D> Square500 = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 500), FVector2D(0, 500) };
	const TArray<FVector2D> Triangle = { FVector2D(-400, -300), FVector2D(600, -300), FVector2D(100, 500) };

	// The last case is the one that exercises the coarsened grid: 1000/(10/sqrt2) projects 142x142 = 20164 buckets
	// against a 64*60 = 3840 budget, so the sampler coarsens twice and narrows its neighbour ring to 1. If that ring
	// were one bucket too narrow the accepted sequence would diverge from the linear-rejection reference here.
	const TArray<FCase> Cases = { { Square1000, 50.0f, 100, 12345 },
		{ Square1000, 80.0f, 50, 42 },
		{ Square500, 40.0f, 50, 99999 },
		{ Triangle, 35.0f, 120, 7 },
		{ Triangle, 12.0f, 400, 202609 },
		{ Square1000, 10.0f, 60, 55501 } };

	int32 TotalPoints = 0;

	for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
	{
		const FCase& Case = Cases[CaseIndex];

		FRandomStream	  LibraryStream(Case.Seed);
		TArray<FVector2D> LibraryPoints;
		FGeometryUtils::PoissonDiskSampling(Case.Polygon, Case.Radius, Case.MaxPoints, LibraryStream, LibraryPoints);

		FRandomStream	  ReferenceStream(Case.Seed);
		TArray<FVector2D> ReferencePoints;
		PoissonRef_SampleWithLinearRejection(Case.Polygon, Case.Radius, Case.MaxPoints, ReferenceStream, ReferencePoints);

		TotalPoints += LibraryPoints.Num();

		TestEqual(FString::Printf(TEXT("Case %d point count matches the linear-rejection reference"), CaseIndex),
			LibraryPoints.Num(),
			ReferencePoints.Num());

		for (int32 i = 0; i < FMath::Min(LibraryPoints.Num(), ReferencePoints.Num()); ++i)
		{
			TestTrue(FString::Printf(TEXT("Case %d point %d matches the reference exactly"), CaseIndex, i), LibraryPoints[i] == ReferencePoints[i]);
		}
	}

	TestTrue(TEXT("The comparison actually sampled points"), TotalPoints > 0);

	return true;
}

// Test 27: A sliver polygon far from the origin has a cell size below the float spacing at its coordinates; the grid
// must still be walked to completion instead of spinning on an accumulator that cannot advance.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaxInscribedCircleFarFromOriginTest, "ProceduralGeometry.GeometryUtils.MaxInscribedCircle.FarFromOrigin", DefaultTestFlags)

bool FMaxInscribedCircleFarFromOriginTest::RunTest(const FString& Parameters)
{
	// The thin axis fixes the cell size at 0.0025 while the float spacing at x = 2e6 is 0.125, which is the whole
	// point of the case; the long axis only has to be long enough to make the grid worth walking, so it stays at
	// 100 rather than the thousands that would reserve tens of megabytes of seed cells to prove the same thing.
	constexpr double BaseX = 2000000.0;
	constexpr double Width = 0.01;
	constexpr double Height = 100.0;

	const TArray<FVector2D> Sliver = {
		FVector2D(BaseX, 0.0), FVector2D(BaseX + Width, 0.0), FVector2D(BaseX + Width, Height), FVector2D(BaseX, Height)
	};

	FVector2D Center = FVector2D::ZeroVector;
	float	  Radius = -1.0f;

	const bool bResult = FGeometryUtils::MaxInscribedCircle(Sliver, Center, Radius);

	TestTrue(TEXT("Sliver polygon far from the origin yields a circle"), bResult);
	TestTrue(TEXT("Radius is positive"), Radius > 0.0f);
	TestTrue(FString::Printf(TEXT("Radius %.6f cannot exceed half the thin extent"), Radius), Radius <= 0.5f * static_cast<float>(Width) + 1e-4f);
	TestTrue(TEXT("Center X lies inside the sliver"), Center.X >= BaseX && Center.X <= BaseX + Width);
	TestTrue(TEXT("Center Y lies inside the sliver"), Center.Y >= 0.0 && Center.Y <= Height);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
