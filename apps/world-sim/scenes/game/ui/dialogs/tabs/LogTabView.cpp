#include "LogTabView.h"
#include "MeterDraw.h"

namespace world_sim {

	void LogTabView::onMount() {
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);
		addChild<tabs::EmptyState>({.title = "No activity log yet", .subtitle = "A chronological log arrives with the events update."});
	}

	void LogTabView::update(const LogData& /*data*/) {}

} // namespace world_sim
