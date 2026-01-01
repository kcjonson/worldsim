#include "CraftingDialog.h"

#include <components/progress/ProgressBar.h>
#include <font/FontRenderer.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>
#include <shapes/Shapes.h>
#include <theme/Theme.h>

namespace {
// Recipe list item dimensions
constexpr float kRecipeItemHeight = 24.0F;
constexpr float kRecipeItemPadding = 8.0F;
constexpr float kRecipeHeaderHeight = 20.0F;
} // anonymous namespace

namespace world_sim {

CraftingDialog::CraftingDialog(const Args& args)
	: onCloseCallback(args.onClose)
	, onQueueRecipeCallback(args.onQueueRecipe)
	, onCancelJobCallback(args.onCancelJob) {
	createDialog();
}

void CraftingDialog::createDialog() {
	auto dialog = UI::Dialog(
		UI::Dialog::Args{
			.title = "Crafting",
			.size = {kDialogWidth, kDialogHeight},
			.onClose = [this]() {
				if (onCloseCallback) {
					onCloseCallback();
				}
			},
			.modal = true
		}
	);
	dialogHandle = addChild(std::move(dialog));
}

void CraftingDialog::createColumns() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}

	auto bounds = dialog->getContentBounds();
	float centerWidth = bounds.width - kLeftColumnWidth - kRightColumnWidth - kColumnGap * 2;

	// Create horizontal layout for the 3 columns (fills dialog content area)
	auto contentLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},  // Relative to content area (Dialog applies offset)
		.size = {bounds.width, bounds.height},
		.direction = UI::Direction::Horizontal,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top,
		.id = "content-layout"
	});

	// Left column - Recipe list (scrollable)
	leftColumnHandle = contentLayout.addChild(UI::ScrollContainer(UI::ScrollContainer::Args{
		.position = {0, 0},
		.size = {kLeftColumnWidth, bounds.height},
		.id = "recipe-list",
		.margin = 0
	}));

	// Center column - Recipe details (add gap via margin)
	centerColumnHandle = contentLayout.addChild(UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {centerWidth, bounds.height},
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top,
		.id = "recipe-details",
		.margin = kColumnGap / 2  // Half gap on each side
	}));

	// Right column - Queue (scrollable)
	rightColumnHandle = contentLayout.addChild(UI::ScrollContainer(UI::ScrollContainer::Args{
		.position = {0, 0},
		.size = {kRightColumnWidth, bounds.height},
		.id = "queue-list",
		.margin = 0
	}));

	// Add content layout to Dialog (Dialog handles clipping and offset)
	contentLayoutHandle = dialog->addChild(std::move(contentLayout));

	contentCreated = true;
}

void CraftingDialog::open(ecs::EntityID stationId, const std::string& stationDefName,
                          float screenWidth, float screenHeight) {
	model.setStation(stationId, stationDefName);

	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		// Update title with station name
		dialog->setTitle("Crafting - " + model.stationName());
		dialog->open(screenWidth, screenHeight);

		// Create columns if not already created
		if (!contentCreated) {
			createColumns();
		}

		// Trigger content rebuild on next update (when we have world/registry)
		needsInitialRebuild = true;
	}
}

void CraftingDialog::close() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->close();
	}
	model.clear();
}

bool CraftingDialog::isOpen() const {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->isOpen();
}

void CraftingDialog::update(const ecs::World& world,
                            const engine::assets::RecipeRegistry& registry,
                            float deltaTime) {
	if (!isOpen()) {
		return;
	}

	// Update dialog animation
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->update(deltaTime);
	}

	// Refresh model data (for queue progress updates)
	// This also updates selectedDetails() based on the current selection
	auto updateType = model.refresh(world, registry);

	// Build initial content after first model refresh
	// We can't do this in open() because we don't have world/registry there
	if (needsInitialRebuild) {
		needsInitialRebuild = false;
		// Select first recipe by default
		if (!model.recipes().empty() && recipeSelectedIndex < 0) {
			recipeSelectedIndex = 0;
			model.selectRecipe(model.recipes()[0].defName);
		}
		rebuildCenterColumn();
		rebuildQueueColumn();
	} else {
		// Update queue column when queue changes (progress, completions)
		if (updateType == CraftingDialogModel::UpdateType::Queue ||
		    updateType == CraftingDialogModel::UpdateType::Full) {
			rebuildQueueColumn();
		}
	}

	// Rebuild center column after selection changed (model now has updated details)
	if (needsCenterRebuild) {
		needsCenterRebuild = false;
		rebuildCenterColumn();
	}
}

// Helper to get the content layout from Dialog
UI::LayoutContainer* CraftingDialog::getContentLayout() {
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return nullptr;
	}
	return dialog->getChild<UI::LayoutContainer>(contentLayoutHandle);
}

void CraftingDialog::render() {
	if (!isOpen()) {
		return;
	}

	// Render dialog (includes overlay and content children)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->render();
	}

	// Render recipe list primitives (direct rendering like TabBar)
	// This is rendered AFTER Dialog so it appears on top of the scroll container
	renderRecipeList();
}

bool CraftingDialog::handleEvent(UI::InputEvent& event) {
	if (!isOpen()) {
		return false;
	}

	// Recipe list - direct hit testing for primitives (like TabBar)
	// Must be checked BEFORE Dialog handles events
	if (event.type == UI::InputEvent::Type::MouseMove) {
		recipeHoveredIndex = getRecipeIndexAtPosition(event.position);
		// Don't consume mouse move - let Dialog handle it
	} else if (event.type == UI::InputEvent::Type::MouseDown &&
	           event.button == engine::MouseButton::Left) {
		int index = getRecipeIndexAtPosition(event.position);
		if (index >= 0) {
			// Clicked on a recipe - select it immediately
			handleRecipeClick(index);
			event.consume();
			return true;
		}
	}

	// Let Dialog handle all other events (content children, chrome, modal)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr && dialog->handleEvent(event)) {
		return true;
	}

	return false;
}

bool CraftingDialog::containsPoint(Foundation::Vec2 point) const {
	if (!isOpen()) {
		return false;
	}
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->containsPoint(point);
}

// Recipe list bounds calculation (like TabBar::getTabBounds)
Foundation::Rect CraftingDialog::getRecipeItemBounds(int index) const {
	if (index < 0 || index >= static_cast<int>(model.recipes().size())) {
		return {0.0F, 0.0F, 0.0F, 0.0F};
	}

	// Get dialog content bounds directly (more reliable than scroll container position
	// which may not be updated on first frame after dialog opens)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return {0.0F, 0.0F, 0.0F, 0.0F};
	}

	auto bounds = dialog->getContentBounds();
	float leftX = bounds.x;
	float leftY = bounds.y;

	// Get scroll offset from the scroll container (now a child of content layout)
	float scrollOffset = 0.0F;
	auto* contentLayout = const_cast<CraftingDialog*>(this)->getContentLayout();
	if (contentLayout != nullptr) {
		auto* leftCol = contentLayout->getChild<UI::ScrollContainer>(leftColumnHandle);
		if (leftCol != nullptr) {
			scrollOffset = leftCol->getScrollPosition();
		}
	}

	// Calculate item Y position (header + items above)
	float itemY = leftY + kRecipeHeaderHeight + (static_cast<float>(index) * kRecipeItemHeight) - scrollOffset;

	return {
		leftX + kRecipeItemPadding,
		itemY,
		kLeftColumnWidth - (kRecipeItemPadding * 2.0F),
		kRecipeItemHeight
	};
}

// Hit testing for recipe list (like TabBar::getTabIndexAtPosition)
int CraftingDialog::getRecipeIndexAtPosition(Foundation::Vec2 pos) const {
	const auto& recipes = model.recipes();
	for (size_t i = 0; i < recipes.size(); ++i) {
		Foundation::Rect bounds = getRecipeItemBounds(static_cast<int>(i));
		// Use exclusive check on bottom/right to avoid boundary overlap
		if (pos.x >= bounds.x && pos.x < bounds.x + bounds.width &&
		    pos.y >= bounds.y && pos.y < bounds.y + bounds.height) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

// Direct rendering of recipe list (like TabBar::render)
void CraftingDialog::renderRecipeList() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* leftCol = contentLayout->getChild<UI::ScrollContainer>(leftColumnHandle);
	if (leftCol == nullptr) {
		return;
	}

	// Get dialog content bounds (same source as getRecipeItemBounds for consistency)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}
	auto dialogBounds = dialog->getContentBounds();
	float leftX = dialogBounds.x;
	float leftY = dialogBounds.y;
	float scrollOffset = leftCol->getScrollPosition();

	// Render the scroll container (handles clipping, scrollbar, etc.)
	leftCol->render();

	// Calculate viewport bounds for culling
	Foundation::Rect viewBounds{leftX, leftY, kLeftColumnWidth, dialogBounds.height};

	// Draw header
	float headerY = leftY + 4.0F - scrollOffset;
	if (headerY + kRecipeHeaderHeight > viewBounds.y && headerY < viewBounds.y + viewBounds.height) {
		Renderer::Primitives::drawText({
			.text = "RECIPES",
			.position = {leftX + kRecipeItemPadding, headerY},
			.scale = 11.0F / 16.0F,  // 11px font
			.color = UI::Theme::Colors::textMuted,
			.id = "recipe-header"
		});
	}

	// Colors for list items
	auto transparentBg = Foundation::Color{0.0F, 0.0F, 0.0F, 0.0F};
	auto hoverBg = Foundation::Color{1.0F, 1.0F, 1.0F, 0.08F};
	auto selectedBg = Foundation::Color{0.0F, 0.0F, 0.0F, 0.2F};
	auto borderColor = Foundation::Color{1.0F, 1.0F, 1.0F, 0.1F};

	// Draw each recipe item
	const auto& recipes = model.recipes();
	for (size_t i = 0; i < recipes.size(); ++i) {
		const auto& recipe = recipes[i];
		int idx = static_cast<int>(i);
		Foundation::Rect bounds = getRecipeItemBounds(idx);

		// Skip items that are completely outside the scroll viewport
		if (bounds.y + bounds.height < viewBounds.y || bounds.y > viewBounds.y + viewBounds.height) {
			continue;
		}

		// Determine background color based on state
		Foundation::Color bgColor = transparentBg;
		if (idx == recipeSelectedIndex) {
			bgColor = selectedBg;
		} else if (idx == recipeHoveredIndex) {
			bgColor = hoverBg;
		}

		// Draw background
		Renderer::Primitives::drawRect({
			.bounds = bounds,
			.style = {.fill = bgColor},
			.id = "recipe-item"
		});

		// Draw bottom border (1px)
		Renderer::Primitives::drawRect({
			.bounds = {bounds.x, bounds.y + bounds.height - 1.0F, bounds.width, 1.0F},
			.style = {.fill = borderColor},
			.id = "recipe-border"
		});

		// Build label with craftability indicator
		std::string label = recipe.label;
		if (!recipe.canCraft) {
			label = "(!) " + label;
		}

		// Text color
		Foundation::Color textColor = recipe.canCraft
			? UI::Theme::Colors::textBody
			: UI::Theme::Colors::textMuted;

		// Draw text (vertically centered in item)
		Renderer::Primitives::drawText({
			.text = label,
			.position = {bounds.x + 4.0F, bounds.y + (kRecipeItemHeight - 12.0F) / 2.0F},
			.scale = 12.0F / 16.0F,  // 12px font
			.color = textColor,
			.id = "recipe-text"
		});
	}

	// Update scroll container content height
	float totalHeight = kRecipeHeaderHeight + (static_cast<float>(recipes.size()) * kRecipeItemHeight) + 10.0F;
	leftCol->setContentHeight(totalHeight);
}

void CraftingDialog::rebuildCenterColumn() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* centerCol = contentLayout->getChild<UI::LayoutContainer>(centerColumnHandle);
	if (centerCol == nullptr) {
		return;
	}

	// Clear previous content before rebuilding
	centerCol->clearChildren();

	// Get dialog for bounds (needed for button sizing)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}
	auto bounds = dialog->getContentBounds();
	float centerWidth = bounds.width - kLeftColumnWidth - kRightColumnWidth - kColumnGap * 2;

	const auto& details = model.selectedDetails();

	if (details.name.empty()) {
		// No recipe selected - auto-sized text
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = "Select a recipe",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 14},
			.margin = 8.0F
		}));
	} else {
		// Recipe name header - auto-sized
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = details.name,
			.style = {.color = UI::Theme::Colors::textTitle, .fontSize = 16},
			.margin = 4.0F
		}));

		// Description - auto-sized with word wrap for longer descriptions
		if (!details.description.empty()) {
			centerCol->addChild(UI::Text(UI::Text::Args{
				.width = centerWidth - 16,  // Set width for wrapping
				.text = details.description,
				.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12, .wordWrap = true},
				.margin = 2.0F
			}));
		}

		// REQUIRES section
		if (!details.materials.empty()) {
			centerCol->addChild(UI::Text(UI::Text::Args{
				.text = "REQUIRES",
				.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
				.margin = 6.0F
			}));

			for (const auto& mat : details.materials) {
				// Use ASCII indicators
				std::string matLine = std::to_string(mat.required) + "x " + mat.label;
				matLine += mat.hasEnough ? " [OK]" : " [X]";

				centerCol->addChild(UI::Text(UI::Text::Args{
					.text = matLine,
					.style = {.color = mat.hasEnough ? UI::Theme::Colors::statusActive : UI::Theme::Colors::statusBlocked, .fontSize = 12},
					.margin = 1.0F
				}));
			}
		}

		// PRODUCES section
		if (!details.outputs.empty()) {
			centerCol->addChild(UI::Text(UI::Text::Args{
				.text = "PRODUCES",
				.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
				.margin = 6.0F
			}));

			for (const auto& output : details.outputs) {
				std::string outLine = std::to_string(output.count) + "x " + output.label;
				centerCol->addChild(UI::Text(UI::Text::Args{
					.text = outLine,
					.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
					.margin = 1.0F
				}));
			}
		}

		// WORK TIME
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = "WORK TIME",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
			.margin = 6.0F
		}));

		std::string timeStr = "~" + std::to_string(static_cast<int>(details.workTime)) + " seconds";
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = timeStr,
			.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
			.margin = 2.0F
		}));

		// Quantity controls in horizontal row: [-10] [-1] [value] [+1] [+10]
		auto quantityRow = UI::LayoutContainer(UI::LayoutContainer::Args{
			.size = {0, 32},  // Auto-width from children
			.direction = UI::Direction::Horizontal,
			.hAlign = UI::HAlign::Left,
			.vAlign = UI::VAlign::Center,
			.margin = 8.0F
		});

		quantityRow.addChild(UI::Button(UI::Button::Args{
			.label = "-10",
			.size = {40, 28},
			.type = UI::Button::Type::Secondary,
			.disabled = (model.quantity() <= 10),
			.onClick = [this]() { handleQuantityChange(-10); },
			.margin = 2.0F
		}));

		quantityRow.addChild(UI::Button(UI::Button::Args{
			.label = "-1",
			.size = {36, 28},
			.type = UI::Button::Type::Secondary,
			.disabled = (model.quantity() <= 1),
			.onClick = [this]() { handleQuantityChange(-1); },
			.margin = 2.0F
		}));

		// Current quantity display
		quantityRow.addChild(UI::Text(UI::Text::Args{
			.width = 40,
			.text = std::to_string(model.quantity()),
			.style = {.color = UI::Theme::Colors::textBody, .fontSize = 16, .hAlign = Foundation::HorizontalAlign::Center},
			.margin = 4.0F
		}));

		quantityRow.addChild(UI::Button(UI::Button::Args{
			.label = "+1",
			.size = {36, 28},
			.type = UI::Button::Type::Secondary,
			.onClick = [this]() { handleQuantityChange(1); },
			.margin = 2.0F
		}));

		quantityRow.addChild(UI::Button(UI::Button::Args{
			.label = "+10",
			.size = {40, 28},
			.type = UI::Button::Type::Secondary,
			.onClick = [this]() { handleQuantityChange(10); },
			.margin = 2.0F
		}));

		centerCol->addChild(std::move(quantityRow));

		// Add to Queue button
		addToQueueHandle = centerCol->addChild(UI::Button(UI::Button::Args{
			.label = "Add to Queue",
			.size = {centerWidth - 16, 36},
			.type = UI::Button::Type::Primary,
			.onClick = [this]() { handleAddToQueue(); },
			.margin = 8.0F
		}));
	}
}

void CraftingDialog::rebuildQueueColumn() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* rightCol = contentLayout->getChild<UI::ScrollContainer>(rightColumnHandle);
	if (rightCol == nullptr) {
		return;
	}

	// Clear previous content before rebuilding
	rightCol->clearChildren();

	// Create layout for queue items - height computed from children (size.y = 0)
	auto queueLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {kRightColumnWidth - 16, 0},  // Height auto-computed from children
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top
	});

	// Header - auto-sized
	queueLayout.addChild(UI::Text(UI::Text::Args{
		.text = "QUEUE",
		.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
		.margin = 4.0F
	}));

	queueItemHandles.clear();
	const auto& queue = model.queue();

	if (queue.empty()) {
		queueLayout.addChild(UI::Text(UI::Text::Args{
			.text = "No items queued",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 12},
			.margin = 4.0F
		}));
	} else {
		bool firstItem = true;
		for (const auto& item : queue) {
			// Section header
			if (firstItem) {
				std::string sectionLabel = item.isInProgress ? "In Progress:" : "Queued:";
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.text = sectionLabel,
					.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 10},
					.margin = 2.0F
				}));
			}

			// Item name with quantity
			uint32_t remaining = item.quantity - item.completed;
			std::string itemLabel = item.label;
			if (remaining > 1) {
				itemLabel += " x" + std::to_string(remaining);
			}

			if (item.isInProgress) {
				// Show item name - auto-sized
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.text = itemLabel,
					.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
					.margin = 2.0F
				}));

				// Progress bar
				queueLayout.addChild(UI::ProgressBar(UI::ProgressBar::Args{
					.size = {kRightColumnWidth - 32, 10},
					.value = item.progress,
					.fillColor = UI::Theme::Colors::statusActive,
					.margin = 2.0F
				}));

				// Add "Queued:" header after in-progress item if there are more
				if (queue.size() > 1) {
					queueLayout.addChild(UI::Text(UI::Text::Args{
						.text = "Queued:",
						.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 10},
						.margin = 4.0F
					}));
				}
			} else {
				// Queued item - show name and cancel button - auto-sized
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.text = itemLabel,
					.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
					.margin = 2.0F
				}));

				queueItemHandles.push_back(queueLayout.addChild(UI::Button(UI::Button::Args{
					.label = "Cancel",
					.size = {60, 24},
					.type = UI::Button::Type::Secondary,
					.onClick = [this, defName = item.recipeDefName]() {
						handleCancelJob(defName);
					},
					.margin = 2.0F
				})));
			}

			firstItem = false;
		}
	}

	// Set scroll content height from layout's computed height
	rightCol->setContentHeight(queueLayout.getHeight() + 10);

	// Add layout to scroll container
	rightCol->addChild(std::move(queueLayout));
}

void CraftingDialog::handleRecipeClick(int recipeIndex) {
	const auto& recipes = model.recipes();
	if (recipeIndex < 0 || recipeIndex >= static_cast<int>(recipes.size())) {
		return;
	}

	// Update selection index (direct rendering will use this immediately)
	recipeSelectedIndex = recipeIndex;

	// Update model selection
	model.selectRecipe(recipes[static_cast<size_t>(recipeIndex)].defName);

	// Schedule center column rebuild after next model.refresh()
	// (model needs to call extractSelectedDetails with the registry first)
	needsCenterRebuild = true;
}

void CraftingDialog::handleQuantityChange(int delta) {
	model.adjustQuantity(delta);
	rebuildCenterColumn();
}

void CraftingDialog::handleAddToQueue() {
	if (model.selectedRecipeDefName().empty()) {
		return;
	}

	// Pass quantity to callback - WorkQueue::addJob handles merging with existing jobs
	if (onQueueRecipeCallback) {
		onQueueRecipeCallback(model.selectedRecipeDefName(), model.quantity());
	}

	// Reset quantity after adding
	model.setQuantity(1);

	// Queue column will be rebuilt on next update() when model detects change
}

void CraftingDialog::handleCancelJob(const std::string& recipeDefName) {
	if (onCancelJobCallback) {
		onCancelJobCallback(recipeDefName);
	}
	// Queue column will be rebuilt on next update() when model detects change
}

} // namespace world_sim
