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
		// Position on right side of viewport
		float zoomX = newBounds.x + newBounds.width - kRightMargin - kControlWidth;
		float zoomY = newBounds.y + kTopMargin;
		control->setPosition(zoomX, zoomY);
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
