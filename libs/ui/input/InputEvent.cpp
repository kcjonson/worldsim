#include "InputEvent.h"

namespace UI {

InputEvent InputEvent::mouseDown(Foundation::Vec2 pos, engine::MouseButton btn, int mods) {
	return InputEvent{
		.type = Type::MouseDown,
		.position = pos,
		.button = btn,
		.modifiers = mods,
	};
}

InputEvent InputEvent::mouseUp(Foundation::Vec2 pos, engine::MouseButton btn, int mods) {
	return InputEvent{
		.type = Type::MouseUp,
		.position = pos,
		.button = btn,
		.modifiers = mods,
	};
}

InputEvent InputEvent::mouseMove(Foundation::Vec2 pos) {
	return InputEvent{
		.type = Type::MouseMove,
		.position = pos,
	};
}

InputEvent InputEvent::scroll(Foundation::Vec2 pos, float delta) {
	return InputEvent{
		.type = Type::Scroll,
		.position = pos,
		.scrollDelta = delta,
	};
}

} // namespace UI
