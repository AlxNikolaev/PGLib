#include "Generators/Voronoi2D/VoronoiGenerator2D.h"
#include "Generators/Voronoi2D/VoronoiSiteIndex.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#include "../../ProceduralGeometryTestFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VoroIndexTest
{
	/** Holds the pruning cvar at a chosen value for the duration of a scope and puts the old value back. */
	struct FScopedPruning
	{
		explicit FScopedPruning(const int32 Value)
		{
			CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProcGen.Voronoi.SpatialPruning"));
			if (CVar)
			{
				SavedValue = CVar->GetInt();
				CVar->Set(Value, ECVF_SetByCode);
			}
		}

		~FScopedPruning()
		{
			if (CVar)
			{
				CVar->Set(SavedValue, ECVF_SetByCode);
			}
		}

		IConsoleVariable* CVar = nullptr;
		int32			  SavedValue = 1;
	};

	static FBox2D TestBounds()
	{
		return FBox2D(FVector2D(-4000.0, -3000.0), FVector2D(5000.0, 6500.0));
	}

	static TArray<FVector2D> MakeUniformSites(const FBox2D& Bounds, const int32 Count, const int32 Seed)
	{
		FRandomStream	  Rng(Seed);
		TArray<FVector2D> Sites;
		Sites.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			Sites.Add(FVector2D(Rng.FRandRange(Bounds.Min.X, Bounds.Max.X), Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y)));
		}
		return Sites;
	}

	/** Row-major jittered lattice: the ordering the substrate generator emits, where index order tracks position. */
	static TArray<FVector2D> MakeJitteredLattice(const FBox2D& Bounds, const int32 Count, const int32 Seed)
	{
		FRandomStream	Rng(Seed);
		const int32		Side = FMath::Max(2, FMath::CeilToInt(FMath::Sqrt(static_cast<double>(Count))));
		const FVector2D Size = Bounds.GetSize();
		const double	CellX = Size.X / Side;
		const double	CellY = Size.Y / Side;

		TArray<FVector2D> Sites;
		Sites.Reserve(Count);
		for (int32 Y = 0; Y < Side && Sites.Num() < Count; ++Y)
		{
			for (int32 X = 0; X < Side && Sites.Num() < Count; ++X)
			{
				const double CenterX = Bounds.Min.X + (X + 0.5) * CellX;
				const double CenterY = Bounds.Min.Y + (Y + 0.5) * CellY;
				Sites.Add(FVector2D(CenterX + Rng.FRandRange(-0.35 * CellX, 0.35 * CellX), CenterY + Rng.FRandRange(-0.35 * CellY, 0.35 * CellY)));
			}
		}
		return Sites;
	}

	/**
	 * Tight clusters with exact duplicates sprinkled in. Collapsed cells and coincident sites are the two
	 * branches where the indexed path could diverge without a well-spread site set ever noticing.
	 */
	static TArray<FVector2D> MakeClusteredSites(const FBox2D& Bounds, const int32 Count, const int32 Seed)
	{
		FRandomStream	  Rng(Seed);
		constexpr int32	  NumClusters = 5;
		TArray<FVector2D> Centers;
		for (int32 i = 0; i < NumClusters; ++i)
		{
			Centers.Add(FVector2D(Rng.FRandRange(Bounds.Min.X, Bounds.Max.X), Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y)));
		}

		const double	  Spread = 0.01 * Bounds.GetSize().GetMax();
		TArray<FVector2D> Sites;
		Sites.Reserve(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			if (i > 0 && (i % 10) == 0)
			{
				// Copy out before appending: TArray::Add asserts whenever the element it is handed lives inside
				// the array's own allocation, reserved capacity or not.
				const FVector2D Duplicate = Sites[i - 1];
				Sites.Add(Duplicate);
				continue;
			}
			const FVector2D& Center = Centers[i % NumClusters];
			Sites.Add(Center + FVector2D(Rng.FRandRange(-Spread, Spread), Rng.FRandRange(-Spread, Spread)));
		}
		return Sites;
	}

	static FVoronoiDiagram2D BuildDiagram(const FBox2D& Bounds, const TArray<FVector2D>& Sites, const int32 PruningValue)
	{
		const FScopedPruning Pruning(PruningValue);

		UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
		Generator->SetBounds(Bounds)->SetSeed(TEXT("SpatialIndexEquivalence"));
		return Generator->GenerateFromSites(Sites);
	}

	/**
	 * Lloyd relaxation feeds each pass's cell centroids back in as the next pass's sites, so the pruned and
	 * exhaustive paths only stay equal here if they agree to the last bit every iteration. This is the shape the
	 * coarse room-placement diagram is built with, which makes it the one that decides room layout.
	 */
	static FVoronoiDiagram2D BuildRelaxedDiagram(const FBox2D& Bounds, const int32 NumSites, const int32 RelaxIterations, const int32 PruningValue)
	{
		const FScopedPruning Pruning(PruningValue);

		UVoronoiGenerator2D* Generator = NewObject<UVoronoiGenerator2D>();
		Generator->SetBounds(Bounds)->SetSeed(TEXT("SpatialIndexRelaxation"))->SetMinSiteDistance(120.0f)->SetRelaxationIterations(RelaxIterations);
		return Generator->GenerateRelaxed(NumSites);
	}

	struct FBuildMeasurement
	{
		double Seconds = 0.0;
		int64  Clips = 0;
		int32  CellCount = 0;
	};

	static FBuildMeasurement MeasureDiagramBuild(const FBox2D& Bounds, const TArray<FVector2D>& Sites, const int32 PruningValue)
	{
		VoronoiUtils::ResetHalfPlaneClipCount();

		const double			Start = FPlatformTime::Seconds();
		const FVoronoiDiagram2D Diagram = BuildDiagram(Bounds, Sites, PruningValue);

		FBuildMeasurement Measurement;
		Measurement.Seconds = FPlatformTime::Seconds() - Start;
		Measurement.Clips = VoronoiUtils::GetHalfPlaneClipCount();
		Measurement.CellCount = Diagram.Cells.Num();
		return Measurement;
	}

	static bool DiagramsMatch(FAutomationTestBase& Test, const FString& Label, const FVoronoiDiagram2D& Exhaustive, const FVoronoiDiagram2D& Pruned)
	{
		if (Exhaustive.Sites.Num() != Pruned.Sites.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: site count %d (exhaustive) vs %d (pruned)"), *Label, Exhaustive.Sites.Num(), Pruned.Sites.Num()));
			return false;
		}

		for (int32 SiteIdx = 0; SiteIdx < Exhaustive.Sites.Num(); ++SiteIdx)
		{
			// Under relaxation the sites of pass N are the cells of pass N-1, so a divergence reaches this array
			// one iteration before it reaches a vertex list. Reporting it here says which pass went wrong.
			if (Exhaustive.Sites[SiteIdx] != Pruned.Sites[SiteIdx])
			{
				Test.AddError(FString::Printf(TEXT("%s: site %d is (%.17g, %.17g) vs (%.17g, %.17g)"),
					*Label,
					SiteIdx,
					Exhaustive.Sites[SiteIdx].X,
					Exhaustive.Sites[SiteIdx].Y,
					Pruned.Sites[SiteIdx].X,
					Pruned.Sites[SiteIdx].Y));
				return false;
			}
		}

		if (Exhaustive.Cells.Num() != Pruned.Cells.Num())
		{
			Test.AddError(FString::Printf(TEXT("%s: cell count %d (exhaustive) vs %d (pruned)"), *Label, Exhaustive.Cells.Num(), Pruned.Cells.Num()));
			return false;
		}

		for (int32 CellIdx = 0; CellIdx < Exhaustive.Cells.Num(); ++CellIdx)
		{
			const FVoronoiCell2D& A = Exhaustive.Cells[CellIdx];
			const FVoronoiCell2D& B = Pruned.Cells[CellIdx];

			if (A.bIsValid != B.bIsValid || A.bIsBoundaryCell != B.bIsBoundaryCell || A.CellIndex != B.CellIndex)
			{
				Test.AddError(FString::Printf(TEXT("%s: cell %d flags differ (valid %d/%d, boundary %d/%d, index %d/%d)"),
					*Label,
					CellIdx,
					A.bIsValid ? 1 : 0,
					B.bIsValid ? 1 : 0,
					A.bIsBoundaryCell ? 1 : 0,
					B.bIsBoundaryCell ? 1 : 0,
					A.CellIndex,
					B.CellIndex));
				return false;
			}

			if (A.Vertices.Num() != B.Vertices.Num())
			{
				Test.AddError(FString::Printf(TEXT("%s: cell %d vertex count %d vs %d"), *Label, CellIdx, A.Vertices.Num(), B.Vertices.Num()));
				return false;
			}

			for (int32 VertIdx = 0; VertIdx < A.Vertices.Num(); ++VertIdx)
			{
				// Exact comparison on purpose: the pruned path only ever skips operations that provably do
				// nothing, so a difference of even one ULP means a clip that mattered was dropped.
				if (A.Vertices[VertIdx] != B.Vertices[VertIdx])
				{
					Test.AddError(FString::Printf(TEXT("%s: cell %d vertex %d is (%.17g, %.17g) vs (%.17g, %.17g)"),
						*Label,
						CellIdx,
						VertIdx,
						A.Vertices[VertIdx].X,
						A.Vertices[VertIdx].Y,
						B.Vertices[VertIdx].X,
						B.Vertices[VertIdx].Y));
					return false;
				}
			}

			if (A.Neighbors != B.Neighbors)
			{
				Test.AddError(FString::Printf(
					TEXT("%s: cell %d neighbour list differs (%d vs %d entries)"), *Label, CellIdx, A.Neighbors.Num(), B.Neighbors.Num()));
				return false;
			}
		}

		return true;
	}
} // namespace VoroIndexTest

// Covers F126: the spatially pruned cell build must produce the same diagram as the exhaustive one, not merely
// an equivalent-looking one. This is the guard that lets the pruning default to on.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPrunedMatchesFullScanTest, "ProceduralGeometry.Voronoi.PrunedMatchesFullScan", DefaultTestFlags)

bool FVoronoiPrunedMatchesFullScanTest::RunTest(const FString& Parameters)
{
	const FBox2D Bounds = VoroIndexTest::TestBounds();
	const int32	 SiteCounts[] = { 200, 700, 1500 };

	for (const int32 Count : SiteCounts)
	{
		const TArray<FVector2D> Distributions[] = { VoroIndexTest::MakeUniformSites(Bounds, Count, 12345 + Count),
			VoroIndexTest::MakeJitteredLattice(Bounds, Count, 777 + Count),
			VoroIndexTest::MakeClusteredSites(Bounds, Count, 909 + Count) };
		const TCHAR*			Names[] = { TEXT("uniform"), TEXT("jittered-lattice"), TEXT("clustered-with-duplicates") };

		for (int32 DistIdx = 0; DistIdx < UE_ARRAY_COUNT(Distributions); ++DistIdx)
		{
			const FString Label = FString::Printf(TEXT("%s N=%d"), Names[DistIdx], Distributions[DistIdx].Num());

			const FVoronoiDiagram2D Exhaustive = VoroIndexTest::BuildDiagram(Bounds, Distributions[DistIdx], 0);
			const FVoronoiDiagram2D Pruned = VoroIndexTest::BuildDiagram(Bounds, Distributions[DistIdx], 1);

			if (!VoroIndexTest::DiagramsMatch(*this, Label, Exhaustive, Pruned))
			{
				return false;
			}
		}
	}

	// Relaxed generation is the shape UCellDungeonGenerator2D builds its coarse room-placement diagram with, and
	// it is the one that compounds: each Lloyd pass reads back the previous pass's centroids, so a single-bit
	// disagreement in one pass becomes a different site set, and a different room layout, in the next.
	{
		constexpr int32 RelaxedSiteCount = 600;
		constexpr int32 RelaxIterations = 8;

		const FVoronoiDiagram2D Exhaustive = VoroIndexTest::BuildRelaxedDiagram(Bounds, RelaxedSiteCount, RelaxIterations, 0);
		const FVoronoiDiagram2D Pruned = VoroIndexTest::BuildRelaxedDiagram(Bounds, RelaxedSiteCount, RelaxIterations, 1);

		if (!TestTrue(TEXT("Relaxed generation produced enough sites to engage the spatial index"), Exhaustive.Sites.Num() >= 64))
		{
			return false;
		}

		if (!VoroIndexTest::DiagramsMatch(*this, TEXT("lloyd-relaxed"), Exhaustive, Pruned))
		{
			return false;
		}
	}

	return true;
}

// Records the F126 benchmark in the automation log and asserts what the index actually guarantees: the pruned
// build performs strictly fewer half-plane clips than the exhaustive one, having skipped only clips that cannot
// change a vertex. The clip count is the quantity the optimisation is about and it is the same on every machine,
// so it is what the assertions read; the durations sit beside it because the exit criterion is stated in time,
// but a wall clock on a shared build machine measures the machine as much as the algorithm.
//
// Two orderings are measured, because the win depends on the order sites are emitted in and not just on how many
// there are. Random order (the coarse placement diagram, every Poisson-sampled diagram) collapses the working
// polygon within a handful of clips. Row-major order at substrate scale is the opposite case and the dominant
// workload: FVoronoiGridGenerator emits its sites row by row, so a cell keeps a full-width working polygon until
// the scan reaches its own row and only the remainder of the scan is skipped. That case carries a duration
// assertion too, because it is where the extra per-candidate bookkeeping could outweigh what it saves, and a
// regression there would be a regression in the path this work exists to speed up.
//
// The 4x-sites ratios are recorded, not asserted: the build is still quadratic. While the working polygon is
// near the bounds box the security disc spans more than FVoronoiSiteIndex's swept-bucket ceiling, so nothing is
// skipped there. Pruning against the FINAL cell radius instead would be asymptotic, but it drops clips that
// graze intermediate polygons, and those clips decide the endpoints later intersections are computed from, so
// the diagram would move. Bit-identity and an asymptotic win are not both available from this algorithm.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiPruningScalingTest, "ProceduralGeometry.Voronoi.PruningScaling", DefaultTestFlags)

bool FVoronoiPruningScalingTest::RunTest(const FString& Parameters)
{
	const FBox2D			Bounds = VoroIndexTest::TestBounds();
	const TArray<FVector2D> SmallSites = VoroIndexTest::MakeUniformSites(Bounds, 1000, 4242);
	const TArray<FVector2D> LargeSites = VoroIndexTest::MakeUniformSites(Bounds, 4000, 4243);

	const VoroIndexTest::FBuildMeasurement SmallPruned = VoroIndexTest::MeasureDiagramBuild(Bounds, SmallSites, 1);
	const VoroIndexTest::FBuildMeasurement LargePruned = VoroIndexTest::MeasureDiagramBuild(Bounds, LargeSites, 1);
	const VoroIndexTest::FBuildMeasurement SmallFull = VoroIndexTest::MeasureDiagramBuild(Bounds, SmallSites, 0);
	const VoroIndexTest::FBuildMeasurement LargeFull = VoroIndexTest::MeasureDiagramBuild(Bounds, LargeSites, 0);

	AddInfo(FString::Printf(TEXT("Random order, pruned: N=1000 %.1f ms / %lld clips, N=4000 %.1f ms / %lld clips"),
		SmallPruned.Seconds * 1000.0,
		SmallPruned.Clips,
		LargePruned.Seconds * 1000.0,
		LargePruned.Clips));
	AddInfo(FString::Printf(TEXT("Random order, exhaustive: N=1000 %.1f ms / %lld clips, N=4000 %.1f ms / %lld clips"),
		SmallFull.Seconds * 1000.0,
		SmallFull.Clips,
		LargeFull.Seconds * 1000.0,
		LargeFull.Clips));
	AddInfo(FString::Printf(TEXT("F126 exit criterion (4x sites under 8x time): pruned %.2fx, exhaustive %.2fx - unmet, the build stays quadratic"),
		(SmallPruned.Seconds > 0.0) ? LargePruned.Seconds / SmallPruned.Seconds : 0.0,
		(SmallFull.Seconds > 0.0) ? LargeFull.Seconds / SmallFull.Seconds : 0.0));
	AddInfo(FString::Printf(TEXT("Clips skipped by pruning: %.1f%% at N=1000, %.1f%% at N=4000"),
		(SmallFull.Clips > 0) ? 100.0 * (1.0 - static_cast<double>(SmallPruned.Clips) / static_cast<double>(SmallFull.Clips)) : 0.0,
		(LargeFull.Clips > 0) ? 100.0 * (1.0 - static_cast<double>(LargePruned.Clips) / static_cast<double>(LargeFull.Clips)) : 0.0));

	TestEqual(TEXT("Pruned build produced one cell per site at N=1000"), SmallPruned.CellCount, SmallSites.Num());
	TestEqual(TEXT("Pruned build produced one cell per site at N=4000"), LargePruned.CellCount, LargeSites.Num());
	TestTrue(FString::Printf(TEXT("Random order N=1000 skips clips (%lld pruned vs %lld exhaustive)"), SmallPruned.Clips, SmallFull.Clips),
		SmallPruned.Clips < SmallFull.Clips);
	TestTrue(FString::Printf(TEXT("Random order N=4000 skips clips (%lld pruned vs %lld exhaustive)"), LargePruned.Clips, LargeFull.Clips),
		LargePruned.Clips < LargeFull.Clips);

	// As many sites as a fine Voronoi substrate carries, emitted row by row the way the grid generator emits them.
	constexpr int32			SubstrateSiteCount = 12288;
	const TArray<FVector2D> SubstrateSites = VoroIndexTest::MakeJitteredLattice(Bounds, SubstrateSiteCount, 8613);

	const VoroIndexTest::FBuildMeasurement SubstratePruned = VoroIndexTest::MeasureDiagramBuild(Bounds, SubstrateSites, 1);
	const VoroIndexTest::FBuildMeasurement SubstrateFull = VoroIndexTest::MeasureDiagramBuild(Bounds, SubstrateSites, 0);

	AddInfo(FString::Printf(TEXT("Row-major substrate order N=%d: pruned %.1f ms / %lld clips, exhaustive %.1f ms / %lld clips"),
		SubstrateSites.Num(),
		SubstratePruned.Seconds * 1000.0,
		SubstratePruned.Clips,
		SubstrateFull.Seconds * 1000.0,
		SubstrateFull.Clips));

	TestEqual(TEXT("Pruned build produced one cell per substrate site"), SubstratePruned.CellCount, SubstrateSites.Num());
	TestTrue(FString::Printf(TEXT("Row-major order skips clips (%lld pruned vs %lld exhaustive)"), SubstratePruned.Clips, SubstrateFull.Clips),
		SubstratePruned.Clips < SubstrateFull.Clips);
	TestTrue(FString::Printf(TEXT("Pruning does not cost time on the row-major substrate order (%.1f ms pruned vs %.1f ms exhaustive)"),
				 SubstratePruned.Seconds * 1000.0,
				 SubstrateFull.Seconds * 1000.0),
		SubstratePruned.Seconds < SubstrateFull.Seconds);

	return true;
}

// Covers F112: the indexed point-location answers must be the answers the linear scans give, probe for probe.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiSiteIndexNearestTest, "ProceduralGeometry.VoronoiSiteIndex.NearestMatchesLinearScan", DefaultTestFlags)

bool FVoronoiSiteIndexNearestTest::RunTest(const FString& Parameters)
{
	const FBox2D			Bounds = VoroIndexTest::TestBounds();
	const TArray<FVector2D> Sites = VoroIndexTest::MakeUniformSites(Bounds, 400, 31337);
	const FVoronoiDiagram2D Diagram = VoroIndexTest::BuildDiagram(Bounds, Sites, 1);

	FVoronoiSiteIndex Index;
	Index.Build(Diagram.Sites, Diagram.Bounds);
	if (!TestTrue(TEXT("An index over a populated diagram is valid"), Index.IsValid()))
	{
		return false;
	}

	FRandomStream	Rng(24680);
	const FVector2D Size = Bounds.GetSize();

	for (int32 Probe = 0; Probe < 1500; ++Probe)
	{
		const FVector2D Point(Rng.FRandRange(Bounds.Min.X, Bounds.Max.X), Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y));

		const int32 ScannedSite = Diagram.FindClosestCellBySite(Point);
		const int32 IndexedSite = Index.FindNearestSite(Point);
		if (ScannedSite != IndexedSite)
		{
			AddError(FString::Printf(TEXT("Nearest site at (%f, %f): scan says %d, index says %d"), Point.X, Point.Y, ScannedSite, IndexedSite));
			return false;
		}

		const int32 ScannedCell = Diagram.FindCellContainingPoint(Point);
		const int32 IndexedCell = Diagram.FindCellContainingPoint(Point, Index);
		if (ScannedCell != IndexedCell)
		{
			AddError(FString::Printf(TEXT("Containing cell at (%f, %f): scan says %d, index says %d"), Point.X, Point.Y, ScannedCell, IndexedCell));
			return false;
		}
	}

	// Probes well outside the diagram: the nearest site is still defined, and no cell can contain them.
	for (int32 Probe = 0; Probe < 200; ++Probe)
	{
		const FVector2D Point(Bounds.Min.X - Rng.FRandRange(0.5 * Size.X, 2.0 * Size.X), Bounds.Min.Y - Rng.FRandRange(0.5 * Size.Y, 2.0 * Size.Y));

		if (Diagram.FindClosestCellBySite(Point) != Index.FindNearestSite(Point))
		{
			AddError(FString::Printf(TEXT("Nearest site outside the bounds at (%f, %f) disagrees with the scan"), Point.X, Point.Y));
			return false;
		}
		if (Diagram.FindCellContainingPoint(Point, Index) != INDEX_NONE)
		{
			AddError(FString::Printf(TEXT("A point outside the bounds at (%f, %f) was reported inside a cell"), Point.X, Point.Y));
			return false;
		}
	}

	return true;
}

// Covers F128: the box gather must never hide a site the caller's own containment filter would have accepted.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiSiteIndexBoxQueryTest, "ProceduralGeometry.VoronoiSiteIndex.BoxQueryIsExactAndOrdered", DefaultTestFlags)

bool FVoronoiSiteIndexBoxQueryTest::RunTest(const FString& Parameters)
{
	const FBox2D			Bounds = VoroIndexTest::TestBounds();
	const TArray<FVector2D> Sites = VoroIndexTest::MakeUniformSites(Bounds, 300, 5150);

	FVoronoiSiteIndex Index;
	Index.Build(Sites, Bounds);

	FRandomStream Rng(9001);
	TArray<int32> Gathered;

	for (int32 Query = 0; Query < 60; ++Query)
	{
		const FVector2D CornerA(Rng.FRandRange(Bounds.Min.X, Bounds.Max.X), Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y));
		const FVector2D CornerB(Rng.FRandRange(Bounds.Min.X, Bounds.Max.X), Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y));
		const FBox2D	Box(FVector2D(FMath::Min(CornerA.X, CornerB.X), FMath::Min(CornerA.Y, CornerB.Y)),
			   FVector2D(FMath::Max(CornerA.X, CornerB.X), FMath::Max(CornerA.Y, CornerB.Y)));

		Gathered.Reset();
		Index.GatherSitesInBox(Box, Gathered);

		for (int32 Slot = 1; Slot < Gathered.Num(); ++Slot)
		{
			if (Gathered[Slot] <= Gathered[Slot - 1])
			{
				AddError(FString::Printf(
					TEXT("Box gather returned indices out of order at slot %d (%d after %d)"), Slot, Gathered[Slot], Gathered[Slot - 1]));
				return false;
			}
		}

		const TSet<int32> GatheredSet(Gathered);
		for (int32 SiteIdx = 0; SiteIdx < Sites.Num(); ++SiteIdx)
		{
			if (Box.IsInside(Sites[SiteIdx]) && !GatheredSet.Contains(SiteIdx))
			{
				AddError(FString::Printf(TEXT("Box gather missed site %d at (%f, %f)"), SiteIdx, Sites[SiteIdx].X, Sites[SiteIdx].Y));
				return false;
			}
		}
	}

	// The rings around any bucket must partition the site set: every site exactly once, none twice. A ring walk
	// that overlaps or leaves a gap would make the nearest-site search silently miss or double-count sites.
	int32 CenterBX = 0;
	int32 CenterBY = 0;
	Index.GetBucketCoords(FVector2D(1200.0, -400.0), CenterBX, CenterBY);

	TArray<int32> RingUnion;
	for (int32 Ring = 0; Ring <= Index.GetMaxRingFrom(CenterBX, CenterBY); ++Ring)
	{
		const int32 Before = RingUnion.Num();
		Index.GatherRing(CenterBX, CenterBY, Ring, RingUnion);

		for (int32 Slot = Before + 1; Slot < RingUnion.Num(); ++Slot)
		{
			if (RingUnion[Slot] <= RingUnion[Slot - 1])
			{
				AddError(FString::Printf(TEXT("Ring %d returned indices out of order at slot %d"), Ring, Slot));
				return false;
			}
		}
	}

	RingUnion.Sort();
	if (!TestEqual(TEXT("The rings around a bucket cover every site exactly once"), RingUnion.Num(), Sites.Num()))
	{
		return false;
	}
	for (int32 SiteIdx = 0; SiteIdx < RingUnion.Num(); ++SiteIdx)
	{
		if (RingUnion[SiteIdx] != SiteIdx)
		{
			AddError(FString::Printf(TEXT("Ring union is not the whole site set: slot %d holds %d"), SiteIdx, RingUnion[SiteIdx]));
			return false;
		}
	}

	return true;
}

// Degenerate site sets are the inputs where a bucket grid divides by zero or answers with an index nobody owns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoronoiSiteIndexDegenerateTest, "ProceduralGeometry.VoronoiSiteIndex.DegenerateInputs", DefaultTestFlags)

bool FVoronoiSiteIndexDegenerateTest::RunTest(const FString& Parameters)
{
	const FBox2D Bounds = VoroIndexTest::TestBounds();

	{
		FVoronoiSiteIndex Index;
		Index.Build(TArrayView<const FVector2D>(), Bounds);
		TestFalse(TEXT("An index over no sites is not valid"), Index.IsValid());
		TestEqual(TEXT("An empty index finds no nearest site"), Index.FindNearestSite(FVector2D::ZeroVector), INDEX_NONE);
		TestEqual(TEXT("An empty index finds no site in radius"), Index.FindNextCandidateSite(FVector2D::ZeroVector, 1e12, 0, -1), INDEX_NONE);
	}

	{
		const TArray<FVector2D> Sites = { FVector2D(120.0, -80.0) };
		FVoronoiSiteIndex		Index;
		Index.Build(Sites, Bounds);
		TestTrue(TEXT("An index over one site is valid"), Index.IsValid());
		TestEqual(TEXT("The single site answers every nearest query"), Index.FindNearestSite(FVector2D(4000.0, 4000.0)), 0);
		TestEqual(TEXT("The single site is skipped when it is the ignored one"),
			Index.FindNextCandidateSite(FVector2D(120.0, -80.0), 1e12, 0, 0),
			INDEX_NONE);
	}

	{
		TArray<FVector2D> Sites;
		Sites.Init(FVector2D(50.0, 50.0), 64);
		FVoronoiSiteIndex Index;
		Index.Build(Sites, FBox2D(ForceInit));
		TestTrue(TEXT("An index over coincident sites is valid"), Index.IsValid());
		TestEqual(TEXT("Coincident sites resolve the tie to the lowest index"), Index.FindNearestSite(FVector2D(75.0, 20.0)), 0);
		TestEqual(TEXT("A coincident set still honours the minimum index"), Index.FindNextCandidateSite(FVector2D(50.0, 50.0), 1.0, 40, -1), 40);
	}

	{
		// Collinear sites give the grid zero area on one axis; it must still answer like the linear scan.
		TArray<FVector2D> Sites;
		for (int32 i = 0; i < 128; ++i)
		{
			Sites.Add(FVector2D(-500.0 + i * 17.0, 300.0));
		}

		FVoronoiSiteIndex Index;
		Index.Build(Sites, FBox2D(ForceInit));
		TestTrue(TEXT("An index over collinear sites is valid"), Index.IsValid());

		FRandomStream Rng(1234);
		for (int32 Probe = 0; Probe < 400; ++Probe)
		{
			const FVector2D Point(Rng.FRandRange(-2000.0, 3000.0), Rng.FRandRange(-1000.0, 1500.0));

			// Mirrors FVoronoiDiagram2D::FindClosestCellBySite exactly, float width included.
			int32 Expected = 0;
			float BestDistSq = FVector2D::DistSquared(Point, Sites[0]);
			for (int32 SiteIdx = 1; SiteIdx < Sites.Num(); ++SiteIdx)
			{
				const float DistSq = FVector2D::DistSquared(Point, Sites[SiteIdx]);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Expected = SiteIdx;
				}
			}

			const int32 Found = Index.FindNearestSite(Point);
			if (Found != Expected)
			{
				AddError(
					FString::Printf(TEXT("Collinear nearest-site query at (%f, %f) returned %d, expected %d"), Point.X, Point.Y, Found, Expected));
				return false;
			}
		}
	}

	return true;
}

#endif
