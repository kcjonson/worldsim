#include "MemoryTabView.h"
#include "MeterDraw.h"
#include "TabStyles.h"

#include <components/badge/Badge.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <string>

namespace world_sim {

namespace {

using namespace UI;
using namespace tabs;

// Sight range is a vision-system constant; not yet exposed in MemoryData.
constexpr const char* kSightRange = "30m";

constexpr size_t kMaxRowsPerCategory = 3;
constexpr float	 kColumnGap			 = 24.0F;
constexpr float	 kSummaryGap		 = 16.0F;
constexpr float	 kCatGap			 = 16.0F;	// vertical gap between category blocks
constexpr float	 kRowH				 = 16.0F;	// entity row height
constexpr float	 kHeadGap			 = 6.0F;	// below category head

// Tone for a category by name (food/water/resources informative, threats hot).
UI::Tone categoryTone(const std::string& name, size_t count) {
	if (count == 0) return UI::Tone::Default;
	if (name == "Threats") return UI::Tone::Crit;
	return UI::Tone::Data;
}

// Draw one category block at (x,y) within colWidth. Returns the y past the block.
float drawCategory(float x, float y, float colWidth, const MemoryCategory& cat) {
	using namespace tabs;

	// Head: name (left) + count badge (right).
	drawText(cat.name, {x, y + 2.0F}, fs_sm, UI::text_bright);
	const std::string countStr = std::to_string(cat.count);
	const float		  badgeW   = (space_2 * 2.0F) + measureText(countStr, fs_2xs, UI::fontMono);
	UI::Badge({.position = {x + colWidth - badgeW, y}, .label = countStr, .tone = categoryTone(cat.name, cat.count)}).render();
	y += 22.0F + kHeadGap;

	if (cat.entities.empty()) {
		drawText("None sighted", {x, y}, fs_xs, UI::text_faint);
		return y + fs_xs;
	}

	const size_t shown = cat.entities.size() < kMaxRowsPerCategory ? cat.entities.size() : kMaxRowsPerCategory;
	for (size_t i = 0; i < shown; ++i) {
		const auto&		  e	  = cat.entities[i];
		drawText(e.name, {x, y}, fs_xs, UI::text);
		const std::string pos = "(" + std::to_string(static_cast<int>(e.x)) + ", " + std::to_string(static_cast<int>(e.y)) + ")";
		Renderer::Primitives::drawText({.text = pos, .position = {x, y}, .scale = fs_2xs / 16.0F, .color = UI::data_bright, .font = UI::fontMono, .hAlign = Foundation::HorizontalAlign::Right, .boxWidth = colWidth});
		y += kRowH;
	}

	if (cat.count > shown) {
		drawText("+" + std::to_string(cat.count - shown) + " more", {x, y}, fs_2xs, UI::text_faint);
		y += kRowH;
	}

	return y;
}

} // namespace

void MemoryTabView::create(const Foundation::Rect& bounds) {
	contentBounds = bounds;
}

void MemoryTabView::update(const MemoryData& data) {
	data_ = data;
}

void MemoryTabView::render() {
	using namespace tabs;
	using namespace UI;

	if (!visible) return;

	const Foundation::Vec2 o	 = getContentPosition();
	const float			   width = contentBounds.width;

	// ---- Summary row: two metrics ----
	const auto metric = [&](float x, const std::string& value, const std::string& label) {
		Renderer::Primitives::drawText({.text = value, .position = {x, o.y}, .scale = fs_xl / 16.0F, .color = UI::text_bright, .font = UI::fontDisplay});
		drawText(label, {x, o.y + fs_xl + 2.0F}, fs_2xs, UI::text_dim);
	};
	metric(o.x, std::to_string(data_.totalKnown), "Locations known");
	metric(o.x + 140.0F, kSightRange, "Sight range");

	float y = o.y + fs_xl + fs_2xs + kSummaryGap + 8.0F;

	// ---- 2-column category grid ----
	const float colWidth = (width - kColumnGap) / 2.0F;
	const float leftX	 = o.x;
	const float rightX	 = o.x + colWidth + kColumnGap;

	float leftY	 = y;
	float rightY = y;
	for (size_t i = 0; i < data_.categories.size(); ++i) {
		const bool	left = (i % 2 == 0);
		const float cx	 = left ? leftX : rightX;
		float&		cy	 = left ? leftY : rightY;
		cy = drawCategory(cx, cy, colWidth, data_.categories[i]);
		cy += kCatGap;
	}
}

} // namespace world_sim
