#pragma once

// ProgressBar - horizontal progress/level bar in the Salvage look.
//
// Draws an inset track, a tone-colored fill (vertical gradient + soft glow), an
// optional bright leading edge, optional segment notches, and either a header
// row (label left, value right, above the track) or an inline overlay (label and
// value drawn inside the bar). Value is normalized 0..1. Tone resolves the fill
// color; Tone::Auto bands by value (red/amber/green). Non-interactive, but a
// Component so it slots into layouts and child pools.

#include "component/Component.h"
#include "theme/Variants.h"

#include <string>

namespace UI {

class ProgressBar : public Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 width{200.0F};
		float			 value{1.0F}; // 0..1 (clamped)
		Tone			 tone{Tone::Auto};
		std::string		 label{};
		std::string		 valueText{};
		Size			 size{Size::Md};
		bool			 segmented{false};	 // overlay notch ticks on the track
		bool			 inlineLabel{false}; // draw label + value inside the bar
		const char*		 id{nullptr};
		float			 margin{0.0F};
	};

	explicit ProgressBar(const Args& args);
	~ProgressBar() override = default;

	ProgressBar(const ProgressBar&) = delete;
	ProgressBar& operator=(const ProgressBar&) = delete;
	ProgressBar(ProgressBar&&) noexcept = default;
	ProgressBar& operator=(ProgressBar&&) noexcept = default;

	// Value control (0..1, clamped)
	void  setValue(float newValue);
	float getValue() const { return value; }

	void setTone(Tone newTone) { tone = newTone; }
	void setLabel(const std::string& newLabel) { label = newLabel; }
	void setValueText(const std::string& newValueText) { valueText = newValueText; }

	void setPosition(Foundation::Vec2 newPos);
	void setPosition(float x, float y) override { setPosition({x, y}); }
	void setWidth(float newWidth);

	void render() override;

  private:
	float		value;
	float		width;
	Tone		tone;
	std::string label;
	std::string valueText;
	Size		sizeVariant;
	bool		segmented;
	bool		inlineLabel;

	// Keep the Component footprint (size) in sync with width + drawn height.
	void syncSize();
};

} // namespace UI
