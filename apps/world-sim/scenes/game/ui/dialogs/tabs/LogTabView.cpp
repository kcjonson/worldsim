#include "LogTabView.h"

namespace world_sim {

	void LogTabView::onMount() {
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);
		list = addChild<ThoughtLogList>({.id = "dossier_log_list", .onFilter = onFilter, .onShowOlder = onShowOlder});
	}

	void LogTabView::update(const ThoughtLogModel& log) {
		list.get()->show(log);
	}

} // namespace world_sim
