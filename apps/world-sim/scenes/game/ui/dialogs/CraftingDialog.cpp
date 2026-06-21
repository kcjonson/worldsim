#include "CraftingDialog.h"

#include <components/list/ListRow.h>
#include <components/progress/ProgressBar.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>
#include <theme/Tokens.h>

namespace {
// Recipe list row height.
constexpr float kRecipeItemHeight = 26.0F;
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

void CraftingDialog::update(ecs::World& world,
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
		rebuildRecipeColumn();
		rebuildCenterColumn();
		rebuildQueueColumn();
	} else {
		// Update queue column when queue changes (progress, completions)
		if (updateType == CraftingDialogModel::UpdateType::Queue ||
		    updateType == CraftingDialogModel::UpdateType::Full) {
			rebuildQueueColumn();
		}
		// A full refresh can flip recipe availability, so refresh the row dimming
		if (updateType == CraftingDialogModel::UpdateType::Full) {
			rebuildRecipeColumn();
		}
	}

	// Rebuild recipe column after a selection change (to move the highlight)
	if (needsRecipeRebuild) {
		needsRecipeRebuild = false;
		rebuildRecipeColumn();
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

	// Dialog renders its chrome plus all content children (the recipe ListRows,
	// the center detail column, and the queue column).
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	if (dialog != nullptr) {
		dialog->render();
	}
}

bool CraftingDialog::handleEvent(UI::InputEvent& event) {
	if (!isOpen()) {
		return false;
	}

	// The recipe ListRows are content children of the Dialog, so the Dialog's
	// dispatch reaches them (with the content-offset transform) and they handle
	// their own hover/click.
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->handleEvent(event);
}

bool CraftingDialog::containsPoint(Foundation::Vec2 point) const {
	if (!isOpen()) {
		return false;
	}
	auto* dialog = getChild<UI::Dialog>(dialogHandle);
	return dialog != nullptr && dialog->containsPoint(point);
}

void CraftingDialog::rebuildRecipeColumn() {
	auto* contentLayout = getContentLayout();
	if (contentLayout == nullptr) {
		return;
	}
	auto* leftCol = contentLayout->getChild<UI::ScrollContainer>(leftColumnHandle);
	if (leftCol == nullptr) {
		return;
	}

	leftCol->clearChildren();

	// Vertical list: a header followed by one selectable ListRow per recipe.
	auto listLayout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {0, 0},
		.size = {kLeftColumnWidth, 0},  // Height auto-computed from children
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left,
		.vAlign = UI::VAlign::Top
	});

	listLayout.addChild(UI::Text(UI::Text::Args{
		.text = "RECIPES",
		.style = {.color = UI::text_dim, .fontSize = 11},
		.margin = 4.0F
	}));

	const auto& recipes = model.recipes();
	for (size_t i = 0; i < recipes.size(); ++i) {
		int idx = static_cast<int>(i);
		listLayout.addChild(UI::ListRow(UI::ListRow::Args{
			.label = recipes[i].label,
			.size = {kLeftColumnWidth, kRecipeItemHeight},
			.selected = (idx == recipeSelectedIndex),
			.dim = !recipes[i].canCraft,
			.onClick = [this, idx]() { handleRecipeClick(idx); }
		}));
	}

	leftCol->setContentHeight(listLayout.getHeight() + 10.0F);
	leftCol->addChild(std::move(listLayout));
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
			.style = {.color = UI::text_dim, .fontSize = 14},
			.margin = 8.0F
		}));
	} else {
		// Recipe name header - auto-sized
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = details.name,
			.style = {.color = UI::text_bright, .fontSize = 16},
			.margin = 4.0F
		}));

		// Description - auto-sized with word wrap for longer descriptions
		if (!details.description.empty()) {
			centerCol->addChild(UI::Text(UI::Text::Args{
				.width = centerWidth - 16,  // Set width for wrapping
				.text = details.description,
				.style = {.color = UI::text, .fontSize = 12, .wordWrap = true},
				.margin = 2.0F
			}));
		}

		// REQUIRES section
		if (!details.materials.empty()) {
			centerCol->addChild(UI::Text(UI::Text::Args{
				.text = "REQUIRES",
				.style = {.color = UI::text_dim, .fontSize = 11},
				.margin = 6.0F
			}));

			for (const auto& mat : details.materials) {
				// Use ASCII indicators
				std::string matLine = std::to_string(mat.required) + "x " + mat.label;
				matLine += mat.hasEnough ? " [OK]" : " [X]";

				centerCol->addChild(UI::Text(UI::Text::Args{
					.text = matLine,
					.style = {.color = mat.hasEnough ? UI::status_ok : UI::status_crit, .fontSize = 12},
					.margin = 1.0F
				}));
			}
		}

		// PRODUCES section
		if (!details.outputs.empty()) {
			centerCol->addChild(UI::Text(UI::Text::Args{
				.text = "PRODUCES",
				.style = {.color = UI::text_dim, .fontSize = 11},
				.margin = 6.0F
			}));

			for (const auto& output : details.outputs) {
				std::string outLine = std::to_string(output.count) + "x " + output.label;
				centerCol->addChild(UI::Text(UI::Text::Args{
					.text = outLine,
					.style = {.color = UI::text, .fontSize = 12},
					.margin = 1.0F
				}));
			}
		}

		// WORK TIME
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = "WORK TIME",
			.style = {.color = UI::text_dim, .fontSize = 11},
			.margin = 6.0F
		}));

		std::string timeStr = "~" + std::to_string(static_cast<int>(details.workTime)) + " seconds";
		centerCol->addChild(UI::Text(UI::Text::Args{
			.text = timeStr,
			.style = {.color = UI::text, .fontSize = 12},
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
			.style = {.color = UI::text, .fontSize = 16, .hAlign = Foundation::HorizontalAlign::Center},
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
		.style = {.color = UI::text_dim, .fontSize = 11},
		.margin = 4.0F
	}));

	queueItemHandles.clear();
	const auto& queue = model.queue();

	if (queue.empty()) {
		queueLayout.addChild(UI::Text(UI::Text::Args{
			.text = "No items queued",
			.style = {.color = UI::text_dim, .fontSize = 12},
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
					.style = {.color = UI::text_dim, .fontSize = 10},
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
					.style = {.color = UI::text, .fontSize = 12},
					.margin = 2.0F
				}));

				// Progress bar
				queueLayout.addChild(UI::ProgressBar(UI::ProgressBar::Args{
					.width = kRightColumnWidth - 32,
					.value = item.progress,
					.tone = UI::Tone::Data,
					.margin = 2.0F
				}));

				// Add "Queued:" header after in-progress item if there are more
				if (queue.size() > 1) {
					queueLayout.addChild(UI::Text(UI::Text::Args{
						.text = "Queued:",
						.style = {.color = UI::text_dim, .fontSize = 10},
						.margin = 4.0F
					}));
				}
			} else {
				// Queued item - show name and cancel button - auto-sized
				queueLayout.addChild(UI::Text(UI::Text::Args{
					.text = itemLabel,
					.style = {.color = UI::text, .fontSize = 12},
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

	recipeSelectedIndex = recipeIndex;

	// Update model selection
	model.selectRecipe(recipes[static_cast<size_t>(recipeIndex)].defName);

	// Move the row highlight, and rebuild the center column after the next
	// model.refresh() (it calls extractSelectedDetails with the registry first).
	needsRecipeRebuild = true;
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
