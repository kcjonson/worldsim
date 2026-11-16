#pragma once

// The 0.1 m grid that identifies a world entity by position: colonist memory keys
// placed entities on it, and per-tree traits are computed from it, so both name a
// tree by the same cell.

#include <glm/vec2.hpp>

#include <cstdint>

namespace assets {

	struct DecimeterCell {
		int32_t x = 0;
		int32_t y = 0;
	};

	/// Truncates toward zero, so the cells either side of an axis share cell 0.
	[[nodiscard]] inline DecimeterCell decimeterCell(glm::vec2 position) {
		return {static_cast<int32_t>(position.x * 10.0F), static_cast<int32_t>(position.y * 10.0F)};
	}

} // namespace assets
