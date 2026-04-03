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
	FVector2D Center;

	UPROPERTY()
	int32 CellIndex;

	UPROPERTY()
	bool bIsOccupied;

	UPROPERTY()
	bool bIsExterior;

	FLayoutCell2D()
	{
		CellIndex = -1;
		Center = FVector2D::ZeroVector;
		bIsExterior = false;
		bIsOccupied = false;
	}
};

USTRUCT()
struct PROCEDURALGEOMETRY_API FLayoutDiagram2D
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FLayoutCell2D> Cells;

	UPROPERTY()
	FBox2D Bounds;

	UPROPERTY()
	FVector2D CenterPoint;

	UPROPERTY()
	int32 CenterCellIndex;

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
};
