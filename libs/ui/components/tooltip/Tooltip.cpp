#include "Tooltip.h"

#include "theme/Tokens.h"
#include "theme/Variants.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "primitives/Primitives.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace UI {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Salvage bubble padding: space_2 block / space_3 inline (Tooltip.module.css).
		constexpr float kPadX = space_3;
		constexpr float kPadY = space_2;

		// Small pointer triangle that bridges the bubble to its trigger.
		constexpr float kPointerSize = 6.0F;

		// Fold the fade opacity into a token color so the legacy fade keeps working.
		Foundation::Color fade(Foundation::Color color, float opacity) {
			return withAlpha(color, color.a * opacity);
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

	Tooltip::Tooltip(const Args& args)
		: content(args.content),
		  maxWidth(args.maxWidth) {
		position = args.position;
		size = {getTooltipWidth(), getTooltipHeight()};
	}

	void Tooltip::setContent(const TooltipContent& tooltipContent) {
		content = tooltipContent;
		size = {getTooltipWidth(), getTooltipHeight()};
	}

	float Tooltip::getTooltipWidth() const {
		// Estimate width based on longest content line
		size_t maxChars = content.title.length();

		if (!content.description.empty()) {
			maxChars = std::max(maxChars, content.description.length());
		}

		if (!content.hotkey.empty()) {
			// Hotkey is displayed as "[hotkey]", so add 2 for brackets
			maxChars = std::max(maxChars, content.hotkey.length() + 2);
		}

		float estimatedWidth = kPadX * 2.0F + static_cast<float>(maxChars) * kEstimatedCharWidth;

		// Clamp to max width
		return std::min(estimatedWidth, maxWidth);
	}

	float Tooltip::getTooltipHeight() const {
		return calculateHeight();
	}

	float Tooltip::calculateHeight() const {
		float height = kPadY * 2.0F;

		// Title is always present
		height += kTitleFontSize;

		// Description (optional)
		if (!content.description.empty()) {
			height += kLineSpacing + kDescFontSize;
		}

		// Hotkey (optional)
		if (!content.hotkey.empty()) {
			height += kLineSpacing + kHotkeyFontSize;
		}

		return height;
	}

	void Tooltip::setPosition(float x, float y) {
		position = {x, y};
	}

	bool Tooltip::containsPoint(Foundation::Vec2 point) const {
		float width = getTooltipWidth();
		float height = getTooltipHeight();
		return point.x >= position.x && point.x < position.x + width && point.y >= position.y && point.y < position.y + height;
	}

	void Tooltip::render() {
		if (!visible || opacity <= 0.0F) {
			return;
		}

		const float width = getTooltipWidth();
		const float height = getTooltipHeight();

		const Foundation::Rect bubble{position.x, position.y, width, height};

		// Salvage bubble: raised panel fill, hairline edge border, small radius.
		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = bubble,
				.style = {.fill = fade(bg_panel_raised, opacity),
						  .border = Foundation::BorderStyle{.color = fade(line_edge, opacity),
														   .width = bw,
														   .cornerRadius = r_sm,
														   .position = Foundation::BorderPosition::Inside}},
				.zIndex = zIndex,
			}
		);

		// Pointer triangle. The TooltipManager floats the bubble below-right of the
		// cursor, so the pointer sits on the top edge and points up toward the trigger.
		const Foundation::Vec2 center = bubble.center();
		drawTriangle({center.x - kPointerSize, bubble.y},
					 {center.x + kPointerSize, bubble.y},
					 {center.x, bubble.y - kPointerSize},
					 fade(bg_panel_raised, opacity),
					 "tooltip_pointer");

		const float textX = bubble.x + kPadX;
		float		textY = bubble.y + kPadY;

		// Title: brightest text, UI typeface.
		Renderer::Primitives::drawText(
			Renderer::Primitives::TextArgs{
				.text = content.title,
				.position = {textX, textY},
				.scale = textScale(kTitleFontSize),
				.color = fade(text_bright, opacity),
				.font = fontUi,
				.zIndex = static_cast<float>(zIndex) + 0.1F,
			}
		);
		textY += kTitleFontSize;

		// Description (optional): body text.
		if (!content.description.empty()) {
			textY += kLineSpacing;
			Renderer::Primitives::drawText(
				Renderer::Primitives::TextArgs{
					.text = content.description,
					.position = {textX, textY},
					.scale = textScale(kDescFontSize),
					.color = fade(text, opacity),
					.font = fontUi,
					.zIndex = static_cast<float>(zIndex) + 0.1F,
				}
			);
			textY += kDescFontSize;
		}

		// Hotkey (optional): dim text, mono typeface (keycap role).
		if (!content.hotkey.empty()) {
			textY += kLineSpacing;
			Renderer::Primitives::drawText(
				Renderer::Primitives::TextArgs{
					.text = "[" + content.hotkey + "]",
					.position = {textX, textY},
					.scale = textScale(kHotkeyFontSize),
					.color = fade(text_dim, opacity),
					.font = fontMono,
					.zIndex = static_cast<float>(zIndex) + 0.1F,
				}
			);
		}
	}

} // namespace UI
