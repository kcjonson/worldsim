#pragma once

#include "../core/Vec2i64.h"
#include "ScalarField.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace geometry {

	struct WarpOffsetMm {
		double x = 0.0;
		double y = 0.0;
	};

	// World-space displacement of a read position. Must be a pure function of its
	// argument (terrain-polygons D6/D14).
	using WarpFunction = std::function<WarpOffsetMm(Vec2i64 worldMm)>;

	// Optimization contract for warpField. The caller promises every offset has
	// |x|, |y| <= maxOffsetMm, and only cares where the result is marched at `iso`.
	struct WarpSkip {
		float		 iso		 = 0.5F;
		std::int64_t maxOffsetMm = 0;
	};

	// Domain-warped bilinear resample (terrain-polygons D6 step 1). Fine sample
	// (i, j) sits at world w = fineOriginMm + (i, j) * fineCellMm and reads `coarse`
	// bilinearly at w + offsetMm(w); coarse samples off the grid read as
	// outsideValue. The coarse cell and fraction come from the global lattice
	// (world position relative to the lattice phase, originMm mod cellMm), so two
	// coarse fields that agree on their overlap give bit-identical fine samples
	// there whatever their origins.
	//
	// With `skip`, a fine sample whose coarse neighborhood (maxOffsetMm +
	// fineCellMm + one coarse cell, per axis) lies entirely on one side of iso takes
	// its unwarped value and offsetMm is never called for it. Neither it nor any
	// fine neighbor can then straddle iso, so marchingSquares at that iso returns
	// exactly what it would without the skip.
	ScalarField warpField(
		const ScalarField&		coarse,
		Vec2i64					fineOriginMm,
		std::int64_t			fineCellMm,
		int						fineWidth,
		int						fineHeight,
		const WarpFunction&		offsetMm,
		float					outsideValue,
		std::optional<WarpSkip> skip
	);

} // namespace geometry
