#include "InputEvent.h"

namespace UI {

InputEvent InputEvent::mouseDown(Foundation::Vec2 pos, engine::MouseButton btn, int mods) {
	InputEvent event;
	event.type = Type::MouseDown;
	event.position = pos;
	event.button = btn;
	event.modifiers = mods;
	return event;
}

InputEvent InputEvent::mouseUp(Foundation::Vec2 pos, engine::MouseButton btn, int mods) {
	InputEvent event;
	event.type = Type::MouseUp;
	event.position = pos;
	event.button = btn;
	event.modifiers = mods;
	return event;
}

InputEvent InputEvent::mouseMove(Foundation::Vec2 pos) {
	InputEvent event;
	event.type = Type::MouseMove;
	event.position = pos;
	return event;
}

InputEvent InputEvent::scroll(Foundation::Vec2 pos, float delta) {
	InputEvent event;
	event.type = Type::Scroll;
	event.position = pos;
	event.scrollDelta = delta;
	return event;
}

} // namespace UI
