#include "HealthTabView.h"
#include "TabStyles.h"

#include <components/progress/ProgressBar.h>
#include <ecs/components/Needs.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>

#include <sstream>

namespace world_sim {

void HealthTabView::create(const Foundation::Rect& contentBounds) {
	using namespace tabs;

	float columnGap = 16.0F;
	float columnWidth = (contentBounds.width - columnGap) / 2.0F;
	float needBarWidth = columnWidth - 4.0F;
	float needBarHeight = 12.0F;

	// Outer horizontal container for two columns
	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {contentBounds.x, contentBounds.y},
		.size = {contentBounds.width, contentBounds.height},
		.direction = UI::Direction::Horizontal,
		.id = "health_content"
	});

	// LEFT COLUMN: Mood + Needs + Modifiers
	auto leftColumn = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {columnWidth, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "health_left"
	});

	// Mood header - store handle for dynamic updates
	auto moodHeader = UI::Text(UI::Text::Args{
		.height = kTitleSize,
		.text = "Mood: -- (Unknown)",
		.style = {.color = titleColor(), .fontSize = kTitleSize},
		.margin = 2.0F
	});
	moodHeaderHandle = leftColumn.addChild(std::move(moodHeader));

	// Needs section header
	leftColumn.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Needs",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 4.0F
	}));

	// Need bars - store handles for dynamic updates
	for (size_t i = 0; i < static_cast<size_t>(ecs::NeedType::Count); ++i) {
		auto bar = UI::ProgressBar(UI::ProgressBar::Args{
			.size = {needBarWidth, needBarHeight},
			.value = 1.0F,
			.fillColor = UI::Theme::Colors::statusActive,
			.label = ecs::kNeedLabels[i],
			.labelWidth = 50.0F,
			.margin = 1.0F
		});
		needBarHandles[i] = leftColumn.addChild(std::move(bar));
	}

	// Mood modifiers section
	leftColumn.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Mood Modifiers",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 6.0F
	}));

	leftColumn.addChild(UI::Text(UI::Text::Args{
		.height = kSmallSize,
		.text = "No active modifiers",
		.style = {.color = mutedColor(), .fontSize = kSmallSize},
		.margin = 2.0F
	}));

	leftColumnHandle = layout.addChild(std::move(leftColumn));

	// RIGHT COLUMN: Body & Ailments
	auto rightColumn = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {columnWidth, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "health_right"
	});

	rightColumn.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Body & Ailments",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 4.0F
	}));

	rightColumn.addChild(UI::Text(UI::Text::Args{
		.height = kSmallSize,
		.text = "No ailments",
		.style = {.color = mutedColor(), .fontSize = kSmallSize},
		.margin = 2.0F
	}));

	// Body part placeholders
	const char* bodyParts[] = {"Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg"};
	for (const char* part : bodyParts) {
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = std::string(part) + ": Healthy",
			.style = {.color = bodyColor(), .fontSize = kSmallSize},
			.margin = 1.0F
		}));
	}

	layout.addChild(std::move(rightColumn));

	layoutHandle = addChild(std::move(layout));
}

void HealthTabView::update(const HealthData& health) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	// Get left column using stored handle
	auto* leftColumn = layout->getChild<UI::LayoutContainer>(leftColumnHandle);
	if (leftColumn == nullptr) return;

	// Update mood header using stored handle
	if (auto* text = leftColumn->getChild<UI::Text>(moodHeaderHandle)) {
		std::ostringstream ss;
		ss << "Mood: " << static_cast<int>(health.mood) << "% (" << health.moodLabel << ")";
		text->text = ss.str();
	}

	// Update need bars using stored handles
	for (size_t i = 0; i < static_cast<size_t>(ecs::NeedType::Count); ++i) {
		if (auto* bar = leftColumn->getChild<UI::ProgressBar>(needBarHandles[i])) {
			bar->setValue(health.needValues[i] / 100.0F);

			// Color based on status
			if (health.isCritical[i]) {
				bar->setFillColor({0.9F, 0.2F, 0.2F, 1.0F}); // Red
			} else if (health.needsAttention[i]) {
				bar->setFillColor({0.9F, 0.7F, 0.2F, 1.0F}); // Yellow
			} else {
				bar->setFillColor({0.2F, 0.8F, 0.4F, 1.0F}); // Green
			}
		}
	}
}

} // namespace world_sim
