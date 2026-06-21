#include "TasksTabView.h"
#include "MeterDraw.h"
#include "TabStyles.h"

#include <components/badge/Badge.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>
#include <theme/Variants.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace world_sim {

namespace {

using namespace UI;
using namespace tabs;

constexpr float kRowH		 = 28.0F; // known-work row height
constexpr float kCurrentH	 = 56.0F; // "Currently" panel height
constexpr float kSectionGap	 = 16.0F;

std::string toLower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// Map a status string to a Badge tone. Available->ok, blocked->crit,
// waiting->warn, in-progress->data; anything else stays default.
UI::Tone statusTone(const std::string& status) {
	const std::string s = toLower(status);
	if (s.find("block") != std::string::npos) return UI::Tone::Crit;
	if (s.find("wait") != std::string::npos) return UI::Tone::Warn;
	if (s.find("progress") != std::string::npos || s.find("working") != std::string::npos) return UI::Tone::Data;
	if (s.find("avail") != std::string::npos) return UI::Tone::Ok;
	return UI::Tone::Default;
}

} // namespace

void TasksTabView::create(const Foundation::Rect& bounds) {
	contentBounds = bounds;
}

void TasksTabView::update(const TasksTabData& data) {
	data_ = data;
}

void TasksTabView::render() {
	using namespace tabs;
	using namespace UI;

	if (!visible) return;

	const Foundation::Vec2 o	 = getContentPosition();
	const float			   width = contentBounds.width;
	float				   y	 = o.y;

	// ---- "Currently" panel with amber left border ----
	const Foundation::Rect panel{o.x, y, width, kCurrentH};
	Renderer::Primitives::drawRect({
		.bounds = panel,
		.style	= {.fill = UI::withAlpha(UI::accent, 0.06F), .border = Foundation::BorderStyle{.color = UI::line_edge, .width = UI::bw, .cornerRadius = UI::r_md, .position = Foundation::BorderPosition::Inside}},
	});
	// Amber left accent bar.
	Renderer::Primitives::drawRect({.bounds = {o.x, y, 3.0F, kCurrentH}, .style = {.fill = UI::accent}});

	const float px = o.x + space_3;
	Renderer::Primitives::drawText({
		.text		   = "CURRENTLY",
		.position	   = {px, y + space_2},
		.scale		   = fs_2xs / 16.0F,
		.color		   = UI::accent,
		.font		   = UI::fontMono,
		.letterSpacing = fs_2xs * UI::ls_wider,
		.transform	   = Foundation::TextTransform::Uppercase,
	});
	const std::string current = data_.currentTask.empty() ? "Idle" : data_.currentTask;
	drawText(current, {px, y + space_2 + fs_2xs + 6.0F}, fs_md, UI::text_bright);
	y += kCurrentH + kSectionGap;

	// ---- Known Work ----
	y = drawDivider(o.x, y, width, "KNOWN WORK");
	y += 8.0F;

	if (data_.tasks.empty()) {
		drawText("No known work nearby.", {o.x, y}, fs_xs, UI::text_faint);
		return;
	}

	// Cap rows to what fits the content area.
	const float		maxY = o.y + contentBounds.height - kRowH;
	for (const auto& task : data_.tasks) {
		if (y > maxY) break;

		drawText(task.description, {o.x, y}, fs_sm, UI::text);

		// Detail under the label (status detail like "0/2 metal" or worker name).
		if (!task.statusDetail.empty()) {
			drawText(task.statusDetail, {o.x, y + fs_sm + 1.0F}, fs_2xs, UI::text_faint);
		}

		// Status badge, right-aligned.
		const UI::Tone	  tone	 = statusTone(task.status);
		const std::string label	 = task.status.empty() ? "Available" : task.status;
		const float		  badgeW = (space_2 * 2.0F) + measureText(label, fs_2xs, UI::fontMono);
		UI::Badge({.position = {o.x + width - badgeW, y}, .label = label, .tone = tone}).render();

		// Distance, just left of the badge.
		if (!task.distance.empty()) {
			Renderer::Primitives::drawText({.text = task.distance, .position = {o.x, y}, .scale = fs_2xs / 16.0F, .color = UI::data_bright, .font = UI::fontMono, .hAlign = Foundation::HorizontalAlign::Right, .boxWidth = width - badgeW - space_2});
		}

		y += kRowH;
	}
}

} // namespace world_sim
