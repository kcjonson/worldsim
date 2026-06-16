#include "design-system/SegmentedControl.h"

#include "design-system/Tokens.h"
#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"

#include <algorithm>
#include <utility>

namespace UI::DS {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Group inner padding and inter-segment gap (SegmentedControl.module.css:
		// 3px padding, 2px gap).
		constexpr float kGroupPad = 3.0F;
		constexpr float kSegGap = 2.0F;

		// Segment height by size (sm/md = 22/28).
		float segHeightFor(Size size) {
			switch (size) {
				case Size::Sm:
					return 22.0F;
				case Size::Lg: // md/lg share the larger chip; no dedicated lg in the prototype
				case Size::Md:
					break;
			}
			return 28.0F;
		}

		// Label font size by size (fs_xs / fs_sm).
		float fontFor(Size size) {
			switch (size) {
				case Size::Sm:
					return fs_xs;
				case Size::Lg:
				case Size::Md:
					break;
			}
			return fs_sm;
		}

		// Per-segment horizontal padding by size (space_2 / space_3).
		float segPadXFor(Size size) {
			switch (size) {
				case Size::Sm:
					return space_2;
				case Size::Lg:
				case Size::Md:
					break;
			}
			return space_3;
		}

		float measureWidth(const std::string& text, float scale, float letterSpacing) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale, fontDisplay, letterSpacing).x;
			}
			return 0.0F;
		}

		// Lift a color's RGB toward white, keeping alpha. Gives the active chip a
		// brighter top edge without a dedicated per-tone bright token.
		Foundation::Color brighten(Foundation::Color color, float amount) {
			return {color.r + ((1.0F - color.r) * amount), color.g + ((1.0F - color.g) * amount), color.b + ((1.0F - color.b) * amount), color.a};
		}

	} // namespace

	SegmentedControl::SegmentedControl(Args controlArgs)
		: args(std::move(controlArgs)) {}

	float SegmentedControl::segmentInnerWidth() const {
		const auto count = static_cast<float>(std::max<size_t>(args.options.size(), 1));

		if (args.segmentWidth > 0.0F) {
			return args.segmentWidth;
		}

		if (args.width > 0.0F) {
			// Split the well's inner content width evenly across the segments.
			const float content = std::max(0.0F, args.width - (kGroupPad * 2.0F) - (kSegGap * (count - 1.0F)));
			return content / count;
		}

		// Auto-fit: widest label plus per-size padding, applied to every segment so
		// they stay equal width.
		const float fontPx = fontFor(args.size);
		const float scale = textScale(fontPx);
		const float spacing = fontPx * ls_wide;
		const float pad = segPadXFor(args.size) * 2.0F;
		float		widest = 0.0F;
		for (const std::string& label : args.options) {
			widest = std::max(widest, measureWidth(label, scale, spacing));
		}
		return widest + pad;
	}

	Foundation::Vec2 SegmentedControl::footprint() const {
		const auto	count = static_cast<float>(std::max<size_t>(args.options.size(), 1));
		const float segW = segmentInnerWidth();
		const float width = (kGroupPad * 2.0F) + (segW * count) + (kSegGap * (count - 1.0F));
		const float height = (kGroupPad * 2.0F) + segHeightFor(args.size);
		return {width, height};
	}

	void SegmentedControl::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Vec2 size = footprint();
		const Foundation::Rect bounds{args.position, size};

		// The inset well: dark fill, hairline border, rounded.
		drawRect({.bounds = bounds,
				  .style = {.fill = bg_inset,
							.border = Foundation::BorderStyle{
								.color = line_hairline,
								.width = bw,
								.cornerRadius = r_md,
								.position = Foundation::BorderPosition::Inside,
							}},
				  .id = "ds_segmented"});

		const Foundation::Color toneCol = toneColor(args.tone);
		const Foundation::Color glowCol = args.tone == Tone::Data ? data_glow : accent_glow;
		const float				segW = segmentInnerWidth();
		const float				segH = segHeightFor(args.size);
		const float				fontPx = fontFor(args.size);
		const float				scale = textScale(fontPx);
		const float				top = bounds.y + kGroupPad;

		float cursorX = bounds.x + kGroupPad;
		for (size_t i = 0; i < args.options.size(); ++i) {
			const bool			   active = static_cast<int>(i) == args.selected;
			const Foundation::Rect segRect{{cursorX, top}, {segW, segH}};

			if (active) {
				// Tone chip with a subtle top-bright -> tone vertical gradient and a
				// soft tone-colored glow behind it via an inline box-shadow
				// (the prototype's box-shadow bloom).
				drawRect({.bounds = segRect,
						  .style = {.fill = toneCol,
									.border = Foundation::BorderStyle{
										.color = Foundation::Color::transparent(),
										.width = 0.0F,
										.cornerRadius = r_sm,
										.position = Foundation::BorderPosition::Inside,
									},
									.gradient = Foundation::LinearGradient{.from = brighten(toneCol, 0.25F), .to = toneCol, .horizontal = false},
									.boxShadow = Foundation::BoxShadow{.color = withAlpha(glowCol, 0.4F), .blur = 10.0F, .spread = 1.0F, .offset = {0.0F, 0.0F}}},
						  .id = "ds_segmented_active"});
			}

			// Centered label: active text is near-black on the chip, inactive is dim.
			// The text primitive owns the uppercase, letter-spacing, and centering.
			const Foundation::Color labelCol = active ? accent_contrast : text_dim;
			drawText({.text = args.options[i],
					  .position = segRect.position(),
					  .scale = scale,
					  .color = labelCol,
					  .font = fontDisplay,
					  .hAlign = Foundation::HorizontalAlign::Center,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxWidth = segRect.width,
					  .boxHeight = segRect.height,
					  .letterSpacing = fontPx * ls_wide,
					  .transform = Foundation::TextTransform::Uppercase,
					  .id = "ds_segmented_label"});

			cursorX += segW + kSegGap;
		}
	}

} // namespace UI::DS
