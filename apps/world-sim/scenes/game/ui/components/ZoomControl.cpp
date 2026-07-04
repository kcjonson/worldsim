#include "ZoomControl.h"

#include <sstream>

namespace world_sim {

ZoomControl::ZoomControl(const Args& args) {
	position = args.position;
	size = {kButtonSize * 3.0F + kTextWidth + kSpacing * 3.0F, kButtonSize};

	// Zoom out button (-)
	zoomOutButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomOut,
		.id = "btn_zoom_out",
		.iconPath = "assets/ui/icons/zoom_out.svg",
		.iconSize = kIconSize}));

	// Zoom percentage text (centered between buttons). vAlign stays Top with the
	// centering done in positionElements: a Middle anchor reports the center as
	// the box origin, which reads as an out-of-bounds child in the layout lint.
	// The zIndex marks the (anchor-reported) overlap with the buttons as layered.
	zoomTextHandle = addChild(UI::Text(UI::Text::Args{
		.position = {0.0F, 0.0F},
		.text = "100%",
		.style =
			{
				.color = Foundation::Color::white(),
				.fontSize = kFontSize,
				.hAlign = Foundation::HorizontalAlign::Center,
				.vAlign = Foundation::VerticalAlign::Top,
			},
		.id = "zoom_text",
		.zIndex = 1}));

	// Zoom in button (+)
	zoomInButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomIn,
		.id = "btn_zoom_in",
		.iconPath = "assets/ui/icons/zoom_in.svg",
		.iconSize = kIconSize}));

	// Zoom reset button
	zoomResetButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomReset,
		.id = "btn_zoom_reset",
		.iconPath = "assets/ui/icons/zoom_reset.svg",
		.iconSize = kIconSize}));

	// Position all elements
	positionElements();
}

void ZoomControl::setZoomPercent(int percent) {
	if (zoomPercent != percent) {
		zoomPercent = percent;
		updateZoomText();
	}
}

void ZoomControl::setPosition(float x, float y) {
	if (position.x == x && position.y == y) {
		return;
	}
	Component::setPosition(x, y);
	positionElements();
}

void ZoomControl::positionElements() {
	float x = position.x;
	float y = position.y;

	if (auto* btn = getChild<UI::Button>(zoomOutButtonHandle)) {
		btn->setPosition(x, y);
	}
	x += kButtonSize + kSpacing;

	if (auto* text = getChild<UI::Text>(zoomTextHandle)) {
		text->setPosition(x + kTextWidth * 0.5F, y + (kButtonSize - text->getHeight()) * 0.5F);
	}
	x += kTextWidth + kSpacing;

	if (auto* btn = getChild<UI::Button>(zoomInButtonHandle)) {
		btn->setPosition(x, y);
	}
	x += kButtonSize + kSpacing;

	if (auto* btn = getChild<UI::Button>(zoomResetButtonHandle)) {
		btn->setPosition(x, y);
	}
}

void ZoomControl::updateZoomText() {
	if (auto* text = getChild<UI::Text>(zoomTextHandle)) {
		std::ostringstream oss;
		oss << zoomPercent << "%";
		text->text = oss.str();
	}
}

bool ZoomControl::handleEvent(UI::InputEvent& event) {
	// Use dispatchEvent to route to children in z-order
	return dispatchEvent(event);
}

// render() inherited from Component - automatically renders all children

} // namespace world_sim
