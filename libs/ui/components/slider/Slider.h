#pragma once

#include "component/Component.h"
#include "focus/FocusableBase.h"
#include "graphics/Color.h"
#include "math/Types.h"
#include "shapes/Shapes.h"

#include <functional>
#include <string>

// Slider - Horizontal range input with optional log scale and step snapping.
//
// Draggable handle on a track. Click-on-track jumps. Keyboard Left/Right (fine step),
// Home/End (min/max). Log scale: value = min * pow(max/min, t) where t in [0,1].

namespace UI {

struct SliderStyle {
	Foundation::Color trackColor{0.25F, 0.25F, 0.3F, 1.0F};
	Foundation::Color trackBorderColor{0.35F, 0.35F, 0.4F, 1.0F};
	Foundation::Color fillColor{0.2F, 0.4F, 0.8F, 1.0F};
	Foundation::Color handleColor{0.8F, 0.85F, 1.0F, 1.0F};
	Foundation::Color handleHoverColor{1.0F, 1.0F, 1.0F, 1.0F};
	Foundation::Color handleActiveColor{0.5F, 0.75F, 1.0F, 1.0F};
	Foundation::Color handleDisabledColor{0.5F, 0.5F, 0.5F, 1.0F};
	Foundation::Color focusRingColor{0.4F, 0.7F, 1.0F, 1.0F};
	Foundation::Color labelColor{0.85F, 0.85F, 0.85F, 1.0F};
	float			  labelFontSize{13.0F};
	float			  trackHeight{6.0F};
	float			  handleRadius{8.0F};
	float			  borderWidth{1.0F};
};

class Slider : public Component, public FocusableBase<Slider> {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{200.0F, 36.0F};
		double			 min{0.0};
		double			 max{1.0};
		double			 step{0.0};	 // 0 = continuous
		double			 value{0.0};
		bool			 logScale{false};
		std::string		 label;
		std::function<std::string(double)> valueFormatter;
		std::function<void(double)>		   onChanged;
		const char*						   id = nullptr;
		int								   tabIndex = -1;
		float							   margin{0.0F};
		bool							   disabled{false};
		SliderStyle						   style;
	};

	explicit Slider(const Args& args);
	~Slider() override = default;

	Slider(const Slider&) = delete;
	Slider& operator=(const Slider&) = delete;
	Slider(Slider&&) noexcept = default;
	Slider& operator=(Slider&&) noexcept = default;

	// Value access
	double getValue() const { return value; }
	void   setValue(double newValue);

	// State
	void setDisabled(bool newDisabled) { disabled = newDisabled; }
	bool isDisabled() const { return disabled; }

	// ILayer
	void update(float deltaTime) override;
	void render() override;

	// IComponent
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// IFocusable
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

	// Conversion helpers (exposed for testing)
	double positionToValue(double t) const;
	double valueToPosition(double val) const; // returns t in [0,1]
	double snapToStep(double val) const;

  private:
	double min;
	double max;
	double step;
	double value;
	bool   logScale;

	std::string								label;
	std::function<std::string(double)>		valueFormatter;
	std::function<void(double)>				onChanged;
	const char*								id;
	bool									disabled{false};
	bool									focused{false};
	bool									dragging{false};
	bool									handleHovered{false};
	bool									inCallback{false};
	SliderStyle								style;

	// Computed layout (updated in computeLayout)
	float trackLeft{0.0F};
	float trackRight{0.0F};
	float trackY{0.0F};

	void   computeLayout();
	float  handleX() const;
	void   setValueFromTrackX(float x);
	void   fireChanged();
	double clampValue(double val) const;
};

} // namespace UI
