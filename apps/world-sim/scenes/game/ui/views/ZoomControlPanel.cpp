#include "ZoomControlPanel.h"

namespace world_sim {

ZoomControlPanel::ZoomControlPanel(const Args& args) {
	zoomControl = std::make_unique<ZoomControl>(ZoomControl::Args{
		.position = {0.0F, 0.0F},  // Will be positioned in layout()
		.onZoomIn = args.onZoomIn,
		.onZoomOut = args.onZoomOut,
		.onZoomReset = args.onZoomReset,
		.id = "zoom_control"
	});
}

void ZoomControlPanel::layout(const Foundation::Rect& newBounds) {
	viewportBounds = newBounds;

	if (zoomControl) {
		// Position on right side of viewport
		// ZoomControl is 28+4+50+4+28+4+28 = 146px wide
		float zoomX = newBounds.x + newBounds.width - kRightMargin - 146.0F;
		float zoomY = newBounds.y + kTopMargin;
		zoomControl->setPosition({zoomX, zoomY});
	}
}

void ZoomControlPanel::setZoomPercent(int percent) {
	if (zoomControl) {
		zoomControl->setZoomPercent(percent);
	}
}

bool ZoomControlPanel::handleEvent(UI::InputEvent& event) {
	if (zoomControl) {
		return zoomControl->handleEvent(event);
	}
	return false;
}

void ZoomControlPanel::render() {
	if (zoomControl) {
		zoomControl->render();
	}
}

}  // namespace world_sim
