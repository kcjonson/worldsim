#include "NeedBar.h"

#include <algorithm>

namespace world_sim {

NeedBar::NeedBar(const Args& args) {
	// Select size-specific constants
	bool isCompact = (args.size == NeedBarSize::Compact);
	float barHeight = (args.height > 0.0F) ? args.height
		: (isCompact ? kCompactHeight : kNormalHeight);
	labelWidth = isCompact ? kCompactLabelWidth : kNormalLabelWidth;
	float fontSize = isCompact ? kCompactFontSize : kNormalFontSize;

	height = barHeight;

	// Set base class members
	position = args.position;
	size = {args.width, barHeight};

	// Create ProgressBar as child with label support
	progressBarHandle = addChild(UI::ProgressBar(UI::ProgressBar::Args{
		.position = args.position,
		.size = {args.width, barHeight},
		.value = value / 100.0F, // Convert 0-100 to 0-1
		.fillColor = valueToColor(value),
		.backgroundColor = Foundation::Color(0.2F, 0.2F, 0.25F, 1.0F),
		.borderColor = Foundation::Color(0.3F, 0.3F, 0.35F, 1.0F),
		.borderWidth = 1.0F,
		.label = args.label,
		.labelWidth = labelWidth,
		.labelGap = kBarGap,
		.labelColor = Foundation::Color::white(),
		.labelFontSize = fontSize,
	}));
}

void NeedBar::setValue(float newValue) {
	value = std::clamp(newValue, 0.0F, 100.0F);

	// Update progress bar via handle
	auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle);
	if (progressBar != nullptr) {
		progressBar->setValue(value / 100.0F);		 // Convert 0-100 to 0-1
		progressBar->setFillColor(valueToColor(value)); // Apply need-specific gradient
	}
}

void NeedBar::setLabel(const std::string& newLabel) {
	auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle);
	if (progressBar != nullptr) {
		progressBar->setLabel(newLabel);
	}
}

float NeedBar::getTotalHeight() const {
	return height;
}

void NeedBar::setPosition(Foundation::Vec2 newPos) {
	position = newPos;

	auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle);
	if (progressBar != nullptr) {
		progressBar->setPosition(newPos);
	}
}

void NeedBar::setWidth(float newWidth) {
	size.x = newWidth;

	auto* progressBar = getChild<UI::ProgressBar>(progressBarHandle);
	if (progressBar != nullptr) {
		progressBar->setWidth(newWidth);
	}
}

Foundation::Color NeedBar::valueToColor(float newValue) {
	// Color gradient: red (0%) → yellow (50%) → green (100%)
	// Note: Low values indicate need is depleted (bad), high values indicate satisfied (good)
	// Using HSL-ish interpolation for pleasing gradient

	if (newValue <= 0.0F) {
		return Foundation::Color(0.8F, 0.2F, 0.2F, 1.0F); // Red
	}
	if (newValue >= 100.0F) {
		return Foundation::Color(0.2F, 0.8F, 0.3F, 1.0F); // Green
	}

	// Normalize to 0-1 range
	float t = newValue / 100.0F;

	if (t < 0.5F) {
		// Red to Yellow (0% - 50%)
		float ratio = t * 2.0F; // 0 to 1
		return Foundation::Color(
			0.8F + 0.15F * ratio, // R: 0.8 → 0.95
			0.2F + 0.6F * ratio,  // G: 0.2 → 0.8
			0.2F,				  // B stays low
			1.0F
		);
	}
	// Yellow to Green (50% - 100%)
	float ratio = (t - 0.5F) * 2.0F; // 0 to 1
	return Foundation::Color(
		0.95F - 0.75F * ratio, // R: 0.95 → 0.2
		0.8F,				   // G stays high
		0.2F + 0.1F * ratio,   // B: 0.2 → 0.3
		1.0F
	);
}

} // namespace world_sim
