#pragma once

// InputEvent - Event-based input system for UI components
//
// This replaces the polling-based handleInput() pattern with proper event consumption.
// Components receive events and can call consume() to stop propagation.
//
// Event dispatch is z-index sorted (highest first) with early termination on consumption.
// MouseMove events are used for hover state instead of per-frame polling.

#include <input/InputTypes.h>
#include <math/Types.h>

namespace UI {

struct InputEvent {
	enum class Type {
		MouseDown,	// Mouse button pressed
		MouseUp,	// Mouse button released
		MouseMove,	// Mouse position changed (for hover states)
		Scroll		// Mouse scroll wheel
		// Note: Key events are handled by FocusManager, not this system
	};

	Type type{Type::MouseMove};
	Foundation::Vec2 position{0.0F, 0.0F};	  // Screen coordinates
	engine::MouseButton button{};			  // For MouseDown/MouseUp
	float scrollDelta{0.0F};				  // For Scroll (positive = up)
	int modifiers{0};						  // GLFW modifier flags (shift, ctrl, alt)

	// Propagation control
	bool consumed{false};

	/// Mark this event as consumed, stopping further propagation
	void consume() { consumed = true; }

	/// Check if this event has been consumed by a component
	[[nodiscard]] bool isConsumed() const { return consumed; }

	// Factory methods for clearer construction
	static InputEvent mouseDown(Foundation::Vec2 pos, engine::MouseButton btn, int mods = 0);
	static InputEvent mouseUp(Foundation::Vec2 pos, engine::MouseButton btn, int mods = 0);
	static InputEvent mouseMove(Foundation::Vec2 pos);
	static InputEvent scroll(Foundation::Vec2 pos, float delta);
};

} // namespace UI
