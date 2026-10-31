#include "TaskListView.h"

#include "scenes/game/ui/views/SectionHeader.h"
#include "scenes/game/ui/views/StatusTextLine.h"
#include "scenes/shared/SalalUi.h"

#include <ecs/components/Action.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/Task.h>

#include <ui/Context.h>
#include <ui/components/Button.h>

#include <cstdint>
#include <format>
#include <string>
#include <unordered_set>
#include <vector>

namespace world_sim {

	namespace {

		constexpr float kPadding = 10.0F;
		constexpr float kTitleFontSize = 14.0F;
		constexpr float kLineSpacing = 3.0F;
		constexpr float kSectionSpacing = 8.0F;
		constexpr float kCloseButtonSize = 16.0F;
		constexpr float kScrollbarReserve = 10.0F;

		// One line of the trace: a section header, or a status line. A key always names the same kind.
		struct Line {
			std::string		   key;
			std::string		   text;
			LineStatus		   status = LineStatus::Available;
			ui::DimensionToken fontSize = tokens::fontSize::fsXs;
			float			   margin = kLineSpacing * 0.5F;
			bool			   header = false;
		};

		LineStatus lineStatusOf(ecs::OptionStatus status) {
			switch (status) {
				case ecs::OptionStatus::Selected:
					return LineStatus::Active;
				case ecs::OptionStatus::Available:
					return LineStatus::Available;
				case ecs::OptionStatus::NoSource:
					return LineStatus::Blocked;
				case ecs::OptionStatus::Satisfied:
					return LineStatus::Idle;
			}
			return LineStatus::Available;
		}

		// Each arbitration tier's name, 1 best, 7 idle.
		const char* tierLabel(int tier) {
			switch (tier) {
				case 1:
					return "Survival";
				case 2:
					return "Critical need";
				case 3:
					return "Forced order";
				case 4:
					return "Work order";
				case 5:
					return "Need";
				case 6:
					return "Opportunistic";
				case 7:
					return "Idle";
				default:
					return "Unknown";
			}
		}

		Line header(std::string key, std::string text) {
			return {
				.key = std::move(key),
				.text = std::move(text),
				.fontSize = tokens::fontSize::fsSm,
				.margin = kSectionSpacing * 0.5F,
				.header = true
			};
		}

		Line status(std::string key, std::string text, LineStatus lineStatus) {
			return {.key = std::move(key), .text = std::move(text), .status = lineStatus};
		}

		std::vector<Line> linesOf(const ecs::World& world, ecs::EntityID colonistId) {
			std::vector<Line>		  lines;
			const ecs::DecisionTrace* trace = world.getComponent<ecs::DecisionTrace>(colonistId);
			if (trace != nullptr && !trace->selectionSummary.empty()) {
				lines.push_back(status("selection_summary", trace->selectionSummary, LineStatus::Active));
			}

			lines.push_back(header("current_header", "Current"));
			if (const auto* task = world.getComponent<ecs::Task>(colonistId)) {
				lines.push_back(
					task->isActive() ? status("current_task", task->reason, LineStatus::Active)
									 : status("current_task", "(No active task)", LineStatus::Idle)
				);
			}
			if (const auto* action = world.getComponent<ecs::Action>(colonistId)) {
				if (action->isActive()) {
					const int progress = static_cast<int>(action->progress() * 100.0F);
					lines.push_back(
						status("current_action", std::format("{} ({}%)", ecs::actionTypeName(action->type), progress), LineStatus::Pending)
					);
				} else {
					lines.push_back(status("current_action", "Idle", LineStatus::Idle));
				}
			}

			lines.push_back(header("queue_header", "Task Queue"));
			if (trace != nullptr) {
				std::unordered_set<std::string> seen;
				for (const ecs::EvaluatedOption& option : trace->options) {
					if (option.status == ecs::OptionStatus::Satisfied) {
						continue;
					}
					// The option's identity, so a row keeps its component while the queue reorders; a repeat
					// takes its position too.
					std::string key = std::format(
						"queue_{}_{}_{}", static_cast<int>(option.taskType), static_cast<int>(option.needType), option.tiebreakId
					);
					if (!seen.insert(key).second) {
						key += std::format("_{}", lines.size());
					}
					lines.push_back(status(
						key,
						std::format(
							"[T{} {}] {} - score {}", option.tier, tierLabel(option.tier), option.reason, static_cast<int>(option.score)
						),
						lineStatusOf(option.status)
					));
					lines.push_back({
						.key = key + "_detail",
						.text = std::format(
							"  dist {}, skill {}, age {}, hys {}",
							static_cast<int>(option.distanceFactor),
							option.skillBonus,
							option.taskAgeBonus,
							option.hysteresisBonus
						),
						.status = LineStatus::Idle,
						.fontSize = tokens::fontSize::fs2xs,
						.margin = kLineSpacing * 0.25F,
					});
				}
			}
			return lines;
		}

	} // namespace

	TaskListView::TaskListView(const Args& args)
		: onClose(args.onClose),
		  givenMaxSize(args.maxSize) {}

	void TaskListView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		if (getSizeSpec().width.isAuto()) {
			setSize({kWidth, getSizeSpec().height});
		}
		if (givenMaxSize.height.isAuto()) {
			setMaxSize({givenMaxSize.width, kMaxHeight});
		}
		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = kLineSpacing;
		column.padding = kPadding;
		setLayout(column);
		setStyle(
			ui::Style{
				.backgroundColor = theme.get(tokens::color::bgPanelRaised),
				.borderColor = theme.get(tokens::color::lineEdge),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rMd),
			}
				.extend(getDeclaredStyle())
		);

		ui::Ref<ui::Component> top = addChild<ui::Component>({
			.layout = {.display = ui::Display::Flex, .gap = kPadding, .alignItems = ui::AlignItems::Center, .flexShrink = 0.0F},
		});
		ui::Style			   titleStyle = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textBright});
		titleStyle.fontSize = kTitleFontSize;
		title = top.addChild<ui::Text>({.id = "task_list_title", .layout = {.flexGrow = 1.0F}, .style = titleStyle, .text = "Task Queue"});
		top.addChild<ui::Button>({
			.id = "task_list_close",
			.size = {kCloseButtonSize, kCloseButtonSize},
			.style =
				{.backgroundColor = theme.get(tokens::color::bgInset),
				 .borderColor = theme.get(tokens::color::lineEdge),
				 .borderWidth = theme.get(tokens::border::bw),
				 .borderRadius = theme.get(tokens::radius::rSm),
				 .color = theme.get(tokens::color::textDim)},
			.icon = "close",
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this]() {
				if (onClose) {
					onClose();
				}
			},
		});
		list = addChild<ui::Component>({
			.id = "task_list_content",
			.layout =
				{.display = ui::Display::Flex,
				 .flexDirection = ui::FlexDirection::Column,
				 .padding = {0.0F, kScrollbarReserve, 0.0F, 0.0F}},
			.overflow = ui::Overflow::Scroll,
		});
	}

	void TaskListView::update(const ecs::World& world, ecs::EntityID colonistId) {
		float traceTime = -1.0F;
		if (const auto* trace = world.getComponent<ecs::DecisionTrace>(colonistId)) {
			traceTime = trace->lastEvaluationTime;
		}
		if (built && lastColonistId == colonistId && traceTime <= lastEvaluationTime) {
			return;
		}
		lastColonistId = colonistId;
		lastEvaluationTime = traceTime;
		built = true;
		rebuild(world, colonistId);
	}

	void TaskListView::rebuild(const ecs::World& world, ecs::EntityID colonistId) {
		ui::Component* column = list.get();
		if (column == nullptr) {
			return;
		}
		const auto* colonist = world.getComponent<ecs::Colonist>(colonistId);
		title.get()->setText(colonist != nullptr ? colonist->name + " - Tasks" : "Task Queue");
		column->reconcileChildren(
			linesOf(world, colonistId),
			[](const Line& line) { return line.key; },
			[column](const Line& line) -> ui::Ref<ui::Component> {
				if (line.header) {
					return column->addChild<SectionHeader>(
						{.id = line.key, .margin = {line.margin, 0.0F}, .text = line.text, .fontSize = line.fontSize}
					);
				}
				return column->addChild<StatusTextLine>({
					.id = line.key,
					.margin = {line.margin, 0.0F},
					.text = line.text,
					.status = line.status,
					.fontSize = line.fontSize,
					.wrap = true,
				});
			},
			[](ui::Component& shown, const Line& line) {
				if (line.header) {
					static_cast<SectionHeader&>(shown).setText(line.text);
				} else {
					static_cast<StatusTextLine&>(shown).setStatus(line.status, line.text);
				}
			}
		);
	}

} // namespace world_sim
