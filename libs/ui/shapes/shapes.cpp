#include "shapes/shapes.h"
#include "font/font_renderer.h"
#include "primitives/primitives.h"
#include <glm/glm.hpp>

namespace UI {

	// Base font size used for font scaling calculations (16px = 1.0 scale)
	constexpr float BASE_FONT_SIZE = 16.0F;

	void Rectangle::Render() const {
		Renderer::Primitives::DrawRect({.bounds = {position.x, position.y, size.x, size.y}, .style = style, .id = id});
	}

	void Circle::Render() const {
		Renderer::Primitives::DrawCircle({.center = center, .radius = radius, .style = style, .id = id});
	}

	void Line::Render() const {
		Renderer::Primitives::DrawLine({.start = start, .end = end, .style = style, .id = id});
	}

	void Text::Render() const {
		// Get font renderer from Primitives API
		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		if (fontRenderer == nullptr || text.empty()) {
			return; // No font renderer available or nothing to render
		}

		// Calculate text scale from fontSize (16px base size = 1.0 scale)
		const float scale = style.fontSize / BASE_FONT_SIZE;

		// Measure text dimensions for alignment
		glm::vec2 textSize = fontRenderer->MeasureText(text, scale);

		// Calculate aligned position
		Foundation::Vec2 alignedPos = position;

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
		float ascent = fontRenderer->GetAscent(scale);
		switch (style.vAlign) {
			case Foundation::VerticalAlign::Middle:
				alignedPos.y -= ascent * 0.5F;
				break;
			case Foundation::VerticalAlign::Bottom:
				alignedPos.y -= ascent;
				break;
			case Foundation::VerticalAlign::Top:
			default:
				// Already at top position
				break;
		}

		// Render text
		glm::vec3 color(style.color.r, style.color.g, style.color.b);
		fontRenderer->RenderText(text, glm::vec2(alignedPos.x, alignedPos.y), scale, color);
	}

} // namespace UI
