#include "OverlapGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace worldview {

	void OverlapGrid::beginFrame(int viewportWidth, int viewportHeight) {
		const auto w = static_cast<uint32_t>(std::max(1.0F, std::ceil(static_cast<float>(viewportWidth) / kCellPixels)));
		const auto h = static_cast<uint32_t>(std::max(1.0F, std::ceil(static_cast<float>(viewportHeight) / kCellPixels)));
		// A frame's keys never reach the top quarter of the range, so stamping restarts well before it wraps.
		constexpr uint32_t kRestartAt = std::numeric_limits<uint32_t>::max() / 4 * 3;
		frameBase += frameMaxKey;
		frameMaxKey = 0;
		if (w != width || h != height || frameBase >= kRestartAt) {
			width = w;
			height = h;
			stamps.assign(static_cast<size_t>(w) * h, 0);
			frameBase = 0;
		}
	}

	OverlapGrid::Cells OverlapGrid::cellsOf(Foundation::Vec2 minPixels, Foundation::Vec2 maxPixels) const {
		const float x0 = std::floor((minPixels.x - kPadPixels) / kCellPixels);
		const float y0 = std::floor((minPixels.y - kPadPixels) / kCellPixels);
		const float x1 = std::floor((maxPixels.x + kPadPixels) / kCellPixels);
		const float y1 = std::floor((maxPixels.y + kPadPixels) / kCellPixels);
		const auto	lastX = static_cast<float>(width - 1);
		const auto	lastY = static_cast<float>(height - 1);
		if (x1 < 0.0F || y1 < 0.0F || x0 > lastX || y0 > lastY || !(x0 <= x1) || !(y0 <= y1)) {
			return {};
		}
		return {
			.x0 = static_cast<uint32_t>(std::max(x0, 0.0F)),
			.y0 = static_cast<uint32_t>(std::max(y0, 0.0F)),
			.x1 = static_cast<uint32_t>(std::min(x1, lastX)),
			.y1 = static_cast<uint32_t>(std::min(y1, lastY)),
		};
	}

	uint32_t OverlapGrid::latest(const Cells& cells) const {
		uint32_t newest = frameBase;
		for (uint32_t y = cells.y0; y <= cells.y1; ++y) {
			const uint32_t* row = stamps.data() + (static_cast<size_t>(y) * width);
			for (uint32_t x = cells.x0; x <= cells.x1; ++x) {
				newest = std::max(newest, row[x]);
			}
		}
		return newest - frameBase;
	}

	void OverlapGrid::mark(const Cells& cells, uint32_t runKey) {
		frameMaxKey = std::max(frameMaxKey, runKey);
		const uint32_t stamp = frameBase + runKey;
		for (uint32_t y = cells.y0; y <= cells.y1; ++y) {
			uint32_t* row = stamps.data() + (static_cast<size_t>(y) * width);
			for (uint32_t x = cells.x0; x <= cells.x1; ++x) {
				row[x] = std::max(row[x], stamp);
			}
		}
	}

} // namespace worldview
