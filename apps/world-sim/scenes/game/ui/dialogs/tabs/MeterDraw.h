#pragma once

// The colonist dossier's shared pieces on salal's ui: a kicker divider (an upper-case mono label with a
// hairline running to the right edge) and the empty-state box (a faint inset box with a title over a
// wrapped subtitle). A meter is a ui::ProgressBar at ControlSize::Sm, its label and value text above the
// track in its tone.

#include "TabStyles.h"

#include <ui/Context.h>
#include <ui/shapes/Rectangle.h>
#include <ui/shapes/Text.h>

#include <string>

namespace world_sim::tabs {

	class KickerDivider : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string label;
		};

		explicit KickerDivider(const Args& args)
			: label(args.label) {}

		[[nodiscard]] const char* typeName() const override { return "KickerDivider"; }

	  protected:
		void onMount() override {
			const ui::Theme& theme = getContext()->getTheme();
			ui::Layout		 row = getLayout();
			row.display = ui::Display::Flex;
			row.gap = theme.get(tokens::spacing::space2);
			row.alignItems = ui::AlignItems::Center;
			setLayout(row);
			addChild<ui::Text>({.style = kickerText(theme, tokens::color::textFaint), .text = label});
			addChild<ui::Rectangle>({
				.size = {ui::kAuto, theme.get(tokens::border::bw)},
				.layout = {.flexGrow = 1.0F},
				.style = {.backgroundColor = theme.get(tokens::color::lineHairline)},
			});
		}

	  private:
		std::string label;
	};

	class EmptyState : public ui::Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string title;
			std::string subtitle;
		};

		explicit EmptyState(const Args& args)
			: title(args.title),
			  subtitle(args.subtitle) {}

		[[nodiscard]] const char* typeName() const override { return "EmptyState"; }

	  protected:
		void onMount() override {
			const ui::Theme&  theme = getContext()->getTheme();
			Foundation::Color fill = theme.get(tokens::color::bgInset);
			fill.a *= 0.5F;
			setStyle({
				.backgroundColor = fill,
				.borderColor = theme.get(tokens::color::lineHairline),
				.borderWidth = theme.get(tokens::border::bw),
				.borderRadius = theme.get(tokens::radius::rMd),
			});
			ui::Layout column = getLayout();
			column.display = ui::Display::Flex;
			column.flexDirection = ui::FlexDirection::Column;
			column.gap = theme.get(tokens::spacing::space1);
			column.padding = theme.get(tokens::spacing::space3);
			setLayout(column);
			addChild<ui::Text>(
				{.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::text}), .text = title}
			);
			addChild<ui::Text>(
				{.style = world_sim::text(theme, {.size = tokens::fontSize::fsXs, .color = tokens::color::textFaint}),
				 .text = subtitle,
				 .wrap = true}
			);
		}

	  private:
		std::string title;
		std::string subtitle;
	};

} // namespace world_sim::tabs
