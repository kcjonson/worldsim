#include "SkillsTabView.h"
#include "MeterDraw.h"

namespace world_sim {

	void SkillsTabView::onMount() {
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);
		addChild<tabs::EmptyState>(
			{.title = "Skill tracking not yet simulated", .subtitle = "Proficiencies arrive with the skills update."}
		);
	}

	void SkillsTabView::update(const SkillsData& /*data*/) {}

} // namespace world_sim
