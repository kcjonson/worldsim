#include "GlobalTaskListView.h"

#include "scenes/game/ui/Look.h"
#include "scenes/game/ui/components/GlobalTaskRow.h"

#include <ui/Context.h>

#include <format>
#include <string>

namespace world_sim {

	namespace {
		constexpr float kHeaderHeight = 28.0F;
		constexpr float kExpandedHeight = 320.0F; // header and content
		constexpr float kPadding = 8.0F;
		constexpr float kChevronSize = 12.0F;
		constexpr float kChevronRightPadding = 8.0F;
		constexpr float kScrollbarReserve = 16.0F;
	} // namespace

	void GlobalTaskListView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);

		header = addChild<ui::Button>({
			.id = "tasks_header",
			.size = {ui::percent(100.0F), kHeaderHeight},
			.label = "Tasks (0)",
			.variant = ui::ButtonVariant::Secondary,
			.onPress = [this]() { toggle(); },
		});
		chevron = header.addChild<ui::Icon>({
			.id = "tasks_chevron",
			.size = {kChevronSize, kChevronSize},
			.layout =
				{.position = ui::Position::Absolute,
				 .inset = {.top = (kHeaderHeight - kChevronSize) * 0.5F, .right = kChevronRightPadding}},
			.style = {.color = theme.get(look::t::color::textBright)},
			.name = "chevron_down",
		});

		// The box keeps its height while the list is short or empty; the list scrolls inside it.
		content = addChild<ui::Rectangle>({
			.id = "tasks_content",
			.visible = false,
			.size = {ui::percent(100.0F), kExpandedHeight - kHeaderHeight},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .padding = kPadding},
			.style = {
				.backgroundColor = theme.get(look::t::color::bgPanel),
				.borderColor = theme.get(look::t::color::lineEdge),
				.borderWidth = theme.get(look::t::border::bw),
				.borderRadius = theme.get(look::t::radius::rMd)
			},
		});
		list = content.addChild<ui::Component>({
			.id = "tasks_scroll",
			.layout =
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .padding = {0.0F, kScrollbarReserve, 0.0F, 0.0F},
				 .flexGrow = 1.0F},
			.overflow = ui::Overflow::Scroll,
		});
		show();
	}

	void GlobalTaskListView::setTaskCount(std::size_t count) {
		if (taskCount != count) {
			taskCount = count;
			show();
		}
	}

	void GlobalTaskListView::setTasks(const std::vector<adapters::GlobalTaskDisplayData>& tasks) {
		setTaskCount(tasks.size());
		ui::Component* column = list.get();
		if (column == nullptr) {
			return;
		}
		column->reconcileChildren(
			tasks,
			[](const adapters::GlobalTaskDisplayData& task) { return std::to_string(task.id); },
			[column](const adapters::GlobalTaskDisplayData& task) -> ui::Ref<ui::Component> {
				return column->addChild<GlobalTaskRow>({.id = std::format("task_row_{}", task.id), .task = task, .showKnownBy = true});
			},
			[](ui::Component& shown, const adapters::GlobalTaskDisplayData& task) { static_cast<GlobalTaskRow&>(shown).setTaskData(task); }
		);
	}

	void GlobalTaskListView::toggle() {
		expanded = !expanded;
		show();
	}

	void GlobalTaskListView::show() {
		if (content.get() == nullptr) {
			return;
		}
		header.get()->setLabel(std::format("Tasks ({})", taskCount));
		content.get()->setVisible(expanded);
		// Up while expanded, a press collapses it; down while collapsed.
		chevron.get()->setName(expanded ? "chevron_up" : "chevron_down");
	}

} // namespace world_sim
