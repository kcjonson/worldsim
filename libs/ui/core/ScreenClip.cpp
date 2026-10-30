#include "core/ScreenClip.h"

#include <primitives/Primitives.h>

#include <glm/common.hpp>

#include <cmath>

namespace UI {

	Foundation::Rect screenClip(const Foundation::Rect& local) {
		if (!(local.width >= 0.0F) || !(local.height >= 0.0F) || std::isnan(local.x) || std::isnan(local.y)) {
			return {}; // No area: the empty clip
		}
		const Foundation::Mat4 transform = Renderer::Primitives::getCurrentTransform();
		const auto			   onScreen = [&](const Foundation::Vec2& point) {
			const Foundation::Vec4 mapped = transform * Foundation::Vec4(point.x, point.y, 0.0F, 1.0F);
			return Foundation::Vec2(mapped.x, mapped.y);
		};
		Foundation::Vec2 low = onScreen(local.topLeft());
		Foundation::Vec2 high = low;
		for (const Foundation::Vec2& corner : {local.topRight(), local.bottomRight(), local.bottomLeft()}) {
			const Foundation::Vec2 point = onScreen(corner);
			low = glm::min(low, point);
			high = glm::max(high, point);
		}
		return {low.x, low.y, high.x - low.x, high.y - low.y};
	}

} // namespace UI
