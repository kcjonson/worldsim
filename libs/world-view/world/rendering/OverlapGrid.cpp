#include "OverlapGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace worldview {

	void OverlapGrid::beginFrame(int viewportWidth, int viewportHeight) {
		const auto w = static_cast<uint32_t>(std::max(1.0F, std::ceil(static_cast<float>(viewportWidth) / kCellPixels)));
		const auto h = static_cast<uint32_t>(std::max(1.0F, std::ceil(static_cast<float>(viewportHeight) / kCellPixels)));
		// A frame's keys are at most kMaxRunKey, a quarter of the range, so stamping restarts before it wraps.
		constexpr uint32_t kRestartAt = std::numeric_limits<uint32_t>::max() / 4 * 3;
		static_assert(uint64_t{kRestartAt} + kMaxRunKey <= std::numeric_limits<uint32_t>::max());
		frameBase += frameMaxKey;
		frameMaxKey = 0;
		if (w != width || h != height || frameBase >= kRestartAt) {
			width = w;
			height = h;
			stamps.assign(static_cast<size_t>(w) * h, 0);
			frameBase = 0;
		}
	}

} // namespace worldview
