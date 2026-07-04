#include "ZoomControlPanel.h"

namespace world_sim {

ZoomControlPanel::ZoomControlPanel(const Args& args) {
	zoomControlHandle = addChild(ZoomControl(ZoomControl::Args{
		.position = {0.0F, 0.0F},  // Will be positioned in layout()
		.onZoomIn = args.onZoomIn,
		.onZoomOut = args.onZoomOut,
		.onZoomReset = args.onZoomReset,
		.id = "zoom_control"}));
}

void ZoomControlPanel::layout(const Foundation::Rect& newBounds) {
	Component::layout(newBounds);

	if (auto* control = getChild<ZoomControl>(zoomControlHandle)) {
		// Dock the column in the bottom-right corner
		float zoomX = newBounds.x + newBounds.width - kRightMargin - ZoomControl::kTotalWidth;
		float zoomY = newBounds.y + newBounds.height - kBottomMargin - ZoomControl::kTotalHeight;
		control->setPosition(zoomX, zoomY);
		// The panel's own reported bounds wrap the control (lint reads these)
		position = {zoomX, zoomY};
		size = {ZoomControl::kTotalWidth, ZoomControl::kTotalHeight};
	}
}

void ZoomControlPanel::setZoomPercent(int percent) {
	if (auto* control = getChild<ZoomControl>(zoomControlHandle)) {
		control->setZoomPercent(percent);
	}
}

bool ZoomControlPanel::handleEvent(UI::InputEvent& event) {
	return dispatchEvent(event);
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
