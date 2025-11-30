#pragma once

#include "component/component.h"
#include "input/input_types.h"

namespace UI {

/**
 * IFocusable
 *
 * Interface for UI components that can receive keyboard focus.
 * Extends ILayer (which extends IComponent) so focusable components
 * participate in the full lifecycle: HandleInput -> Update -> Render.
 *
 * Components implementing this interface can participate in Tab navigation
 * and receive keyboard input events when focused.
 *
 * Usage:
 *   class MyComponent : public Component, public IFocusable {
 *       void onFocusGained() override { focused = true; }
 *       void onFocusLost() override { focused = false; }
 *       void handleKeyInput(...) override { ... }
 *       void handleCharInput(...) override { ... }
 *       bool canReceiveFocus() const override { return enabled; }
 *   };
 */
class IFocusable {
  public:
	virtual ~IFocusable() = default;

	/**
	 * Called when this component receives keyboard focus.
	 * Use this to update visual state (show focus indicator, start cursor blink, etc.)
	 */
	virtual void onFocusGained() = 0;

	/**
	 * Called when this component loses keyboard focus.
	 * Use this to clean up (hide cursor, clear selection, etc.)
	 */
	virtual void onFocusLost() = 0;

	/**
	 * Handle keyboard input when focused.
	 *
	 * @param key - Key pressed (from engine::Key enum)
	 * @param shift - True if Shift modifier is pressed
	 * @param ctrl - True if Ctrl/Cmd modifier is pressed
	 * @param alt - True if Alt/Option modifier is pressed
	 */
	virtual void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) = 0;

	/**
	 * Handle character input when focused.
	 * Used for text editing - receives Unicode codepoints for typed characters.
	 *
	 * @param codepoint - Unicode codepoint of character typed
	 */
	virtual void handleCharInput(char32_t codepoint) = 0;

	/**
	 * Query whether this component can currently receive focus.
	 * Return false to skip this component during Tab navigation.
	 *
	 * @return true if component can receive focus (e.g., enabled=true)
	 */
	virtual bool canReceiveFocus() const = 0;
};

} // namespace UI
