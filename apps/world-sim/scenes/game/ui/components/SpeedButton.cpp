#include "SpeedButton.h"

#include "scenes/game/ui/Look.h"

#include <ui/Context.h>

namespace world_sim {

	namespace {
		constexpr float kButtonSize = 28.0F;
		constexpr float kIconSize = 16.0F;
	} // namespace

	SpeedButton::SpeedButton(const Args& args)
		: iconName(args.icon),
		  onPress(args.onPress) {}

	void SpeedButton::onMount() {
		Control::onMount();
		setSize({kButtonSize, kButtonSize});
		setLayout({.display = ui::Display::Flex, .alignItems = ui::AlignItems::Center, .justifyContent = ui::JustifyContent::Center});
		icon = addChild<ui::Icon>({.size = {kIconSize, kIconSize}, .name = iconName});
		tint();
	}

	void SpeedButton::setCurrent(bool value) {
		if (current != value) {
			current = value;
			tint();
		}
	}

	void SpeedButton::activate() {
		if (onPress) {
			onPress();
		}
	}

	void SpeedButton::tint() {
		if (getContext() == nullptr) {
			return;
		}
		const ui::Theme& theme = getContext()->getTheme();
		setStyle({
			.backgroundColor = theme.get(current ? look::t::color::accent : look::t::color::bgInset),
			.borderColor = theme.get(current ? look::t::color::accent : look::t::color::lineEdge),
			.borderWidth = theme.get(look::t::border::bw),
			.borderRadius = theme.get(look::t::radius::rMd),
		});
		if (ui::Icon* shown = icon.get()) {
			shown->setStyle({.color = theme.get(current ? look::t::color::accentContrast : look::t::color::textDim)});
		}
	}

} // namespace world_sim
