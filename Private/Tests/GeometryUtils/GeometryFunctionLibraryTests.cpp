#include "GeometryUtils/GeometryFunctionLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DefaultTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
		| EAutomationTestFlags::ProductFilter | EAutomationTestFlags::MediumPriority;
} // namespace

// ============================================================
// ChaikinSubdivide
// ============================================================

// Test 1: Basic subdivision doubles vertex count
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaikinBasicDoublingTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.BasicDoubling", DefaultTestFlags)

bool FChaikinBasicDoublingTest::RunTest(const FString& Parameters)
{
	// Square polygon — 4 vertices
	TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };

	// After 1 iteration: 4 edges * 2 points = 8 vertices
	TArray<FVector2D> OneIter = Square;
	FGeometryUtils::ChaikinSubdivide(OneIter, 1);
	TestEqual("1 iteration on 4-vertex polygon should produce 8 vertices", OneIter.Num(), 8);

	// After 2 iterations: 8 edges * 2 points = 16 vertices
	TArray<FVector2D> TwoIter = Square;
	FGeometryUtils::ChaikinSubdivide(TwoIter, 2);
	TestEqual("2 iterations on 4-vertex polygon should produce 16 vertices", TwoIter.Num(), 16);

	return true;
}

// Test 2: Triangle subdivision — output within convex hull
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChaikinTriangleConvexHullTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.TriangleConvexHull", DefaultTestFlags)

bool FChaikinTriangleConvexHullTest::RunTest(const FString& Parameters)
{
	TArray<FVector2D> Triangle = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(50, 100) };

	// After 1 iteration: 3 edges * 2 points = 6 vertices
	FGeometryUtils::ChaikinSubdivide(Triangle, 1);
	TestEqual("1 iteration on triangle should produce 6 vertices", Triangle.Num(), 6);

	// Chaikin never expands beyond the original convex hull.
	// For a triangle with vertices (0,0), (100,0), (50,100):
	// All output vertices must have X in [0, 100] and Y in [0, 100]
	for (int32 i = 0; i < Triangle.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("Vertex %d X >= 0"), i), Triangle[i].X >= -UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("Vertex %d X <= 100"), i), Triangle[i].X <= 100.0f + UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("Vertex %d Y >= 0"), i), Triangle[i].Y >= -UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("Vertex %d Y <= 100"), i), Triangle[i].Y <= 100.0f + UE_KINDA_SMALL_NUMBER);
	}

	return true;
}

// Test 3: Empty and degenerate inputs — no-ops
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaikinDegenerateInputsTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.DegenerateInputs", DefaultTestFlags)

bool FChaikinDegenerateInputsTest::RunTest(const FString& Parameters)
{
	// Empty array
	{
		TArray<FVector2D> Empty;
		FGeometryUtils::ChaikinSubdivide(Empty, 2);
		TestEqual("Empty array should remain empty", Empty.Num(), 0);
	}

	// 1 vertex
	{
		TArray<FVector2D> Single = { FVector2D(42, 42) };
		FGeometryUtils::ChaikinSubdivide(Single, 2);
		TestEqual("1 vertex should remain 1 vertex", Single.Num(), 1);
	}

	// 2 vertices
	{
		TArray<FVector2D> Two = { FVector2D(0, 0), FVector2D(100, 100) };
		FGeometryUtils::ChaikinSubdivide(Two, 2);
		TestEqual("2 vertices should remain 2 vertices", Two.Num(), 2);
	}

	// 0 iterations
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

	// Negative iterations
	{
		TArray<FVector2D> Square = { FVector2D(0, 0), FVector2D(100, 0), FVector2D(100, 100), FVector2D(0, 100) };
		TArray<FVector2D> Original = Square;
		FGeometryUtils::ChaikinSubdivide(Square, -1);
		TestEqual("Negative iterations should not change vertex count", Square.Num(), Original.Num());
	}

	return true;
}

// Test 4: Subdivision preserves closure
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaikinPreservesClosureTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.PreservesClosure", DefaultTestFlags)

bool FChaikinPreservesClosureTest::RunTest(const FString& Parameters)
{
	// Pentagon
	TArray<FVector2D> Pentagon = { FVector2D(50, 0), FVector2D(100, 35), FVector2D(80, 100), FVector2D(20, 100), FVector2D(0, 35) };

	FGeometryUtils::ChaikinSubdivide(Pentagon, 2);

	// 5 edges -> 10 after iter 1 -> 20 after iter 2
	TestEqual("2 iterations on pentagon should produce 20 vertices", Pentagon.Num(), 20);

	// Verify the polygon is still implicitly closed: the last vertex should NOT equal the first
	// (Chaikin produces a new vertex pair per edge, including the wrap-around edge from last to first,
	// so the polygon remains implicitly closed without duplicating the first vertex)
	const FVector2D& First = Pentagon[0];
	const FVector2D& Last = Pentagon.Last();

	// The last and first vertices should be distinct (they come from different edges)
	const float DistFirstLast = FVector2D::Distance(First, Last);
	TestTrue("First and last vertices should be distinct (implicitly closed polygon)", DistFirstLast > UE_KINDA_SMALL_NUMBER);

	return true;
}

// Test 5: Smoothing effect on right-angle corners
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChaikinSmoothsRightAnglesTest, "ProceduralGeometry.GeometryUtils.ChaikinSubdivide.SmoothsRightAngles", DefaultTestFlags)

bool FChaikinSmoothsRightAnglesTest::RunTest(const FString& Parameters)
{
	// L-shaped polygon with 90-degree corners
	TArray<FVector2D> LShape = {
		FVector2D(0, 0), FVector2D(200, 0), FVector2D(200, 100), FVector2D(100, 100), FVector2D(100, 200), FVector2D(0, 200)
	};

	TArray<FVector2D> Original = LShape;

	FGeometryUtils::ChaikinSubdivide(LShape, 2);

	// After 2 iterations, NO output vertex should coincide with any original vertex
	// This confirms all corners have been cut
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

#endif // WITH_DEV_AUTOMATION_TESTS
