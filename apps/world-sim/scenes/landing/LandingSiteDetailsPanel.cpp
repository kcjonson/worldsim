#include "scenes/landing/LandingSiteDetailsPanel.h"

#include <primitives/Primitives.h>
#include <shapes/Shapes.h>

namespace world_sim {

namespace {

void drawText(float x, float y, const std::string& text, float fontSize,
              const Foundation::Color& color,
              Foundation::HorizontalAlign hAlign = Foundation::HorizontalAlign::Left) {
	UI::Text t(UI::Text::Args{
		.position = {x, y},
		.text = text,
		.style = {
			.color = color,
			.fontSize = fontSize,
			.hAlign = hAlign,
			.vAlign = Foundation::VerticalAlign::Top,
			.wordWrap = false,
		},
	});
	t.render();
}

float measureHeight(const LandingSiteDetails& details) {
	float h = 14.0F;            // top padding
	h += 20.0F;                 // location header
	h += 34.0F;                 // verdict (wraps up to two lines)
	h += 24.0F + 10.0F;         // difficulty badge + gap
	for (const auto& s : details.sections) {
		h += 20.0F;             // section header
		h += static_cast<float>(s.rows.size()) * 19.0F;
		h += 10.0F;             // section gap
	}
	h += 4.0F;                  // bottom padding
	return h;
}

} // namespace

Foundation::Rect LandingSiteDetailsPanel::render(
		const LandingSiteDetails& details, float anchorRightX, float anchorTopY) const {
	const float panelH = measureHeight(details);
	const float panelX = anchorRightX - kWidth;
	Foundation::Rect bounds{panelX, anchorTopY, kWidth, panelH};

	// Non-modal: translucent so the globe stays readable behind it.
	Renderer::Primitives::drawRect({
		.bounds = bounds,
		.style = {
			.fill = Foundation::Color{0.07F, 0.08F, 0.11F, 0.92F},
			.border = Foundation::BorderStyle{
				.color = Foundation::Color{0.30F, 0.34F, 0.42F, 1.0F},
				.width = 1.0F,
			},
		},
		.id = "landing_details_panel",
	});

	const float contentX = panelX + kPadding;
	const float contentRight = panelX + kWidth - kPadding;
	float y = anchorTopY + kPadding;

	// Location header.
	drawText(contentX, y, details.location, 15.0F, Foundation::Color::white());
	y += kHeaderHeight;

	// Water verdict headline (the survival-critical line). Width is set so long
	// verdicts wrap rather than overflow the panel.
	{
		UI::Text verdict(UI::Text::Args{
			.position = {contentX, y},
			.width = kWidth - 2.0F * kPadding,
			.text = details.verdict,
			.style = {
				.color = details.verdictColor,
				.fontSize = 13.0F,
				.hAlign = Foundation::HorizontalAlign::Left,
				.vAlign = Foundation::VerticalAlign::Top,
				.wordWrap = true,
			},
		});
		verdict.render();
	}
	y += kVerdictHeight;

	// Difficulty badge: a filled pill colored by rating.
	{
		Foundation::Rect badge{contentX, y, kWidth - 2.0F * kPadding, kBadgeHeight - 4.0F};
		Foundation::Color badgeFill = details.habitabilityColor;
		badgeFill.a = 0.22F;
		Renderer::Primitives::drawRect({
			.bounds = badge,
			.style = {
				.fill = badgeFill,
				.border = Foundation::BorderStyle{
					.color = details.habitabilityColor,
					.width = 1.0F,
				},
			},
			.id = "landing_difficulty_badge",
		});
		drawText(contentX + 8.0F, y + 3.0F, "Difficulty", 12.0F,
		         Foundation::Color{0.85F, 0.85F, 0.88F, 1.0F});
		drawText(contentRight - 8.0F, y + 3.0F, details.habitabilityText, 13.0F,
		         details.habitabilityColor, Foundation::HorizontalAlign::Right);
	}
	y += kBadgeHeight + kSectionGap;

	// Sections.
	for (const auto& section : details.sections) {
		drawText(contentX, y, section.header, 12.0F,
		         Foundation::Color{0.55F, 0.6F, 0.7F, 1.0F});
		y += kHeaderHeight;
		for (const auto& row : section.rows) {
			drawText(contentX, y, row.label, 12.0F,
			         Foundation::Color{0.7F, 0.7F, 0.74F, 1.0F});
			drawText(contentRight, y, row.value, 12.0F, row.accent,
			         Foundation::HorizontalAlign::Right);
			y += kRowHeight;
		}
		y += kSectionGap;
	}

	return bounds;
}

} // namespace world_sim
