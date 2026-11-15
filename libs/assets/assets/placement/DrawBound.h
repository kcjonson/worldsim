#pragma once

// The part of the world a frame can show (player-visibility.md 3.4): boxes in world meters that hold
// everything not under the cover's opaque fill. Drawing culls against it where it culls against the
// view, so nothing is drawn only to be covered. With the cover off it is one box holding everything.

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace assets {

/// Visible world bounds (meters) for sub-chunk / entity culling.
struct VisibleBounds {
	float minX = 0, maxX = 0, minY = 0, maxY = 0;
};

class DrawBound {
  public:
	/// Nothing is covered: the bound holds the whole world.
	[[nodiscard]] static DrawBound everywhere() {
		constexpr float kFar = std::numeric_limits<float>::max();
		DrawBound bound;
		bound.boxes.push_back({-kFar, kFar, -kFar, kFar});
		return bound;
	}

	void add(const VisibleBounds& box) { boxes.push_back(box); }

	/// The box holding all of the bound inside `region`, each box grown by `margin` first: the
	/// bounding box of the union of those that meet the region, each cut to it. None when none meets it.
	[[nodiscard]] std::optional<VisibleBounds> within(const VisibleBounds& region, float margin = 0.0F) const {
		std::optional<VisibleBounds> held;
		for (const VisibleBounds& box : boxes) {
			if (box.maxX + margin < region.minX || box.minX - margin > region.maxX || box.maxY + margin < region.minY ||
				box.minY - margin > region.maxY) {
				continue;
			}
			const VisibleBounds cut{
				std::max(box.minX - margin, region.minX),
				std::min(box.maxX + margin, region.maxX),
				std::max(box.minY - margin, region.minY),
				std::min(box.maxY + margin, region.maxY)
			};
			if (!held.has_value()) {
				held = cut;
			} else {
				held = VisibleBounds{
					std::min(held->minX, cut.minX), std::max(held->maxX, cut.maxX), std::min(held->minY, cut.minY), std::max(held->maxY, cut.maxY)
				};
			}
		}
		return held;
	}

	[[nodiscard]] bool meets(const VisibleBounds& region, float margin = 0.0F) const { return within(region, margin).has_value(); }

  private:
	std::vector<VisibleBounds> boxes;
};

} // namespace assets
