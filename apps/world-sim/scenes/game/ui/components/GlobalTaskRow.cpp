#include "GlobalTaskRow.h"

#include "scenes/shared/SalalUi.h"

#include <ui/Context.h>

#include <format>
#include <string>

namespace world_sim {

	namespace {

		constexpr float kPadding = 4.0F;
		constexpr float kGap = 8.0F; // between the description and its position and distance
		constexpr float kLine1Size = 12.0F;
		constexpr float kLine2Size = 10.0F;

		ui::ColorToken statusColor(const adapters::GlobalTaskDisplayData& task) {
			if (task.isBlocked || task.status == "Far" || task.status == "Waiting for harvest") {
				return tokens::color::textDim;
			}
			return tokens::color::statusOk;
		}

	} // namespace

	GlobalTaskRow::GlobalTaskRow(const Args& args)
		: task(args.task) {}

	void GlobalTaskRow::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		setLayout({.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F, .padding = kPadding});
		setMargin(2.0F);
		const ui::Style		   line1Style{.color = theme.get(tokens::color::text), .fontSize = kLine1Size};
		ui::Ref<ui::Component> head =
			addChild<ui::Component>({.layout = {.display = ui::Display::Flex, .gap = kGap, .alignItems = ui::AlignItems::Start}});
		description = head.addChild<ui::Text>({.layout = {.flexGrow = 1.0F}, .style = line1Style, .wrap = true});
		where = head.addChild<ui::Text>({.layout = {.flexShrink = 0.0F}, .style = line1Style});
		line2 = addChild<ui::Text>({.style = {.fontSize = kLine2Size}, .wrap = true});
		show();
	}

	void GlobalTaskRow::setTaskData(const adapters::GlobalTaskDisplayData& value) {
		task = value;
		show();
	}

	void GlobalTaskRow::show() {
		ui::Text* what = description.get();
		ui::Text* at = where.get();
		ui::Text* second = line2.get();
		if (what == nullptr || at == nullptr || second == nullptr) {
			return;
		}
		what->setText(task.description);
		at->setText(std::format("{}  {}", task.position, task.distance));
		std::string status = task.status;
		if (!task.statusDetail.empty()) {
			status += " - " + task.statusDetail;
		}
		second->setText(status);
		second->setStyle({.color = getContext()->getTheme().get(statusColor(task)), .fontSize = kLine2Size});
	}

} // namespace world_sim
