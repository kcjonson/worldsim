#include "WorldPointer.h"

#include "scenes/game/world/construction/DrawingSystem.h"

#include <cmath>

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

	bool pointerWithTool(PointerTarget target, bool pointerInWindow, bool windowFocused, bool backgroundLaunch) {
		return pointerInWindow && target != PointerTarget::OutsideWindow && (windowFocused || backgroundLaunch);
	}

	PointerAction drawingPointerAction(
		PointerInput			input,
		foundation::MouseButton button,
		PointerTarget			target,
		bool					withTool,
		std::optional<ToolKind> activeTool,
		bool					pressPending
	) {
		const bool rectangleTool = activeTool == ToolKind::FoundationRectangle;
		const bool onWorld = withTool && target == PointerTarget::World;
		switch (input) {
			case PointerInput::Press:
				return rectangleTool && button == foundation::MouseButton::Left && onWorld ? PointerAction::DeliverPress : PointerAction::Ignore;
			case PointerInput::LeftButtonUpThisFrame:
				return pressPending ? PointerAction::CancelPress : PointerAction::Ignore;
			case PointerInput::Release:
				if (!activeTool) {
					return PointerAction::PassOn;
				}
				if (button == foundation::MouseButton::Right) {
					return PointerAction::CancelTool;
				}
				if (button != foundation::MouseButton::Left) {
					return PointerAction::Ignore;
				}
				if (onWorld) {
					return PointerAction::DeliverClick;
				}
				return rectangleTool ? PointerAction::CancelPress : PointerAction::Ignore;
		}
		return PointerAction::Ignore;
	}

	bool secondClickOfDoubleClick(
		const std::optional<PointerRelease>& previous, const PointerRelease& release, double limitSeconds, float limitPixels
	) {
		if (!previous) {
			return false;
		}
		const glm::vec2 offset = release.position - previous->position;
		return release.timeSeconds - previous->timeSeconds <= limitSeconds && std::hypot(offset.x, offset.y) <= limitPixels;
	}

} // namespace world_sim
