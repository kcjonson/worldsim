#include "BioTabView.h"
#include "MeterDraw.h"

#include <ui/Context.h>

#include <string>

namespace world_sim {

	namespace {
		constexpr float kRowGap = 10.0F;
	} // namespace

	void BioTabView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = kRowGap;
		setLayout(column);
		background = addChild<ui::Text>(
			{.style = world_sim::text(theme, {.size = tokens::fontSize::fsMd, .color = tokens::color::text}), .wrap = true}
		);
		addChild<tabs::KickerDivider>({.label = "Traits"});
		traits = addChild<ui::Text>(
			{.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::textDim}), .wrap = true}
		);
		currently = addChild<ui::Text>({.style = world_sim::text(theme, {.size = tokens::fontSize::fsSm, .color = tokens::color::accent})});
		update({});
	}

	void BioTabView::update(const BioData& data) {
		if (background.get() == nullptr) {
			return;
		}
		background.get()->setText(data.background);
		std::string traitsLine;
		for (const std::string& trait : data.traits) {
			traitsLine += traitsLine.empty() ? trait : ", " + trait;
		}
		traits.get()->setText(traitsLine.empty() ? "None" : traitsLine);
		currently.get()->setText("Currently: " + (data.currentTask.empty() ? std::string("Idle") : data.currentTask));
	}

} // namespace world_sim
