#include "GlobalTaskListView.h"

#include "scenes/game/ui/components/GlobalTaskRow.h"

#include <primitives/Primitives.h>
#include <theme/Theme.h>

#include <format>

namespace world_sim {

namespace {
// Chevron icon size and positioning
constexpr float kChevronSize = 12.0F;
constexpr float kChevronRightPadding = 8.0F;
} // namespace

GlobalTaskListView::GlobalTaskListView(const Args& args)
	: panelWidth(args.width) {

	// Create header button (chevron icon is separate)
	headerButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "Tasks (0)",
		.position = {0.0F, 0.0F},
		.size = {panelWidth, kCollapsedHeight},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { toggle(); },
		.id = "tasks_header"
	}));

	// Create chevron icon (down arrow when collapsed, up when expanded)
	chevronHandle = addChild(UI::Icon(UI::Icon::Args{
		.position = {0.0F, 0.0F},  // Will be updated in updateLayout
		.size = kChevronSize,
		.svgPath = "assets/ui/icons/chevron_down.svg",
		.tint = Foundation::Color::white(),
		.id = "tasks_chevron"
	}));

	// Create content background (only visible when expanded)
	contentBackgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
		.position = {0.0F, kHeaderHeight},
		.size = {panelWidth, kExpandedMaxHeight - kHeaderHeight},
		.style = {
			.fill = UI::Theme::Colors::panelBackground,
			.border = Foundation::BorderStyle{
				.color = UI::Theme::Colors::panelBorder,
				.width = 1.0F
			}
		},
		.id = "tasks_content_bg",
		.visible = false
	}));

	// Create scroll container
	auto scrollContainer = UI::ScrollContainer(UI::ScrollContainer::Args{
		.position = {kPadding, kHeaderHeight + kPadding},
		.size = {panelWidth - kPadding * 2, kExpandedMaxHeight - kHeaderHeight - kPadding * 2},
		.id = "tasks_scroll"
	});
	scrollContainer.visible = false;

	// Create layout container inside scroll
	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {panelWidth - kPadding * 2 - 16.0F, 0.0F},  // 16 for scrollbar
		.direction = UI::Direction::Vertical,
		.id = "tasks_layout"
	});
	layoutHandle = scrollContainer.addChild(std::move(layout));

	scrollContainerHandle = addChild(std::move(scrollContainer));
}

void GlobalTaskListView::setAnchorPosition(float x, float y) {
	// Anchor is top-right, so offset by panel width
	position = {x - panelWidth, y};
	updateLayout();
}

void GlobalTaskListView::toggle() {
	expanded = !expanded;
	updateHeaderText();
	updateChevron();

	auto* contentBg = getChild<UI::Rectangle>(contentBackgroundHandle);
	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);

	if (contentBg) {
		contentBg->visible = expanded;
	}
	if (scroll) {
		scroll->visible = expanded;
	}
}

void GlobalTaskListView::updateHeaderText() {
	auto* header = getChild<UI::Button>(headerButtonHandle);
	if (header) {
		header->label = std::format("Tasks ({})", cachedTaskCount);
	}
}

void GlobalTaskListView::updateChevron() {
	auto* chevron = getChild<UI::Icon>(chevronHandle);
	if (chevron) {
		// Up arrow when expanded (click to collapse), down arrow when collapsed (click to expand)
		std::string path = expanded ? "assets/ui/icons/chevron_up.svg" : "assets/ui/icons/chevron_down.svg";
		chevron->setSvgPath(path);
	}
}

void GlobalTaskListView::updateLayout() {
	// Position header at top
	auto* header = getChild<UI::Button>(headerButtonHandle);
	if (header) {
		header->setPosition(position.x, position.y);
	}

	// Position chevron on the right side of the header button, vertically centered
	auto* chevron = getChild<UI::Icon>(chevronHandle);
	if (chevron) {
		float chevronX = position.x + panelWidth - kChevronSize - kChevronRightPadding;
		float chevronY = position.y + (kCollapsedHeight - kChevronSize) / 2.0F;
		chevron->setPosition(chevronX, chevronY);
	}

	// Position content background
	auto* contentBg = getChild<UI::Rectangle>(contentBackgroundHandle);
	if (contentBg) {
		contentBg->setPosition(position.x, position.y + kHeaderHeight);
	}

	// Position scroll container
	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scroll) {
		scroll->setPosition(position.x + kPadding, position.y + kHeaderHeight + kPadding);
	}

	// Update overall size
	size = {panelWidth, expanded ? kExpandedMaxHeight : kCollapsedHeight};
}

Foundation::Rect GlobalTaskListView::getBounds() const {
	return {position.x, position.y, panelWidth, expanded ? kExpandedMaxHeight : kCollapsedHeight};
}

void GlobalTaskListView::setTaskCount(size_t count) {
	if (cachedTaskCount != count) {
		cachedTaskCount = count;
		updateHeaderText();
	}
}

void GlobalTaskListView::setTasks(const std::vector<adapters::GlobalTaskDisplayData>& tasks) {
	setTaskCount(tasks.size());

	// Get scroll container
	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (!scroll) return;

	auto* layout = scroll->getChild<UI::LayoutContainer>(layoutHandle);
	if (!layout) return;

	// Always rebuild for now (can optimize later with change detection)
	if (taskRowHandles.size() != tasks.size()) {
		rebuildContent(tasks);
	} else {
		// Update existing rows
		for (size_t i = 0; i < tasks.size() && i < taskRowHandles.size(); ++i) {
			auto* row = layout->getChild<GlobalTaskRow>(taskRowHandles[i]);
			if (row) {
				row->setTaskData(tasks[i]);
			}
		}
	}
}

void GlobalTaskListView::rebuildContent(const std::vector<adapters::GlobalTaskDisplayData>& tasks) {
	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (!scroll) return;

	auto* layout = scroll->getChild<UI::LayoutContainer>(layoutHandle);
	if (!layout) return;

	// Clear existing rows
	layout->clearChildren();
	taskRowHandles.clear();

	// Add new rows
	float rowWidth = panelWidth - kPadding * 2 - 16.0F;
	for (size_t i = 0; i < tasks.size(); ++i) {
		auto handle = layout->addChild(GlobalTaskRow(GlobalTaskRow::Args{
			.task = tasks[i],
			.width = rowWidth,
			.showKnownBy = true,
			.id = std::format("task_row_{}", i)
		}));
		taskRowHandles.push_back(handle);
	}

	// Update scroll content height
	float contentHeight = static_cast<float>(tasks.size()) * kRowHeight;
	scroll->setContentHeight(contentHeight);
}

void GlobalTaskListView::update(float deltaTime) {
	auto* scroll = getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scroll) {
		scroll->update(deltaTime);
	}
}

bool GlobalTaskListView::handleEvent(UI::InputEvent& event) {
	// Use Component's dispatchEvent to properly route to children
	// This handles scroll wheel events correctly for Apple Mouse
	return dispatchEvent(event);
}

// render() inherited from Component - automatically renders all children

} // namespace world_sim
