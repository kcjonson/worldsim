#include "scenes/shared/LoadFailureView.h"

#include "scenes/shared/SalalUi.h"

#include <theme/Panel.h>
#include <ui/Context.h>
#include <ui/shapes/Text.h>
#include <utils/LogFile.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace world_sim {

	namespace {
		namespace family = ui::tokens::font::family;

		// Wide enough that a message naming a file, its element, and its attribute takes a line or two; well inside
		// the smallest canvas's 1481 ui px.
		constexpr float kWidth = 960.0F;
	} // namespace

	LoadFailureView::LoadFailureView(const Args& args)
		: messages(args.messages),
		  instruction(args.instruction) {}

	void LoadFailureView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		setSize({kWidth, ui::kAuto});
		setLayout({.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column});
		const ui::Ref<::theme::Panel> panel = addChild<::theme::Panel>({
			.id = "load_failure_panel",
			.title = "Loading failed",
			.variant = ::theme::PanelVariant::Inset,
			.accent = ::theme::PanelAccent::None,
		});
		ui::Ref<ui::Component>		  body = panel.get()->body();
		const auto					  line = [&theme](ui::ColorToken color) {
			   return world_sim::text(theme, {.family = family::mono, .size = tokens::fontSize::fsXs, .color = color});
		};

		const std::size_t shown = std::min(messages.size(), kShownMessages);
		for (std::size_t index = 0; index < shown; ++index) {
			body.addChild<ui::Text>(
				{.id = "load_failure_message", .style = line(tokens::color::statusCrit), .text = messages[index], .wrap = true}
			);
		}
		if (messages.size() > shown) {
			body.addChild<ui::Text>(
				{.id = "load_failure_more",
				 .style = line(tokens::color::textDim),
				 .text = std::format("and {} more", messages.size() - shown)}
			);
		}
		const std::filesystem::path logFile = foundation::logFilePath();
		if (!logFile.empty()) {
			body.addChild<ui::Text>(
				{.id = "load_failure_log",
				 .margin = {theme.get(tokens::spacing::space2), 0.0F, 0.0F, 0.0F},
				 .style = line(tokens::color::textFaint),
				 .text = "Full log: " + logFile.string(),
				 .wrap = true}
			);
		}
		body.addChild<ui::Text>(
			{.id = "load_failure_instruction",
			 .margin = {theme.get(tokens::spacing::space2), 0.0F, 0.0F, 0.0F},
			 .style = world_sim::text(
				 theme,
				 {.family = family::mono,
				  .size = tokens::fontSize::fsXs,
				  .color = tokens::color::text,
				  .tracking = tokens::letterSpacing::lsWide,
				  .transform = Foundation::TextTransform::Uppercase}
			 ),
			 .text = instruction}
		);
	}

} // namespace world_sim
