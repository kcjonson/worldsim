#include "design-system/Modal.h"

#include "design-system/Tokens.h"
#include "design-system/Variants.h"
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

		// Dialog width by size (components.md / Modal.module.css).
		float widthForSize(Size size) {
			switch (size) {
				case Size::Sm:
					return 400.0F;
				case Size::Lg:
					return 760.0F;
				case Size::Md:
					break;
			}
			return 560.0F;
		}

		// Accent color of the Panel chrome, mirrored for the faux glow halo.
		Foundation::Color accentColor(PanelAccent panelAccent) {
			return panelAccent == PanelAccent::Data ? data : accent;
		}

	} // namespace

	Modal::Modal(Args modalArgs)
		: args(std::move(modalArgs)) {}

	float Modal::dialogWidth() const {
		// Cap at 92% of the viewport so the dialog never overflows (matches CSS).
		const float maxWidth = args.viewport.x * 0.92F;
		const float width = widthForSize(args.size);
		return maxWidth > 0.0F ? std::min(width, maxWidth) : width;
	}

	float Modal::dialogHeight() const {
		// Caller-provided height, or a sensible default; capped at 86% of viewport.
		const float height = args.height > 0.0F ? args.height : 360.0F;
		const float maxHeight = args.viewport.y * 0.86F;
		return maxHeight > 0.0F ? std::min(height, maxHeight) : height;
	}

	void Modal::render() const {
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		// Full-bleed scrim over the whole viewport.
		drawRect({.bounds = {0.0F, 0.0F, args.viewport.x, args.viewport.y}, .style = {.fill = scrim}, .id = "ds_modal_scrim"});

		// Center the dialog in the viewport.
		const float			   width = dialogWidth();
		const float			   height = dialogHeight();
		const Foundation::Vec2 origin{(args.viewport.x - width) * 0.5F, (args.viewport.y - height) * 0.5F};

		// The dialog: a raised, bracketed Panel carrying the title/kicker, lit with
		// an accent glow (box-shadow) declared inline via the Panel's glow option.
		const Panel dialog{{.position = origin,
							.size = {width, height},
							.title = args.title,
							.kicker = args.kicker,
							.variant = PanelVariant::Raised,
							.accent = args.accent,
							.corners = true,
							.glow = withAlpha(accentColor(args.accent), 0.4F)}};
		dialog.render();

		// Placeholder body line in the Panel's content area.
		if (!args.body.empty()) {
			const Foundation::Rect body = dialog.bodyBounds();
			drawText({.text = args.body,
					  .position = {body.x, body.y},
					  .scale = textScale(fs_base),
					  .color = text,
					  .font = fontUi,
					  .id = "ds_modal_body"});
		}
	}

} // namespace UI::DS
