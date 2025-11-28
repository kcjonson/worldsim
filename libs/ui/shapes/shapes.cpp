#include "shapes/shapes.h"
#include "font/font_renderer.h"
#include "font/text_batch_renderer.h"
#include "core/render_context.h"
#include "primitives/primitives.h"
#include "utils/log.h"
#include <glm/glm.hpp>

namespace UI {

	// Base font size used for font scaling calculations (16px = 1.0 scale)
	constexpr float BASE_FONT_SIZE = 16.0F;

	void Rectangle::Render() {
		Renderer::Primitives::DrawRect(
			{.bounds = {position.x, position.y, size.x, size.y}, .style = style, .id = id, .zIndex = RenderContext::GetZIndex()}
		);
	}

	void Circle::Render() {
		Renderer::Primitives::DrawCircle(
			{.center = center, .radius = radius, .style = style, .id = id, .zIndex = RenderContext::GetZIndex()}
		);
	}

	void Line::Render() {
		Renderer::Primitives::DrawLine(
			{.start = start, .end = end, .style = style, .id = id, .zIndex = RenderContext::GetZIndex()}
		);
	}

	void Text::Render() {
		// Get text batch renderer from Primitives API
		ui::TextBatchRenderer* textBatchRenderer = Renderer::Primitives::GetTextBatchRenderer();
		if (textBatchRenderer == nullptr || text.empty()) {
			return; // No text batch renderer available or nothing to render
		}

		// Get font renderer for alignment calculations
		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		if (fontRenderer == nullptr) {
			return; // No font renderer available for measurements
		}

		// Calculate text scale from fontSize (16px base size = 1.0 scale)
		const float scale = style.fontSize / BASE_FONT_SIZE;

		// Measure text dimensions for alignment
		glm::vec2 textSize = fontRenderer->MeasureText(text, scale);
		float ascent = fontRenderer->GetAscent(scale);

		// Calculate aligned position
		Foundation::Vec2 alignedPos = position;

		// Check if we're in bounding box mode or point mode
		if (width.has_value() && height.has_value()) {
			// BOUNDING BOX MODE
			// Position is top-left of box, align text within the box
			float boxWidth = *width;
			float boxHeight = *height;

			// Horizontal alignment within box
			switch (style.hAlign) {
				case Foundation::HorizontalAlign::Center:
					alignedPos.x += (boxWidth - textSize.x) * 0.5F;
					break;
				case Foundation::HorizontalAlign::Right:
					alignedPos.x += boxWidth - textSize.x;
					break;
				case Foundation::HorizontalAlign::Left:
				default:
					// Text at left edge of box (no offset)
					break;
			}

			// Vertical alignment within box
			switch (style.vAlign) {
				case Foundation::VerticalAlign::Middle:
					alignedPos.y += (boxHeight - ascent) * 0.5F;
					break;
				case Foundation::VerticalAlign::Bottom:
					alignedPos.y += boxHeight - ascent;
					break;
				case Foundation::VerticalAlign::Top:
				default:
					// Text at top of box (no offset)
					break;
			}
		} else {
			// POINT MODE (legacy behavior)
			// Position is the alignment anchor, text is offset based on alignment

			// Horizontal alignment
			switch (style.hAlign) {
				case Foundation::HorizontalAlign::Center:
					alignedPos.x -= textSize.x * 0.5F;
					break;
				case Foundation::HorizontalAlign::Right:
					alignedPos.x -= textSize.x;
					break;
				case Foundation::HorizontalAlign::Left:
				default:
					// Already at left position
					break;
			}

			// Vertical alignment
			// Note: We use font ascent (not textSize.y) for consistent alignment across different text
			// strings. This ensures buttons and labels align consistently regardless of whether the
			// specific text contains descenders (like g, y, p). This is standard for UI text.
			// In POINT MODE: position Y represents the text baseline, offsets adjust relative to that.
			switch (style.vAlign) {
				case Foundation::VerticalAlign::Middle:
					alignedPos.y -= ascent * 0.5F;  // Shift up by half ascent to center on the point
					break;
				case Foundation::VerticalAlign::Bottom:
					// No offset - baseline is already at the alignment point (bottom of text)
					break;
				case Foundation::VerticalAlign::Top:
				default:
					// No offset for top alignment (TODO: should this offset by -ascent?)
					break;
			}
		}

		// Get current z-index from render context (set by Component before render)
		short zIdx = RenderContext::GetZIndex();

		// Add text to batch renderer with z-index for proper sorting
		// NOTE: We call TextBatchRenderer directly here rather than using Primitives::DrawText()
		// to avoid circular dependency (renderer → ui → renderer). This is fine since shapes.cpp
		// is in the ui library which has access to both renderer and ui libraries.
		glm::vec4 color(style.color.r, style.color.g, style.color.b, style.color.a);
		textBatchRenderer->AddText(text, glm::vec2(alignedPos.x, alignedPos.y), scale, color, zIdx);
	}

} // namespace UI
