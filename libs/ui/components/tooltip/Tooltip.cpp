#include "Tooltip.h"

#include "primitives/Primitives.h"

#include <algorithm>

namespace UI {

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

		float estimatedWidth = Theme::Tooltip::padding * 2 + static_cast<float>(maxChars) * kEstimatedCharWidth;

		// Clamp to max width
		return std::min(estimatedWidth, maxWidth);
	}

	float Tooltip::getTooltipHeight() const {
		return calculateHeight();
	}

	float Tooltip::calculateHeight() const {
		float height = Theme::Tooltip::padding * 2;

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

		float width = getTooltipWidth();
		float height = getTooltipHeight();

		// Apply opacity to colors
		Foundation::Color bgColor = Theme::Tooltip::background;
		bgColor.a *= opacity;
		Foundation::Color borderColor = Theme::Tooltip::border;
		borderColor.a *= opacity;

		// Draw background
		Renderer::Primitives::drawRect(
			Renderer::Primitives::RectArgs{
				.bounds = {position.x, position.y, width, height},
				.style = {.fill = bgColor, .border = Foundation::BorderStyle{.color = borderColor, .width = 1.0F}},
				.zIndex = zIndex,
			}
		);

		float textX = position.x + Theme::Tooltip::padding;
		float textY = position.y + Theme::Tooltip::padding;

		// Draw title
		Foundation::Color titleColor = Theme::Colors::textTitle;
		titleColor.a *= opacity;

		Renderer::Primitives::drawText(
			Renderer::Primitives::TextArgs{
				.text = content.title,
				.position = {textX, textY},
				.scale = kTitleFontSize / 16.0F,
				.color = titleColor,
				.zIndex = static_cast<float>(zIndex) + 0.1F,
			}
		);
		textY += kTitleFontSize;

		// Draw description (optional)
		if (!content.description.empty()) {
			textY += kLineSpacing;
			Foundation::Color descColor = Theme::Colors::textBody;
			descColor.a *= opacity;

			Renderer::Primitives::drawText(
				Renderer::Primitives::TextArgs{
					.text = content.description,
					.position = {textX, textY},
					.scale = kDescFontSize / 16.0F,
					.color = descColor,
					.zIndex = static_cast<float>(zIndex) + 0.1F,
				}
			);
			textY += kDescFontSize;
		}

		// Draw hotkey (optional)
		if (!content.hotkey.empty()) {
			textY += kLineSpacing;
			Foundation::Color hotkeyColor = Theme::Colors::textSecondary;
			hotkeyColor.a *= opacity;

			Renderer::Primitives::drawText(
				Renderer::Primitives::TextArgs{
					.text = "[" + content.hotkey + "]",
					.position = {textX, textY},
					.scale = kHotkeyFontSize / 16.0F,
					.color = hotkeyColor,
					.zIndex = static_cast<float>(zIndex) + 0.1F,
				}
			);
		}
	}

} // namespace UI
