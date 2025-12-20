#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

class FVoronoiTestBase : public FAutomationTestBase
{
public:
	FVoronoiTestBase(const FString& InName, const bool bInComplexTask) : FAutomationTestBase(InName, bInComplexTask) {}

	static bool	 AreVerticesClockwise(const TArray<FVector2D>& Vertices);
	static bool	 IsConvexPolygon(const TArray<FVector2D>& Vertices);
	static float CalculatePolygonArea(const TArray<FVector2D>& Vertices);
};

#endif