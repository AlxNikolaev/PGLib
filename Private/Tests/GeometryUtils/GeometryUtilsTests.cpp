#include "GeometryUtils/GeometryFunctionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;

	/** Unit square [0,0]-[100,100] in CCW order */
	TArray<FVector2D> MakeSquare100()
	{
		return { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };
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

#endif // WITH_DEV_AUTOMATION_TESTS
