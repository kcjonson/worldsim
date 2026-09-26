#pragma once

#include "../core/Vec2i64.h"
#include "ScalarField.h"

#include <cmath>
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

	// One axis of a bilinear read on a coarse lattice: the global cell index and
	// the fraction across it, for a world coordinate given relative to the
	// lattice phase (the world position of index 0), displaced by offsetMm.
	struct LatticeAxisRead {
		std::int64_t cell = 0;
		double		 frac = 0.0;
	};

	inline LatticeAxisRead latticeAxisRead(std::int64_t fromPhaseMm, double offsetMm, std::int64_t cellMm) {
		const double u = (static_cast<double>(fromPhaseMm) + offsetMm) / static_cast<double>(cellMm);
		const double k = std::floor(u);
		return {static_cast<std::int64_t>(k), u - k};
	}

	// Bilinear read of a coarse lattice given per axis; sample(gx, gy) returns the
	// sample at global index (gx, gy). The lerps stay within the range of their
	// four samples in floating point, which warpField's skip relies on.
	template <typename Sample> float latticeBilinear(const Sample& sample, LatticeAxisRead x, LatticeAxisRead y) {
		const double v00	= static_cast<double>(sample(x.cell, y.cell));
		const double v10	= static_cast<double>(sample(x.cell + 1, y.cell));
		const double v01	= static_cast<double>(sample(x.cell, y.cell + 1));
		const double v11	= static_cast<double>(sample(x.cell + 1, y.cell + 1));
		const double bottom = v00 + (v10 - v00) * x.frac;
		const double top	= v01 + (v11 - v01) * x.frac;
		return static_cast<float>(bottom + (top - bottom) * y.frac);
	}

	// The warped read warpField stores for a fine sample at world w: the coarse
	// lattice, index 0 at world phaseMm and cellMm apart, read bilinearly at
	// w + offset. The one formula for both the fine lattice and a point query of
	// the same field, so the two agree bit for bit wherever their coarse samples do.
	template <typename Sample>
	float warpedBilinear(const Sample& sample, Vec2i64 phaseMm, std::int64_t cellMm, Vec2i64 w, WarpOffsetMm offset) {
		return latticeBilinear(
			sample, latticeAxisRead(w.x - phaseMm.x, offset.x, cellMm), latticeAxisRead(w.y - phaseMm.y, offset.y, cellMm)
		);
	}

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
