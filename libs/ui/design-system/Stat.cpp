#include "design-system/Stat.h"

#include "design-system/Tokens.h"
#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "primitives/Primitives.h"

#include <utility>

namespace UI::DS {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Value font size by size (components.md: sm=fs_md, md=fs_xl, lg=fs_3xl).
		float valueFont(Size size) {
			switch (size) {
				case Size::Sm:
					return fs_md;
				case Size::Lg:
					return fs_3xl;
				case Size::Md:
					break;
			}
			return fs_xl;
		}

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale).x;
			}
			return 0.0F;
		}

	} // namespace

	Stat::Stat(Args statArgs)
		: args(std::move(statArgs)) {}

	void Stat::render() const {
		using Renderer::Primitives::drawText;

		// Label on top: small, dim, uppercase mono with wide tracking.
		if (!args.label.empty()) {
			drawText({.text = args.label,
					  .position = args.position,
					  .scale = textScale(fs_2xs),
					  .color = text_dim,
					  .font = fontMono,
					  .letterSpacing = fs_2xs * ls_wider,
					  .transform = Foundation::TextTransform::Uppercase,
					  .id = "ds_stat_label"});
		}

		// Value below, sized by size, colored by tone (text_bright for Default).
		const float			   valuePx = valueFont(args.size);
		const float			   valueScale = textScale(valuePx);
		const Foundation::Color valueColor = args.tone == Tone::Default ? text_bright : toneColor(args.tone);
		const float			   valueY = args.position.y + fs_2xs + space_0_5; // label height + 2px gap

		drawText({.text = args.value,
				  .position = {args.position.x, valueY},
				  .scale = valueScale,
				  .color = valueColor,
				  .font = fontDisplay,
				  .id = "ds_stat_value"});

		// Unit trails the value, smaller (0.62em of the value) and dim, sitting on
		// the value's baseline (offset down by the font-size difference).
		if (!args.unit.empty()) {
			const float unitPx = valuePx * 0.62F;
			const float valueWidth = measureWidth(args.value, valueScale);
			drawText({.text = args.unit,
					  .position = {args.position.x + valueWidth + space_0_5, valueY + (valuePx - unitPx)},
					  .scale = textScale(unitPx),
					  .color = text_dim,
					  .font = fontUi,
					  .id = "ds_stat_unit"});
		}
	}

} // namespace UI::DS
