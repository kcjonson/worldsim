#include "design-system/Button.h"

#include "theme/Tokens.h"
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

		// Height per size (components.md: sm/md/lg = 26/34/46).
		float heightFor(Size size) {
			switch (size) {
				case Size::Sm:
					return 26.0F;
				case Size::Lg:
					return 46.0F;
				case Size::Md:
					break;
			}
			return 34.0F;
		}

		// Horizontal padding per size (Button.module.css: space_3/space_4/space_6).
		float padXFor(Size size) {
			switch (size) {
				case Size::Sm:
					return space_3;
				case Size::Lg:
					return space_6;
				case Size::Md:
					break;
			}
			return space_4;
		}

		// Label font size per size (Button.module.css: fs_xs/fs_sm/fs_md).
		float fontFor(Size size) {
			switch (size) {
				case Size::Sm:
					return fs_xs;
				case Size::Lg:
					return fs_md;
				case Size::Md:
					break;
			}
			return fs_sm;
		}

		struct VariantStyle {
			Foundation::Color fill;
			Foundation::Color label;
			Foundation::Color border;
			bool			  hasBorder;
			bool			  gradientFill = false; // Primary only: accent_bright -> accent
		};

		VariantStyle styleFor(ButtonVariant variant) {
			switch (variant) {
				case ButtonVariant::Primary:
					return {accent, accent_contrast, accent_bright, true, true};
				case ButtonVariant::Ghost:
					return {Foundation::Color::transparent(), text_dim, {}, false};
				case ButtonVariant::Danger:
					return {Foundation::Color::transparent(), status_crit, status_crit, true};
				case ButtonVariant::Data:
					return {withAlpha(data, 0.12F), data_bright, data, true};
				case ButtonVariant::Secondary:
					break;
			}
			return {Foundation::Color::transparent(), text, line_edge, true};
		}

	} // namespace

	Button::Button(Args buttonArgs)
		: args(std::move(buttonArgs)) {}

	Foundation::Vec2 Button::footprint() const {
		if (args.size.x > 0.0F && args.size.y > 0.0F) {
			return args.size;
		}

		const float height = heightFor(args.sizeVariant);

		const float fontPx = fontFor(args.sizeVariant);
		float		labelWidth = 0.0F;
		if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
			labelWidth = font->MeasureText(args.label, textScale(fontPx), fontDisplay, fontPx * ls_wide).x;
		}

		return {labelWidth + (padXFor(args.sizeVariant) * 2.0F), height};
	}

	void Button::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Vec2 size = footprint();
		const Foundation::Rect bounds{args.position, size};
		const VariantStyle	   style = styleFor(args.variant);

		Foundation::RectStyle rectStyle{.fill = style.fill};
		if (style.gradientFill) {
			rectStyle.gradient = Foundation::LinearGradient{.from = accent_bright, .to = accent, .horizontal = false};
		}
		if (style.hasBorder) {
			rectStyle.border = Foundation::BorderStyle{
				.color = style.border,
				.width = bw,
				.cornerRadius = r_sm,
				.position = Foundation::BorderPosition::Inside,
			};
		}
		drawRect({.bounds = bounds, .style = rectStyle, .id = "ds_button"});

		// Center the label within the button box; the text primitive owns the
		// uppercase, the letter-spacing, and the alignment math.
		const float fontPx = fontFor(args.sizeVariant);
		drawText({.text = args.label,
				  .position = bounds.position(),
				  .scale = textScale(fontPx),
				  .color = style.label,
				  .font = fontDisplay,
				  .hAlign = Foundation::HorizontalAlign::Center,
				  .vAlign = Foundation::VerticalAlign::Middle,
				  .boxWidth = bounds.width,
				  .boxHeight = bounds.height,
				  .letterSpacing = fontPx * ls_wide,
				  .transform = Foundation::TextTransform::Uppercase,
				  .id = "ds_button_label"});
	}

} // namespace UI::DS
