#include "ZoomControlPanel.h"

namespace world_sim {

	ZoomControlPanel::ZoomControlPanel(const Args& args)
		: onZoomIn(args.onZoomIn),
		  onZoomOut(args.onZoomOut),
		  onZoomReset(args.onZoomReset) {}

	void ZoomControlPanel::onMount() {
		control = addChild<ZoomControl>({.id = "zoom_control", .onZoomIn = onZoomIn, .onZoomOut = onZoomOut, .onZoomReset = onZoomReset});
	}

	void ZoomControlPanel::setZoomPercent(int percent) {
		if (ZoomControl* shown = control.get()) {
			shown->setZoomPercent(percent);
		}
	}

} // namespace world_sim
