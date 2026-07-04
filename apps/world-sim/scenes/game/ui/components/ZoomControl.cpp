#include "ZoomControl.h"

#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

namespace world_sim {

ZoomControl::ZoomControl(const Args& args) {
	position = args.position;
	size = {kTotalWidth, kTotalHeight};

	// Vertical column, top to bottom: + / reset / -.
	zoomInButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomIn,
		.id = "btn_zoom_in",
		.iconPath = "assets/ui/icons/zoom_in.svg",
		.iconSize = kIconSize}));

	zoomResetButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomReset,
		.id = "btn_zoom_reset",
		.iconPath = "assets/ui/icons/zoom_reset.svg",
		.iconSize = kIconSize}));

	zoomOutButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "",  // Icon-only
		.position = {0.0F, 0.0F},
		.size = {kButtonSize, kButtonSize},
		.type = UI::Button::Type::Primary,
		.onClick = args.onZoomOut,
		.id = "btn_zoom_out",
		.iconPath = "assets/ui/icons/zoom_out.svg",
		.iconSize = kIconSize}));

	positionElements();
}

void ZoomControl::setZoomPercent(int percent) {
	zoomPercent = percent;
}

void ZoomControl::setPosition(float x, float y) {
	if (position.x == x && position.y == y) {
		return;
	}
	Component::setPosition(x, y);
	positionElements();
}

void ZoomControl::positionElements() {
	float y = position.y;

	auto place = [&](UI::LayerHandle handle) {
		if (auto* btn = getChild<UI::Button>(handle)) {
			btn->setPosition(position.x, y);
		}
		y += kButtonSize + kSpacing;
	};
	place(zoomInButtonHandle);
	place(zoomResetButtonHandle);
	place(zoomOutButtonHandle);
}

void ZoomControl::render() {
	UI::Component::render();

	// Mono % readout beneath the column.
	const float labelY = position.y + kButtonSize * 3.0F + kSpacing * 3.0F;
	Renderer::Primitives::drawText(Renderer::Primitives::TextArgs{
		.text = std::to_string(zoomPercent) + "%",
		.position = {position.x, labelY},
		.scale = kFontSize / 16.0F,
		.color = UI::text_dim,
		.font = UI::fontMono,
		.hAlign = Foundation::HorizontalAlign::Center,
		.vAlign = Foundation::VerticalAlign::Top,
		.boxWidth = kButtonSize,
		.id = "zoom_percent"});
}

bool ZoomControl::handleEvent(UI::InputEvent& event) {
	// Use dispatchEvent to route to children in z-order
	return dispatchEvent(event);
}

}  // namespace world_sim
