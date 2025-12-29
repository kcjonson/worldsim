#include "BioTabView.h"
#include "TabStyles.h"

#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <sstream>

namespace world_sim {

void BioTabView::create(const Foundation::Rect& contentBounds) {
	using namespace tabs;

	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {contentBounds.x, contentBounds.y},
		.size = {contentBounds.width, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "bio_content"
	});

	// Name (title size)
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kTitleSize,
		.text = "--",
		.style = {.color = titleColor(), .fontSize = kTitleSize},
		.margin = 2.0F
	}));

	// Age
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kBodySize,
		.text = "Age: --",
		.style = {.color = bodyColor(), .fontSize = kBodySize},
		.margin = 2.0F
	}));

	// Mood
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kBodySize,
		.text = "Mood: --",
		.style = {.color = bodyColor(), .fontSize = kBodySize},
		.margin = 2.0F
	}));

	// Current task
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kBodySize,
		.text = "Current: Idle",
		.style = {.color = bodyColor(), .fontSize = kBodySize},
		.margin = 2.0F
	}));

	// Section: Traits
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Traits",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 6.0F
	}));

	layout.addChild(UI::Text(UI::Text::Args{
		.height = kSmallSize,
		.text = "None defined",
		.style = {.color = mutedColor(), .fontSize = kSmallSize},
		.margin = 2.0F
	}));

	// Section: Background
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Background",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 6.0F
	}));

	layout.addChild(UI::Text(UI::Text::Args{
		.height = kSmallSize,
		.text = "Not available",
		.style = {.color = mutedColor(), .fontSize = kSmallSize},
		.margin = 2.0F
	}));

	layoutHandle = addChild(std::move(layout));
}

void BioTabView::update(const BioData& bio) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	const auto& children = layout->getChildren();
	size_t idx = 0;

	// Name (idx 0)
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			text->text = bio.name;
		}
		++idx;
	}

	// Age (idx 1)
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			text->text = "Age: " + bio.age;
		}
		++idx;
	}

	// Mood (idx 2)
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			std::ostringstream ss;
			ss << "Mood: " << static_cast<int>(bio.mood) << "% (" << bio.moodLabel << ")";
			text->text = ss.str();
		}
		++idx;
	}

	// Current task (idx 3)
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			text->text = "Current: " + bio.currentTask;
		}
		++idx;
	}

	// Traits header (idx 4) - skip
	// Traits text (idx 5) - skip for now
	// Background header (idx 6) - skip
	// Background text (idx 7) - skip for now
}

} // namespace world_sim
