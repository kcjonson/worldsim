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
		background = addChild<ui::Text>({.style = look::text(theme, look::t::color::text, look::t::fontSize::fsMd), .wrap = true});
		addChild<tabs::KickerDivider>({.label = "Traits"});
		traits = addChild<ui::Text>({.style = look::text(theme, look::t::color::textDim, look::t::fontSize::fsSm), .wrap = true});
		currently = addChild<ui::Text>({.style = look::text(theme, look::t::color::accent, look::t::fontSize::fsSm)});
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
