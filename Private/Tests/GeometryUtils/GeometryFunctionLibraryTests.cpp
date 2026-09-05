#include "GeometryUtils/GeometryFunctionLibrary.h"
#include "../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named rather than anonymous: adaptive unity builds merge translation units, and short helper names collide.
namespace GeomFnLibSatTestUtils
{
	/** Axis-aligned rectangle in the same counter-clockwise winding RotatedRectCorners emits. */
	static TArray<FVector2D> MakeRect(const double MinX, const double MinY, const double MaxX, const double MaxY)
	{
		return { FVector2D(MinX, MinY), FVector2D(MaxX, MinY), FVector2D(MaxX, MaxY), FVector2D(MinX, MaxY) };
	}

	/** Shoelace signed area; positive for counter-clockwise winding. */
	static double SignedArea(const TArray<FVector2D>& Polygon)
	{
		double		Sum = 0.0;
		const int32 Num = Polygon.Num();
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector2D& V1 = Polygon[i];
			const FVector2D& V2 = Polygon[(i + 1) % Num];
			Sum += V1.X * V2.Y - V2.X * V1.Y;
		}
		return Sum * 0.5;
	}

	/** Regular hexagon standing in for a Voronoi cell: six distinct edge normals, none axis-aligned in pairs. */
	static TArray<FVector2D> MakeHexagon(const FVector2D& Center, const double Radius)
	{
		TArray<FVector2D> Hexagon;
		Hexagon.Reserve(6);
		for (int32 i = 0; i < 6; ++i)
		{
			const double Angle = FMath::DegreesToRadians(60.0 * i);
			Hexagon.Add(Center + FVector2D(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle)));
		}
		return Hexagon;
	}
} // namespace GeomFnLibSatTestUtils

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaikinBasicDoublingTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.BasicDoubling", DefaultTestFlags)

bool FChaikinBasicDoublingTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };

	TArray<FVector2D> OneIter = Square;
	FGeometryUtils::ChaikinSubdivide(OneIter, 1);
	TestEqual("1 iteration on 4-vertex polygon should produce 8 vertices", OneIter.Num(), 8);

	TArray<FVector2D> TwoIter = Square;
	FGeometryUtils::ChaikinSubdivide(TwoIter, 2);
	TestEqual("2 iterations on 4-vertex polygon should produce 16 vertices", TwoIter.Num(), 16);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChaikinTriangleConvexHullTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.TriangleConvexHull", DefaultTestFlags)

bool FChaikinTriangleConvexHullTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Triangle = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(50, 100) };

	FGeometryUtils::ChaikinSubdivide(Triangle, 1);
	TestEqual("1 iteration on triangle should produce 6 vertices", Triangle.Num(), 6);

	// Chaikin never expands beyond the original convex hull.
	for (int32 i = 0; i < Triangle.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Vertex %d X >= 0"), i), Triangle[i].X >= -UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("Vertex %d X <= 100"), i), Triangle[i].X <= 100.0f + UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("Vertex %d Y >= 0"), i), Triangle[i].Y >= -UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("Vertex %d Y <= 100"), i), Triangle[i].Y <= 100.0f + UE_KINDA_SMALL_NUMBER);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaikinDegenerateInputsTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.DegenerateInputs", DefaultTestFlags)

bool FChaikinDegenerateInputsTest::RunTest(const FString& Parameters)
{
	{
		TArray<FVector2D> Empty;
		FGeometryUtils::ChaikinSubdivide(Empty, 2);
		TestEqual("Empty array should remain empty", Empty.Num(), 0);
	}

	{
		TArray<FVector2D> Single = { FVector2D(42, 42) };
		FGeometryUtils::ChaikinSubdivide(Single, 2);
		TestEqual("1 vertex should remain 1 vertex", Single.Num(), 1);
	}

	{
		TArray<FVector2D> Two = { FVector2D(0, 0), FVector2D(100, 100) };
		FGeometryUtils::ChaikinSubdivide(Two, 2);
		TestEqual("2 vertices should remain 2 vertices", Two.Num(), 2);
	}

	{
		TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };
		TArray<FVector2D> Original = Square;
		FGeometryUtils::ChaikinSubdivide(Square, 0);
		TestEqual("0 iterations should not change vertex count", Square.Num(), Original.Num());
		for (int32 i = 0; i < Square.Num(); ++i)
		{
			TestEqual(FString::Printf(TEXT("0 iterations vertex %d X unchanged"), i),
				static_cast<float>(Square[i].X),
				static_cast<float>(Original[i].X),
				0.01f);
			TestEqual(FString::Printf(TEXT("0 iterations vertex %d Y unchanged"), i),
				static_cast<float>(Square[i].Y),
				static_cast<float>(Original[i].Y),
				0.01f);
		}
	}

	{
		TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };
		TArray<FVector2D> Original = Square;
		FGeometryUtils::ChaikinSubdivide(Square, -1);
		TestEqual("Negative iterations should not change vertex count", Square.Num(), Original.Num());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaikinPreservesClosureTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.PreservesClosure", DefaultTestFlags)

bool FChaikinPreservesClosureTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Pentagon = { FVector2D(50, 0), FVector2D(100, 35), FVector2D(80, 100), FVector2D(20, 100), FVector2D(0, 35) };

	FGeometryUtils::ChaikinSubdivide(Pentagon, 2);

	TestEqual("2 iterations on pentagon should produce 20 vertices", Pentagon.Num(), 20);

	// The wrap-around edge gets its own vertex pair, so the polygon stays implicitly closed without
	// duplicating the first vertex.
	const FVector2D& First = Pentagon[0];
	const FVector2D& Last = Pentagon.Last();

	const float DistFirstLast = FVector2D::Distance(First, Last);
	TestTrue("First and last vertices should be distinct (implicitly closed polygon)", DistFirstLast > UE_KINDA_SMALL_NUMBER);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChaikinSmoothsRightAnglesTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.SmoothsRightAngles", DefaultTestFlags)

bool FChaikinSmoothsRightAnglesTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> LShape = {
		FVector2D(0, 0), FVector2D(200, 0), FVector2D(200, 100), FVector2D(100, 100), FVector2D(100, 200), FVector2D(0, 200)
	};

	TArray<FVector2D> Original = LShape;

	FGeometryUtils::ChaikinSubdivide(LShape, 2);

	for (int32 i = 0; i < LShape.Num(); ++i)
	{
		for (int32 j = 0; j < Original.Num(); ++j)
		{
			const float Dist = FVector2D::Distance(LShape[i], Original[j]);
			TestTrue(FString::Printf(TEXT("Output vertex %d should not coincide with original vertex %d (dist=%.4f)"), i, j, Dist),
				Dist > UE_KINDA_SMALL_NUMBER);
		}
	}

	return true;
}

// The angle convention and corner order here feed the SAT gate that decides which Voronoi cells a rotated
// room footprint claims, so a regression silently moves every room.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRotatedRectCornersAnglesTest, "ProceduralGeometry.GeometryUtils.RotatedRectCorners.CornerPositions", DefaultTestFlags)

bool FRotatedRectCornersAnglesTest::RunTest(const FString& Parameters)
{
	// 200 x 100 footprint => half-extents 100 x 50.
	const FVector2D Footprint(200.0, 100.0);
	const float		Tolerance = 0.01f;

	auto CheckCorners = [this, Tolerance](const TCHAR* Label, const TArray<FVector2D>& Corners, const FVector2D(&Expected)[4]) {
		for (int32 i = 0; i < 4; ++i)
		{
			TestEqual(
				FString::Printf(TEXT("%s corner %d X"), Label, i), static_cast<float>(Corners[i].X), static_cast<float>(Expected[i].X), Tolerance);
			TestEqual(
				FString::Printf(TEXT("%s corner %d Y"), Label, i), static_cast<float>(Corners[i].Y), static_cast<float>(Expected[i].Y), Tolerance);
		}
	};

	// The output array is Reset, not appended to.
	TArray<FVector2D> Corners = { FVector2D(9, 9), FVector2D(9, 9), FVector2D(9, 9), FVector2D(9, 9), FVector2D(9, 9) };

	// 0 degrees: local order (-HX,-HY), (HX,-HY), (HX,HY), (-HX,HY) translated by Center.
	FGeometryUtils::RotatedRectCorners(FVector2D::ZeroVector, 0.0f, Footprint, Corners);
	TestEqual("Pre-populated output array is reset to exactly 4 corners", Corners.Num(), 4);
	{
		const FVector2D Expected[4] = { FVector2D(-100, -50), FVector2D(100, -50), FVector2D(100, 50), FVector2D(-100, 50) };
		CheckCorners(TEXT("0deg"), Corners, Expected);
	}

	// 90 degrees counter-clockwise: (x, y) -> (-y, x).
	FGeometryUtils::RotatedRectCorners(FVector2D::ZeroVector, 90.0f, Footprint, Corners);
	{
		const FVector2D Expected[4] = { FVector2D(50, -100), FVector2D(50, 100), FVector2D(-50, 100), FVector2D(-50, -100) };
		CheckCorners(TEXT("90deg"), Corners, Expected);
	}

	// 45 degrees: 100 * cos45 = 70.710678, 50 * cos45 = 35.355339.
	FGeometryUtils::RotatedRectCorners(FVector2D::ZeroVector, 45.0f, Footprint, Corners);
	{
		const FVector2D Expected[4] = {
			FVector2D(-35.355339, -106.066017), FVector2D(106.066017, 35.355339), FVector2D(35.355339, 106.066017), FVector2D(-106.066017, -35.355339)
		};
		CheckCorners(TEXT("45deg"), Corners, Expected);
	}

	// Center is a pure translation of the rotated local corners.
	FGeometryUtils::RotatedRectCorners(FVector2D(1000.0, -250.0), 0.0f, Footprint, Corners);
	{
		const FVector2D Expected[4] = { FVector2D(900, -300), FVector2D(1100, -300), FVector2D(1100, -200), FVector2D(900, -200) };
		CheckCorners(TEXT("Offset center"), Corners, Expected);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRotatedRectCornersWindingTest, "ProceduralGeometry.GeometryUtils.RotatedRectCorners.WindingAndArea", DefaultTestFlags)

bool FRotatedRectCornersWindingTest::RunTest(const FString& Parameters)
{
	const FVector2D Footprint(200.0, 100.0);
	const double	ExpectedArea = 200.0 * 100.0;

	const float Angles[3] = { 0.0f, 90.0f, 45.0f };
	for (const float Angle : Angles)
	{
		TArray<FVector2D> Corners;
		FGeometryUtils::RotatedRectCorners(FVector2D(37.0, -11.0), Angle, Footprint, Corners);

		const double Area = GeomFnLibSatTestUtils::SignedArea(Corners);

		TestTrue(FString::Printf(TEXT("Winding stays counter-clockwise at %.0f degrees (signed area %.2f)"), Angle, Area), Area > 0.0);
		TestEqual(
			FString::Printf(TEXT("Area is preserved at %.0f degrees"), Angle), static_cast<float>(Area), static_cast<float>(ExpectedArea), 1.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConvexPolygonsOverlapAxisAlignedTest, "ProceduralGeometry.GeometryUtils.ConvexPolygonsOverlap.AxisAligned", DefaultTestFlags)

bool FConvexPolygonsOverlapAxisAlignedTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Base = GeomFnLibSatTestUtils::MakeRect(0, 0, 100, 100);

	// The gap test is strict, so a shared edge counts as overlap: adjacent rooms must contest the shared cell.
	TestTrue(
		"Edge-to-edge touching rectangles overlap", FGeometryUtils::ConvexPolygonsOverlap(Base, GeomFnLibSatTestUtils::MakeRect(100, 0, 200, 100)));

	TestTrue("Corner-to-corner touching rectangles overlap",
		FGeometryUtils::ConvexPolygonsOverlap(Base, GeomFnLibSatTestUtils::MakeRect(100, 100, 200, 200)));

	TestFalse("Rectangles separated by one unit do not overlap",
		FGeometryUtils::ConvexPolygonsOverlap(Base, GeomFnLibSatTestUtils::MakeRect(101, 0, 201, 100)));

	TestTrue(
		"Rectangles overlapping by one unit overlap", FGeometryUtils::ConvexPolygonsOverlap(Base, GeomFnLibSatTestUtils::MakeRect(99, 0, 199, 100)));

	TestTrue("Contained rectangle overlaps", FGeometryUtils::ConvexPolygonsOverlap(Base, GeomFnLibSatTestUtils::MakeRect(25, 25, 75, 75)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConvexPolygonsOverlapRotatedTest, "ProceduralGeometry.GeometryUtils.ConvexPolygonsOverlap.RotatedAABBFalsePositive", DefaultTestFlags)

bool FConvexPolygonsOverlapRotatedTest::RunTest(const FString& Parameters)
{
	// A diamond with vertices (100,0), (0,100), (-100,0), (0,-100).
	TArray<FVector2D> Diamond;
	FGeometryUtils::RotatedRectCorners(FVector2D::ZeroVector, 45.0f, FVector2D(141.421356, 141.421356), Diamond);

	// Bounds overlap in [60,100]^2, but the nearest corner (60,60) sits outside the x+y=100 edge.
	const TArray<FVector2D> BoundsOverlapOnly = GeomFnLibSatTestUtils::MakeRect(60, 60, 160, 160);
	TestFalse("Rotated rect vs AABB-overlapping rect is separated", FGeometryUtils::ConvexPolygonsOverlap(Diamond, BoundsOverlapOnly));
	TestFalse("Separation result is symmetric", FGeometryUtils::ConvexPolygonsOverlap(BoundsOverlapOnly, Diamond));

	// The same pair moved in until the corner (20,20) is inside the diamond.
	const TArray<FVector2D> GenuineOverlap = GeomFnLibSatTestUtils::MakeRect(20, 20, 120, 120);
	TestTrue("Rotated rect vs genuinely overlapping rect overlaps", FGeometryUtils::ConvexPolygonsOverlap(Diamond, GenuineOverlap));
	TestTrue("Overlap result is symmetric", FGeometryUtils::ConvexPolygonsOverlap(GenuineOverlap, Diamond));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConvexPolygonsOverlapHexagonTest, "ProceduralGeometry.GeometryUtils.ConvexPolygonsOverlap.RectVsHexagon", DefaultTestFlags)

bool FConvexPolygonsOverlapHexagonTest::RunTest(const FString& Parameters)
{
	// Vertices (100,0), (50,86.6), (-50,86.6), (-100,0), (-50,-86.6), (50,-86.6).
	const TArray<FVector2D> Hexagon = GeomFnLibSatTestUtils::MakeHexagon(FVector2D::ZeroVector, 100.0);

	// (80,0) is inside: the upper-right edge is at y = 34.64 for x = 80.
	TestTrue(
		"Rect reaching into the hexagon overlaps", FGeometryUtils::ConvexPolygonsOverlap(Hexagon, GeomFnLibSatTestUtils::MakeRect(80, -20, 180, 80)));

	// Bounds overlap, but only the upper-right edge's own axis separates them: a dropped or mis-wrapped
	// polygon edge would report an overlap here.
	const TArray<FVector2D> BeyondEdge = GeomFnLibSatTestUtils::MakeRect(80, 40, 180, 140);
	TestFalse("Rect beyond a single hexagon edge is separated", FGeometryUtils::ConvexPolygonsOverlap(Hexagon, BeyondEdge));
	TestFalse("Hexagon separation is symmetric", FGeometryUtils::ConvexPolygonsOverlap(BeyondEdge, Hexagon));

	TestFalse(
		"Rect clear of the hexagon is separated", FGeometryUtils::ConvexPolygonsOverlap(Hexagon, GeomFnLibSatTestUtils::MakeRect(150, -50, 350, 50)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConvexPolygonsOverlapDegenerateTest, "ProceduralGeometry.GeometryUtils.ConvexPolygonsOverlap.DegenerateInputs", DefaultTestFlags)

bool FConvexPolygonsOverlapDegenerateTest::RunTest(const FString& Parameters)
{
	const TArray<FVector2D> Rect = GeomFnLibSatTestUtils::MakeRect(0, 0, 100, 100);

	// Fewer than three vertices is rejected outright, before any axis is built.
	TestFalse("Empty polygon never overlaps", FGeometryUtils::ConvexPolygonsOverlap(TArray<FVector2D>(), Rect));
	TestFalse("Single point never overlaps", FGeometryUtils::ConvexPolygonsOverlap({ FVector2D(50, 50) }, Rect));
	TestFalse("Two-vertex segment never overlaps", FGeometryUtils::ConvexPolygonsOverlap({ FVector2D(10, 10), FVector2D(90, 90) }, Rect));
	TestFalse("Rejection applies to the second polygon too", FGeometryUtils::ConvexPolygonsOverlap(Rect, { FVector2D(50, 50) }));

	// A zero-area triangle clears the vertex-count gate but contributes no axes, so the verdict rests
	// entirely on the rectangle's own four axes.
	const TArray<FVector2D> PointTriangleInside = { FVector2D(50, 50), FVector2D(50, 50), FVector2D(50, 50) };
	TestTrue("Zero-area triangle inside the rect overlaps", FGeometryUtils::ConvexPolygonsOverlap(Rect, PointTriangleInside));

	const TArray<FVector2D> PointTriangleOutside = { FVector2D(500, 500), FVector2D(500, 500), FVector2D(500, 500) };
	TestFalse("Zero-area triangle outside the rect is separated", FGeometryUtils::ConvexPolygonsOverlap(Rect, PointTriangleOutside));

	// A rectangle collapsed to a segment keeps two usable axes and must still be separable.
	const TArray<FVector2D> FlatRect = GeomFnLibSatTestUtils::MakeRect(200, 0, 300, 0);
	TestFalse("Zero-height rect clear of the base rect is separated", FGeometryUtils::ConvexPolygonsOverlap(Rect, FlatRect));
	TestTrue("Zero-height rect crossing the base rect overlaps",
		FGeometryUtils::ConvexPolygonsOverlap(Rect, GeomFnLibSatTestUtils::MakeRect(-50, 50, 150, 50)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
