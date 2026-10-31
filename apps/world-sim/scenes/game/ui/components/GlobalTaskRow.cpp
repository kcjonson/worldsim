#include "GlobalTaskRow.h"

#include "scenes/shared/SalalUi.h"

#include <ui/Context.h>

#include <format>
#include <string>

namespace world_sim {

	namespace {

		constexpr float kPadding = 4.0F;
		constexpr float kLine1Size = 12.0F;
		constexpr float kLine2Size = 10.0F;

		ui::ColorToken statusColor(const adapters::GlobalTaskDisplayData& task) {
			if (task.isMine) {
				return tokens::color::data;
			}
			if (task.isBlocked || task.status == "Far" || task.status == "Waiting for harvest") {
				return tokens::color::textDim;
			}
			if (task.isReserved) {
				return tokens::color::statusWarn;
			}
			return tokens::color::statusOk;
		}

	} // namespace

	GlobalTaskRow::GlobalTaskRow(const Args& args)
		: task(args.task),
		  showKnownBy(args.showKnownBy) {}

	void GlobalTaskRow::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		setLayout({.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F, .padding = kPadding});
		setMargin(2.0F);
		line1 = addChild<ui::Text>({.style = {.color = theme.get(tokens::color::text), .fontSize = kLine1Size}});
		line2 = addChild<ui::Text>({.style = {.fontSize = kLine2Size}});
		show();
	}

	void GlobalTaskRow::setTaskData(const adapters::GlobalTaskDisplayData& value) {
		task = value;
		show();
	}

	void GlobalTaskRow::show() {
		ui::Text* first = line1.get();
		ui::Text* second = line2.get();
		if (first == nullptr || second == nullptr) {
			return;
		}
		first->setText(std::format("{:<20}  {}  {}", task.description, task.position, task.distance));
		std::string status = task.status;
		if (!task.statusDetail.empty()) {
			status += " - " + task.statusDetail;
		}
		if (showKnownBy && !task.knownBy.empty()) {
			status += " - Known by: " + task.knownBy;
		}
		second->setText(status);
		second->setStyle({.color = getContext()->getTheme().get(statusColor(task)), .fontSize = kLine2Size});
	}

} // namespace world_sim
