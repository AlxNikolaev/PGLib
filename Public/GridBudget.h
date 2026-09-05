#pragma once

#include "CoreMinimal.h"

namespace PGGrid
{
	/**
	 * Ceiling on the cells a single generation may rasterize, bounding peak grid allocation. Generators
	 * coarsen their cell size to fit rather than refusing to generate, and every generator and OOM-guard
	 * test must read this same number.
	 */
	inline constexpr int64 MaxGridCells = 4'194'304;
} // namespace PGGrid
