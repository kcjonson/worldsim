#include "TopBar.h"

#include <theme/Theme.h>

namespace world_sim {

TopBar::TopBar(const Args& args)
	: onPause(args.onPause)
	, onSpeedChange(args.onSpeedChange)
	, onMenuClick(args.onMenuClick) {

	// Create background rectangle with high z-index to render above game world
	backgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
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
		.zIndex = 500}));

	// Create date/time display
	dateTimeDisplayHandle = addChild(DateTimeDisplay(DateTimeDisplay::Args{
		.position = {kLeftPadding, 0.0F},
		.id = args.id + "_datetime"}));

	// Create speed buttons with SVG icons
	pauseButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/pause.svg",
		.onClick = [this]() {
			if (onPause) {
				onPause();
			}
		},
		.id = args.id + "_pause"}));

	speed1ButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/play.svg",
		.onClick = [this]() {
			if (onSpeedChange) {
				onSpeedChange(ecs::GameSpeed::Normal);
			}
		},
		.id = args.id + "_speed1"}));

	speed2ButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/fast_forward.svg",
		.onClick = [this]() {
			if (onSpeedChange) {
				onSpeedChange(ecs::GameSpeed::Fast);
			}
		},
		.id = args.id + "_speed2"}));

	speed3ButtonHandle = addChild(SpeedButton(SpeedButton::Args{
		.iconPath = "ui/icons/very_fast.svg",
		.onClick = [this]() {
			if (onSpeedChange) {
				onSpeedChange(ecs::GameSpeed::VeryFast);
			}
		},
		.id = args.id + "_speed3"}));

	// Create menu button with custom appearance
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
		.position = Foundation::BorderPosition::Inside};

	menuButtonHandle = addChild(UI::Button(UI::Button::Args{
		.label = "Menu",
		.size = {60.0F, 24.0F},
		.type = UI::Button::Type::Custom,
		.customAppearance = &menuAppearance,
		.onClick = [this]() {
			if (onMenuClick) {
				onMenuClick();
			}
		},
		.id = "top_bar_menu"}));

	// Set initial active state (Normal speed)
	updateSpeedButtonStates(ecs::GameSpeed::Normal);
}

void TopBar::layout(const Foundation::Rect& newBounds) {
	Component::layout(newBounds);

	// Update background size to full width
	if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
		bg->size = {newBounds.width, kBarHeight};
		bg->setPosition(newBounds.x, newBounds.y);
	}

	positionElements();
}

void TopBar::updateData(const TimeModel& timeModel) {
	// Update date/time display
	if (auto* dtd = getChild<DateTimeDisplay>(dateTimeDisplayHandle)) {
		dtd->setDateTime(timeModel.displayString());
	}

	// Update speed button active states
	updateSpeedButtonStates(timeModel.data().speed);
}

bool TopBar::handleEvent(UI::InputEvent& event) {
	return dispatchEvent(event);
}

void TopBar::updateSpeedButtonStates(ecs::GameSpeed currentSpeed) {
	if (auto* btn = getChild<SpeedButton>(pauseButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::Paused);
	}
	if (auto* btn = getChild<SpeedButton>(speed1ButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::Normal);
	}
	if (auto* btn = getChild<SpeedButton>(speed2ButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::Fast);
	}
	if (auto* btn = getChild<SpeedButton>(speed3ButtonHandle)) {
		btn->setActive(currentSpeed == ecs::GameSpeed::VeryFast);
	}
}

void TopBar::positionElements() {
	float x = bounds.x + kLeftPadding;
	float centerY = bounds.y + (kBarHeight / 2.0F);

	// Position date/time display (vertically centered)
	if (auto* dtd = getChild<DateTimeDisplay>(dateTimeDisplayHandle)) {
		float dtHeight = dtd->getHeight();
		dtd->setPosition(x, centerY - (dtHeight / 2.0F));
		x += dtd->getWidth() + 20.0F;  // Add gap before speed buttons
	}

	// Position speed buttons (vertically centered)
	if (auto* btn = getChild<SpeedButton>(pauseButtonHandle)) {
		float btnHeight = btn->getHeight();
		btn->setPosition(x, centerY - (btnHeight / 2.0F));
		x += btn->getWidth() + kButtonSpacing;
	}
	if (auto* btn = getChild<SpeedButton>(speed1ButtonHandle)) {
		float btnHeight = btn->getHeight();
		btn->setPosition(x, centerY - (btnHeight / 2.0F));
		x += btn->getWidth() + kButtonSpacing;
	}
	if (auto* btn = getChild<SpeedButton>(speed2ButtonHandle)) {
		float btnHeight = btn->getHeight();
		btn->setPosition(x, centerY - (btnHeight / 2.0F));
		x += btn->getWidth() + kButtonSpacing;
	}
	if (auto* btn = getChild<SpeedButton>(speed3ButtonHandle)) {
		float btnHeight = btn->getHeight();
		btn->setPosition(x, centerY - (btnHeight / 2.0F));
	}

	// Position menu button on the right side
	if (auto* btn = getChild<UI::Button>(menuButtonHandle)) {
		float menuWidth = 60.0F;
		float menuHeight = 24.0F;
		float rightPadding = 12.0F;
		btn->setPosition(
			bounds.x + bounds.width - menuWidth - rightPadding,
			centerY - (menuHeight / 2.0F));
	}
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
