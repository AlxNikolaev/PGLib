#include "GeometryFunctionLibrary.h"

bool FGeometryUtils::SortPlaneVerticesByAngle(const TArray<FVector2D>& InVertices, TArray<FVector2D>& OutSortedVertices)
{
	if (InVertices.Num() < 3)
	{
		OutSortedVertices = InVertices;
		return false;
	}

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : InVertices)
	{
		Centroid += Vertex;
	}
	Centroid /= InVertices.Num();

	OutSortedVertices = InVertices;

	OutSortedVertices.Sort([&Centroid](const FVector2D& A, const FVector2D& B) {
		const float AngleA = FMath::Atan2(A.Y - Centroid.Y, A.X - Centroid.X);
		const float AngleB = FMath::Atan2(B.Y - Centroid.Y, B.X - Centroid.X);
		return AngleA < AngleB;
	});

	return true;
}

bool FGeometryUtils::ComputeBisector2D(const FVector2D& P1, const FVector2D& P2, const FVector2D& OutStart, const FVector2D& OutEnd)
{
	if (P1.Equals(P2, UE_KINDA_SMALL_NUMBER))
		return false;

	FVector2D Mid = (P1 + P2) / 2;

	FVector2D Direction = (P2 - P1).GetRotated(90.f);
	Direction.Normalize();

	return true;
}

bool FGeometryUtils::ClipPolygonByHalfPlane(TArray<FVector2D>& OutPolygon, const FVector2D& PlanePoint, const FVector2D& PlaneNormal)
{
	if (OutPolygon.Num() == 0)
	{
		return false;
	}

	TArray<FVector2D> Result;
	FVector2D		  Prev = OutPolygon.Last();
	double			  PrevSide = FVector2D::DotProduct(Prev - PlanePoint, PlaneNormal);

	for (const FVector2D& Curr : OutPolygon)
	{
		double CurrSide = FVector2D::DotProduct(Curr - PlanePoint, PlaneNormal);

		if (PrevSide <= 0.0 && CurrSide <= 0.0)
		{
			Result.Add(Curr);
		}
		else if (PrevSide <= 0.0 && CurrSide > 0.0)
		{
			float Alpha = PrevSide / (PrevSide - CurrSide);
			Result.Add(FMath::Lerp(Prev, Curr, Alpha));
		}
		else if (PrevSide > 0.0 && CurrSide <= 0.0)
		{
			float Alpha = PrevSide / (PrevSide - CurrSide);
			Result.Add(FMath::Lerp(Prev, Curr, Alpha));
			Result.Add(Curr);
		}

		Prev = Curr;
		PrevSide = CurrSide;
	}

	OutPolygon = Result;
	return OutPolygon.Num() >= 3;
}
