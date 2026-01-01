#include "StorageConfigDialog.h"

#include <components/TextInput/TextInput.h>
#include <font/FontRenderer.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>

namespace {
// Item list dimensions
constexpr float kItemHeight = 24.0F;
constexpr float kItemPadding = 8.0F;
constexpr float kCategoryHeaderHeight = 28.0F;
constexpr float kIndentWidth = 16.0F;
} // namespace

namespace world_sim {

StorageConfigDialog::StorageConfigDialog(const Args& args)
	: onCloseCallback(args.onClose) {
	createDialog();
}

void StorageConfigDialog::createDialog() {
	auto dialog = UI::Dialog(UI::Dialog::Args{
		.title = "Storage Settings",
		.size = {kDialogWidth, kDialogHeight},
		.onClose =
			[this]() {
				if (onCloseCallback) {
					onCloseCallback();
				}
			},
		.modal = true});
	dialogHandle = addChild(std::move(dialog));
}

void StorageConfigDialog::createColumns() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}

	auto  bounds = dialog->getContentBounds();
	float centerWidth = bounds.width - kLeftColumnWidth - kRightColumnWidth - kColumnGap * 2;

	// Create horizontal layout for the 3 columns
	auto contentLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {bounds.width, bounds.height},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top,
		.id = "content-layout"});

	// Left column - Available items (scrollable)
	leftColumnHandle = contentLayout.addChild(UI::ScrollContainer(
		UI::ScrollContainer::Args{.position = {0, 0},
								  .size = {kLeftColumnWidth, bounds.height},
								  .id = "item-list",
								  .margin = 0}));

	// Center column - Rule configuration
	centerColumnHandle = contentLayout.addChild(UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {centerWidth, bounds.height},
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top,
		.id = "rule-config",
		.margin = kColumnGap / 2}));

	// Right column - Rules for selected item (scrollable)
	rightColumnHandle = contentLayout.addChild(UI::ScrollContainer(
		UI::ScrollContainer::Args{.position = {0, 0},
								  .size = {kRightColumnWidth, bounds.height},
								  .id = "rules-list",
								  .margin = 0}));

	// Add content layout to Dialog
	contentLayoutHandle = dialog->addChild(std::move(contentLayout));

	contentCreated = true;
}

void StorageConfigDialog::open(ecs::EntityID containerId, const std::string& containerDefName,
							   float screenWidth, float screenHeight) {
	model.setContainer(containerId, containerDefName);

	// Initialize all categories as expanded
	expandedCategories.clear();
	for (size_t i = 0; i < model.categoryGroups().size(); ++i) {
		expandedCategories.push_back(static_cast<int>(i));
	}

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->setTitle(model.containerName() + " - Storage Settings");
		dialog->open(screenWidth, screenHeight);

		if (!contentCreated) {
			createColumns();
		}

		needsInitialRebuild = true;
	}
}

void StorageConfigDialog::close() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->close();
	}
	model.clear();
	worldPtr = nullptr;
}

bool StorageConfigDialog::isOpen() const {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->isOpen();
}

void StorageConfigDialog::update(ecs::World& world,
								 const engine::assets::AssetRegistry& registry,
								 float deltaTime) {
	if (!isOpen()) {
		return;
	}

	// Store world reference for event handlers
	worldPtr = &world;

	// Update dialog animation
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->update(deltaTime);
	}

	// Refresh model data
	auto updateType = model.refresh(world, registry);

	// Build initial content after first model refresh
	if (needsInitialRebuild) {
		needsInitialRebuild = false;
		rebuildFlatList();
		rebuildCenterColumn();
		rebuildRulesColumn();
	} else {
		if (updateType == StorageConfigDialogModel::UpdateType::Rules ||
			updateType == StorageConfigDialogModel::UpdateType::Full) {
			rebuildFlatList();
			rebuildRulesColumn();
		}
		if (updateType == StorageConfigDialogModel::UpdateType::Inventory) {
			rebuildFlatList();
		}
	}

	if (needsCenterRebuild) {
		needsCenterRebuild = false;
		rebuildCenterColumn();
	}

	if (needsRulesRebuild) {
		needsRulesRebuild = false;
		rebuildRulesColumn();
	}
}

UI::LayoutContainer* StorageConfigDialog::getContentLayout() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return nullptr;
	}
	return dialog->getChild<UI::LayoutContainer>(contentLayoutHandle);
}

void StorageConfigDialog::render() {
	if (!isOpen()) {
		return;
	}

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->render();
	}

	// Render item list primitives (direct rendering)
	renderItemList();
}

bool StorageConfigDialog::handleEvent(UI::InputEvent& event) {
	if (!isOpen()) {
		return false;
	}

	// Item list - direct hit testing
	if (event.type == UI::InputEvent::Type::MouseMove) {
		itemHoveredIndex = getItemIndexAtPosition(event.position);
	} else if (event.type == UI::InputEvent::Type::MouseDown &&
			   event.button == engine::MouseButton::Left) {
		int index = getItemIndexAtPosition(event.position);
		if (index >= 0) {
			handleItemClick(index);
			event.consume();
			return true;
		}
	}

	// Let Dialog handle all other events
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr && dialog->handleEvent(event)) {
		return true;
	}

	return false;
}

bool StorageConfigDialog::containsPoint(Foundation::Vec2 point) const {
	if (!isOpen()) {
		return false;
	}
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->containsPoint(point);
}

void StorageConfigDialog::rebuildFlatList() {
	flatItems.clear();

	const auto& groups = model.categoryGroups();
	for (size_t gi = 0; gi < groups.size(); ++gi) {
		// Add category header
		flatItems.push_back({FlatItem::Type::CategoryHeader, gi});

		// Check if expanded
		bool expanded = std::find(expandedCategories.begin(), expandedCategories.end(),
								  static_cast<int>(gi)) != expandedCategories.end();

		if (expanded) {
			for (size_t itemIdx : groups[gi].itemIndices) {
				flatItems.push_back({FlatItem::Type::Item, itemIdx});
			}
		}
	}
}

Foundation::Rect StorageConfigDialog::getItemBounds(int flatIndex) const {
	if (flatIndex < 0 || flatIndex >= static_cast<int>(flatItems.size())) {
		return {0.0F, 0.0F, 0.0F, 0.0F};
	}

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return {0.0F, 0.0F, 0.0F, 0.0F};
	}

	auto  bounds = dialog->getContentBounds();
	float leftX = bounds.x;
	float leftY = bounds.y;

	// Get scroll offset
	float scrollOffset = 0.0F;
	auto* contentLayout = const_cast<StorageConfigDialog*>(this)->getContentLayout();
	if (contentLayout != nullptr) {
		auto* leftCol = contentLayout->getChild<UI::ScrollContainer>(leftColumnHandle);
		if (leftCol != nullptr) {
			scrollOffset = leftCol->getScrollPosition();
		}
	}

	// Calculate Y position based on items above
	float itemY = leftY - scrollOffset;
	for (int i = 0; i < flatIndex; ++i) {
		if (flatItems[i].type == FlatItem::Type::CategoryHeader) {
			itemY += kCategoryHeaderHeight;
		} else {
			itemY += kItemHeight;
		}
	}

	float itemHeight = (flatItems[flatIndex].type == FlatItem::Type::CategoryHeader)
						   ? kCategoryHeaderHeight
						   : kItemHeight;

	float indentX = (flatItems[flatIndex].type == FlatItem::Type::Item) ? kIndentWidth : 0.0F;

	return {leftX + kItemPadding + indentX, itemY, kLeftColumnWidth - kItemPadding * 2 - indentX,
			itemHeight};
}

int StorageConfigDialog::getItemIndexAtPosition(Foundation::Vec2 pos) const {
	for (size_t i = 0; i < flatItems.size(); ++i) {
		Foundation::Rect bounds = getItemBounds(static_cast<int>(i));
		if (pos.x >= bounds.x && pos.x < bounds.x + bounds.width && pos.y >= bounds.y &&
			pos.y < bounds.y + bounds.height) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void StorageConfigDialog::renderItemList() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* leftCol = contentLayout->getChild<UI::ScrollContainer>(leftColumnHandle);
	if (leftCol == nullptr) {
		return;
	}

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}

	auto  dialogBounds = dialog->getContentBounds();
	float scrollOffset = leftCol->getScrollPosition();

	// Render scroll container
	leftCol->render();

	// Calculate viewport bounds
	Foundation::Rect viewBounds{dialogBounds.x, dialogBounds.y, kLeftColumnWidth,
								dialogBounds.height};

	// Colors
	auto transparentBg = Foundation::Color{0.0F, 0.0F, 0.0F, 0.0F};
	auto hoverBg = Foundation::Color{1.0F, 1.0F, 1.0F, 0.08F};
	auto selectedBg = Foundation::Color{0.0F, 0.0F, 0.0F, 0.2F};
	auto borderColor = Foundation::Color{1.0F, 1.0F, 1.0F, 0.1F};
	auto categoryBg = Foundation::Color{1.0F, 1.0F, 1.0F, 0.03F};

	const auto& groups = model.categoryGroups();
	const auto& items = model.availableItems();

	for (size_t fi = 0; fi < flatItems.size(); ++fi) {
		const auto& flat = flatItems[fi];
		int			idx = static_cast<int>(fi);
		auto		bounds = getItemBounds(idx);

		// Skip items outside viewport
		if (bounds.y + bounds.height < viewBounds.y ||
			bounds.y > viewBounds.y + viewBounds.height) {
			continue;
		}

		if (flat.type == FlatItem::Type::CategoryHeader) {
			// Category header
			const auto& group = groups[flat.index];
			bool		expanded =
				std::find(expandedCategories.begin(), expandedCategories.end(),
						  static_cast<int>(flat.index)) != expandedCategories.end();

			// Background
			Renderer::Primitives::drawRect(
				{.bounds = bounds, .style = {.fill = categoryBg}, .id = "category-bg"});

			// Expand indicator
			std::string indicator = expanded ? "v " : "> ";

			// Draw text
			Renderer::Primitives::drawText(
				{.text = indicator + group.label,
				 .position = {bounds.x + 4.0F, bounds.y + (kCategoryHeaderHeight - 14.0F) / 2.0F},
				 .scale = 13.0F / 16.0F,
				 .color = UI::Theme::Colors::textTitle,
				 .id = "category-text"});

		} else {
			// Item row
			const auto& item = items[flat.index];

			// Check if this item is selected
			bool isSelected = (item.defName == model.selectedItemDefName());

			// Background
			Foundation::Color bgColor = transparentBg;
			if (isSelected) {
				bgColor = selectedBg;
			} else if (idx == itemHoveredIndex) {
				bgColor = hoverBg;
			}

			Renderer::Primitives::drawRect(
				{.bounds = bounds, .style = {.fill = bgColor}, .id = "item-bg"});

			// Bottom border
			Renderer::Primitives::drawRect(
				{.bounds = {bounds.x, bounds.y + bounds.height - 1.0F, bounds.width, 1.0F},
				 .style = {.fill = borderColor},
				 .id = "item-border"});

			// Item label with counts
			std::string label = item.label;

			// Add count indicator
			std::string countStr;
			if (item.requestedCount == 0 && item.hasRules) {
				// Unlimited
				countStr = std::to_string(item.currentCount) + "/~";
			} else if (item.hasRules) {
				countStr =
					std::to_string(item.currentCount) + "/" + std::to_string(item.requestedCount);
			} else {
				countStr = std::to_string(item.currentCount) + "/0";
			}

			// Text color based on rules
			Foundation::Color textColor =
				item.hasRules ? UI::Theme::Colors::textBody : UI::Theme::Colors::textMuted;

			// Draw item label
			Renderer::Primitives::drawText(
				{.text = label,
				 .position = {bounds.x + 4.0F, bounds.y + (kItemHeight - 12.0F) / 2.0F},
				 .scale = 12.0F / 16.0F,
				 .color = textColor,
				 .id = "item-text"});

			// Draw count on the right
			float countWidth = 50.0F;
			Renderer::Primitives::drawText(
				{.text = countStr,
				 .position = {bounds.x + bounds.width - countWidth,
							  bounds.y + (kItemHeight - 11.0F) / 2.0F},
				 .scale = 11.0F / 16.0F,
				 .color = UI::Theme::Colors::textMuted,
				 .id = "item-count"});
		}
	}

	// Update scroll content height
	float totalHeight = 0.0F;
	for (const auto& flat : flatItems) {
		totalHeight +=
			(flat.type == FlatItem::Type::CategoryHeader) ? kCategoryHeaderHeight : kItemHeight;
	}
	leftCol->setContentHeight(totalHeight + 10.0F);
}

void StorageConfigDialog::rebuildCenterColumn() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* centerCol = contentLayout->getChild<UI::LayoutContainer>(centerColumnHandle);
	if (centerCol == nullptr) {
		return;
	}

	centerCol->clearChildren();

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}
	auto  bounds = dialog->getContentBounds();
	float centerWidth = bounds.width - kLeftColumnWidth - kRightColumnWidth - kColumnGap * 2;

	// Bulk action buttons at top
	auto bulkRow = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {0, 32},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Center,
		.margin = 4.0F});

	bulkRow.addChild(UI::Button(UI::Button::Args{
		.label = "Select All",
		.size = {90, 28},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { handleSelectAll(); },
		.margin = 2.0F}));

	bulkRow.addChild(UI::Button(UI::Button::Args{
		.label = "None",
		.size = {60, 28},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { handleSelectNone(); },
		.margin = 2.0F}));

	centerCol->addChild(std::move(bulkRow));

	const auto* selectedData = model.selectedItemData();
	if (selectedData == nullptr) {
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = "Select an item to configure",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 14},
			.margin = 8.0F}));
		return;
	}

	// Selected item name
	centerCol->addChild(UI::Text(UI::Text::Args{
		.text = selectedData->label,
		.style = {.color = UI::Theme::Colors::textTitle, .fontSize = 16},
		.margin = 8.0F}));

	// ADD RULE section header
	centerCol->addChild(UI::Text(UI::Text::Args{
		.text = "ADD RULE",
		.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
		.margin = 6.0F}));

	// Priority dropdown
	auto priorityRow = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {centerWidth - 16, 36},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Center,
		.margin = 2.0F});

	priorityRow.addChild(UI::Text(UI::Text::Args{
		.width = 80,
		.text = "Priority:",
		.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
		.margin = 4.0F}));

	prioritySelectHandle = priorityRow.addChild(UI::Select(UI::Select::Args{
		.size = {120, 32},
		.options =
			{
				{.label = "Critical", .value = "critical"},
				{.label = "High", .value = "high"},
				{.label = "Medium", .value = "medium"},
				{.label = "Low", .value = "low"},
			},
		.value = "medium",
		.onChange =
			[this](const std::string& value) {
				if (value == "critical") {
					model.setPriority(ecs::StoragePriority::Critical);
				} else if (value == "high") {
					model.setPriority(ecs::StoragePriority::High);
				} else if (value == "low") {
					model.setPriority(ecs::StoragePriority::Low);
				} else {
					model.setPriority(ecs::StoragePriority::Medium);
				}
			},
		.margin = 4.0F}));

	centerCol->addChild(std::move(priorityRow));

	// Min Amount
	auto minRow = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {centerWidth - 16, 36},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Center,
		.margin = 2.0F});

	minRow.addChild(UI::Text(UI::Text::Args{
		.width = 80,
		.text = "Min:",
		.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
		.margin = 4.0F}));

	minAmountHandle = minRow.addChild(UI::TextInput(UI::TextInput::Args{
		.size = {80, 32},
		.text = "0",
		.placeholder = "0",
		.margin = 4.0F,
		.onChange =
			[this](const std::string& value) {
				try {
					model.setMinAmount(static_cast<uint32_t>(std::stoul(value)));
				} catch (...) {
					model.setMinAmount(0);
				}
			}}));

	centerCol->addChild(std::move(minRow));

	// Max Amount with Unlimited checkbox
	auto maxRow = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {centerWidth - 16, 36},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Center,
		.margin = 2.0F});

	maxRow.addChild(UI::Text(UI::Text::Args{
		.width = 80,
		.text = "Max:",
		.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
		.margin = 4.0F}));

	maxAmountHandle = maxRow.addChild(UI::TextInput(UI::TextInput::Args{
		.size = {80, 32},
		.text = "0",
		.placeholder = "0",
		.enabled = !model.pendingRuleUnlimited(),
		.margin = 4.0F,
		.onChange =
			[this](const std::string& value) {
				try {
					model.setMaxAmount(static_cast<uint32_t>(std::stoul(value)));
				} catch (...) {
					model.setMaxAmount(0);
				}
			}}));

	// Toggle button for unlimited
	std::string unlimitedLabel = model.pendingRuleUnlimited() ? "[X] Unlimited" : "[ ] Unlimited";
	unlimitedCheckHandle = maxRow.addChild(UI::Button(UI::Button::Args{
		.label = unlimitedLabel,
		.size = {100, 32},
		.type = UI::Button::Type::Secondary,
		.onClick =
			[this]() {
				model.setUnlimited(!model.pendingRuleUnlimited());
				needsCenterRebuild = true;
			},
		.margin = 4.0F}));

	centerCol->addChild(std::move(maxRow));

	// Quality dropdowns (disabled for now)
	centerCol->addChild(UI::Text(UI::Text::Args{
		.text = "Min Quality:",
		.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 12},
		.margin = 6.0F}));

	centerCol->addChild(UI::Select(UI::Select::Args{
		.size = {120, 32},
		.options = {{.label = "Any", .value = "any"}},
		.value = "any",
		.margin = 4.0F}));

	centerCol->addChild(UI::Text(UI::Text::Args{
		.text = "Max Quality:",
		.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 12},
		.margin = 6.0F}));

	centerCol->addChild(UI::Select(UI::Select::Args{
		.size = {120, 32},
		.options = {{.label = "Any", .value = "any"}},
		.value = "any",
		.margin = 4.0F}));

	// Action buttons
	auto buttonRow = UI::LayoutContainer(UI::LayoutContainer::Args{
		.size = {centerWidth - 16, 40},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Center,
		.margin = 8.0F});

	addRuleButtonHandle = buttonRow.addChild(UI::Button(UI::Button::Args{
		.label = "Add Rule",
		.size = {100, 36},
		.type = UI::Button::Type::Primary,
		.onClick = [this]() { handleAddRule(); },
		.margin = 4.0F}));

	addAllButtonHandle = buttonRow.addChild(UI::Button(UI::Button::Args{
		.label = "Add All",
		.size = {80, 36},
		.type = UI::Button::Type::Secondary,
		.onClick = [this]() { handleAddAll(); },
		.margin = 4.0F}));

	centerCol->addChild(std::move(buttonRow));
}

void StorageConfigDialog::rebuildRulesColumn() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* rightCol = contentLayout->getChild<UI::ScrollContainer>(rightColumnHandle);
	if (rightCol == nullptr) {
		return;
	}

	rightCol->clearChildren();
	ruleDeleteHandles.clear();

	auto rulesLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {kRightColumnWidth - 16, 0},
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top});

	const auto* selectedData = model.selectedItemData();
	if (selectedData == nullptr) {
		rulesLayout.addChild(UI::Text(UI::Text::Args{
			.text = "RULES",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
			.margin = 4.0F}));

		rulesLayout.addChild(UI::Text(UI::Text::Args{
			.text = "Select an item",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 12},
			.margin = 4.0F}));
	} else {
		// Header
		rulesLayout.addChild(UI::Text(UI::Text::Args{
			.text = selectedData->label + " RULES",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
			.margin = 4.0F}));

		const auto& rules = model.rulesForSelectedItem();
		if (rules.empty()) {
			rulesLayout.addChild(UI::Text(UI::Text::Args{
				.text = "No rules configured",
				.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 12},
				.margin = 4.0F}));
		} else {
			for (size_t i = 0; i < rules.size(); ++i) {
				const auto& rule = rules[i];

				// Rule summary
				std::string summary;
				if (rule.isWildcard) {
					summary = "* " + rule.label;
				} else {
					summary = rule.label;
				}

				// Priority and amounts
				std::string details = ecs::storagePriorityToString(rule.priority);
				if (rule.minAmount > 0) {
					details += ", Min: " + std::to_string(rule.minAmount);
				}
				if (rule.maxAmount > 0) {
					details += ", Max: " + std::to_string(rule.maxAmount);
				} else {
					details += ", Unlimited";
				}

				// Rule row
				auto ruleRow = UI::LayoutContainer(UI::LayoutContainer::Args{
					.size = {kRightColumnWidth - 32, 48},
					.direction = UI::Direction::Vertical,
					.hAlign = UI::HAlign::Left,
					.vAlign = UI::VAlign::Top,
					.margin = 2.0F});

				ruleRow.addChild(UI::Text(UI::Text::Args{
					.text = summary,
					.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
					.margin = 1.0F}));

				ruleRow.addChild(UI::Text(UI::Text::Args{
					.text = details,
					.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 10},
					.margin = 1.0F}));

				// Delete button on the side
				size_t ruleIndex = rule.ruleIndex;
				ruleDeleteHandles.push_back(ruleRow.addChild(UI::Button(UI::Button::Args{
					.label = "X",
					.size = {24, 24},
					.type = UI::Button::Type::Secondary,
					.onClick = [this, ruleIndex]() { handleRemoveRule(ruleIndex); },
					.margin = 2.0F})));

				rulesLayout.addChild(std::move(ruleRow));
			}
		}
	}

	rightCol->setContentHeight(rulesLayout.getHeight() + 10);
	rightCol->addChild(std::move(rulesLayout));
}

void StorageConfigDialog::handleItemClick(int flatIndex) {
	if (flatIndex < 0 || flatIndex >= static_cast<int>(flatItems.size())) {
		return;
	}

	const auto& flat = flatItems[flatIndex];

	if (flat.type == FlatItem::Type::CategoryHeader) {
		// Toggle category expansion
		int catIdx = static_cast<int>(flat.index);
		auto iter = std::find(expandedCategories.begin(), expandedCategories.end(), catIdx);
		if (iter != expandedCategories.end()) {
			expandedCategories.erase(iter);
		} else {
			expandedCategories.push_back(catIdx);
		}
		rebuildFlatList();
	} else {
		// Select item
		const auto& items = model.availableItems();
		if (flat.index < items.size()) {
			model.selectItem(items[flat.index].defName);
			itemSelectedIndex = flatIndex;
			needsCenterRebuild = true;
			needsRulesRebuild = true;
		}
	}
}

void StorageConfigDialog::handleCategoryToggle(int categoryIndex) {
	auto iter = std::find(expandedCategories.begin(), expandedCategories.end(), categoryIndex);
	if (iter != expandedCategories.end()) {
		expandedCategories.erase(iter);
	} else {
		expandedCategories.push_back(categoryIndex);
	}
	rebuildFlatList();
}

void StorageConfigDialog::handleAddRule() {
	if (worldPtr == nullptr) {
		return;
	}
	if (model.addRule(*worldPtr)) {
		needsRulesRebuild = true;
		rebuildFlatList();
	}
}

void StorageConfigDialog::handleAddAll() {
	if (worldPtr == nullptr) {
		return;
	}
	if (model.addCategoryWildcard(*worldPtr)) {
		needsRulesRebuild = true;
		rebuildFlatList();
	}
}

void StorageConfigDialog::handleRemoveRule(size_t ruleIndex) {
	if (worldPtr == nullptr) {
		return;
	}
	model.removeRule(*worldPtr, ruleIndex);
	needsRulesRebuild = true;
	rebuildFlatList();
}

void StorageConfigDialog::handleSelectAll() {
	if (worldPtr == nullptr) {
		return;
	}
	model.addAllCategories(*worldPtr);
	needsRulesRebuild = true;
	rebuildFlatList();
}

void StorageConfigDialog::handleSelectNone() {
	if (worldPtr == nullptr) {
		return;
	}
	model.removeAllRules(*worldPtr);
	needsRulesRebuild = true;
	rebuildFlatList();
}

} // namespace world_sim
