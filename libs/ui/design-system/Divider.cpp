#include "design-system/Divider.h"

#include "design-system/Tokens.h"
#include "design-system/Variants.h"
#include "font/FontRenderer.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"

#include <algorithm>
#include <utility>

namespace UI::DS {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		float measureWidth(const std::string& text, float scale) {
			if (const ui::FontRenderer* font = Renderer::Primitives::getFontRenderer(); font != nullptr) {
				return font->MeasureText(text, scale).x;
			}
			return 0.0F;
		}

	} // namespace

	Divider::Divider(Args dividerArgs)
		: args(std::move(dividerArgs)) {}

	void Divider::render() const {
		using Renderer::Primitives::drawLine;
		using Renderer::Primitives::drawText;

		const float lineY = args.position.y;

		// Bare rule: a single hairline across the full width.
		if (args.label.empty()) {
			drawLine({.start = {args.position.x, lineY},
					  .end = {args.position.x + args.width, lineY},
					  .style = {.color = line_hairline, .width = bw}});
			return;
		}

		// Labeled: a hairline segment, the centered caption, another hairline
		// segment. Segment widths are the remaining space after the label and the
		// two gaps (space_3 on each side of the label).
		const float scale = textScale(fs_2xs);
		const float labelWidth = measureWidth(args.label, scale);
		const float gap = space_3;
		const float segWidth = std::max(0.0F, (args.width - labelWidth - (gap * 2.0F)) * 0.5F);

		const float leftStart = args.position.x;
		const float leftEnd = leftStart + segWidth;
		drawLine({.start = {leftStart, lineY}, .end = {leftEnd, lineY}, .style = {.color = line_hairline, .width = bw}});

		const float labelX = leftEnd + gap;
		drawText({.text = args.label,
				  .position = {labelX, lineY - (fs_2xs * 0.5F)}, // center the caption on the line
				  .scale = scale,
				  .color = text_faint,
				  .font = fontMono,
				  .id = "ds_divider_label"});

		const float rightStart = labelX + labelWidth + gap;
		const float rightEnd = args.position.x + args.width;
		drawLine({.start = {rightStart, lineY}, .end = {rightEnd, lineY}, .style = {.color = line_hairline, .width = bw}});
	}

} // namespace UI::DS
