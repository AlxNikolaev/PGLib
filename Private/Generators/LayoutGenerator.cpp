#include "Generators/LayoutGenerator.h"
#include "DrawDebugHelpers.h"

ULayoutGenerator::ULayoutGenerator()
{
	Bounds = FBox2D();
	GridSize = 100;
	CenterPoint = FVector2D::ZeroVector;
}

ULayoutGenerator* ULayoutGenerator::SetBounds(const FBox2D& InBounds)
{
	Bounds = InBounds;
	if (CenterPoint == FVector2D::ZeroVector)
	{
		CenterPoint = Bounds.GetCenter();
	}
	return this;
}

ULayoutGenerator* ULayoutGenerator::SetCenter(const FVector2D& InCenter)
{
	CenterPoint = InCenter;
	return this;
}

ULayoutGenerator* ULayoutGenerator::SetSeed(const FString& InSeed)
{
	Seed = InSeed;
	InitializeRandomStream();
	return this;
}

ULayoutGenerator* ULayoutGenerator::SetGridSize(int32 InSize)
{
	GridSize = FMath::Max(10, InSize);
	return this;
}

void ULayoutGenerator::InitializeRandomStream()
{
	if (Seed.IsEmpty())
	{
		Seed = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
	RandomStream = FRandomStream(GetTypeHash(Seed));
}

FVector2D ULayoutGenerator::ClampToBounds(const FVector2D& Point) const
{
	return FVector2D(FMath::Clamp(Point.X, Bounds.Min.X, Bounds.Max.X), FMath::Clamp(Point.Y, Bounds.Min.Y, Bounds.Max.Y));
}