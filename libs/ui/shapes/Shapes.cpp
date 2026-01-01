#include "shapes/Shapes.h"
#include "font/FontRenderer.h"
#include "core/RenderContext.h"
#include "primitives/BatchRenderer.h"
#include "primitives/Primitives.h"
#include "utils/Log.h"
#include <glm/glm.hpp>

namespace UI {

	// Base font size used for font scaling calculations (16px = 1.0 scale)
	constexpr float BASE_FONT_SIZE = 16.0F;

	void Rectangle::render() {
		Renderer::Primitives::drawRect(
			{.bounds = {position.x, position.y, size.x, size.y}, .style = style, .id = id, .zIndex = RenderContext::getZIndex()}
		);
	}

	void Circle::render() {
		Renderer::Primitives::drawCircle(
			{.center = center, .radius = radius, .style = style, .id = id, .zIndex = RenderContext::getZIndex()}
		);
	}

	void Line::render() {
		Renderer::Primitives::drawLine(
			{.start = start, .end = end, .style = style, .id = id, .zIndex = RenderContext::getZIndex()}
		);
	}

	// Text layout API implementation
	void Text::ensureCacheValid() const {
		// Check if cache is still valid
		if (cachedWidth.has_value() && cachedText == text && cachedFontSize == style.fontSize &&
			cachedWrapWidth == width && cachedWordWrap == style.wordWrap) {
			return; // Cache is valid
		}

		// Get FontRenderer
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		if (fontRenderer == nullptr || text.empty()) {
			cachedWidth = 0.0F;
			cachedHeight = 0.0F;
			cachedText = text;
			cachedFontSize = style.fontSize;
			cachedWrapWidth = width;
			cachedWordWrap = style.wordWrap;
			return;
		}

		float scale = style.fontSize / BASE_FONT_SIZE;

		// Determine if wrapping is enabled
		bool shouldWrap = style.wordWrap && width.has_value();
		float wrapWidth = shouldWrap ? *width : 0.0F;

		glm::vec2 size = fontRenderer->measureTextWithWrapping(text, scale, wrapWidth);

		cachedWidth = size.x;
		cachedHeight = size.y;
		cachedText = text;
		cachedFontSize = style.fontSize;
		cachedWrapWidth = width;
		cachedWordWrap = style.wordWrap;
	}

	float Text::getWidth() const {
		// If explicit width is set, return it
		if (width.has_value()) {
			return *width + margin * 2.0F;
		}

		// Auto-size: compute from text
		ensureCacheValid();
		return cachedWidth.value_or(0.0F) + margin * 2.0F;
	}

	float Text::getHeight() const {
		// If explicit height is set, return it
		if (height.has_value()) {
			return *height + margin * 2.0F;
		}

		// Auto-size or wrap mode: compute from text
		ensureCacheValid();
		return cachedHeight.value_or(0.0F) + margin * 2.0F;
	}

	void Text::render() {
		// Get batch renderer for unified shape + text rendering
		Renderer::BatchRenderer* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr || text.empty()) {
			return; // No batch renderer available or nothing to render
		}

		// Get font renderer for glyph generation and alignment calculations
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		if (fontRenderer == nullptr) {
			return; // No font renderer available for measurements
		}

		// Calculate text scale from fontSize (16px base size = 1.0 scale)
		const float scale = style.fontSize / BASE_FONT_SIZE;

		// Determine if we should use wrapped text mode
		bool shouldWrap = style.wordWrap && width.has_value();
		glm::vec4 glyphColor(style.color.r, style.color.g, style.color.b, style.color.a);
		std::vector<ui::FontRenderer::GlyphQuad> glyphs;

		if (shouldWrap) {
			// WRAPPED TEXT MODE
			const ui::FontRenderer::WrappedTextResult& wrapped = fontRenderer->wrapText(text, scale, *width);

			// Calculate vertical alignment offset for the text block
			Foundation::Vec2 alignedPos = position;
			if (height.has_value()) {
				float boxHeight = *height;
				switch (style.vAlign) {
					case Foundation::VerticalAlign::Middle:
						alignedPos.y += (boxHeight - wrapped.totalHeight) * 0.5F;
						break;
					case Foundation::VerticalAlign::Bottom:
						alignedPos.y += boxHeight - wrapped.totalHeight;
						break;
					case Foundation::VerticalAlign::Top:
					default:
						// Text at top of box (no offset)
						break;
				}
			}

			// Generate quads for all lines with per-line horizontal alignment
			fontRenderer->generateWrappedGlyphQuads(
				wrapped.lines,
				glm::vec2(alignedPos.x, alignedPos.y),
				scale,
				glyphColor,
				wrapped.lineHeight,
				style.hAlign,
				*width,
				glyphs
			);
		} else {
			// SINGLE LINE MODE (original behavior)
			glm::vec2 textSize = fontRenderer->MeasureText(text, scale);
			float	  ascent = fontRenderer->getAscent(scale);

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
				// Note: We use font ascent (not textSize.y) for consistent alignment across different
				// text strings. This ensures buttons and labels align consistently regardless of whether
				// the specific text contains descenders (like g, y, p). This is standard for UI text.
				// In POINT MODE: position Y represents the text baseline, offsets adjust relative to that.
				switch (style.vAlign) {
					case Foundation::VerticalAlign::Middle:
						alignedPos.y -= ascent * 0.5F; // Shift up by half ascent to center on the point
						break;
					case Foundation::VerticalAlign::Bottom:
						// No offset - baseline is already at the alignment point (bottom of text)
						break;
					case Foundation::VerticalAlign::Top:
					default:
						// No offset for top alignment
						break;
				}
			}

			// Generate glyph quads using FontRenderer
			fontRenderer->generateGlyphQuads(
				text, glm::vec2(alignedPos.x, alignedPos.y), scale, glyphColor, glyphs
			);
		}

		// Add each glyph to the unified batch renderer
		// Text is interleaved with shapes in submission order, preserving correct z-ordering
		Foundation::Color textColor(style.color.r, style.color.g, style.color.b, style.color.a);
		for (const auto& glyph : glyphs) {
			batchRenderer->addTextQuad(
				Foundation::Vec2(glyph.position.x, glyph.position.y),
				Foundation::Vec2(glyph.size.x, glyph.size.y),
				Foundation::Vec2(glyph.uvMin.x, glyph.uvMin.y),
				Foundation::Vec2(glyph.uvMax.x, glyph.uvMax.y),
				textColor
			);
		}
	}

} // namespace UI
