#pragma once

#include "Generators/LayoutGenerator.h"
#include "VoronoiGenerator2D.generated.h"

#define VORO_SITE_GEN_MAX_ATTEMPTS (30)

USTRUCT()
struct PROCEDURALGEOMETRY_API FVoronoiCell2D
{
	GENERATED_BODY()

	// Convex, clockwise vertices
	UPROPERTY()
	TArray<FVector2D> Vertices;

	UPROPERTY()
	TArray<int32> Neighbors;

	UPROPERTY()
	FVector2D SiteLocation;

	UPROPERTY()
	int32 CellIndex;

	UPROPERTY()
	bool bIsValid;

	UPROPERTY()
	bool bIsBoundaryCell;

	FVoronoiCell2D()
	{
		SiteLocation = FVector2D::ZeroVector;
		CellIndex = -1;
		bIsValid = false;
		bIsBoundaryCell = false;
	}

	explicit FVoronoiCell2D(const TArray<FVector2D>& InVerts)
	{
		SiteLocation = FVector2D::ZeroVector;
		CellIndex = -1;
		bIsValid = false;
		bIsBoundaryCell = false;
		Vertices = InVerts;
	}

	float	  GetArea() const;
	FVector2D GetCentroid() const;
	bool	  ContainsPoint(const FVector2D& Point) const;
};

USTRUCT()
struct PROCEDURALGEOMETRY_API FVoronoiDiagram2D
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVoronoiCell2D> Cells;

	UPROPERTY()
	FBox2D Bounds;

	UPROPERTY()
	TArray<FVector2D> Sites;

	UPROPERTY()
	FString Seed;

	void  DrawDebug(const UWorld* World, float Duration = 5.0f, float ZHeight = 0.0f) const;
	int32 FindCellContainingPoint(const FVector2D& Point) const;
	bool  GetSharedEdge(int32 CellA, int32 CellB, FVector2D& OutStart, FVector2D& OutEnd) const;
	int32 FindClosestCellBySite(const FVector2D& Point) const;
};

UCLASS()
class PROCEDURALGEOMETRY_API UVoronoiGenerator2D final : public ULayoutGenerator
{
	GENERATED_BODY()

	float MinSiteDistance;
	int32 RelaxationIterations;

public:
	UVoronoiGenerator2D();

	// Config
	virtual UVoronoiGenerator2D* SetBounds(const FBox2D& InBounds) override;
	virtual UVoronoiGenerator2D* SetSeed(const FString& InSeed) override;
	UVoronoiGenerator2D*		 SetMinSiteDistance(float Distance);
	UVoronoiGenerator2D*		 SetRelaxationIterations(int32 Iterations);

	// Generation
	FVoronoiDiagram2D GenerateFromSites(const TArray<FVector2D>& SiteLocations) const;
	FVoronoiDiagram2D GenerateRandomSites(int32 NumSites, bool bUsePoissonDisc = false) const;
	FVoronoiDiagram2D GenerateRelaxed(int32 NumSites);

private:
	void		   ComputeVoronoiCells(const TArray<FVector2D>& Sites, FVoronoiDiagram2D& OutDiagram) const;
	FVoronoiCell2D ComputeCellForSite(FVoronoiCell2D& OutCell, int32 SiteIndex, const TArray<FVector2D>& AllSites) const;

	// Helpers
	TArray<FVector2D> GeneratePoissonDiscSites(int32 TargetCount) const;
	void			  RelaxSites(TArray<FVector2D>& Sites);
};
