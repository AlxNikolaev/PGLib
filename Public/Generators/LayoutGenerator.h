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

	/** Set when InitializeRandomStream had to invent a seed; cleared by SetSeed. Not a UPROPERTY: it describes
	 *  this instance's provenance, which must not be copied off the class default object. */
	bool bSeedSubstituted = false;

public:
	/**
	 * Smallest cell size a raster generator will run at, and the single owner of that floor: SetGridSize raises
	 * any smaller request to this value, so callers that clamp their own request must clamp against it too.
	 */
	static constexpr int32 MinGridCellSize = 10;

	ULayoutGenerator();

	virtual ULayoutGenerator* SetBounds(const FBox2D& InBounds);
	virtual ULayoutGenerator* SetCenter(const FVector2D& InCenter);
	virtual ULayoutGenerator* SetSeed(const FString& InSeed);
	virtual ULayoutGenerator* SetGridSize(int32 InSize);

	virtual FLayoutDiagram2D Generate() PURE_VIRTUAL(ULayoutGenerator::Generate, return FLayoutDiagram2D(););

#if WITH_DEV_AUTOMATION_TESTS
	/** Test seam onto the raster conversion; the exterior-ring rule has no other entry point a test can reach. */
	FLayoutDiagram2D ConvertGridToDiagramForTests(const TArray<bool>& Grid, int32 GridWidth, int32 GridHeight) const
	{
		return ConvertGridToDiagram(Grid, GridWidth, GridHeight);
	}
#endif

protected:
	void InitializeRandomStream();

	/**
	 * Reports a seed this generator invented for itself. Called where the diagram is built, not where the stream
	 * is seeded, because generators also seed in their constructor and a construction is not yet a layout.
	 */
	void WarnIfSeedSubstituted() const;

	FVector2D		 ClampToBounds(const FVector2D& Point) const;
	FLayoutDiagram2D ConvertGridToDiagram(const TArray<bool>& Grid, int32 GridWidth, int32 GridHeight) const;

	/** BFS flood-fill over a boolean grid. OutCenterRegionId is the region containing (CenterX, CenterY),
	 *  or -1 when that cell is a wall. */
	static void FloodFillRegions(const TArray<bool>& Grid,
		int32										 GridWidth,
		int32										 GridHeight,
		int32										 CenterX,
		int32										 CenterY,
		TArray<int32>&								 OutRegionIds,
		TArray<TArray<FIntPoint>>&					 OutRegions,
		int32&										 OutCenterRegionId);
};
