#include "BioTabView.h"
#include "TabStyles.h"

#include <components/avatar/Avatar.h>
#include <components/stat/Stat.h>
#include <font/FontRenderer.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <string>
#include <vector>

namespace world_sim {

namespace {

// Header geometry (mirrors prototype: 72px Avatar + 16px gap + stat grid).
constexpr float kAvatarSize = 72.0F;
constexpr float kAvatarGap	= 16.0F; // gap between avatar and stat grid
constexpr float kStatCol	= 150.0F; // compact stat column width
constexpr float kStatRow	= 38.0F;  // stat row height
constexpr float kHeaderH	= 88.0F;  // vertical space the header reserves

constexpr float kRowGap = 8.0F; // gap between stacked text rows

// Draw one line of text at an absolute point, using the UI font.
void drawLine(const std::string& text, Foundation::Vec2 pos, float fontSize, const Foundation::Color& color) {
	Renderer::Primitives::drawText({
		.text = text,
		.position = pos,
		.scale = fontSize / 16.0F,
		.color = color,
		.font = UI::fontUi
	});
}

// Word-wrap a paragraph to boxWidth and draw it, returning the y past the last
// line. Falls back to a single line if no font renderer is available to measure.
float drawWrapped(const std::string& text, Foundation::Vec2 pos, float fontSize, float boxWidth, const Foundation::Color& color) {
	const float		   scale	  = fontSize / 16.0F;
	const float		   lineHeight = fontSize + 4.0F;
	auto*			   fonts	  = Renderer::Primitives::getFontRenderer();

	if (fonts == nullptr || text.empty()) {
		drawLine(text, pos, fontSize, color);
		return pos.y + lineHeight;
	}

	std::string line;
	float		y = pos.y;
	size_t		i = 0;
	while (i < text.size()) {
		// Pull the next whitespace-delimited word.
		size_t start = i;
		while (i < text.size() && text[i] != ' ') ++i;
		std::string word	  = text.substr(start, i - start);
		while (i < text.size() && text[i] == ' ') ++i; // consume trailing spaces

		std::string candidate = line.empty() ? word : line + " " + word;
		if (fonts->MeasureText(candidate, scale, UI::fontUi).x > boxWidth && !line.empty()) {
			drawLine(line, {pos.x, y}, fontSize, color);
			y += lineHeight;
			line = word;
		} else {
			line = candidate;
		}
	}
	if (!line.empty()) {
		drawLine(line, {pos.x, y}, fontSize, color);
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

	if (!visible) return;

	const Foundation::Vec2 o = getContentPosition();

	// ---- Header: Avatar + compact 2x2 Stat grid ----
	UI::Avatar({.position = {o.x, o.y}, .size = kAvatarSize, .seed = data_.name, .mood = data_.mood / 100.0F}).render();

	const float gx = o.x + kAvatarSize + kAvatarGap;

	const std::string moodValue = data_.moodLabel.empty()
		? std::to_string(static_cast<int>(data_.mood)) + "%"
		: data_.moodLabel;
	const UI::Tone moodTone = data_.mood < 40.0F ? UI::Tone::Crit : UI::Tone::Ok;

	UI::Stat({.position = {gx, o.y}, .label = "ROLE", .value = "--", .size = UI::Size::Sm}).render();
	UI::Stat({.position = {gx + kStatCol, o.y}, .label = "ORIGIN", .value = "--", .size = UI::Size::Sm}).render();
	UI::Stat({.position = {gx, o.y + kStatRow}, .label = "AGE", .value = data_.age, .unit = "yrs", .size = UI::Size::Sm}).render();
	UI::Stat({.position = {gx + kStatCol, o.y + kStatRow}, .label = "MOOD", .value = moodValue, .tone = moodTone, .size = UI::Size::Sm}).render();

	// ---- Stacked text rows below the header ----
	float y = o.y + kHeaderH;

	drawLine("BACKGROUND", {o.x, y}, kLabelSize, labelColor());
	y += kLabelSize + 4.0F;
	y = drawWrapped(data_.background, {o.x, y}, kBodySize, contentBounds.width, bodyColor());
	y += kRowGap;

	drawLine("TRAITS", {o.x, y}, kLabelSize, labelColor());
	y += kLabelSize + 4.0F;
	std::string traitsLine;
	if (data_.traits.empty()) {
		traitsLine = "None";
	} else {
		for (size_t i = 0; i < data_.traits.size(); ++i) {
			if (i > 0) traitsLine += ", ";
			traitsLine += data_.traits[i];
		}
	}
	drawLine(traitsLine, {o.x, y}, kBodySize, mutedColor());
	y += kBodySize + 4.0F + kRowGap;

	const std::string taskLine = data_.currentTask.empty()
		? "Currently: Idle"
		: "Currently: " + data_.currentTask;
	drawLine(taskLine, {o.x, y}, kBodySize, UI::accent);
}

} // namespace world_sim
