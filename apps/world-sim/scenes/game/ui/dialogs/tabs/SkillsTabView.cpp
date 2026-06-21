#include "SkillsTabView.h"
#include "MeterDraw.h"

namespace world_sim {

void SkillsTabView::create(const Foundation::Rect& bounds) {
	contentBounds = bounds;
}

void SkillsTabView::update(const SkillsData& /*data*/) {}

void SkillsTabView::render() {
	if (!visible) return;

	const Foundation::Vec2 o = getContentPosition();
	const Foundation::Rect panel{o.x, o.y, contentBounds.width, 64.0F};
	tabs::drawEmptyState(panel, "Skill tracking not yet simulated", "Proficiencies arrive with the skills update.");
}

} // namespace world_sim
