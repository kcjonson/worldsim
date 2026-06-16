#include "design-system/Meter.h"

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

		// Stacked track height by size (Meter.module.css: md=8px, sm=5px).
		float trackHeight(Size size) { return size == Size::Sm ? 5.0F : 8.0F; }

		// Inline-variant track height (Meter.module.css: 18px, 16px at sm).
		float inlineHeight(Size size) { return size == Size::Sm ? 16.0F : 18.0F; }

		// Segment rhythm from Meter.module.css: 7px segment, 2px gutter.
		constexpr float kSegmentWidth = 7.0F;
		constexpr float kSegmentGap = 2.0F;

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale).x;
			}
			return 0.0F;
		}

		// Scale a color's RGB toward black, keeping alpha. Used to give the fill a
		// subtle top-bright -> bottom-darker gradient without inventing new tokens.
		Foundation::Color darken(Foundation::Color color, float factor) {
			return {color.r * factor, color.g * factor, color.b * factor, color.a};
		}

		// Inline text with a dark text-shadow, so labels stay legible over both the
		// filled and empty track (the shadow is a style prop, not a double draw).
		void drawShadowedText(
			const std::string&		  text,
			Foundation::Vec2		  pos,
			float					  scale,
			Foundation::Color		  color,
			const char*				  id,
			float					  letterSpacing = 0.0F,
			Foundation::TextTransform transform = Foundation::TextTransform::None
		) {
			Renderer::Primitives::drawText({.text = text,
											.position = pos,
											.scale = scale,
											.color = color,
											.font = fontMono,
											.shadowColor = bg_void,
											.shadowOffset = {1.0F, 1.0F},
											.letterSpacing = letterSpacing,
											.transform = transform,
											.id = id});
		}

	} // namespace

	Meter::Meter(Args meterArgs)
		: args(std::move(meterArgs)) {}

	void Meter::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const float			 value = std::clamp(args.value, 0.0F, 1.0F);
		const Foundation::Color toneCol = toneColor(args.tone, value);

		if (args.inlineLabel) {
			const float			   height = inlineHeight(args.size);
			const Foundation::Rect track{args.position.x, args.position.y, args.width, height};

			drawRect({.bounds = track,
					  .style = {.fill = bg_inset,
								.border = Foundation::BorderStyle{.color = line_hairline,
																  .width = bw,
																  .cornerRadius = r_sm,
																  .position = Foundation::BorderPosition::Inside}},
					  .id = "ds_meter_track"});

			const float fillWidth = value * args.width;

			// Dim wash fill with a bright 2px leading edge (matches the inline CSS).
			// Subtle vertical gradient: a touch brighter at the top, settling to the
			// dim tone wash below, so the bar reads with a little depth. A soft tone
			// glow sits behind it as an inline box-shadow.
			drawRect({.bounds = {track.x, track.y, fillWidth, height},
					  .style = {.fill = withAlpha(toneCol, 0.22F),
								.gradient = Foundation::LinearGradient{
									.from = withAlpha(toneCol, 0.30F), .to = withAlpha(darken(toneCol, 0.85F), 0.18F), .horizontal = false},
								.boxShadow = Foundation::BoxShadow{.color = withAlpha(toneCol, 0.35F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
					  .id = "ds_meter_fill"});
			if (fillWidth >= bw_thick) {
				drawRect({.bounds = {track.x + fillWidth - bw_thick, track.y, bw_thick, height},
						  .style = {.fill = toneCol},
						  .id = "ds_meter_edge"});
			}

			// Label left, value right, both inside the bar, with faked text-shadow.
			const float scale = textScale(fs_2xs);
			const float pad = space_2;
			const float textY = track.y + ((height - fs_2xs) * 0.5F);

			if (!args.label.empty()) {
				drawShadowedText(args.label, {track.x + pad, textY}, scale, text_bright, "ds_meter_label",
								 fs_2xs * ls_wider, Foundation::TextTransform::Uppercase);
			}
			if (!args.valueText.empty()) {
				const float valueWidth = measureWidth(args.valueText, scale);
				drawShadowedText(args.valueText, {track.right() - pad - valueWidth, textY}, scale, toneCol, "ds_meter_value");
			}
			return;
		}

		// Stacked layout: optional header row, then the track below.
		float trackTop = args.position.y;

		const bool hasHeader = !args.label.empty() || !args.valueText.empty();
		if (hasHeader) {
			const float scale = textScale(fs_xs);
			if (!args.label.empty()) {
				drawText({.text = args.label,
						  .position = {args.position.x, args.position.y},
						  .scale = scale,
						  .color = text_dim,
						  .font = fontMono,
						  .letterSpacing = fs_xs * ls_wider,
						  .transform = Foundation::TextTransform::Uppercase,
						  .id = "ds_meter_label"});
			}
			if (!args.valueText.empty()) {
				const float valueWidth = measureWidth(args.valueText, scale);
				drawText({.text = args.valueText,
						  .position = {args.position.x + args.width - valueWidth, args.position.y},
						  .scale = scale,
						  .color = toneCol,
						  .font = fontMono,
						  .id = "ds_meter_value"});
			}
			trackTop += fs_xs + space_1; // header text + column gap
		}

		const float			   height = trackHeight(args.size);
		const float			   radius = height * 0.5F; // full pill
		const Foundation::Rect track{args.position.x, trackTop, args.width, height};

		drawRect({.bounds = track,
				  .style = {.fill = bg_inset,
							.border = Foundation::BorderStyle{
								.color = line_hairline, .width = bw, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside}},
				  .id = "ds_meter_track"});

		const float fillWidth = value * args.width;
		if (fillWidth > 0.0F) {
			// Soft tone glow behind the fill as an inline box-shadow.
			drawRect({.bounds = {track.x, track.y, fillWidth, height},
					  .style = {.fill = toneCol,
								.border = Foundation::BorderStyle{
									.color = toneCol, .width = 0.0F, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside},
								.gradient = Foundation::LinearGradient{.from = toneCol, .to = darken(toneCol, 0.78F), .horizontal = false},
								.boxShadow = Foundation::BoxShadow{.color = withAlpha(toneCol, 0.35F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
					  .id = "ds_meter_fill"});
		}

		// Segmented notches: bg_panel gutters laid over the track on the 7+2 rhythm.
		if (args.segmented) {
			float x = track.x + kSegmentWidth;
			while (x < track.right()) {
				const float gapWidth = std::min(kSegmentGap, track.right() - x);
				drawRect({.bounds = {x, track.y, gapWidth, height}, .style = {.fill = bg_panel}, .id = "ds_meter_notch"});
				x += kSegmentWidth + kSegmentGap;
			}
		}
	}

} // namespace UI::DS
