#include "SocialTabView.h"
#include "TabStyles.h"

#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

namespace world_sim {

void SocialTabView::create(const Foundation::Rect& contentBounds) {
	using namespace tabs;

	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {contentBounds.x, contentBounds.y},
		.size = {contentBounds.width, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "social_content"
	});

	layout.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Relationships",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 4.0F
	}));

	layout.addChild(UI::Text(UI::Text::Args{
		.height = kBodySize,
		.text = "Not yet tracked",
		.style = {.color = mutedColor(), .fontSize = kBodySize},
		.margin = 8.0F
	}));

	layout.addChild(UI::Text(UI::Text::Args{
		.height = kSmallSize,
		.text = "Future: Opinion modifiers, social interactions",
		.style = {.color = mutedColor(), .fontSize = kSmallSize},
		.margin = 2.0F
	}));

	layoutHandle = addChild(std::move(layout));
}

void SocialTabView::update(const SocialData& /*data*/) {
	// Placeholder - no dynamic content yet
}

} // namespace world_sim
