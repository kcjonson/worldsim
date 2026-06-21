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
