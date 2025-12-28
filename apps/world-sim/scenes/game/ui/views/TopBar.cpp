#include "TopBar.h"

#include <core/RenderContext.h>
#include <theme/Theme.h>

namespace world_sim {

TopBar::TopBar(const Args& args)
	: onPause(args.onPause)
	, onSpeedChange(args.onSpeedChange)
	, onMenuClick(args.onMenuClick) {
	// Create background rectangle with high z-index to render above game world
	background = std::make_unique<UI::Rectangle>(UI::Rectangle::Args{
		.position = {0.0F, 0.0F},
		.size = {100.0F, kBarHeight},  // Width will be set in layout()
		.style =
			{
				.fill = UI::Theme::Colors::sidebarBackground,
				.border =
					Foundation::BorderStyle{
						.color = UI::Theme::Colors::cardBorder,
						.width = 1.0F,
						.position = Foundation::BorderPosition::Inside},
			},
		.id = "top_bar_background",
		.zIndex = 500});

	// Create date/time display
	dateTimeDisplay = std::make_unique<DateTimeDisplay>(DateTimeDisplay::Args{
		.position = {kLeftPadding, 0.0F},
		.id = args.id + "_datetime"});

	// Create speed buttons with SVG icons
	pauseButton = std::make_unique<SpeedButton>(SpeedButton::Args{
		.iconPath = "ui/icons/pause.svg",
		.onClick = [this]() {
			if (onPause) {
				onPause();
			}
		},
		.id = args.id + "_pause"});

	speed1Button = std::make_unique<SpeedButton>(SpeedButton::Args{
		.iconPath = "ui/icons/play.svg",
		.onClick = [this]() {
			if (onSpeedChange) {
				onSpeedChange(ecs::GameSpeed::Normal);
			}
		},
		.id = args.id + "_speed1"});

	speed2Button = std::make_unique<SpeedButton>(SpeedButton::Args{
		.iconPath = "ui/icons/fast_forward.svg",
		.onClick = [this]() {
			if (onSpeedChange) {
				onSpeedChange(ecs::GameSpeed::Fast);
			}
		},
		.id = args.id + "_speed2"});

	speed3Button = std::make_unique<SpeedButton>(SpeedButton::Args{
		.iconPath = "ui/icons/very_fast.svg",
		.onClick = [this]() {
			if (onSpeedChange) {
				onSpeedChange(ecs::GameSpeed::VeryFast);
			}
		},
		.id = args.id + "_speed3"});

	// Create menu button
	static UI::ButtonAppearance menuAppearance;
	menuAppearance.normal.background.fill = UI::Theme::Colors::cardBackground;
	menuAppearance.normal.background.border = Foundation::BorderStyle{
		.color = UI::Theme::Colors::cardBorder,
		.width = 1.0F,
		.cornerRadius = 4.0F,
		.position = Foundation::BorderPosition::Inside};
	menuAppearance.normal.textColor = UI::Theme::Colors::textBody;
	menuAppearance.normal.fontSize = 14.0F;
	menuAppearance.normal.paddingX = 12.0F;
	menuAppearance.normal.paddingY = 4.0F;

	menuAppearance.hover = menuAppearance.normal;
	menuAppearance.hover.background.fill = Foundation::Color{
		UI::Theme::Colors::cardBackground.r + 0.1F,
		UI::Theme::Colors::cardBackground.g + 0.1F,
		UI::Theme::Colors::cardBackground.b + 0.1F,
		UI::Theme::Colors::cardBackground.a};

	menuAppearance.pressed = menuAppearance.normal;
	menuAppearance.pressed.background.fill = Foundation::Color{
		UI::Theme::Colors::cardBackground.r - 0.05F,
		UI::Theme::Colors::cardBackground.g - 0.05F,
		UI::Theme::Colors::cardBackground.b - 0.05F,
		UI::Theme::Colors::cardBackground.a};

	menuAppearance.disabled = menuAppearance.normal;
	menuAppearance.focused = menuAppearance.normal;
	menuAppearance.focused.background.border = Foundation::BorderStyle{
		.color = UI::Theme::Colors::selectionBorder,
		.width = 2.0F,
		.cornerRadius = 4.0F,
		.position = Foundation::BorderPosition::Inside
	};

	menuButton = std::make_unique<UI::Button>(UI::Button::Args{
		.label = "Menu",
		.size = {60.0F, 24.0F},
		.type = UI::Button::Type::Custom,
		.customAppearance = &menuAppearance,
		.onClick = [this]() {
			if (onMenuClick) {
				onMenuClick();
			}
		},
		.id = "top_bar_menu"});

	// Set initial active state (Normal speed)
	updateSpeedButtonStates(ecs::GameSpeed::Normal);
}

void TopBar::layout(const Foundation::Rect& bounds) {
	viewportBounds = bounds;

	// Update background size to full width
	if (background) {
		background->size = {bounds.width, kBarHeight};
		background->setPosition(bounds.x, bounds.y);
	}

	positionElements();
}

void TopBar::update(const TimeModel& timeModel) {
	// Update date/time display
	if (dateTimeDisplay) {
		dateTimeDisplay->setDateTime(timeModel.displayString());
	}

	// Update speed button active states
	updateSpeedButtonStates(timeModel.data().speed);
}

bool TopBar::handleEvent(UI::InputEvent& event) {
	// Try speed buttons first (left to right)
	if (pauseButton && pauseButton->handleEvent(event)) {
		return true;
	}
	if (speed1Button && speed1Button->handleEvent(event)) {
		return true;
	}
	if (speed2Button && speed2Button->handleEvent(event)) {
		return true;
	}
	if (speed3Button && speed3Button->handleEvent(event)) {
		return true;
	}

	// Try menu button
	if (menuButton && menuButton->handleEvent(event)) {
		return true;
	}

	return false;
}

void TopBar::render() {
	// Render background first
	if (background) {
		UI::RenderContext::setZIndex(background->zIndex);
		background->render();
	}

	// Render date/time display
	if (dateTimeDisplay) {
		dateTimeDisplay->render();
	}

	// Render speed buttons
	if (pauseButton) {
		pauseButton->render();
	}
	if (speed1Button) {
		speed1Button->render();
	}
	if (speed2Button) {
		speed2Button->render();
	}
	if (speed3Button) {
		speed3Button->render();
	}

	// Render menu button
	if (menuButton) {
		menuButton->render();
	}
}

void TopBar::updateSpeedButtonStates(ecs::GameSpeed currentSpeed) {
	if (pauseButton) {
		pauseButton->setActive(currentSpeed == ecs::GameSpeed::Paused);
	}
	if (speed1Button) {
		speed1Button->setActive(currentSpeed == ecs::GameSpeed::Normal);
	}
	if (speed2Button) {
		speed2Button->setActive(currentSpeed == ecs::GameSpeed::Fast);
	}
	if (speed3Button) {
		speed3Button->setActive(currentSpeed == ecs::GameSpeed::VeryFast);
	}
}

void TopBar::positionElements() {
	float x = viewportBounds.x + kLeftPadding;
	float centerY = viewportBounds.y + (kBarHeight / 2.0F);

	// Position date/time display (vertically centered)
	if (dateTimeDisplay) {
		float dtHeight = dateTimeDisplay->getHeight();
		dateTimeDisplay->setPosition({x, centerY - (dtHeight / 2.0F)});
		x += dateTimeDisplay->getWidth() + 20.0F;  // Add gap before speed buttons
	}

	// Position speed buttons (vertically centered)
	if (pauseButton) {
		float btnHeight = pauseButton->getHeight();
		pauseButton->setPosition({x, centerY - (btnHeight / 2.0F)});
		x += pauseButton->getWidth() + kButtonSpacing;
	}
	if (speed1Button) {
		float btnHeight = speed1Button->getHeight();
		speed1Button->setPosition({x, centerY - (btnHeight / 2.0F)});
		x += speed1Button->getWidth() + kButtonSpacing;
	}
	if (speed2Button) {
		float btnHeight = speed2Button->getHeight();
		speed2Button->setPosition({x, centerY - (btnHeight / 2.0F)});
		x += speed2Button->getWidth() + kButtonSpacing;
	}
	if (speed3Button) {
		float btnHeight = speed3Button->getHeight();
		speed3Button->setPosition({x, centerY - (btnHeight / 2.0F)});
	}

	// Position menu button on the right side
	if (menuButton) {
		float menuWidth = 60.0F;
		float menuHeight = 24.0F;
		float rightPadding = 12.0F;
		menuButton->setPosition(
			viewportBounds.x + viewportBounds.width - menuWidth - rightPadding,
			centerY - (menuHeight / 2.0F));
	}
}

}  // namespace world_sim
