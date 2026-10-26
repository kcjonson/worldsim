#pragma once

// The primitive scenes (polygon, arc, polyline, blend) are laid out on a fixed
// design canvas that is scaled to the window, so the same scene fits a laptop
// window and fills a large one.

#include <primitives/Primitives.h>

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace ui_sandbox {

	// The transform that fits a `width` x `height` canvas into the logical window,
	// centred, keeping its aspect.
	inline Foundation::Mat4 designCanvas(float width, float height) {
		int windowWidth = 0;
		int windowHeight = 0;
		Renderer::Primitives::getLogicalViewport(windowWidth, windowHeight);
		const float			   w = static_cast<float>(windowWidth);
		const float			   h = static_cast<float>(windowHeight);
		const float			   scale = std::min(w / width, h / height);
		const Foundation::Mat4 centred =
			glm::translate(Foundation::Mat4(1.0F), Foundation::Vec3((w - (width * scale)) * 0.5F, (h - (height * scale)) * 0.5F, 0.0F));
		return glm::scale(centred, Foundation::Vec3(scale, scale, 1.0F));
	}

} // namespace ui_sandbox
