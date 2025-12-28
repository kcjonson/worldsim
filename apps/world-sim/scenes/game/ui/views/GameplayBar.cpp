#include "GameplayBar.h"

#include <theme/Theme.h>

namespace world_sim {

GameplayBar::GameplayBar(const Args& args)
	: onBuildClick(args.onBuildClick)
	, onActionSelected(args.onActionSelected)
	, onProductionSelected(args.onProductionSelected)
	, onFurnitureSelected(args.onFurnitureSelected) {

	// Create background rectangle (added first so it renders behind other children)
	backgroundHandle = addChild(UI::Rectangle(UI::Rectangle::Args{
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
		.id = "gameplay_bar_background"}));

	// Create Actions dropdown (stub items for now)
	actionsDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
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
		.openUpward = true}));

	// Create Build dropdown
	buildDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
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
		.openUpward = true}));

	// Create Production dropdown (stub items for now)
	productionDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
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
		.openUpward = true}));

	// Create Furniture dropdown (stub items for now)
	furnitureDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
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
		.openUpward = true}));
}

void GameplayBar::layout(const Foundation::Rect& newBounds) {
	Component::layout(newBounds);

	// Calculate and cache bar layout (used by positionElements)
	float totalButtonWidth = (kButtonWidth * 4.0F) + (kButtonSpacing * 3.0F);
	cachedBarWidth = totalButtonWidth + (kHorizontalPadding * 2.0F);
	cachedBarX = bounds.x + (bounds.width - cachedBarWidth) / 2.0F;
	cachedBarY = bounds.y + bounds.height - kBarHeight - kBottomMargin;

	if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
		bg->size = {cachedBarWidth, kBarHeight};
		bg->setPosition(cachedBarX, cachedBarY);
	}

	positionElements();
}

void GameplayBar::positionElements() {
	// Use cached layout values from layout()
	float buttonY = cachedBarY + (kBarHeight - kButtonHeight) / 2.0F;
	float x = cachedBarX + kHorizontalPadding;

	if (auto* dropdown = getChild<UI::DropdownButton>(actionsDropdownHandle)) {
		dropdown->setPosition(x, buttonY);
		x += kButtonWidth + kButtonSpacing;
	}

	if (auto* dropdown = getChild<UI::DropdownButton>(buildDropdownHandle)) {
		dropdown->setPosition(x, buttonY);
		x += kButtonWidth + kButtonSpacing;
	}

	if (auto* dropdown = getChild<UI::DropdownButton>(productionDropdownHandle)) {
		dropdown->setPosition(x, buttonY);
		x += kButtonWidth + kButtonSpacing;
	}

	if (auto* dropdown = getChild<UI::DropdownButton>(furnitureDropdownHandle)) {
		dropdown->setPosition(x, buttonY);
	}
}

bool GameplayBar::handleEvent(UI::InputEvent& event) {
	return dispatchEvent(event);
}

void GameplayBar::closeAllDropdowns() {
	if (auto* dropdown = getChild<UI::DropdownButton>(actionsDropdownHandle)) {
		dropdown->closeMenu();
	}
	if (auto* dropdown = getChild<UI::DropdownButton>(buildDropdownHandle)) {
		dropdown->closeMenu();
	}
	if (auto* dropdown = getChild<UI::DropdownButton>(productionDropdownHandle)) {
		dropdown->closeMenu();
	}
	if (auto* dropdown = getChild<UI::DropdownButton>(furnitureDropdownHandle)) {
		dropdown->closeMenu();
	}
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
