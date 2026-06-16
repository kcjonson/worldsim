#include "design-system/Panel.h"

#include "design-system/Variants.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"

#include <algorithm>
#include <utility>

namespace UI::DS {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		Foundation::Color surfaceFill(PanelVariant variant) {
			switch (variant) {
				case PanelVariant::Raised:
					return bg_panel_raised;
				case PanelVariant::Inset:
					return bg_inset;
				case PanelVariant::Panel:
					break;
			}
			return bg_panel;
		}

		// Raised surfaces lift the border to the brighter edge tint; panel and inset
		// keep the hairline.
		Foundation::Color borderColor(PanelVariant variant) {
			return variant == PanelVariant::Raised ? line_edge : line_hairline;
		}

		Foundation::Color accentColor(PanelAccent panelAccent) {
			switch (panelAccent) {
				case PanelAccent::Data:
					return data;
				case PanelAccent::None:
					return line_strong;
				case PanelAccent::Accent:
					break;
			}
			return UI::DS::accent;
		}

		// Vertical/horizontal header and body padding by density mode.
		float padX(bool compact) { return compact ? space_3 : space_4; }
		float padY(bool compact) { return compact ? space_1_5 : space_3; }

	} // namespace

	Panel::Panel(Args panelArgs)
		: args(std::move(panelArgs)) {}

	bool Panel::hasHeader() const { return !args.title.empty() || !args.kicker.empty(); }

	float Panel::headerHeight() const {
		if (!hasHeader()) {
			return 0.0F;
		}

		float textBlock = 0.0F;
		if (!args.kicker.empty()) {
			textBlock += fs_2xs;
		}
		if (!args.title.empty()) {
			if (!args.kicker.empty()) {
				textBlock += space_0_5; // gap between kicker and title
			}
			textBlock += fs_md;
		}

		return (padY(args.compact) * 2.0F) + textBlock;
	}

	void Panel::render() const {
		using Renderer::Primitives::drawLine;
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Rect bounds{args.position, args.size};

		// Background surface with a 1px border that sits inside the bounds so the
		// corner brackets can straddle the outer edge cleanly.
		drawRect({.bounds = bounds,
				  .style = {.fill = surfaceFill(args.variant),
							.border = Foundation::BorderStyle{
								.color = borderColor(args.variant),
								.width = bw,
								.cornerRadius = r_sm,
								.position = Foundation::BorderPosition::Inside,
							}},
				  .id = "ds_panel"});

		// Header: hairline divider, kicker eyebrow, title.
		if (hasHeader()) {
			const float originX = bounds.x + padX(args.compact);
			float		cursorY = bounds.y + padY(args.compact);

			if (!args.kicker.empty()) {
				drawText({.text = args.kicker,
						  .position = {originX, cursorY},
						  .scale = textScale(fs_2xs),
						  .color = accentColor(args.accent),
						  .font = fontMono,
						  .id = "ds_panel_kicker"});
				cursorY += fs_2xs + space_0_5;
			}

			if (!args.title.empty()) {
				drawText({.text = args.title,
						  .position = {originX, cursorY},
						  .scale = textScale(fs_md),
						  .color = text_bright,
						  .font = fontDisplay,
						  .id = "ds_panel_title"});
			}

			const float dividerY = bounds.y + headerHeight();
			drawLine({.start = {bounds.x, dividerY},
					  .end = {bounds.x + bounds.width, dividerY},
					  .style = {.color = line_hairline, .width = bw}});
		}

		// L-bracket corner ticks: two short legs per corner straddling the border.
		if (args.corners) {
			const Foundation::Color tick = accentColor(args.accent);
			const float				leg = space_3; // ~12px legs
			const float				th = bw_thick; // 2px thickness
			const float				ox = bounds.x;
			const float				oy = bounds.y;
			const float				rx = bounds.x + bounds.width;
			const float				by = bounds.y + bounds.height;

			const auto fill = [&](Foundation::Rect r) { drawRect({.bounds = r, .style = {.fill = tick}}); };

			// Top-left
			fill({ox, oy, leg, th});
			fill({ox, oy, th, leg});
			// Top-right
			fill({rx - leg, oy, leg, th});
			fill({rx - th, oy, th, leg});
			// Bottom-left
			fill({ox, by - th, leg, th});
			fill({ox, by - leg, th, leg});
			// Bottom-right
			fill({rx - leg, by - th, leg, th});
			fill({rx - th, by - leg, th, leg});
		}
	}

	Foundation::Rect Panel::bodyBounds() const {
		const float pad = args.flush ? 0.0F : (args.compact ? space_1_5 : space_4);
		const float top = args.position.y + headerHeight() + pad;
		const float left = args.position.x + pad;
		const float width = std::max(0.0F, args.size.x - (pad * 2.0F));
		const float height = std::max(0.0F, (args.size.y - headerHeight()) - (pad * 2.0F));
		return {left, top, width, height};
	}

} // namespace UI::DS
