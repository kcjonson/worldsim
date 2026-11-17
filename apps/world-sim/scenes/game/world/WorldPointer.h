#pragma once

// Where the pointer is for the game scene: on the world, on the interface, or outside the
// window. The pointer and the world are in the window's logical pixels; the interface lays
// out in ui px, each `uiScale` logical pixels (the Interface size), so its hit test takes
// the pointer divided by the scale. The one place that division happens for world input.

#include <glm/vec2.hpp>

#include <functional>

namespace world_sim {

	enum class PointerTarget {
		World,
		Interface,
		OutsideWindow,
	};

	/// `windowPointer` in logical pixels, the window `windowWidth` by `windowHeight` of them;
	/// `interfaceHit` answers whether a point in ui px lies on the interface.
	[[nodiscard]] PointerTarget pointerTarget(
		glm::vec2								  windowPointer,
		int										  windowWidth,
		int										  windowHeight,
		float									  uiScale,
		const std::function<bool(glm::vec2 uiPoint)>& interfaceHit
	);

} // namespace world_sim
