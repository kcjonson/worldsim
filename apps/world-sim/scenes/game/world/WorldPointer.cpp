#include "WorldPointer.h"

#include "scenes/game/world/construction/DrawingSystem.h"

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

	PointerAction drawingPointerAction(
		PointerInput			input,
		foundation::MouseButton button,
		PointerTarget			target,
		std::optional<ToolKind> activeTool,
		bool					pressPending
	) {
		const bool rectangleTool = activeTool == ToolKind::FoundationRectangle;
		switch (input) {
			case PointerInput::Press:
				return rectangleTool && button == foundation::MouseButton::Left && target == PointerTarget::World
						   ? PointerAction::DeliverPress
						   : PointerAction::Ignore;
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
				return rectangleTool && target != PointerTarget::World ? PointerAction::CancelPress : PointerAction::DeliverClick;
		}
		return PointerAction::Ignore;
	}

} // namespace world_sim
