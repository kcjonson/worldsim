#include "LogTabView.h"

namespace world_sim {

	void LogTabView::onMount() {
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);
		list = addChild<ThoughtLogList>({.id = "dossier_log_list"});
	}

	void LogTabView::update(const LogData& data) {
		if (shown && data.revision == shownRevision) {
			return;
		}
		shown = true;
		shownRevision = data.revision;
		list.get()->show(data.entries);
	}

} // namespace world_sim
