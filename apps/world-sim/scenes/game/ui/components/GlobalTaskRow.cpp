#include "GlobalTaskRow.h"

#include <primitives/Primitives.h>
#include <theme/Theme.h>

#include <format>

namespace world_sim {

GlobalTaskRow::GlobalTaskRow(const Args& args)
	: rowWidth(args.width),
	  showKnownBy(args.showKnownBy) {

	// Set component size for layout (LayoutContainer uses this)
	size = {rowWidth, kRowHeight};
	margin = 2.0F;

	// Line 1: "Harvest Berry Bush      (10, 15)  5m"
	std::string line1Text = std::format("{:<20}  {}  {}",
		args.task.description,
		args.task.position,
		args.task.distance);

	line1Handle = addChild(UI::Text(UI::Text::Args{
		.position = {kPadding, kPadding},
		.text = line1Text,
		.style = {
			.color = UI::Theme::Colors::textBody,
			.fontSize = kLine1FontSize
		},
		.id = (args.id + "_line1").c_str()
	}));

	// Line 2: "Available • Need 3 • Known by: Bob, Alice" or "Blocked • 0/3 materials"
	std::string line2Text = args.task.status;
	if (!args.task.statusDetail.empty()) {
		line2Text += " \xE2\x80\xA2 " + args.task.statusDetail;
	}
	if (showKnownBy && !args.task.knownBy.empty()) {
		line2Text += " \xE2\x80\xA2 Known by: " + args.task.knownBy;
	}

	line2Handle = addChild(UI::Text(UI::Text::Args{
		.position = {kPadding, kLineSpacing},
		.text = line2Text,
		.style = {
			.color = getStatusColor(args.task),
			.fontSize = kLine2FontSize
		},
		.id = (args.id + "_line2").c_str()
	}));
}

void GlobalTaskRow::setPosition(float x, float y) {
	Component::setPosition(x, y);

	// Update child positions relative to our new position
	Foundation::Vec2 contentPos = getContentPosition();
	if (auto* text1 = getChild<UI::Text>(line1Handle)) {
		text1->position = {contentPos.x + kPadding, contentPos.y + kPadding};
	}
	if (auto* text2 = getChild<UI::Text>(line2Handle)) {
		text2->position = {contentPos.x + kPadding, contentPos.y + kLineSpacing};
	}
}

void GlobalTaskRow::setTaskData(const adapters::GlobalTaskDisplayData& task) {
	if (auto* text1 = getChild<UI::Text>(line1Handle)) {
		text1->text = std::format("{:<20}  {}  {}",
			task.description,
			task.position,
			task.distance);
	}

	if (auto* text2 = getChild<UI::Text>(line2Handle)) {
		std::string line2Text = task.status;
		if (!task.statusDetail.empty()) {
			line2Text += " \xE2\x80\xA2 " + task.statusDetail;
		}
		if (showKnownBy && !task.knownBy.empty()) {
			line2Text += " \xE2\x80\xA2 Known by: " + task.knownBy;
		}
		text2->text = line2Text;
		text2->style.color = getStatusColor(task);
	}
}

Foundation::Color GlobalTaskRow::getStatusColor(const adapters::GlobalTaskDisplayData& task) const {
	if (task.isMine) {
		return UI::Theme::Colors::textClickable;
	}
	if (task.isBlocked) {
		return UI::Theme::Colors::textMuted; // Blocked tasks shown in muted color
	}
	if (task.isReserved) {
		return UI::Theme::Colors::statusPending;
	}
	if (task.status == "Far" || task.status == "Waiting for harvest") {
		return UI::Theme::Colors::textMuted;
	}
	return UI::Theme::Colors::statusActive;
}

} // namespace world_sim
