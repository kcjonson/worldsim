#include "theme/Panel.h"

#include <theme/Tokens.h>

#include <ui/Context.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>

namespace theme {

	namespace {

		namespace t = tokens::theme;

		Foundation::Color accentColor(const ui::Theme& theme, PanelAccent accent) {
			switch (accent) {
				case PanelAccent::Data:
					return theme.get(t::color::data);
				case PanelAccent::None:
					return theme.get(t::color::lineStrong);
				case PanelAccent::Accent:
					break;
			}
			return theme.get(t::color::accent);
		}

	} // namespace

	Panel::Panel(const Args& args)
		: title(args.title),
		  kicker(args.kicker),
		  variant(args.variant),
		  accent(args.accent),
		  corners(args.corners),
		  compact(args.compact),
		  flush(args.flush),
		  glow(args.glow) {}

	ui::StyleId Panel::themeStyle() const {
		return ui::styles::pane.theme;
	}

	void Panel::onMount() {
		const ui::Theme&		theme = getContext()->getTheme();
		const Foundation::Color highlight = accentColor(theme, accent);
		const float				padX = theme.get(compact ? t::spacing::space3 : t::spacing::space4);
		const float				padY = theme.get(compact ? t::spacing::space15 : t::spacing::space3);

		ui::Layout column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		setLayout(column);

		if (!title.empty() || !kicker.empty()) {
			ui::Ref<ui::Component> header = addChild<ui::Component>(
				{.layout = {
					 .display = ui::Display::Flex,
					 .flexDirection = ui::FlexDirection::Column,
					 .gap = theme.get(t::spacing::space05),
					 .padding = {padY, padX},
					 .alignItems = ui::AlignItems::Start,
				 }}
			);
			if (!kicker.empty()) {
				const float size = theme.get(t::fontSize::fs2xs);
				header.addChild<ui::Text>(
					{.style =
						 {.color = highlight,
						  .fontFamily = theme.get(ui::tokens::font::family::mono),
						  .fontSize = size,
						  .letterSpacing = size * theme.get(t::typography::labelSpacing),
						  .textTransform = Foundation::TextTransform::Uppercase},
					 .text = kicker}
				);
			}
			if (!title.empty()) {
				const float size = theme.get(t::fontSize::fsMd);
				header.addChild<ui::Text>(
					{.style =
						 {.color = theme.get(t::color::textBright),
						  .fontFamily = theme.get(ui::tokens::font::family::display),
						  .fontSize = size,
						  .letterSpacing = size * theme.get(t::typography::titleSpacing),
						  .textTransform = Foundation::TextTransform::Uppercase},
					 .text = title}
				);
			}
			addChild<ui::Rectangle>(
				{.size = {ui::percent(100.0F), theme.get(t::border::bw)}, .style = {.backgroundColor = theme.get(t::color::lineHairline)}}
			);
		}

		const float bodyPad = flush ? 0.0F : theme.get(compact ? t::spacing::space15 : t::spacing::space4);
		bodySlot = addChild<ui::Component>({
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Column,
				.gap = theme.get(t::spacing::space2),
				.padding = bodyPad,
				.alignItems = ui::AlignItems::Start,
				.flexGrow = 1.0F
			},
		});

		// Two legs a corner, over the border and the body: decoration, which the pointer passes through.
		if (corners) {
			const float		leg = theme.get(t::spacing::space3);
			const float		thick = theme.get(t::border::bwThick);
			const ui::Style tick{.backgroundColor = highlight};
			const auto		corner = [&](ui::Inset inset) {
				 addChild<ui::Rectangle>(
					 {.size = {leg, thick},
						  .layout = {.position = ui::Position::Absolute, .inset = inset},
						  .style = tick,
						  .pointerEvents = ui::PointerEvents::None}
				 );
				 addChild<ui::Rectangle>(
					 {.size = {thick, leg},
						  .layout = {.position = ui::Position::Absolute, .inset = inset},
						  .style = tick,
						  .pointerEvents = ui::PointerEvents::None}
				 );
			};
			corner({.top = 0.0F, .left = 0.0F});
			corner({.top = 0.0F, .right = 0.0F});
			corner({.bottom = 0.0F, .left = 0.0F});
			corner({.right = 0.0F, .bottom = 0.0F});
		}
		tint();
	}

	void Panel::setStyle(const ui::Style& value) {
		Rectangle::setStyle(value);
		tint();
	}

	// The variant's surface and border, and its shadow, under whatever the panel's own style sets.
	void Panel::tint() {
		if (getContext() == nullptr) {
			return;
		}
		const ui::Theme& theme = getContext()->getTheme();
		const ui::Style& declared = getDeclaredStyle();
		if (!declared.backgroundColor) {
			style.backgroundColor = theme.get(
				variant == PanelVariant::Raised	 ? t::color::bgPanelRaised
				: variant == PanelVariant::Inset ? t::color::bgInset
												 : t::color::bgPanel
			);
		}
		if (!declared.borderColor) {
			style.borderColor = theme.get(variant == PanelVariant::Raised ? t::color::lineEdge : t::color::lineHairline);
		}
		if (!declared.borderWidth) {
			style.borderWidth = theme.get(t::border::bw);
		}
		if (!declared.borderRadius) {
			style.borderRadius = theme.get(t::radius::radiusPanel);
		}
		if (!declared.boxShadow) {
			if (glow.has_value()) {
				style.boxShadow = Foundation::BoxShadow{.color = *glow, .blur = 24.0F, .spread = 2.0F};
			} else if (variant == PanelVariant::Raised) {
				style.boxShadow = Foundation::BoxShadow{.color = {0.0F, 0.0F, 0.0F, 0.5F}, .blur = 24.0F, .offset = {0.0F, 8.0F}};
			} else {
				style.boxShadow.reset();
			}
		}
	}

} // namespace theme
