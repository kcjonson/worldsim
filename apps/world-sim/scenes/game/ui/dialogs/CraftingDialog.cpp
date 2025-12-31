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

	// Calculate column positions
	float leftX = bounds.x;
	float centerX = leftX + kLeftColumnWidth + kColumnGap;
	float centerWidth = bounds.width - kLeftColumnWidth - kRightColumnWidth - kColumnGap * 2;
	float rightX = centerX + centerWidth + kColumnGap;
	float columnHeight = bounds.height;

	// Left column - Recipe list (scrollable)
	auto leftScroll = UI::ScrollContainer(UI::ScrollContainer::Args{
		.position = {leftX, bounds.y},
		.size = {kLeftColumnWidth, columnHeight},
		.id = "recipe-list"
	});
	leftColumnHandle = addChild(std::move(leftScroll));

	// Center column - Recipe details
	auto centerLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {centerX, bounds.y},
		.size = {centerWidth, columnHeight},
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top,
		.id = "recipe-details"
	});
	centerColumnHandle = addChild(std::move(centerLayout));

	// Right column - Queue (scrollable)
	auto rightScroll = UI::ScrollContainer(UI::ScrollContainer::Args{
		.position = {rightX, bounds.y},
		.size = {kRightColumnWidth, columnHeight},
		.id = "queue-list"
	});
	rightColumnHandle = addChild(std::move(rightScroll));

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
	model.refresh(world, registry);

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
	}

	// Rebuild center column after selection changed (model now has updated details)
	if (needsCenterRebuild) {
		needsCenterRebuild = false;
		rebuildCenterColumn();
	}
}

void CraftingDialog::render() {
	if (!isOpen()) {
		return;
	}

	// Render dialog (includes overlay)
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->render();
	}

	// Render recipe list (direct rendering like TabBar)
	renderRecipeList();

	// Render other columns
	if (auto* centerCol = getChild<UI::LayoutContainer>(centerColumnHandle)) {
		centerCol->render();
	}
	if (auto* rightCol = getChild<UI::ScrollContainer>(rightColumnHandle)) {
		rightCol->render();
	}
}

bool CraftingDialog::handleEvent(UI::InputEvent& event) {
	if (!isOpen()) {
		return false;
	}

	auto* dialog = getChild<UI::Dialog>(dialogHandle);

	// IMPORTANT: For modal dialogs, we must let content handle events BEFORE
	// the dialog, because Dialog::handleEvent consumes all mouse events for modals.

	// Recipe list - direct hit testing like TabBar
	if (event.type == UI::InputEvent::Type::MouseMove) {
		recipeHoveredIndex = getRecipeIndexAtPosition(event.position);
		// Don't consume mouse move
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

	// Center column - quantity buttons, add button
	if (auto* centerCol = getChild<UI::LayoutContainer>(centerColumnHandle)) {
		if (centerCol->handleEvent(event)) {
			return true;
		}
	}

	// Right column - cancel buttons
	if (auto* rightCol = getChild<UI::ScrollContainer>(rightColumnHandle)) {
		if (rightCol->handleEvent(event)) {
			return true;
		}
	}

	// Now let dialog handle remaining events (close button, overlay click, escape)
	if (dialog != nullptr && dialog->handleEvent(event)) {
		return true;
	}

	// Consume events within dialog bounds
	if (dialog != nullptr && dialog->containsPoint(event.position)) {
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

	// Get scroll offset from the scroll container
	auto* leftCol = getChild<UI::ScrollContainer>(leftColumnHandle);
	float scrollOffset = (leftCol != nullptr) ? leftCol->getScrollPosition() : 0.0F;

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
	auto* leftCol = getChild<UI::ScrollContainer>(leftColumnHandle);
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
	// Get dialog for bounds
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog == nullptr) {
		return;
	}
	auto bounds = dialog->getContentBounds();
	float centerWidth = bounds.width - kLeftColumnWidth - kRightColumnWidth - kColumnGap * 2;
	float centerX = bounds.x + kLeftColumnWidth + kColumnGap;

	// Create center column layout with proper LayoutContainer pattern
	auto newCenter = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {centerX, bounds.y},
		.size = {centerWidth, bounds.height},
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top,
		.id = "recipe-details"
	});

	const auto& details = model.selectedDetails();

	if (details.name.empty()) {
		// No recipe selected - use proper layout child (height, no position)
		newCenter.addChild(UI::Text(UI::Text::Args{
			.height = 20,
			.text = "Select a recipe",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 14},
			.margin = 8.0F
		}));
	} else {
		// Recipe name header
		newCenter.addChild(UI::Text(UI::Text::Args{
			.height = 20,
			.text = details.name,
			.style = {.color = UI::Theme::Colors::textTitle, .fontSize = 16},
			.margin = 4.0F
		}));

		// Description
		if (!details.description.empty()) {
			newCenter.addChild(UI::Text(UI::Text::Args{
				.height = 16,
				.text = details.description,
				.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
				.margin = 2.0F
			}));
		}

		// REQUIRES section
		if (!details.materials.empty()) {
			newCenter.addChild(UI::Text(UI::Text::Args{
				.height = 14,
				.text = "REQUIRES",
				.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
				.margin = 6.0F
			}));

			for (const auto& mat : details.materials) {
				// Use ASCII indicators
				std::string matLine = std::to_string(mat.required) + "x " + mat.label;
				matLine += mat.hasEnough ? " [OK]" : " [X]";

				newCenter.addChild(UI::Text(UI::Text::Args{
					.height = 14,
					.text = matLine,
					.style = {.color = mat.hasEnough ? UI::Theme::Colors::statusActive : UI::Theme::Colors::statusBlocked, .fontSize = 12},
					.margin = 1.0F
				}));
			}
		}

		// PRODUCES section
		if (!details.outputs.empty()) {
			newCenter.addChild(UI::Text(UI::Text::Args{
				.height = 14,
				.text = "PRODUCES",
				.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
				.margin = 6.0F
			}));

			for (const auto& output : details.outputs) {
				std::string outLine = std::to_string(output.count) + "x " + output.label;
				newCenter.addChild(UI::Text(UI::Text::Args{
					.height = 14,
					.text = outLine,
					.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
					.margin = 1.0F
				}));
			}
		}

		// WORK TIME
		newCenter.addChild(UI::Text(UI::Text::Args{
			.height = 14,
			.text = "WORK TIME",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
			.margin = 6.0F
		}));

		std::string timeStr = "~" + std::to_string(static_cast<int>(details.workTime)) + " seconds";
		newCenter.addChild(UI::Text(UI::Text::Args{
			.height = 14,
			.text = timeStr,
			.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
			.margin = 2.0F
		}));

		// Quantity label
		newCenter.addChild(UI::Text(UI::Text::Args{
			.height = 14,
			.text = "Quantity: " + std::to_string(model.quantity()),
			.style = {.color = UI::Theme::Colors::textBody, .fontSize = 12},
			.margin = 8.0F
		}));

		// Quantity buttons in a row - these need explicit positioning within layout
		// For simplicity, use separate buttons with margin
		quantityMinusHandle = newCenter.addChild(UI::Button(UI::Button::Args{
			.label = " - ",
			.size = {40, 28},
			.type = UI::Button::Type::Secondary,
			.disabled = (model.quantity() <= 1),
			.onClick = [this]() { handleQuantityChange(-1); },
			.margin = 2.0F
		}));

		quantityPlusHandle = newCenter.addChild(UI::Button(UI::Button::Args{
			.label = " + ",
			.size = {40, 28},
			.type = UI::Button::Type::Secondary,
			.onClick = [this]() { handleQuantityChange(1); },
			.margin = 2.0F
		}));

		// Add to Queue button
		addToQueueHandle = newCenter.addChild(UI::Button(UI::Button::Args{
			.label = "Add to Queue",
			.size = {centerWidth - 16, 36},
			.type = UI::Button::Type::Primary,
			.onClick = [this]() { handleAddToQueue(); },
			.margin = 8.0F
		}));
	}

	// Replace the center column handle
	centerColumnHandle = addChild(std::move(newCenter));
}

void CraftingDialog::rebuildQueueColumn() {
	auto* rightCol = getChild<UI::ScrollContainer>(rightColumnHandle);
	if (rightCol == nullptr) {
		return;
	}

	// Create layout for queue items with proper LayoutContainer pattern
	auto queueLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {kRightColumnWidth - 16, 400},  // Fixed height for layout
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top
	});

	// Header - use .height, no .position
	queueLayout.addChild(UI::Text(UI::Text::Args{
		.height = 16,
		.text = "QUEUE",
		.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 11},
		.margin = 4.0F
	}));

	queueItemHandles.clear();
	const auto& queue = model.queue();
	float totalHeight = 24;

	if (queue.empty()) {
		queueLayout.addChild(UI::Text(UI::Text::Args{
			.height = 14,
			.text = "No items queued",
			.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 12},
			.margin = 4.0F
		}));
		totalHeight += 22;
	} else {
		bool firstItem = true;
		for (const auto& item : queue) {
			// Section header
			if (firstItem) {
				std::string sectionLabel = item.isInProgress ? "In Progress:" : "Queued:";
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.height = 12,
					.text = sectionLabel,
					.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 10},
					.margin = 2.0F
				}));
				totalHeight += 16;
			}

			// Item name with quantity
			uint32_t remaining = item.quantity - item.completed;
			std::string itemLabel = item.label;
			if (remaining > 1) {
				itemLabel += " x" + std::to_string(remaining);
			}

			if (item.isInProgress) {
				// Show item name
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.height = 14,
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
				totalHeight += 32;

				// Add "Queued:" header after in-progress item if there are more
				if (queue.size() > 1) {
					queueLayout.addChild(UI::Text(UI::Text::Args{
						.height = 12,
						.text = "Queued:",
						.style = {.color = UI::Theme::Colors::textMuted, .fontSize = 10},
						.margin = 4.0F
					}));
					totalHeight += 20;
				}
			} else {
				// Queued item - show name and cancel button
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.height = 14,
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
				totalHeight += 44;
			}

			firstItem = false;
		}
	}

	// Set scroll content height
	rightCol->setContentHeight(totalHeight + 10);

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

	// Call the callback once for each item in quantity
	// The WorkQueue::addJob will merge them if the same recipe is already queued
	if (onQueueRecipeCallback) {
		uint32_t qty = model.quantity();
		for (uint32_t i = 0; i < qty; ++i) {
			onQueueRecipeCallback(model.selectedRecipeDefName());
		}
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
