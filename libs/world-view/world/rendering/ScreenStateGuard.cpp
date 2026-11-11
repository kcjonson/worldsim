#include "world/rendering/ScreenStateGuard.h"

#include <primitives/Primitives.h>

namespace worldview {

	ScreenStateGuard::ScreenStateGuard(Renderer::CoordinateSystem* restore)
		: coordinateSystem(restore) {
		Renderer::Primitives::getViewport(width, height);
	}

	ScreenStateGuard::~ScreenStateGuard() {
		Renderer::Primitives::setCoordinateSystem(coordinateSystem);
		Renderer::Primitives::setViewport(width, height);
	}

} // namespace worldview
