#include "GameplayBar.h"

#include <theme/Theme.h>

namespace world_sim {

GameplayBar::GameplayBar(const Args& args)
	: onBuildClick(args.onBuildClick)
	, onActionSelected(args.onActionSelected)
	, onProductionSelected(args.onProductionSelected)
	, onFurnitureSelected(args.onFurnitureSelected) {

	// Create background rectangle
	background = std::make_unique<UI::Rectangle>(UI::Rectangle::Args{
		.position = {0.0F, 0.0F},
		.size = {400.0F, kBarHeight},  // Width will be set in layout()
		.style =
			{
				.fill = UI::Theme::Colors::sidebarBackground,
				.border =
					Foundation::BorderStyle{
						.color = UI::Theme::Colors::cardBorder,
						.width = 1.0F,
						.position = Foundation::BorderPosition::Inside},
			},
		.id = "gameplay_bar_background",
		.zIndex = 400});

	// Create Actions dropdown (stub items for now)
	actionsDropdown = std::make_unique<UI::DropdownButton>(UI::DropdownButton::Args{
		.label = "Actions",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items =
			{
				UI::DropdownItem{
					.label = "Hunt",
					.onSelect = [this]() {
						if (onActionSelected) onActionSelected("hunt");
					}},
				UI::DropdownItem{
					.label = "Harvest",
					.onSelect = [this]() {
						if (onActionSelected) onActionSelected("harvest");
					}},
				UI::DropdownItem{
					.label = "Haul",
					.onSelect = [this]() {
						if (onActionSelected) onActionSelected("haul");
					}},
			},
		.id = "actions_dropdown",
		.openUpward = true});

	// Create Build dropdown
	buildDropdown = std::make_unique<UI::DropdownButton>(UI::DropdownButton::Args{
		.label = "Build",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items =
			{
				UI::DropdownItem{
					.label = "Structures...",
					.onSelect = [this]() {
						if (onBuildClick) onBuildClick();
					}},
			},
		.id = "build_dropdown",
		.openUpward = true});

	// Create Production dropdown (stub items for now)
	productionDropdown = std::make_unique<UI::DropdownButton>(UI::DropdownButton::Args{
		.label = "Production",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items =
			{
				UI::DropdownItem{
					.label = "Crafting",
					.onSelect = [this]() {
						if (onProductionSelected) onProductionSelected("crafting");
					}},
				UI::DropdownItem{
					.label = "Cooking",
					.onSelect = [this]() {
						if (onProductionSelected) onProductionSelected("cooking");
					}},
			},
		.id = "production_dropdown",
		.openUpward = true});

	// Create Furniture dropdown (stub items for now)
	furnitureDropdown = std::make_unique<UI::DropdownButton>(UI::DropdownButton::Args{
		.label = "Furniture",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items =
			{
				UI::DropdownItem{
					.label = "Beds",
					.onSelect = [this]() {
						if (onFurnitureSelected) onFurnitureSelected("beds");
					}},
				UI::DropdownItem{
					.label = "Tables",
					.onSelect = [this]() {
						if (onFurnitureSelected) onFurnitureSelected("tables");
					}},
				UI::DropdownItem{
					.label = "Storage",
					.onSelect = [this]() {
						if (onFurnitureSelected) onFurnitureSelected("storage");
					}},
			},
		.id = "furniture_dropdown",
		.openUpward = true});
}

void GameplayBar::layout(const Foundation::Rect& bounds) {
	viewportBounds = bounds;

	// Calculate total width of all buttons
	float totalButtonWidth = (kButtonWidth * 4.0F) + (kButtonSpacing * 3.0F);
	float barWidth = totalButtonWidth + 24.0F;  // Add padding

	// Position bar at bottom center
	float barX = bounds.x + (bounds.width - barWidth) / 2.0F;
	float barY = bounds.y + bounds.height - kBarHeight - kBottomMargin;

	if (background) {
		background->size = {barWidth, kBarHeight};
		background->setPosition(barX, barY);
	}

	positionElements();
}

void GameplayBar::positionElements() {
	// Calculate total width and starting position
	float totalButtonWidth = (kButtonWidth * 4.0F) + (kButtonSpacing * 3.0F);
	float barWidth = totalButtonWidth + 24.0F;

	float barX = viewportBounds.x + (viewportBounds.width - barWidth) / 2.0F;
	float barY = viewportBounds.y + viewportBounds.height - kBarHeight - kBottomMargin;

	// Center buttons within bar
	float buttonY = barY + (kBarHeight - kButtonHeight) / 2.0F;
	float x = barX + 12.0F;  // Left padding

	if (actionsDropdown) {
		actionsDropdown->setPosition(x, buttonY);
		x += kButtonWidth + kButtonSpacing;
	}

	if (buildDropdown) {
		buildDropdown->setPosition(x, buttonY);
		x += kButtonWidth + kButtonSpacing;
	}

	if (productionDropdown) {
		productionDropdown->setPosition(x, buttonY);
		x += kButtonWidth + kButtonSpacing;
	}

	if (furnitureDropdown) {
		furnitureDropdown->setPosition(x, buttonY);
	}
}

bool GameplayBar::handleEvent(UI::InputEvent& event) {
	// Try dropdowns in order (right to left so topmost gets priority)
	if (furnitureDropdown && furnitureDropdown->handleEvent(event)) {
		return true;
	}
	if (productionDropdown && productionDropdown->handleEvent(event)) {
		return true;
	}
	if (buildDropdown && buildDropdown->handleEvent(event)) {
		return true;
	}
	if (actionsDropdown && actionsDropdown->handleEvent(event)) {
		return true;
	}

	return false;
}

void GameplayBar::render() {
	if (background) {
		background->render();
	}

	if (actionsDropdown) {
		actionsDropdown->render();
	}
	if (buildDropdown) {
		buildDropdown->render();
	}
	if (productionDropdown) {
		productionDropdown->render();
	}
	if (furnitureDropdown) {
		furnitureDropdown->render();
	}
}

void GameplayBar::closeAllDropdowns() {
	if (actionsDropdown) {
		actionsDropdown->closeMenu();
	}
	if (buildDropdown) {
		buildDropdown->closeMenu();
	}
	if (productionDropdown) {
		productionDropdown->closeMenu();
	}
	if (furnitureDropdown) {
		furnitureDropdown->closeMenu();
	}
}

}  // namespace world_sim
