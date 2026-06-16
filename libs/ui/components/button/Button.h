#pragma once

#include "components/button/ButtonStyle.h"
#include "components/icon/Icon.h"
#include "component/Component.h"
#include "focus/FocusableBase.h"
#include "math/Types.h"
#include <functional>
#include <memory>
#include <string>

// Button Component
//
// Interactive UI button with state management and event handling. Renders the
// Salvage look: a variant-tinted rounded background (primary carries an
// accent gradient), a per-variant border, and a centered uppercase label in the
// display font. State overlays cover hover, pressed, focused, and disabled.
// Extends Component (for child management) and IFocusable (for keyboard focus).

namespace UI {

// Button component - extends Component and uses FocusableBase for auto-registration
class Button : public Component, public FocusableBase<Button> {
  public:
	// Visual variant. Primary/Secondary/Ghost/Danger/Data are the Salvage
	// variants; Custom defers to a caller-supplied ButtonAppearance.
	enum class Type { Primary, Secondary, Custom, Ghost, Danger, Data };

	// Visual interaction state (mouse-driven)
	enum class State { Normal, Hover, Pressed };

	// Constructor arguments struct (C++20 designated initializers)
	struct Args {
		std::string			  label;						 // Text label (can be empty for icon-only)
		Foundation::Vec2	  position{0.0F, 0.0F};
		Foundation::Vec2	  size{120.0F, 40.0F};
		Type				  type = Type::Primary;
		ButtonAppearance*	  customAppearance = nullptr;	 // Only used if type == Custom
		bool				  disabled = false;
		std::function<void()> onClick = nullptr;
		const char*			  id = nullptr;
		int					  tabIndex = -1;				 // Tab order (-1 for auto-assign)
		float				  margin{0.0F};
		std::string			  iconPath;						 // Optional SVG icon path
		float				  iconSize{16.0F};				 // Icon size (default 16px)
	};

	// --- Public Members ---

	// Geometry: position and size inherited from Component base class
	std::string label;

	// State
	State state{State::Normal};
	bool  disabled{false};
	bool  focused{false};
	Type  type{Type::Primary};

	// Custom appearance (only consulted when type == Custom)
	ButtonAppearance appearance;

	// Callback
	std::function<void()> onClick;

	// Properties: visible inherited from IComponent base class
	const char* id = nullptr;

	// --- Public Methods ---

	explicit Button(const Args& args);
	~Button() override = default;

	// Disable copy (Button owns arena memory and is registered with FocusManager)
	Button(const Button&) = delete;
	Button& operator=(const Button&) = delete;

	// Allow move (FocusableBase handles FocusManager re-registration)
	Button(Button&&) noexcept = default;
	Button& operator=(Button&&) noexcept = default;

	// ILayer implementation (overrides Component)
	void update(float deltaTime) override;
	void render() override;

	// IComponent event handling
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// Position override to reposition the icon
	void setPosition(float x, float y) override;

	// Update the displayed label.
	void setLabel(const std::string& newLabel);

	// The text actually rendered (equals `label`).
	[[nodiscard]] const std::string& renderedLabel() const { return label; }

	// State management
	void setFocused(bool newFocused) { focused = newFocused; }
	void setDisabled(bool newDisabled) { disabled = newDisabled; }
	bool isFocused() const { return focused; }
	bool isDisabled() const { return disabled; }

	// IFocusable implementation
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

	// Geometry queries
	Foundation::Vec2 getCenter() const {
		Foundation::Vec2 contentPos = getContentPosition();
		return Foundation::Vec2{contentPos.x + size.x * 0.5F, contentPos.y + size.y * 0.5F};
	}

  private:
	// Internal state tracking
	bool mouseOver{false};
	bool mouseDown{false};

	// Optional icon (legacy SVG-path icon)
	std::unique_ptr<Icon> icon;
	float				  iconSize{16.0F};

	void updateIconPosition();
};

} // namespace UI
