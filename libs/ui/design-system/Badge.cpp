#include "design-system/Badge.h"

#include "design-system/Tokens.h"
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

		// Badge geometry from Badge.module.css / components.md.
		constexpr float kHeight = 20.0F;
		constexpr float kDotDiameter = 6.0F;
		constexpr float kDotGap = 5.0F; // gap between dot and label

	} // namespace

	Badge::Badge(Args badgeArgs)
		: args(std::move(badgeArgs)) {}

	void Badge::render() const {
		using Renderer::Primitives::drawCircle;
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Color toneCol = args.tone == Tone::Default ? text : toneColor(args.tone);

		const float scale = textScale(fs_2xs);
		float		labelWidth = 0.0F;
		if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
			labelWidth = font->MeasureText(args.label, scale).x;
		}

		const float dotSpace = args.dot ? (kDotDiameter + kDotGap) : 0.0F;
		const float width = (space_2 * 2.0F) + dotSpace + labelWidth;

		const Foundation::Rect bounds{args.position, {width, kHeight}};

		// Tinted pill: ~14% tone fill over a ~45% tone border (crit runs a touch
		// hotter in the prototype, but a single mix keeps this token-driven).
		drawRect({.bounds = bounds,
				  .style = {.fill = args.tone == Tone::Default ? bg_panel_raised : withAlpha(toneCol, 0.14F),
							.border = Foundation::BorderStyle{.color = args.tone == Tone::Default ? line_edge : withAlpha(toneCol, 0.45F),
															  .width = bw,
															  .cornerRadius = r_sm,
															  .position = Foundation::BorderPosition::Inside}},
				  .id = "ds_badge"});

		float contentX = bounds.x + space_2;

		// Optional leading dot: a filled circle in the tone color with a larger,
		// more-transparent faux-glow ring behind it.
		if (args.dot) {
			const float			   radius = kDotDiameter * 0.5F;
			const Foundation::Vec2 center{contentX + radius, bounds.y + (kHeight * 0.5F)};
			drawCircle({.center = center, .radius = radius + 2.0F, .style = {.fill = withAlpha(toneCol, 0.4F)}, .id = "ds_badge_dot_glow"});
			drawCircle({.center = center, .radius = radius, .style = {.fill = toneCol}, .id = "ds_badge_dot"});
			contentX += kDotDiameter + kDotGap;
		}

		const float textY = bounds.y + ((kHeight - fs_2xs) * 0.5F);
		drawText({.text = args.label, .position = {contentX, textY}, .scale = scale, .color = toneCol, .font = fontMono, .id = "ds_badge_label"});
	}

} // namespace UI::DS
