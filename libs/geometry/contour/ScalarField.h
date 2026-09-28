#pragma once

#include "../core/Vec2i64.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// A regular lattice of float samples anchored in world millimeters. Sample (x, y)
// sits at originMm + (x, y) * cellMm; +y is up. Contour code computes every
// position from these world coordinates, never from (x, y) alone, so two fields
// that share sample positions but not origins give identical results where they
// overlap (terrain-polygons D4).

namespace geometry {

	struct ScalarField {
		Vec2i64			   originMm{};
		std::int64_t	   cellMm = 1000;
		int				   width  = 0;
		int				   height = 0;
		std::vector<float> values{}; // row-major: values[y * width + x]

		ScalarField() = default;
		ScalarField(Vec2i64 newOriginMm, std::int64_t newCellMm, int newWidth, int newHeight, float fill = 0.0F)
			: originMm(newOriginMm),
			  cellMm(newCellMm),
			  width(newWidth),
			  height(newHeight),
			  values(static_cast<std::size_t>(newWidth) * static_cast<std::size_t>(newHeight), fill) {}

		float at(int x, int y) const { return values[index(x, y)]; }
		float& at(int x, int y) { return values[index(x, y)]; }

		Vec2i64 samplePositionMm(int x, int y) const { return {originMm.x + x * cellMm, originMm.y + y * cellMm}; }

	  private:
		std::size_t index(int x, int y) const {
			return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
		}
	};

} // namespace geometry
