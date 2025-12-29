#include "ColonistDetailsDialog.h"

#include <components/progress/ProgressBar.h>
#include <components/scroll/ScrollContainer.h>
#include <components/treeview/TreeView.h>
#include <ecs/components/Needs.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>

#include <sstream>
#include <unordered_map>

namespace world_sim {

ColonistDetailsDialog::ColonistDetailsDialog(const Args& args)
	: onCloseCallback(args.onClose) {
	createDialog();
}

void ColonistDetailsDialog::createDialog() {
	// Create the underlying Dialog component (non-modal so game continues)
	auto dialog = UI::Dialog(UI::Dialog::Args{
		.title = "Colonist Details",
		.size = {kDialogWidth, kDialogHeight},
		.onClose = [this]() {
			if (onCloseCallback) {
				onCloseCallback();
			}
		},
		.modal = false // Game continues running, no overlay
	});
	dialogHandle = addChild(std::move(dialog));
}

void ColonistDetailsDialog::createTabBar() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) return;

	auto contentBounds = dialog->getContentBounds();

	// Create TabBar at top of content area
	auto tabBar = UI::TabBar(UI::TabBar::Args{
		.position = {contentBounds.x, contentBounds.y},
		.width = contentBounds.width,
		.tabs = {
			{.id = kTabBio, .label = "Bio"},
			{.id = kTabHealth, .label = "Health"},
			{.id = kTabSocial, .label = "Social"},
			{.id = kTabGear, .label = "Gear"},
			{.id = kTabMemory, .label = "Memory"}
		},
		.selectedId = kTabBio,
		.onSelect = [this](const std::string& tabId) { switchToTab(tabId); }
	});
	tabBarHandle = addChild(std::move(tabBar));
}

void ColonistDetailsDialog::createTabContent() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) return;

	auto contentBounds = dialog->getContentBounds();
	float contentY = contentBounds.y + kTabBarHeight + kContentPadding;
	float contentHeight = contentBounds.height - kTabBarHeight - kContentPadding;

	// Font sizes matching NeedBar labels (12px normal, 10px compact)
	constexpr float kTitleSize = 14.0F;
	constexpr float kLabelSize = 12.0F;  // Matches NeedBar normal font
	constexpr float kBodySize = 12.0F;   // Same as labels for consistency
	constexpr float kSmallSize = 10.0F;  // Matches NeedBar compact font

	// Colors - use bright colors to match NeedBar white labels
	auto titleColor = Foundation::Color::white();
	auto labelColor = Foundation::Color{0.85F, 0.85F, 0.90F, 1.0F};  // Slightly dimmer than white
	auto bodyColor = Foundation::Color{0.80F, 0.80F, 0.85F, 1.0F};
	auto mutedColor = Foundation::Color{0.55F, 0.55F, 0.60F, 1.0F};  // Brighter than textMuted

	// Bio tab content
	{
		auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {contentBounds.x, contentY},
			.size = {contentBounds.width, contentHeight},
			.direction = UI::Direction::Vertical,
			.id = "bio_content"
		});

		// Name (title size) - height must be explicit since Text doesn't compute from font
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kTitleSize,
			.text = "--",
			.style = {.color = titleColor, .fontSize = kTitleSize},
			.margin = 2.0F
		}));

		// Age
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Age: --",
			.style = {.color = bodyColor, .fontSize = kBodySize},
			.margin = 2.0F
		}));

		// Mood
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Mood: --",
			.style = {.color = bodyColor, .fontSize = kBodySize},
			.margin = 2.0F
		}));

		// Current task
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Current: Idle",
			.style = {.color = bodyColor, .fontSize = kBodySize},
			.margin = 2.0F
		}));

		// Section: Traits
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Traits",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 6.0F
		}));

		layout.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "None defined",
			.style = {.color = mutedColor, .fontSize = kSmallSize},
			.margin = 2.0F
		}));

		// Section: Background
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Background",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 6.0F
		}));

		layout.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Not available",
			.style = {.color = mutedColor, .fontSize = kSmallSize},
			.margin = 2.0F
		}));

		bioContentHandle = addChild(std::move(layout));
	}

	// Health tab content - two column layout
	// Left: Mood + Needs bars + Mood modifiers  |  Right: Body parts & ailments
	{
		float columnGap = 16.0F;
		float columnWidth = (contentBounds.width - columnGap) / 2.0F;
		float needBarWidth = columnWidth - 4.0F;
		float needBarHeight = 12.0F; // Slightly bigger than compact for readability

		// Outer horizontal container for two columns
		auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {contentBounds.x, contentY},
			.size = {contentBounds.width, contentHeight},
			.direction = UI::Direction::Horizontal,
			.id = "health_content"
		});

		// LEFT COLUMN: Mood + Needs + Modifiers
		auto leftColumn = UI::LayoutContainer(UI::LayoutContainer::Args{
			.size = {columnWidth, contentHeight},
			.direction = UI::Direction::Vertical,
			.id = "health_left"
		});

		// Mood header
		leftColumn.addChild(UI::Text(UI::Text::Args{
			.height = kTitleSize,
			.text = "Mood: -- (Unknown)",
			.style = {.color = titleColor, .fontSize = kTitleSize},
			.margin = 2.0F
		}));

		// Needs section header
		leftColumn.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Needs",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 4.0F
		}));

		// Compact need bars
		for (size_t i = 0; i < static_cast<size_t>(ecs::NeedType::Count); ++i) {
			leftColumn.addChild(UI::ProgressBar(UI::ProgressBar::Args{
				.size = {needBarWidth, needBarHeight},
				.value = 1.0F,
				.fillColor = UI::Theme::Colors::statusActive,
				.label = ecs::kNeedLabels[i],
				.labelWidth = 50.0F,
				.margin = 1.0F
			}));
		}

		// Mood modifiers section
		leftColumn.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Mood Modifiers",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 6.0F
		}));

		leftColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "No active modifiers",
			.style = {.color = mutedColor, .fontSize = kSmallSize},
			.margin = 2.0F
		}));

		layout.addChild(std::move(leftColumn));

		// RIGHT COLUMN: Body & Ailments
		auto rightColumn = UI::LayoutContainer(UI::LayoutContainer::Args{
			.size = {columnWidth, contentHeight},
			.direction = UI::Direction::Vertical,
			.id = "health_right"
		});

		// Body & Ailments header
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Body & Ailments",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 4.0F
		}));

		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "No ailments",
			.style = {.color = mutedColor, .fontSize = kSmallSize},
			.margin = 2.0F
		}));

		// Placeholder for body part list (future)
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Head: Healthy",
			.style = {.color = bodyColor, .fontSize = kSmallSize},
			.margin = 1.0F
		}));
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Torso: Healthy",
			.style = {.color = bodyColor, .fontSize = kSmallSize},
			.margin = 1.0F
		}));
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Left Arm: Healthy",
			.style = {.color = bodyColor, .fontSize = kSmallSize},
			.margin = 1.0F
		}));
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Right Arm: Healthy",
			.style = {.color = bodyColor, .fontSize = kSmallSize},
			.margin = 1.0F
		}));
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Left Leg: Healthy",
			.style = {.color = bodyColor, .fontSize = kSmallSize},
			.margin = 1.0F
		}));
		rightColumn.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Right Leg: Healthy",
			.style = {.color = bodyColor, .fontSize = kSmallSize},
			.margin = 1.0F
		}));

		layout.addChild(std::move(rightColumn));

		layout.visible = false;
		healthContentHandle = addChild(std::move(layout));
	}

	// Social tab content
	{
		auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {contentBounds.x, contentY},
			.size = {contentBounds.width, contentHeight},
			.direction = UI::Direction::Vertical,
			.id = "social_content"
		});

		layout.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Relationships",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 4.0F
		}));

		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Not yet tracked",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 8.0F
		}));

		layout.addChild(UI::Text(UI::Text::Args{
			.height = kSmallSize,
			.text = "Future: Opinion modifiers, social interactions",
			.style = {.color = mutedColor, .fontSize = kSmallSize},
			.margin = 2.0F
		}));

		layout.visible = false;
		socialContentHandle = addChild(std::move(layout));
	}

	// Gear tab content - with attire slots section
	{
		auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {contentBounds.x, contentY},
			.size = {contentBounds.width, contentHeight},
			.direction = UI::Direction::Vertical,
			.id = "gear_content"
		});

		// Attire section
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Attire",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 4.0F
		}));

		// Attire slots (placeholders)
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Head: (empty)",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 1.0F
		}));
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Chest: (empty)",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 1.0F
		}));
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Legs: (empty)",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 1.0F
		}));
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Feet: (empty)",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 1.0F
		}));
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Hands: (empty)",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 1.0F
		}));

		// Inventory section
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Inventory: 0/0 slots",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 6.0F
		}));

		// Empty state / items list
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = "Empty",
			.style = {.color = mutedColor, .fontSize = kBodySize},
			.margin = 2.0F
		}));

		layout.visible = false;
		gearContentHandle = addChild(std::move(layout));
	}

	// Memory tab content
	{
		constexpr float kCompactRowHeight = 18.0F;  // Compact row height for TreeView
		float headerHeight = kLabelSize + 8.0F;  // Header text + margins
		float treeViewHeight = contentHeight - headerHeight;

		auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
			.position = {contentBounds.x, contentY},
			.size = {contentBounds.width, contentHeight},
			.direction = UI::Direction::Vertical,
			.id = "memory_content"
		});

		// Header
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kLabelSize,
			.text = "Known Entities: 0 total",
			.style = {.color = labelColor, .fontSize = kLabelSize},
			.margin = 4.0F
		}));

		// ScrollContainer with TreeView inside for scrollable entity list
		// Pattern from TreeViewScene.cpp: ScrollContainer clips, TreeView auto-heights
		float scrollWidth = contentBounds.width - 8.0F;
		auto scrollContainer = UI::ScrollContainer(UI::ScrollContainer::Args{
			.size = {scrollWidth, treeViewHeight},
			.id = "memory_scroll"
		});

		// TreeView with compact row height - auto-height mode (size.y = 0)
		// Position {0,0} relative to ScrollContainer, width leaves 8px for scrollbar
		scrollContainer.addChild(UI::TreeView(UI::TreeView::Args{
			.position = {0.0F, 0.0F},
			.size = {scrollWidth - 8.0F, 0.0F},  // 0 height = auto-size to content
			.rowHeight = kCompactRowHeight,
			.id = "memory_tree"
		}));

		layout.addChild(std::move(scrollContainer));

		layout.visible = false;
		memoryContentHandle = addChild(std::move(layout));
	}
}

void ColonistDetailsDialog::open(ecs::EntityID newColonistId, float screenWidth, float screenHeight) {
	colonistId = newColonistId;
	currentTab = kTabBio;

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->open(screenWidth, screenHeight);

		// Create tab bar and content after dialog opens (needs content bounds)
		if (tabBarHandle == UI::LayerHandle{}) {
			createTabBar();
			createTabContent();
		}
	}
}

void ColonistDetailsDialog::close() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->close();
	}
}

bool ColonistDetailsDialog::isOpen() const {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->isOpen();
}

void ColonistDetailsDialog::update(const ecs::World& world, float deltaTime) {
	if (!isOpen()) return;

	// Update dialog animation
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->update(deltaTime);
	}

	// Refresh model data
	auto updateType = model.refresh(world, colonistId);

	// Update dialog title with colonist name
	if (model.isValid() && dialog != nullptr) {
		dialog->setTitle(model.bio().name);
	}

	// Update content if data changed
	if (updateType == ColonistDetailsModel::UpdateType::Structure ||
		updateType == ColonistDetailsModel::UpdateType::Values) {
		updateTabContent();
	}
}

void ColonistDetailsDialog::render() {
	if (!isOpen()) return;

	// Render dialog (which handles its own rendering including children)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->render();
	}

	// Render tab bar
	auto* tabBar = getChild<UI::TabBar>(tabBarHandle);
	if (tabBar != nullptr) {
		tabBar->render();
	}

	// Render active tab content
	if (currentTab == kTabBio) {
		if (auto* content = getChild<UI::LayoutContainer>(bioContentHandle)) {
			content->render();
		}
	} else if (currentTab == kTabHealth) {
		if (auto* content = getChild<UI::LayoutContainer>(healthContentHandle)) {
			content->render();
		}
	} else if (currentTab == kTabSocial) {
		if (auto* content = getChild<UI::LayoutContainer>(socialContentHandle)) {
			content->render();
		}
	} else if (currentTab == kTabGear) {
		if (auto* content = getChild<UI::LayoutContainer>(gearContentHandle)) {
			content->render();
		}
	} else if (currentTab == kTabMemory) {
		if (auto* content = getChild<UI::LayoutContainer>(memoryContentHandle)) {
			content->render();
		}
	}
}

bool ColonistDetailsDialog::handleEvent(UI::InputEvent& event) {
	if (!isOpen()) return false;

	// Let dialog handle events first (close button, escape, overlay click)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr && dialog->handleEvent(event)) {
		return true;
	}

	// Tab bar
	auto* tabBar = getChild<UI::TabBar>(tabBarHandle);
	if (tabBar != nullptr && tabBar->handleEvent(event)) {
		return true;
	}

	// Active tab content
	if (currentTab == kTabBio) {
		if (auto* content = getChild<UI::LayoutContainer>(bioContentHandle)) {
			if (content->handleEvent(event)) return true;
		}
	} else if (currentTab == kTabHealth) {
		if (auto* content = getChild<UI::LayoutContainer>(healthContentHandle)) {
			if (content->handleEvent(event)) return true;
		}
	} else if (currentTab == kTabSocial) {
		if (auto* content = getChild<UI::LayoutContainer>(socialContentHandle)) {
			if (content->handleEvent(event)) return true;
		}
	} else if (currentTab == kTabGear) {
		if (auto* content = getChild<UI::LayoutContainer>(gearContentHandle)) {
			if (content->handleEvent(event)) return true;
		}
	} else if (currentTab == kTabMemory) {
		if (auto* content = getChild<UI::LayoutContainer>(memoryContentHandle)) {
			if (content->handleEvent(event)) return true;
		}
	}

	// Dialog consumes all events when open
	return true;
}

bool ColonistDetailsDialog::containsPoint(Foundation::Vec2 point) const {
	if (!isOpen()) return false;
	// Defer to dialog's containsPoint (which checks panel bounds for non-modal)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->containsPoint(point);
}

void ColonistDetailsDialog::switchToTab(const std::string& tabId) {
	currentTab = tabId;

	// Update visibility
	if (auto* bio = getChild<UI::LayoutContainer>(bioContentHandle)) {
		bio->visible = (tabId == kTabBio);
	}
	if (auto* health = getChild<UI::LayoutContainer>(healthContentHandle)) {
		health->visible = (tabId == kTabHealth);
	}
	if (auto* social = getChild<UI::LayoutContainer>(socialContentHandle)) {
		social->visible = (tabId == kTabSocial);
	}
	if (auto* gear = getChild<UI::LayoutContainer>(gearContentHandle)) {
		gear->visible = (tabId == kTabGear);
	}
	if (auto* memory = getChild<UI::LayoutContainer>(memoryContentHandle)) {
		memory->visible = (tabId == kTabMemory);
	}
}

void ColonistDetailsDialog::updateTabContent() {
	updateBioTab();
	updateHealthTab();
	updateSocialTab();
	updateGearTab();
	updateMemoryTab();
}

void ColonistDetailsDialog::updateBioTab() {
	auto* layout = getChild<UI::LayoutContainer>(bioContentHandle);
	if (layout == nullptr || !model.isValid()) return;

	const auto& bio = model.bio();

	// Elements: name, age, mood, task, traits header, traits text, background header, background text
	auto& children = layout->getChildren();
	size_t idx = 0;

	// Name
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			text->text = bio.name;
		}
		++idx;
	}
	// Age
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			text->text = "Age: " + bio.age;
		}
		++idx;
	}
	// Mood
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			std::ostringstream ss;
			ss << "Mood: " << static_cast<int>(bio.mood) << "% (" << bio.moodLabel << ")";
			text->text = ss.str();
		}
		++idx;
	}
	// Current task
	if (idx < children.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[idx])) {
			text->text = "Current: " + bio.currentTask;
		}
		++idx;
	}
	// Skip traits header (idx 4), traits text is 5
	// Skip background header (idx 6), background text is 7
}

void ColonistDetailsDialog::updateHealthTab() {
	auto* layout = getChild<UI::LayoutContainer>(healthContentHandle);
	if (layout == nullptr || !model.isValid()) return;

	const auto& health = model.health();

	// Health tab uses nested LayoutContainers: layout has 2 children (left/right columns)
	auto& columns = layout->getChildren();
	if (columns.empty()) return;

	// Left column is first child
	auto* leftColumn = dynamic_cast<UI::LayoutContainer*>(columns[0]);
	if (leftColumn == nullptr) return;

	auto& leftChildren = leftColumn->getChildren();
	size_t idx = 0;

	// Mood header (idx 0 in left column)
	if (idx < leftChildren.size()) {
		if (auto* text = dynamic_cast<UI::Text*>(leftChildren[idx])) {
			std::ostringstream ss;
			ss << "Mood: " << static_cast<int>(health.mood) << "% (" << health.moodLabel << ")";
			text->text = ss.str();
		}
		++idx;
	}

	// Skip "Needs" header (idx 1)
	++idx;

	// Need bars (idx 2 onwards in left column)
	for (size_t i = 0; i < static_cast<size_t>(ecs::NeedType::Count) && idx < leftChildren.size(); ++i) {
		if (auto* bar = dynamic_cast<UI::ProgressBar*>(leftChildren[idx])) {
			bar->setValue(health.needValues[i] / 100.0F);

			// Color based on status
			if (health.isCritical[i]) {
				bar->setFillColor({0.9F, 0.2F, 0.2F, 1.0F}); // Red
			} else if (health.needsAttention[i]) {
				bar->setFillColor({0.9F, 0.7F, 0.2F, 1.0F}); // Yellow
			} else {
				bar->setFillColor({0.2F, 0.8F, 0.4F, 1.0F}); // Green
			}
		}
		++idx;
	}
	// Right column (body parts) is static for now - no dynamic updates needed
}

void ColonistDetailsDialog::updateSocialTab() {
	// Placeholder - no update needed
}

void ColonistDetailsDialog::updateGearTab() {
	auto* layout = getChild<UI::LayoutContainer>(gearContentHandle);
	if (layout == nullptr || !model.isValid()) return;

	const auto& gear = model.gear();
	auto& children = layout->getChildren();

	// Structure:
	// idx 0: Attire header
	// idx 1-5: Attire slots (head, chest, legs, feet, hands)
	// idx 6: Inventory header
	// idx 7: Items text

	// Update inventory header (idx 6)
	if (children.size() > 6) {
		if (auto* text = dynamic_cast<UI::Text*>(children[6])) {
			std::ostringstream ss;
			ss << "Inventory: " << gear.slotCount << "/" << gear.maxSlots << " slots";
			text->text = ss.str();
		}
	}

	// Update items text (idx 7)
	if (children.size() > 7) {
		if (auto* text = dynamic_cast<UI::Text*>(children[7])) {
			if (gear.items.empty()) {
				text->text = "Empty";
			} else {
				std::ostringstream ss;
				for (const auto& item : gear.items) {
					ss << item.defName << " x" << item.quantity << "\n";
				}
				text->text = ss.str();
			}
		}
	}
}

void ColonistDetailsDialog::updateMemoryTab() {
	auto* layout = getChild<UI::LayoutContainer>(memoryContentHandle);
	if (layout == nullptr || !model.isValid()) return;

	const auto& memory = model.memory();
	auto& children = layout->getChildren();

	// Update header (children[0])
	if (!children.empty()) {
		if (auto* text = dynamic_cast<UI::Text*>(children[0])) {
			std::ostringstream ss;
			ss << "Known Entities: " << memory.totalKnown << " total";
			text->text = ss.str();
		}
	}

	// Navigate: Layout -> children[1]=ScrollContainer -> children[0]=TreeView
	if (children.size() > 1) {
		auto* scrollContainer = dynamic_cast<UI::ScrollContainer*>(children[1]);
		if (scrollContainer == nullptr) return;

		auto& scrollChildren = scrollContainer->getChildren();
		if (scrollChildren.empty()) return;

		auto* treeView = dynamic_cast<UI::TreeView*>(scrollChildren[0]);
		if (treeView == nullptr) return;

		// Preserve expanded state from current nodes (categories and type groups)
		// Keys: "CategoryName" for categories, "CategoryName/TypeName" for type groups
		std::unordered_map<std::string, bool> expandedState;
		for (const auto& categoryNode : treeView->getRootNodes()) {
			expandedState[categoryNode.label] = categoryNode.expanded;
			for (const auto& typeNode : categoryNode.children) {
				expandedState[categoryNode.label + "/" + typeNode.label] = typeNode.expanded;
			}
		}

		std::vector<UI::TreeNode> nodes;

		for (const auto& category : memory.categories) {
			UI::TreeNode categoryNode;
			categoryNode.label = category.name;
			categoryNode.count = static_cast<int>(category.count);
			// Preserve expanded state, default to false for new categories
			auto catIt = expandedState.find(category.name);
			categoryNode.expanded = (catIt != expandedState.end()) ? catIt->second : false;

			// Group entities by type (defName)
			std::unordered_map<std::string, std::vector<const MemoryEntity*>> byType;
			for (const auto& entity : category.entities) {
				byType[entity.name].push_back(&entity);
			}

			// Create a child node for each type
			for (const auto& [typeName, entities] : byType) {
				UI::TreeNode typeNode;
				typeNode.label = typeName;
				typeNode.count = static_cast<int>(entities.size());

				// Preserve expanded state for type nodes
				std::string typeKey = category.name + "/" + typeName;
				auto typeIt = expandedState.find(typeKey);
				typeNode.expanded = (typeIt != expandedState.end()) ? typeIt->second : false;

				// Add location children
				for (const auto* entity : entities) {
					UI::TreeNode locationNode;
					std::ostringstream ss;
					ss << "at (" << static_cast<int>(entity->x) << ", " << static_cast<int>(entity->y) << ")";
					locationNode.label = ss.str();
					typeNode.children.push_back(locationNode);
				}

				categoryNode.children.push_back(typeNode);
			}

			nodes.push_back(categoryNode);
		}

		treeView->setRootNodes(std::move(nodes));
	}
}

} // namespace world_sim
