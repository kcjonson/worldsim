#include "GameplayBar.h"

#include <theme/Theme.h>
#include <utils/Log.h>

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

	// Create Build dropdown - shows directly placeable structures (walls, floors, etc.)
	// Currently empty as we don't have those yet
	buildDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
		.label = "Build",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items =
			{
				UI::DropdownItem{
					.label = "(Coming Soon)",
					.enabled = false,
					.onSelect = []() {}},
			},
		.id = "build_dropdown",
		.openUpward = true}));

	// Create Production dropdown - production stations that can be placed
	// Items are populated dynamically via setProductionItems()
	productionDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
		.label = "Production",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items = {}, // Populated dynamically
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

void GameplayBar::setProductionItems(const std::vector<std::pair<std::string, std::string>>& items) {
	auto* dropdown = getChild<UI::DropdownButton>(productionDropdownHandle);
	if (dropdown == nullptr) {
		LOG_WARNING(Game, "[GameplayBar] Production dropdown not found");
		return;
	}

	LOG_INFO(Game, "[GameplayBar] Setting %zu production items", items.size());

	std::vector<UI::DropdownItem> dropdownItems;
	for (const auto& [defName, label] : items) {
		LOG_INFO(Game, "[GameplayBar] Adding production item: %s (%s)", label.c_str(), defName.c_str());
		dropdownItems.push_back(UI::DropdownItem{
			.label = label,
			.onSelect = [this, defName]() {
				LOG_INFO(Game, "[GameplayBar] Production item selected: %s", defName.c_str());
				if (onProductionSelected) {
					onProductionSelected(defName);
				} else {
					LOG_WARNING(Game, "[GameplayBar] onProductionSelected callback is null!");
				}
			}});
	}

	dropdown->setItems(std::move(dropdownItems));
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
