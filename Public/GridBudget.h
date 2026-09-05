#pragma once

#include "CoreMinimal.h"

namespace PGGrid
{
	/**
	 * Ceiling on the number of cells a single generation may rasterize. It bounds peak grid allocation
	 * (one byte-per-cell array plus parallel region/type arrays) so a mis-authored bounds/cell-size pair
	 * cannot exhaust memory. Generators coarsen their cell size to fit rather than refusing to generate,
	 * so the budget is a resolution knob, not a hard failure — and every generator and its OOM-guard test
	 * must read the same number from here, or a tightened budget goes unverified.
	 */
	inline constexpr int64 MaxGridCells = 4'194'304;
} // namespace PGGrid
