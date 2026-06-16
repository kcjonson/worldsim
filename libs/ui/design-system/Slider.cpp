#include "design-system/Slider.h"

#include "theme/Tokens.h"
#include "theme/Variants.h"
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

		// Slider.module.css: track 4px tall, full pill radius, inside a 18px wrap.
		constexpr float kTrackHeight = 4.0F;
		constexpr float kTrackWrapHeight = 18.0F;

		// Slider.module.css: 14px square thumb, radius-sm corners.
		constexpr float kThumbSize = 14.0F;

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale).x;
			}
			return 0.0F;
		}

	} // namespace

	Slider::Slider(Args sliderArgs)
		: args(std::move(sliderArgs)) {}

	void Slider::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const float value = std::clamp(args.value, 0.0F, 1.0F);

		// Header row: label left, value right (both mono, baseline of the row).
		float trackWrapTop = args.position.y;

		const bool hasHeader = !args.label.empty() || !args.valueText.empty();
		if (hasHeader) {
			const float scale = textScale(fs_sm);
			if (!args.label.empty()) {
				drawText({.text = args.label,
						  .position = {args.position.x, args.position.y},
						  .scale = scale,
						  .color = text_dim,
						  .font = fontMono,
						  .id = "ds_slider_label"});
			}
			if (!args.valueText.empty()) {
				const float valueWidth = measureWidth(args.valueText, scale);
				drawText({.text = args.valueText,
						  .position = {args.position.x + args.width - valueWidth, args.position.y},
						  .scale = scale,
						  .color = accent_bright,
						  .font = fontMono,
						  .id = "ds_slider_value"});
			}
			trackWrapTop += fs_sm + space_2; // header text + column gap
		}

		// The track is vertically centered within the interaction wrap so the
		// thumb (taller than the track) reads as sitting on the rail.
		const float trackY = trackWrapTop + ((kTrackWrapHeight - kTrackHeight) * 0.5F);
		const float radius = kTrackHeight * 0.5F; // full pill
		const Foundation::Rect track{args.position.x, trackY, args.width, kTrackHeight};

		drawRect({.bounds = track,
				  .style = {.fill = bg_inset,
							.border = Foundation::BorderStyle{
								.color = line_hairline, .width = bw, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside}},
				  .id = "ds_slider_track"});

		const float fillWidth = value * args.width;
		if (fillWidth > 0.0F) {
			drawRect({.bounds = {track.x, track.y, fillWidth, kTrackHeight},
					  .style = {.fill = accent,
								.border = Foundation::BorderStyle{
									.color = accent, .width = 0.0F, .cornerRadius = radius, .position = Foundation::BorderPosition::Inside}},
					  .id = "ds_slider_fill"});
		}

		// Detent: a 2px teal reference tick, inset 2px top and bottom of the wrap,
		// centered on its position. Visual reference only.
		if (args.detent >= 0.0F) {
			const float detent = std::clamp(args.detent, 0.0F, 1.0F);
			const float tickX = (args.position.x + (detent * args.width)) - (bw_thick * 0.5F);
			drawRect({.bounds = {tickX, trackWrapTop + space_0_5, bw_thick, kTrackWrapHeight - (space_0_5 * 2.0F)},
					  .style = {.fill = withAlpha(data, 0.7F)},
					  .id = "ds_slider_detent"});
		}

		// Thumb: a 14px square (radius-sm) centered on the value position, with a
		// bg_void hairline border and a soft accent glow behind it via box-shadow.
		const float thumbCenterX = args.position.x + (value * args.width);
		const float thumbX = thumbCenterX - (kThumbSize * 0.5F);
		const float thumbY = (trackWrapTop + (kTrackWrapHeight * 0.5F)) - (kThumbSize * 0.5F);

		drawRect({.bounds = {thumbX, thumbY, kThumbSize, kThumbSize},
				  .style = {.fill = accent_bright,
							.border = Foundation::BorderStyle{
								.color = bg_void, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside},
							.boxShadow = Foundation::BoxShadow{.color = withAlpha(accent, 0.4F), .blur = 8.0F, .spread = 0.0F, .offset = {0.0F, 0.0F}}},
				  .id = "ds_slider_thumb"});
	}

} // namespace UI::DS
