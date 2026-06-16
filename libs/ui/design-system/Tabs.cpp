#include "design-system/Tabs.h"

#include "design-system/Tokens.h"
#include "design-system/Variants.h"
#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"

#include <utility>

namespace UI::DS {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Bar geometry from Tabs.module.css / components.md.
		constexpr float kBarHeight = 34.0F;
		constexpr float kLabelPx = fs_sm;	// label font size
		constexpr float kUnderline = 2.0F;	// active underline thickness

		float tabGap(float requested) { return requested >= 0.0F ? requested : space_1; }

		// Display labels are letter-spaced (ls_wide) and uppercased by the text
		// primitive; the width must account for the same spacing.
		constexpr float kLabelSpacing = kLabelPx * ls_wide;

		// Per-tab content width: label plus the css padding (space_3 each side).
		float tabWidth(const std::string& label, float scale) {
			float labelWidth = 0.0F;
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				labelWidth = font->MeasureText(label, scale, fontDisplay, kLabelSpacing).x;
			}
			return labelWidth + (space_3 * 2.0F);
		}

	} // namespace

	Tabs::Tabs(Args tabsArgs)
		: args(std::move(tabsArgs)) {}

	Foundation::Vec2 Tabs::footprint() const {
		const float scale = textScale(kLabelPx);
		const float gap = tabGap(args.gap);

		float width = 0.0F;
		for (size_t i = 0; i < args.tabs.size(); ++i) {
			width += tabWidth(args.tabs[i], scale);
			if (i + 1 < args.tabs.size()) {
				width += gap;
			}
		}
		return {width, kBarHeight};
	}

	void Tabs::render() const {
		using Renderer::Primitives::drawLine;
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Vec2 size = footprint();
		const float			   scale = textScale(kLabelPx);
		const float			   gap = tabGap(args.gap);
		const float			   baselineY = args.position.y + kBarHeight;

		// Hairline baseline under the whole bar.
		drawLine({.start = {args.position.x, baselineY},
				  .end = {args.position.x + size.x, baselineY},
				  .style = {.color = line_hairline, .width = bw}});

		float cursorX = args.position.x;
		for (size_t i = 0; i < args.tabs.size(); ++i) {
			const bool	active = static_cast<int>(i) == args.selected;
			const float width = tabWidth(args.tabs[i], scale);

			// Center the label within the tab cell; the text primitive owns the
			// uppercase, the letter-spacing, and the alignment.
			drawText({.text = args.tabs[i],
					  .position = {cursorX, args.position.y},
					  .scale = scale,
					  .color = active ? text_bright : text_dim,
					  .font = fontDisplay,
					  .hAlign = Foundation::HorizontalAlign::Center,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxWidth = width,
					  .boxHeight = kBarHeight,
					  .letterSpacing = kLabelSpacing,
					  .transform = Foundation::TextTransform::Uppercase,
					  .id = "ds_tab_label"});

			// Active underline: a 2px accent bar sitting flush on the baseline so it
			// overlaps the bar's own hairline cleanly.
			if (active) {
				drawRect({.bounds = {cursorX, baselineY - kUnderline, width, kUnderline},
						  .style = {.fill = accent},
						  .id = "ds_tab_underline"});
			}

			cursorX += width + gap;
		}
	}

} // namespace UI::DS
