#include "BioTabView.h"
#include "MeterDraw.h"
#include "TabStyles.h"

#include <font/FontRenderer.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <string>
#include <vector>

namespace world_sim {

namespace {

constexpr float kRowGap = 10.0F; // gap between stacked sections

// Word-wrap a paragraph to boxWidth and draw it, returning the y past the last
// line. Falls back to a single line if no font renderer is available to measure.
float drawWrapped(const std::string& text, Foundation::Vec2 pos, float fontSize, float boxWidth, const Foundation::Color& color) {
	const float lineHeight = fontSize + 6.0F;
	auto*		fonts	   = Renderer::Primitives::getFontRenderer();

	if (fonts == nullptr || text.empty()) {
		tabs::drawText(text, pos, fontSize, color);
		return pos.y + lineHeight;
	}

	const float scale = fontSize / 16.0F;
	std::string line;
	float		y = pos.y;
	size_t		i = 0;
	while (i < text.size()) {
		size_t start = i;
		while (i < text.size() && text[i] != ' ') ++i;
		std::string word = text.substr(start, i - start);
		while (i < text.size() && text[i] == ' ') ++i;

		std::string candidate = line.empty() ? word : line + " " + word;
		if (fonts->MeasureText(candidate, scale, UI::fontUi).x > boxWidth && !line.empty()) {
			tabs::drawText(line, {pos.x, y}, fontSize, color);
			y += lineHeight;
			line = word;
		} else {
			line = candidate;
		}
	}
	if (!line.empty()) {
		tabs::drawText(line, {pos.x, y}, fontSize, color);
		y += lineHeight;
	}
	return y;
}

} // namespace

void BioTabView::create(const Foundation::Rect& bounds) {
	contentBounds = bounds;
}

void BioTabView::update(const BioData& data) {
	data_ = data;
}

void BioTabView::render() {
	using namespace tabs;
	using namespace UI;

	if (!visible) return;

	const Foundation::Vec2 o	 = getContentPosition();
	const float			   width = contentBounds.width;
	float				   y	 = o.y;

	// Backstory paragraph.
	y = drawWrapped(data_.background, {o.x, y}, fs_md, width, UI::text);
	y += kRowGap;

	// Traits divider + chips (rendered as a comma-joined line).
	y = drawDivider(o.x, y, width, "TRAITS");
	y += 6.0F;
	std::string traitsLine;
	if (data_.traits.empty()) {
		traitsLine = "None";
	} else {
		for (size_t i = 0; i < data_.traits.size(); ++i) {
			if (i > 0) traitsLine += ", ";
			traitsLine += data_.traits[i];
		}
	}
	drawText(traitsLine, {o.x, y}, fs_sm, UI::text_dim);
	y += fs_sm + kRowGap;

	// Current task note, in amber.
	const std::string taskLine = data_.currentTask.empty() ? "Currently: Idle" : "Currently: " + data_.currentTask;
	drawText(taskLine, {o.x, y}, fs_sm, UI::accent);
}

} // namespace world_sim
