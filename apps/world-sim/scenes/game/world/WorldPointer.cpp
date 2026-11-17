#include "WorldPointer.h"

namespace world_sim {

	PointerTarget pointerTarget(
		glm::vec2									  windowPointer,
		int											  windowWidth,
		int											  windowHeight,
		float										  uiScale,
		const std::function<bool(glm::vec2 uiPoint)>& interfaceHit
	) {
		if (windowPointer.x < 0.0F || windowPointer.y < 0.0F || windowPointer.x >= static_cast<float>(windowWidth) ||
			windowPointer.y >= static_cast<float>(windowHeight)) {
			return PointerTarget::OutsideWindow;
		}
		return interfaceHit(windowPointer / uiScale) ? PointerTarget::Interface : PointerTarget::World;
	}

} // namespace world_sim
