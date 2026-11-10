#pragma once

#include <primitives/Primitives.h>

namespace Renderer {
	class CoordinateSystem;
}

namespace worldview {

	// Puts the screen's coordinate system and viewport back when an offscreen render is done.
	// RenderTarget::end() already rebinds the target below it; the projection the batch draws with is
	// this. Taken before the target's begin(), so the viewport it saves is the screen's.
	class ScreenStateGuard {
	  public:
		explicit ScreenStateGuard(Renderer::CoordinateSystem* restore)
			: coordinateSystem(restore) {
			Renderer::Primitives::getViewport(width, height);
		}
		~ScreenStateGuard() {
			Renderer::Primitives::setCoordinateSystem(coordinateSystem);
			Renderer::Primitives::setViewport(width, height);
		}
		ScreenStateGuard(const ScreenStateGuard&) = delete;
		ScreenStateGuard& operator=(const ScreenStateGuard&) = delete;
		ScreenStateGuard(ScreenStateGuard&&) = delete;
		ScreenStateGuard& operator=(ScreenStateGuard&&) = delete;

	  private:
		Renderer::CoordinateSystem* coordinateSystem;
		int							width = 0;
		int							height = 0;
	};

} // namespace worldview
