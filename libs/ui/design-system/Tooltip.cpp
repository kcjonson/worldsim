#include "design-system/Tooltip.h"

#include "theme/Tokens.h"
#include "theme/Variants.h"
#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace UI::DS {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Tooltip.module.css: padding space_2 (block) / space_3 (inline), max 240px.
		constexpr float kPadX = space_3;
		constexpr float kPadY = space_2;
		constexpr float kMaxContentWidth = 240.0F;

		// Small pointer triangle that bridges the bubble to its anchor.
		constexpr float kPointerSize = 6.0F;

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale).x;
			}
			return 0.0F;
		}

		// Draw a filled triangle from three corners.
		void drawTriangle(Foundation::Vec2 a, Foundation::Vec2 b, Foundation::Vec2 c, Foundation::Color color, const char* id) {
			const std::array<Foundation::Vec2, 3> verts{a, b, c};
			const std::array<uint16_t, 3>		  idx{0, 1, 2};
			Renderer::Primitives::drawTriangles({.vertices = verts.data(),
												 .indices = idx.data(),
												 .vertexCount = verts.size(),
												 .indexCount = idx.size(),
												 .color = color,
												 .id = id});
		}

	} // namespace

	Tooltip::Tooltip(Args tooltipArgs)
		: args(std::move(tooltipArgs)) {}

	void Tooltip::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const float scale = textScale(fs_xs);

		// Size the bubble to the measured content plus padding, capped to the max.
		const float contentWidth = std::min(measureWidth(args.content, scale), kMaxContentWidth);
		const float bubbleWidth = contentWidth + (kPadX * 2.0F);
		const float bubbleHeight = fs_xs + (kPadY * 2.0F);

		const Foundation::Rect bubble{args.position.x, args.position.y, bubbleWidth, bubbleHeight};

		drawRect({.bounds = bubble,
				  .style = {.fill = bg_panel_raised,
							.border = Foundation::BorderStyle{
								.color = line_strong, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}},
				  .id = "ds_tooltip_bubble"});

		// Content text, vertically centered in the padded box, left-aligned.
		const float textY = bubble.y + ((bubbleHeight - fs_xs) * 0.5F);
		drawText({.text = args.content,
				  .position = {bubble.x + kPadX, textY},
				  .scale = scale,
				  .color = text,
				  .font = fontUi,
				  .id = "ds_tooltip_content"});

		// Pointer: a small triangle on the side the bubble is anchored from,
		// centered on that edge, pointing away from the bubble toward the trigger.
		const Foundation::Vec2 center = bubble.center();
		const Foundation::Color pointerColor = bg_panel_raised;
		switch (args.side) {
			case TooltipSide::Top:
				// Bubble sits above the trigger; pointer drops off the bottom edge.
				drawTriangle({center.x - kPointerSize, bubble.bottom()},
							 {center.x + kPointerSize, bubble.bottom()},
							 {center.x, bubble.bottom() + kPointerSize},
							 pointerColor,
							 "ds_tooltip_pointer");
				break;
			case TooltipSide::Bottom:
				drawTriangle({center.x - kPointerSize, bubble.y},
							 {center.x + kPointerSize, bubble.y},
							 {center.x, bubble.y - kPointerSize},
							 pointerColor,
							 "ds_tooltip_pointer");
				break;
			case TooltipSide::Left:
				drawTriangle({bubble.right(), center.y - kPointerSize},
							 {bubble.right(), center.y + kPointerSize},
							 {bubble.right() + kPointerSize, center.y},
							 pointerColor,
							 "ds_tooltip_pointer");
				break;
			case TooltipSide::Right:
				drawTriangle({bubble.x, center.y - kPointerSize},
							 {bubble.x, center.y + kPointerSize},
							 {bubble.x - kPointerSize, center.y},
							 pointerColor,
							 "ds_tooltip_pointer");
				break;
		}
	}

} // namespace UI::DS
