#include "design-system/KeyCap.h"

#include "design-system/Tokens.h"
#include "design-system/Variants.h"
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

		// KeyCap geometry from KeyCap.module.css / components.md.
		constexpr float kHeight = 18.0F;
		constexpr float kMinWidth = 18.0F;
		constexpr float kPadX = 5.0F;		// horizontal padding each side
		constexpr float kBottomEdge = 2.0F; // doubled bottom border for the bevel

		// Mono labels are letter-spaced (ls_wider) and uppercased by the text
		// primitive; auto-width must account for the same spacing.
		constexpr float kLabelSpacing = fs_2xs * ls_wider;

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale, fontMono, kLabelSpacing).x;
			}
			return 0.0F;
		}

	} // namespace

	KeyCap::KeyCap(Args keyCapArgs)
		: args(std::move(keyCapArgs)) {}

	Foundation::Vec2 KeyCap::footprint() const {
		const float labelWidth = measureWidth(args.label, textScale(fs_2xs));
		const float width = std::max(kMinWidth, labelWidth + (kPadX * 2.0F));
		return {width, kHeight};
	}

	void KeyCap::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Vec2 size = footprint();
		const Foundation::Rect bounds{args.position, size};

		// Key body: inset fill, edge border, rounded.
		drawRect({.bounds = bounds,
				  .style = {.fill = bg_inset,
							.border = Foundation::BorderStyle{
								.color = line_edge,
								.width = bw,
								.cornerRadius = r_sm,
								.position = Foundation::BorderPosition::Inside,
							}},
				  .id = "ds_keycap"});

		// Beveled bottom edge: a thicker strong-line strip along the inside bottom,
		// reading as the extruded key face without a full bevel.
		drawRect({.bounds = {bounds.x, bounds.y + bounds.height - kBottomEdge, bounds.width, kBottomEdge},
				  .style = {.fill = line_strong},
				  .id = "ds_keycap_edge"});

		// Centered mono label; the text primitive owns the uppercase, the
		// letter-spacing, and the centering.
		drawText({.text = args.label,
				  .position = bounds.position(),
				  .scale = textScale(fs_2xs),
				  .color = text_dim,
				  .font = fontMono,
				  .hAlign = Foundation::HorizontalAlign::Center,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxWidth = bounds.width,
				  .boxHeight = bounds.height,
				  .letterSpacing = kLabelSpacing,
				  .transform = Foundation::TextTransform::Uppercase,
				  .id = "ds_keycap_label"});
	}

} // namespace UI::DS
