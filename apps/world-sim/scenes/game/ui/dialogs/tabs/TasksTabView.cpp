#include "TasksTabView.h"
#include "MeterDraw.h"

#include <ui/Context.h>
#include <ui/components/Badge.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace world_sim {

	namespace {

		constexpr float		  kRowHeight = 28.0F;
		constexpr float		  kCurrentHeight = 72.0F;
		constexpr float		  kEdge = 3.0F;
		constexpr float		  kCurrentWash = 0.06F;
		constexpr std::size_t kMaxRows = 40; // past these, a "+N more" line

		std::string toLower(std::string text) {
			std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		ui::Tone statusTone(const std::string& status) {
			const std::string lower = toLower(status);
			if (lower.find("block") != std::string::npos) {
				return ui::Tone::Danger;
			}
			if (lower.find("wait") != std::string::npos) {
				return ui::Tone::Warning;
			}
			if (lower.find("progress") != std::string::npos || lower.find("working") != std::string::npos) {
				return ui::Tone::Info;
			}
			if (lower.find("avail") != std::string::npos) {
				return ui::Tone::Positive;
			}
			return ui::Tone::Default;
		}

		// A known task: its description over its detail, then its distance and status badge at the right.
		class KnownTaskRow : public ui::Component {
		  public:
			struct Args {
				UI_COMMON_ARGS
			};

			[[nodiscard]] const char* typeName() const override { return "KnownTaskRow"; }

			void setTask(const adapters::GlobalTaskDisplayData& task) {
				description.get()->setText(task.description);
				detail.get()->setText(task.statusDetail);
				detail.get()->setVisible(!task.statusDetail.empty());
				distance.get()->setText(task.distance);
				distance.get()->setVisible(!task.distance.empty());
				badge.get()->setLabel(task.status.empty() ? "Available" : task.status);
				badge.get()->setTone(statusTone(task.status));
			}

		  protected:
			void onMount() override {
				const ui::Theme& theme = getContext()->getTheme();
				ui::Layout		 row = getLayout();
				row.display = ui::Display::Flex;
				row.gap = theme.get(tokens::spacing::space2);
				row.alignItems = ui::AlignItems::Start;
				setLayout(row);
				setMinSize({ui::kAuto, kRowHeight});
				ui::Ref<ui::Component> text = addChild<ui::Component>({
					.size = {0.0F, ui::kAuto},
					.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 1.0F, .flexGrow = 1.0F},
				});
				description = text.addChild<ui::Text>(
					{.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text})}
				);
				detail = text.addChild<ui::Text>(
					{.style = world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint})}
				);
				distance = addChild<ui::Text>(
					{.style = world_sim::text(
						 theme,
						 {.family = ui::tokens::font::family::mono, .size = tokens::fontSize::fs2xs, .color = tokens::color::dataBright}
					 )}
				);
				badge = addChild<ui::Badge>({});
			}

		  private:
			ui::Ref<ui::Text>  description;
			ui::Ref<ui::Text>  detail;
			ui::Ref<ui::Text>  distance;
			ui::Ref<ui::Badge> badge;
		};

	} // namespace

	void TasksTabView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 layout = getLayout();
		layout.display = ui::Display::Flex;
		layout.flexDirection = ui::FlexDirection::Column;
		layout.gap = theme.get(tokens::spacing::space2);
		setLayout(layout);

		Foundation::Color wash = theme.get(tokens::color::accent);
		wash.a *= kCurrentWash;
		const float			   pad = theme.get(tokens::spacing::space3);
		ui::Ref<ui::Rectangle> panel = addChild<ui::Rectangle>({
			.minSize = {ui::kAuto, kCurrentHeight},
			.margin = {0.0F, 0.0F, theme.get(tokens::spacing::space2), 0.0F},
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 6.0F, .padding = {pad, pad}},
			.style = {
				.backgroundColor = wash,
				.borderColor = theme.get(tokens::color::lineEdge),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rMd),
			},
		});
		panel.addChild<ui::Rectangle>({
			.size = {kEdge, ui::kAuto},
			.layout = {.position = ui::Position::Absolute, .inset = {.top = 0.0F, .bottom = 0.0F, .left = 0.0F}},
			.style = {.backgroundColor = theme.get(tokens::color::accent)},
		});
		panel.addChild<ui::Text>({.style = tabs::kickerText(theme, tokens::color::accent), .text = "Currently"});
		current = panel.addChild<ui::Text>(
			{.style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::textBright})}
		);
		progress = panel.addChild<ui::ProgressBar>({.tone = ui::Tone::Accent, .valueText = " ", .controlSize = ui::ControlSize::Sm});

		addChild<tabs::KickerDivider>({.label = "Known work"});
		noWork = addChild<ui::Text>(
			{.style = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textFaint}),
			 .text = "No known work nearby."}
		);
		taskList = addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column}});
		more = addChild<ui::Text>({.style = world_sim::text(theme, {.size = tokens::fontSize::fs2xs, .color = tokens::color::textFaint})});
		update({});
	}

	void TasksTabView::update(const TasksTabData& data) {
		if (current.get() == nullptr) {
			return;
		}
		current.get()->setText(data.currentTask.empty() ? "Idle" : data.currentTask);
		const bool acting = data.currentProgress >= 0.0F;
		progress.get()->setVisible(acting);
		if (acting) {
			progress.get()->setValue(data.currentProgress);
			progress.get()->setValueText(std::to_string(static_cast<int>(data.currentProgress * 100.0F)) + "%");
		}

		noWork.get()->setVisible(data.tasks.empty());
		const std::size_t								   shown = std::min(data.tasks.size(), kMaxRows);
		const std::vector<adapters::GlobalTaskDisplayData> rows(
			data.tasks.begin(), data.tasks.begin() + static_cast<std::ptrdiff_t>(shown)
		);
		ui::Component& list = *taskList.get();
		list.reconcileChildren(
			rows,
			[](const adapters::GlobalTaskDisplayData& task) { return std::to_string(task.id); },
			[&list](const adapters::GlobalTaskDisplayData& task) -> ui::Ref<ui::Component> {
				ui::Ref<KnownTaskRow> row = list.addChild<KnownTaskRow>({});
				row.get()->setTask(task);
				return row;
			},
			[](ui::Component& row, const adapters::GlobalTaskDisplayData& task) { static_cast<KnownTaskRow&>(row).setTask(task); }
		);
		more.get()->setVisible(data.tasks.size() > shown);
		more.get()->setText("+" + std::to_string(data.tasks.size() - shown) + " more");
	}

} // namespace world_sim
