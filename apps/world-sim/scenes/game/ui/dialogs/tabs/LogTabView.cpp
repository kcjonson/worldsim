#include "LogTabView.h"
#include "MeterDraw.h"

namespace world_sim {

void LogTabView::create(const Foundation::Rect& bounds) {
	contentBounds = bounds;
}

void LogTabView::update(const LogData& /*data*/) {}

void LogTabView::render() {
	if (!visible) return;

	const Foundation::Vec2 o = getContentPosition();
	const Foundation::Rect panel{o.x, o.y, contentBounds.width, 64.0F};
	tabs::drawEmptyState(panel, "No activity log yet", "A chronological log arrives with the events update.");
}

} // namespace world_sim
