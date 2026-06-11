#pragma once

#include "CoreMinimal.h"
#include "LayoutGenerator.generated.h"

USTRUCT()
struct PROCEDURALGEOMETRY_API FLayoutCell2D
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVector2D> Vertices;

	UPROPERTY()
	TArray<int32> Neighbors;

	UPROPERTY()
	FVector2D Center = FVector2D::ZeroVector;

	UPROPERTY()
	int32 CellIndex = -1;

	UPROPERTY()
	bool bIsExterior = false;

	FLayoutCell2D() = default;
};

USTRUCT()
struct PROCEDURALGEOMETRY_API FLayoutDiagram2D
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FLayoutCell2D> Cells;

	UPROPERTY()
	FBox2D Bounds = FBox2D(ForceInit);

	UPROPERTY()
	FVector2D CenterPoint = FVector2D::ZeroVector;

	UPROPERTY()
	int32 CenterCellIndex = INDEX_NONE;

	UPROPERTY()
	FString Seed;
};

UCLASS(Abstract)
class PROCEDURALGEOMETRY_API ULayoutGenerator : public UObject
{
	GENERATED_BODY()

protected:
	UPROPERTY()
	FBox2D Bounds;

	UPROPERTY()
	FVector2D CenterPoint;

	UPROPERTY()
	FString Seed;

	UPROPERTY()
	int32 GridSize;

	FRandomStream RandomStream;

private:
	bool bCenterSet = false;

public:
	ULayoutGenerator();

	virtual ULayoutGenerator* SetBounds(const FBox2D& InBounds);
	virtual ULayoutGenerator* SetCenter(const FVector2D& InCenter);
	virtual ULayoutGenerator* SetSeed(const FString& InSeed);
	virtual ULayoutGenerator* SetGridSize(int32 InSize);

	virtual FLayoutDiagram2D Generate() PURE_VIRTUAL(ULayoutGenerator::Generate, return FLayoutDiagram2D(););

protected:
	void			 InitializeRandomStream();
	FVector2D		 ClampToBounds(const FVector2D& Point) const;
	FLayoutDiagram2D ConvertGridToDiagram(const TArray<bool>& Grid, int32 GridWidth, int32 GridHeight) const;

	/** BFS flood-fill over a boolean grid. Populates OutRegionIds and OutRegions, and identifies which
	 *  region contains the cell (CenterX, CenterY) via OutCenterRegionId (-1 if that cell is a wall).
	 * Used by CA and DrunkardWalk generators. */
	static void FloodFillRegions(const TArray<bool>& Grid,
		int32										 GridWidth,
		int32										 GridHeight,
		int32										 CenterX,
		int32										 CenterY,
		TArray<int32>&								 OutRegionIds,
		TArray<TArray<FIntPoint>>&					 OutRegions,
		int32&										 OutCenterRegionId);
};
