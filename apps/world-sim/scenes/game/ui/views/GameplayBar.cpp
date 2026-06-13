#include "GameplayBar.h"

#include <theme/Theme.h>
#include <utils/Log.h>

namespace world_sim {

GameplayBar::GameplayBar(const Args& args)
	: onProductionSelected(args.onProductionSelected)
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

	// Create Production dropdown - production stations that can be placed.
	// Items are populated dynamically via setProductionItems().
	productionDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
		.label = "Production",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items = {}, // Populated dynamically
		.id = "production_dropdown",
		.openUpward = true}));

	// Create Furniture dropdown - placeable furniture (storage containers, etc.).
	// Items are populated dynamically via setFurnitureItems().
	furnitureDropdownHandle = addChild(UI::DropdownButton(UI::DropdownButton::Args{
		.label = "Furniture",
		.position = {0.0F, 0.0F},
		.buttonSize = {kButtonWidth, kButtonHeight},
		.items = {}, // Populated dynamically
		.id = "furniture_dropdown",
		.openUpward = true}));
}

void GameplayBar::layout(const Foundation::Rect& newBounds) {
	Component::layout(newBounds);

	// Calculate and cache bar layout (used by positionElements)
	float totalButtonWidth = (kButtonWidth * kButtonCount) + (kButtonSpacing * (kButtonCount - 1));
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
	if (auto* dropdown = getChild<UI::DropdownButton>(productionDropdownHandle)) {
		dropdown->closeMenu();
	}
	if (auto* dropdown = getChild<UI::DropdownButton>(furnitureDropdownHandle)) {
		dropdown->closeMenu();
	}
}

void GameplayBar::setProductionItems(const std::vector<std::pair<std::string, std::string>>& items) {
	setDropdownItems(productionDropdownHandle, items, onProductionSelected);
}

void GameplayBar::setFurnitureItems(const std::vector<std::pair<std::string, std::string>>& items) {
	setDropdownItems(furnitureDropdownHandle, items, onFurnitureSelected);
}

void GameplayBar::setDropdownItems(
	UI::LayerHandle handle,
	const std::vector<std::pair<std::string, std::string>>& items,
	const std::function<void(const std::string&)>& callback
) {
	auto* dropdown = getChild<UI::DropdownButton>(handle);
	if (dropdown == nullptr) {
		LOG_WARNING(Game, "[GameplayBar] Dropdown not found while setting items");
		return;
	}

	std::vector<UI::DropdownItem> dropdownItems;
	dropdownItems.reserve(items.size());
	for (const auto& [defName, label] : items) {
		dropdownItems.push_back(UI::DropdownItem{
			.label = label,
			.onSelect = [callback, defName]() {
				if (callback) {
					callback(defName);
				}
			}});
	}

	dropdown->setItems(std::move(dropdownItems));
}

// render() inherited from Component - automatically renders all children

}  // namespace world_sim
