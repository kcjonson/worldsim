#include "TasksTabView.h"
#include "TabStyles.h"

#include "scenes/game/ui/components/GlobalTaskRow.h"

#include <components/scroll/ScrollContainer.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>

#include <format>

namespace world_sim {

namespace {

// Layout constants
constexpr float kRowHeight = 36.0F;

} // anonymous namespace

void TasksTabView::create(const Foundation::Rect& contentBounds) {
	using namespace tabs;

	tabWidth = contentBounds.width;
	float headerHeight = kLabelSize + 8.0F;
	float scrollHeight = contentBounds.height - headerHeight;

	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {contentBounds.x, contentBounds.y},
		.size = {contentBounds.width, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "tasks_content"
	});

	// Header - "Known Tasks: N"
	auto headerText = UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Known Tasks: 0",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 4.0F
	});
	headerTextHandle = layout.addChild(std::move(headerText));

	// ScrollContainer with LayoutContainer for task rows
	float scrollWidth = contentBounds.width - 8.0F;
	auto scrollContainer = UI::ScrollContainer(UI::ScrollContainer::Args{
		.size = {scrollWidth, scrollHeight},
		.id = "tasks_scroll"
	});

	// Inner layout for vertical stacking of task rows
	auto taskLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0.0F, 0.0F},
		.size = {scrollWidth - 16.0F, 0.0F},  // Width fixed, height auto
		.direction = UI::Direction::Vertical,
		.id = "tasks_layout"
	});
	taskLayoutHandle = scrollContainer.addChild(std::move(taskLayout));

	scrollContainerHandle = layout.addChild(std::move(scrollContainer));

	layoutHandle = addChild(std::move(layout));
}

void TasksTabView::update(const TasksTabData& data) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	// Update header
	if (auto* text = layout->getChild<UI::Text>(headerTextHandle)) {
		text->text = std::format("Known Tasks: {}", data.totalCount);
	}

	// Rebuild or update task rows based on count change
	if (taskRowHandles.size() != data.tasks.size()) {
		rebuildTaskRows(data);
	} else {
		updateTaskRows(data);
	}
}

void TasksTabView::rebuildTaskRows(const TasksTabData& data) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	auto* scroll = layout->getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scroll == nullptr) return;

	auto* taskLayout = scroll->getChild<UI::LayoutContainer>(taskLayoutHandle);
	if (taskLayout == nullptr) return;

	// Clear existing rows
	taskLayout->clearChildren();
	taskRowHandles.clear();

	// Create new rows using GlobalTaskRow component
	// showKnownBy=false for colonist-specific view (no "Known by: X" line)
	float rowWidth = tabWidth - 32.0F;  // Account for padding and scrollbar
	for (size_t i = 0; i < data.tasks.size(); ++i) {
		auto handle = taskLayout->addChild(GlobalTaskRow(GlobalTaskRow::Args{
			.task = data.tasks[i],
			.width = rowWidth,
			.showKnownBy = false,  // Colonist view doesn't show "Known by"
			.id = std::format("colonist_task_row_{}", i)
		}));
		taskRowHandles.push_back(handle);
	}

	// Update scroll content height
	float contentHeight = static_cast<float>(data.tasks.size()) * kRowHeight;
	scroll->setContentHeight(contentHeight);
}

void TasksTabView::updateTaskRows(const TasksTabData& data) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	auto* scroll = layout->getChild<UI::ScrollContainer>(scrollContainerHandle);
	if (scroll == nullptr) return;

	auto* taskLayout = scroll->getChild<UI::LayoutContainer>(taskLayoutHandle);
	if (taskLayout == nullptr) return;

	// Update each row's data using GlobalTaskRow::setTaskData()
	for (size_t i = 0; i < data.tasks.size() && i < taskRowHandles.size(); ++i) {
		auto* row = taskLayout->getChild<GlobalTaskRow>(taskRowHandles[i]);
		if (row != nullptr) {
			row->setTaskData(data.tasks[i]);
		}
	}
}

} // namespace world_sim
