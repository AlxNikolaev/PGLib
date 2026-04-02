#pragma once

#include "CoreMinimal.h"
#include "Generators/LayoutGenerator.h"
#include "CellularAutomataGenerator2D.generated.h"

UCLASS()
class PROCEDURALGEOMETRY_API UCellularAutomataGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	float FillProbability;
	int32 Iterations;
	TArray<int32> BirthRule;
	TArray<int32> SurvivalRule;
	int32 MinRegionSize;
	bool bKeepCenterRegion;

public:
	UCellularAutomataGenerator2D();

	// Covariant base class overrides
	virtual UCellularAutomataGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UCellularAutomataGenerator2D* SetSeed(const FString& InSeed) override;
	virtual UCellularAutomataGenerator2D* SetGridSize(int32 InSize) override;
	virtual UCellularAutomataGenerator2D* SetCenter(const FVector2D& InCenter) override;

	// Generator-specific config
	UCellularAutomataGenerator2D* SetFillProbability(float InProbability);
	UCellularAutomataGenerator2D* SetIterations(int32 InIterations);
	UCellularAutomataGenerator2D* SetBirthRule(const TArray<int32>& InRule);
	UCellularAutomataGenerator2D* SetSurvivalRule(const TArray<int32>& InRule);
	UCellularAutomataGenerator2D* SetMinRegionSize(int32 InSize);
	UCellularAutomataGenerator2D* SetKeepCenterRegion(bool bKeep);

	// Generation
	virtual FLayoutDiagram2D Generate() override;

private:
	static uint16 RuleToBitmask(const TArray<int32>& Rule);
	int32 CountWallNeighbors(const TArray<bool>& Grid, int32 X, int32 Y, int32 GridWidth, int32 GridHeight) const;

	// Region merging pipeline
	FLayoutDiagram2D BuildDiagramFromRegions(
		const TArray<bool>& Grid,
		const TArray<int32>& RegionIds,
		const TArray<TArray<FIntPoint>>& Regions,
		int32 CenterRegionId,
		int32 GridWidth,
		int32 GridHeight);
	TArray<FVector2D> TraceBoundaryPolygon(
		const TArray<FIntPoint>& Region,
		const TArray<int32>& RegionIds,
		int32 RegionId,
		int32 GridWidth,
		int32 GridHeight,
		float CellSize) const;
	static float ComputePolygonArea(const TArray<FIntPoint>& Loop);
	TArray<FVector2D> SimplifyAndConvert(const TArray<FIntPoint>& Loop, float CellSize) const;
};
