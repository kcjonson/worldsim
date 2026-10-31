#include "SocialTabView.h"
#include "TabStyles.h"

#include <ui/Context.h>
#include <ui/shapes/Text.h>

namespace world_sim {

	void SocialTabView::onMount() {
		const ui::Theme& theme = getContext()->getTheme();
		ui::Layout		 column = getLayout();
		column.display = ui::Display::Flex;
		column.flexDirection = ui::FlexDirection::Column;
		column.gap = theme.get(look::t::spacing::space2);
		setLayout(column);
		addChild<ui::Text>({.style = tabs::labelText(theme), .text = "Relationships"});
		addChild<ui::Text>({.style = tabs::mutedText(theme), .text = "Not yet tracked"});
		addChild<ui::Text>({.style = tabs::smallText(theme), .text = "Future: Opinion modifiers, social interactions"});
	}

	void SocialTabView::update(const SocialData& /*data*/) {}

} // namespace world_sim
