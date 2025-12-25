#include "EntityInfoView.h"

#include "scenes/game/ui/adapters/SelectionAdapter.h"

#include <theme/PanelStyle.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace world_sim {

	EntityInfoView::EntityInfoView(const Args& args)
		: panelWidth(args.width),
		  panelX(args.position.x),
		  onCloseCallback(args.onClose),
		  onTaskListToggleCallback(args.onTaskListToggle),
		  onQueueRecipeCallback(args.onQueueRecipe) {

		contentWidth = panelWidth - (2.0F * kPadding);

		// Estimate max panel height (will resize dynamically based on content)
		panelHeight = 160.0F;

		// Add background panel (semi-transparent dark)
		backgroundHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = args.position,
					.size = {panelWidth, panelHeight},
					.style = UI::PanelStyles::floating(),
					.zIndex = 0,
					.id = (args.id + "_bg").c_str()
				}
			)
		);

		// Add close button background [X] in top-right corner
		auto closePos = getCloseButtonPosition(args.position.y);
		closeButtonBgHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = closePos,
					.size = {kCloseButtonSize, kCloseButtonSize},
					.style = UI::PanelStyles::closeButton(),
					.zIndex = 2,
					.id = (args.id + "_close_bg").c_str()
				}
			)
		);

		// Add close button text
		closeButtonTextHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {closePos.x + kCloseButtonSize * 0.5F, closePos.y + kCloseButtonSize * 0.5F - 1.0F},
					.text = "X",
					.style =
						{
							.color = UI::Theme::Colors::closeButtonText,
							.fontSize = 10.0F,
							.hAlign = Foundation::HorizontalAlign::Center,
							.vAlign = Foundation::VerticalAlign::Middle,
						},
					.zIndex = 3,
					.id = (args.id + "_close_text").c_str()
				}
			)
		);

		// Add title text
		titleHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, args.position.y + kPadding},
					.text = "Select Entity",
					.style =
						{
							.color = UI::Theme::Colors::textTitle,
							.fontSize = kTitleFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_title").c_str()
				}
			)
		);

		// Create text slot pool (positions set when shown via renderContent)
		textHandles.reserve(kMaxTextSlots);
		for (size_t i = 0; i < kMaxTextSlots; ++i) {
			textHandles.push_back(addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding, args.position.y},
						.text = "",
						.style =
							{
								.color = UI::Theme::Colors::textBody,
								.fontSize = kTextFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
						.zIndex = 1,
						.id = (args.id + "_text_" + std::to_string(i)).c_str()
					}
				)
			));
		}

		// Create progress bar pool (positions set when shown via renderContent)
		progressBarHandles.reserve(kMaxProgressBars);
		for (size_t i = 0; i < kMaxProgressBars; ++i) {
			progressBarHandles.push_back(addChild(NeedBar(
				NeedBar::Args{
					.position = {args.position.x + kPadding, args.position.y},
					.width = contentWidth,
					.height = kProgressBarHeight,
					.label = "",
					.id = args.id + "_bar_" + std::to_string(i)
				}
			)));
		}

		// Create list header (position set when shown via renderContent)
		listHeaderHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, args.position.y},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textBody,
							.fontSize = kTextFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_list_header").c_str()
				}
			)
		);

		// Create list item pool (positions set when shown via renderContent)
		listItemHandles.reserve(kMaxListItems);
		for (size_t i = 0; i < kMaxListItems; ++i) {
			listItemHandles.push_back(addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding + 8.0F, args.position.y},
						.text = "",
						.style =
							{
								.color = UI::Theme::Colors::statusActive,
								.fontSize = kTextFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
						.zIndex = 1,
						.id = (args.id + "_list_" + std::to_string(i)).c_str()
					}
				)
			));
		}

		// Create clickable text element (for ClickableTextSlot)
		clickableTextHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, args.position.y},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textClickable,
							.fontSize = kTextFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_clickable").c_str()
				}
			)
		);

		// Create recipe card pool (for RecipeSlot)
		recipeCardHandles.reserve(kMaxRecipeCards);
		recipeCallbacks.resize(kMaxRecipeCards);
		recipeButtonBounds.resize(kMaxRecipeCards);
		for (size_t i = 0; i < kMaxRecipeCards; ++i) {
			RecipeCardHandles card;

			// Card background
			card.background = addChild(
				UI::Rectangle(
					UI::Rectangle::Args{
						.position = {args.position.x + kPadding, args.position.y},
						.size = {contentWidth, kRecipeCardHeight},
						.style = UI::PanelStyles::card(),
						.zIndex = 1,
						.id = (args.id + "_recipe_bg_" + std::to_string(i)).c_str()
					}
				)
			);

			// Recipe name text
			card.nameText = addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding + kRecipeCardPadding, args.position.y},
						.text = "",
						.style =
							{
								.color = UI::Theme::Colors::textTitle,
								.fontSize = kRecipeNameFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
						.zIndex = 2,
						.id = (args.id + "_recipe_name_" + std::to_string(i)).c_str()
					}
				)
			);

			// Ingredients text
			card.ingredientsText = addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding + kRecipeCardPadding, args.position.y},
						.text = "",
						.style =
							{
								.color = UI::Theme::Colors::textSecondary,
								.fontSize = kRecipeIngredientsFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
						.zIndex = 2,
						.id = (args.id + "_recipe_ingredients_" + std::to_string(i)).c_str()
					}
				)
			);

			// Queue button background [+]
			card.queueButton = addChild(
				UI::Rectangle(
					UI::Rectangle::Args{
						.position = {args.position.x + contentWidth - kRecipeQueueButtonSize, args.position.y},
						.size = {kRecipeQueueButtonSize, kRecipeQueueButtonSize},
						.style = UI::PanelStyles::actionButton(),
						.zIndex = 2,
						.id = (args.id + "_recipe_btn_" + std::to_string(i)).c_str()
					}
				)
			);

			// Queue button text
			card.queueButtonText = addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + contentWidth - kRecipeQueueButtonSize * 0.5F, args.position.y},
						.text = "+",
						.style =
							{
								.color = UI::Theme::Colors::actionButtonText,
								.fontSize = 14.0F,
								.hAlign = Foundation::HorizontalAlign::Center,
								.vAlign = Foundation::VerticalAlign::Middle,
							},
						.zIndex = 3,
						.id = (args.id + "_recipe_btn_text_" + std::to_string(i)).c_str()
					}
				)
			);

			recipeCardHandles.push_back(card);
		}

		// Create tab bar for colonist selection (hidden initially)
		// Capture 'this' for callback
		tabBarHandle = addChild(
			UI::TabBar(
				UI::TabBar::Args{
					.position = {args.position.x + kPadding, args.position.y + kPadding + kTitleFontSize + kLineSpacing},
					.width = contentWidth,
					.tabs =
						{
							{.id = "status", .label = "Status"},
							{.id = "inventory", .label = "Inventory"},
						},
					.selectedId = "status",
					.onSelect = [this](const std::string& tabId) { onTabChanged(tabId); },
					.id = (args.id + "_tabbar").c_str(),
				}
			)
		);

		// Disable child sorting to preserve LayerHandle indices
		childrenNeedSorting = false;

		// Start hidden (inherited IComponent::visible defaults to true)
		visible = false;
		hideSlots();
	}

	void EntityInfoView::update(
		const ecs::World& world,
		const engine::assets::AssetRegistry& assetRegistry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		const Selection& selection
	) {
		// Prepare callbacks for model
		EntityInfoModel::Callbacks callbacks{
			.onTaskListToggle = onTaskListToggleCallback,
			.onQueueRecipe = onQueueRecipeCallback,
		};

		// Let model handle all the logic (selection detection, change detection, content generation)
		auto updateType = m_model.refresh(selection, world, assetRegistry, recipeRegistry, callbacks);

		// React based on update type
		switch (updateType) {
			case EntityInfoModel::UpdateType::None:
				// No change needed
				break;

			case EntityInfoModel::UpdateType::Hide:
				visible = false;
				hideSlots();
				break;

			case EntityInfoModel::UpdateType::Show:
				visible = true;
				// Sync tab bar with model's tab selection
				if (auto* tabBar = getChild<UI::TabBar>(tabBarHandle)) {
					tabBar->setSelected(m_model.activeTab());
				}
				renderContent(m_model.content());
				break;

			case EntityInfoModel::UpdateType::Structure:
				// Sync tab bar with model's tab selection (in case selection changed)
				if (auto* tabBar = getChild<UI::TabBar>(tabBarHandle)) {
					tabBar->setSelected(m_model.activeTab());
				}
				renderContent(m_model.content());
				break;

			case EntityInfoModel::UpdateType::Values:
				updateValues(m_model.content());
				break;
		}
	}

	void EntityInfoView::renderContent(const PanelContent& content) {
		// Reset slot usage counters
		usedTextSlots = 0;
		usedProgressBars = 0;
		usedListItems = 0;
		usedRecipeCards = 0;

		// Clear clickable slot state (will be set if content has ClickableTextSlot)
		clickableCallback = nullptr;
		clickableBoundsMin = {};
		clickableBoundsMax = {};

		// Clear recipe callbacks
		for (auto& cb : recipeCallbacks) {
			cb = nullptr;
		}

		// Hide all pool elements first (will show ones we use)
		hideSlots();

		// Helper lambda to compute height for a set of slots
		auto computeSlotsHeight = [this](const std::vector<InfoSlot>& slots) -> float {
			float height = 0.0F;
			for (const auto& slot : slots) {
				height += std::visit(
					[this](const auto& s) -> float {
						using T = std::decay_t<decltype(s)>;
						if constexpr (std::is_same_v<T, TextSlot>) {
							return kTextFontSize + kLineSpacing;
						} else if constexpr (std::is_same_v<T, ProgressBarSlot>) {
							return kProgressBarHeight + kLineSpacing;
						} else if constexpr (std::is_same_v<T, TextListSlot>) {
							float  h = kTextFontSize + 2.0F; // header
							size_t itemCount = std::min(s.items.size(), kMaxListItems);
							h += static_cast<float>(itemCount) * (kTextFontSize + 2.0F);
							return h + kLineSpacing;
						} else if constexpr (std::is_same_v<T, SpacerSlot>) {
							return s.height;
						} else if constexpr (std::is_same_v<T, ClickableTextSlot>) {
							return kTextFontSize + kLineSpacing;
						} else if constexpr (std::is_same_v<T, RecipeSlot>) {
							return kRecipeCardHeight + kRecipeCardSpacing;
						}
						return 0.0F;
					},
					slot
				);
			}
			return height;
		};

		// First pass: compute content height to determine panel position
		// (panel bottom aligns with viewport bottom)
		float baseHeight = kPadding + kTitleFontSize + kLineSpacing * 2.0F;

		// Add tab bar height if showing tabs (with extra spacing below)
		if (m_model.showsTabs()) {
			baseHeight += kTabBarHeight + kLineSpacing * 3.0F;
		}

		// For tabbed panels, use fixed height based on Status tab (which is typically tallest)
		// This prevents the panel from jumping when switching tabs
		float contentHeight = computeSlotsHeight(content.slots);

		if (m_model.showsTabs()) {
			// Use a fixed minimum content height for colonist panels
			// Status tab has: Mood + 8 needs + spacer + Task + Action + Tasks clickable
			// = 9 progress bars + 1 spacer + 3 text slots
			constexpr float kMinColonistContentHeight = 9.0F * (kProgressBarHeight + kLineSpacing) + // 9 progress bars (mood + 8 needs)
														8.0F +										 // spacer
														3.0F * (kTextFontSize + kLineSpacing);		 // Task, Action, Tasks clickable
			contentHeight = std::max(contentHeight, kMinColonistContentHeight);
		}

		float totalHeight = baseHeight + contentHeight + kPadding; // bottom padding

		panelHeight = totalHeight;
		float panelY = m_viewportHeight - panelHeight;

		// Show and position background
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->visible = true;
			bg->position = {panelX, panelY};
			bg->size.y = panelHeight;
		}

		// Show and position close button
		auto closePos = getCloseButtonPosition(panelY);
		if (auto* closeBg = getChild<UI::Rectangle>(closeButtonBgHandle)) {
			closeBg->visible = true;
			closeBg->position = closePos;
		}
		if (auto* closeText = getChild<UI::Text>(closeButtonTextHandle)) {
			closeText->visible = true;
			closeText->position = {closePos.x + kCloseButtonSize * 0.5F, closePos.y + kCloseButtonSize * 0.5F - 1.0F};
		}

		// Show and position title
		if (auto* title = getChild<UI::Text>(titleHandle)) {
			title->visible = true;
			title->position = {panelX + kPadding, panelY + kPadding};
			title->text = content.title;
		}

		// Show/hide and position tab bar
		float yOffset = panelY + kPadding + kTitleFontSize + kLineSpacing * 2.0F;
		if (auto* tabBar = getChild<UI::TabBar>(tabBarHandle)) {
			tabBar->visible = m_model.showsTabs();
			if (m_model.showsTabs()) {
				tabBar->position = {panelX + kPadding, yOffset};
				yOffset += kTabBarHeight + kLineSpacing * 3.0F; // Extra spacing below tab bar
			}
		}

		// Render slots (each slot renderer sets visible=true on used elements)
		for (const auto& slot : content.slots) {
			yOffset += renderSlot(slot, yOffset);
		}
	}

	void EntityInfoView::hideSlots() {
		// Hide all children via inherited Component::children vector
		// This is O(n) but n is small (~30 elements) and avoids handle lookups
		for (auto* child : children) {
			child->visible = false;
		}
	}

	float EntityInfoView::renderSlot(const InfoSlot& slot, float yOffset) {
		return std::visit(
			[this, yOffset](const auto& s) -> float {
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, TextSlot>) {
					return renderTextSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, ProgressBarSlot>) {
					return renderProgressBarSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, TextListSlot>) {
					return renderTextListSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, SpacerSlot>) {
					return renderSpacerSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, ClickableTextSlot>) {
					return renderClickableTextSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, RecipeSlot>) {
					return renderRecipeSlot(s, yOffset);
				}
				return 0.0F;
			},
			slot
		);
	}

	float EntityInfoView::renderTextSlot(const TextSlot& slot, float yOffset) {
		if (usedTextSlots >= textHandles.size()) {
			return 0.0F;
		}

		if (auto* text = getChild<UI::Text>(textHandles[usedTextSlots])) {
			text->visible = true;
			text->position = {panelX + kPadding, yOffset};
			text->text = slot.label + ": " + slot.value;
		}

		++usedTextSlots;
		return kTextFontSize + kLineSpacing;
	}

	float EntityInfoView::renderProgressBarSlot(const ProgressBarSlot& slot, float yOffset) {
		if (usedProgressBars >= progressBarHandles.size()) {
			return 0.0F;
		}

		if (auto* bar = getChild<NeedBar>(progressBarHandles[usedProgressBars])) {
			bar->visible = true;
			bar->setPosition({panelX + kPadding, yOffset});
			bar->setValue(slot.value);
			bar->setLabel(slot.label);
		}

		++usedProgressBars;
		return kProgressBarHeight + kLineSpacing;
	}

	float EntityInfoView::renderTextListSlot(const TextListSlot& slot, float yOffset) {
		float height = 0.0F;

		// Render header
		if (auto* header = getChild<UI::Text>(listHeaderHandle)) {
			header->visible = true;
			header->position = {panelX + kPadding, yOffset};
			header->text = slot.header + ":";
		}
		height += kTextFontSize + 2.0F;

		// Render items
		for (size_t i = 0; i < slot.items.size() && usedListItems < listItemHandles.size(); ++i) {
			if (auto* item = getChild<UI::Text>(listItemHandles[usedListItems])) {
				item->visible = true;
				item->position = {panelX + kPadding + 8.0F, yOffset + height};
				item->text = "- " + slot.items[i];
			}
			++usedListItems;
			height += kTextFontSize + 2.0F;
		}

		return height + kLineSpacing;
	}

	float EntityInfoView::renderSpacerSlot(const SpacerSlot& slot, float /*yOffset*/) {
		return slot.height;
	}

	float EntityInfoView::renderClickableTextSlot(const ClickableTextSlot& slot, float yOffset) {
		if (auto* text = getChild<UI::Text>(clickableTextHandle)) {
			text->visible = true;
			text->position = {panelX + kPadding, yOffset};
			text->text = slot.label + ": " + slot.value;

			// Store callback and bounds for click handling
			clickableCallback = slot.onClick;
			clickableBoundsMin = {panelX + kPadding, yOffset};
			clickableBoundsMax = {panelX + contentWidth, yOffset + kTextFontSize};
		}
		return kTextFontSize + kLineSpacing;
	}

	float EntityInfoView::renderRecipeSlot(const RecipeSlot& slot, float yOffset) {
		if (usedRecipeCards >= recipeCardHandles.size()) {
			return 0.0F;
		}

		auto& card = recipeCardHandles[usedRecipeCards];
		float cardX = panelX + kPadding;
		float buttonX = panelX + kPadding + contentWidth - kRecipeQueueButtonSize - kRecipeCardPadding;
		float buttonY = yOffset + (kRecipeCardHeight - kRecipeQueueButtonSize) * 0.5F;

		// Position card background
		if (auto* bg = getChild<UI::Rectangle>(card.background)) {
			bg->visible = true;
			bg->position = {cardX, yOffset};
			bg->size = {contentWidth, kRecipeCardHeight};
		}

		// Position recipe name (top-left inside card)
		if (auto* name = getChild<UI::Text>(card.nameText)) {
			name->visible = true;
			name->position = {cardX + kRecipeCardPadding, yOffset + kRecipeCardPadding};
			name->text = slot.name;
		}

		// Position ingredients (below name, smaller text)
		if (auto* ingredients = getChild<UI::Text>(card.ingredientsText)) {
			ingredients->visible = true;
			ingredients->position = {cardX + kRecipeCardPadding, yOffset + kRecipeCardPadding + kRecipeNameFontSize + 2.0F};
			ingredients->text = slot.ingredients;
		}

		// Position queue button [+] (right side, vertically centered)
		if (auto* btn = getChild<UI::Rectangle>(card.queueButton)) {
			btn->visible = true;
			btn->position = {buttonX, buttonY};
		}

		// Position button text
		if (auto* btnText = getChild<UI::Text>(card.queueButtonText)) {
			btnText->visible = true;
			btnText->position = {buttonX + kRecipeQueueButtonSize * 0.5F, buttonY + kRecipeQueueButtonSize * 0.5F};
		}

		// Store callback and bounds for click handling
		recipeCallbacks[usedRecipeCards] = slot.onQueue;
		recipeButtonBounds[usedRecipeCards] = Foundation::Rect{
			buttonX, buttonY, kRecipeQueueButtonSize, kRecipeQueueButtonSize
		};

		++usedRecipeCards;
		return kRecipeCardHeight + kRecipeCardSpacing;
	}

	Foundation::Vec2 EntityInfoView::getCloseButtonPosition(float panelY) const {
		return {panelX + panelWidth - kPadding - kCloseButtonSize, panelY + kPadding};
	}

	void EntityInfoView::updateValues(const PanelContent& content) {
		// Tier 3: Value-only update - same entity, just update dynamic slot values
		// Updates progress bars and text slots (for action/task status changes)
		// Skips all position calculations for significant performance savings

		size_t barIndex = 0;
		size_t textIndex = 0;
		for (const auto& slot : content.slots) {
			if (const auto* barSlot = std::get_if<ProgressBarSlot>(&slot)) {
				if (barIndex < progressBarHandles.size()) {
					if (auto* bar = getChild<NeedBar>(progressBarHandles[barIndex])) {
						bar->setValue(barSlot->value);
					}
				}
				++barIndex;
			} else if (const auto* textSlot = std::get_if<TextSlot>(&slot)) {
				// Update text slots (for Task/Action status that changes frequently)
				if (textIndex < textHandles.size()) {
					if (auto* text = getChild<UI::Text>(textHandles[textIndex])) {
						text->text = textSlot->label + ": " + textSlot->value;
					}
				}
				++textIndex;
			}
		}
	}

	void EntityInfoView::setBottomLeftPosition(float x, float viewportHeight) {
		if (panelX == x && m_viewportHeight == viewportHeight) {
			return; // No change
		}

		panelX = x;
		m_viewportHeight = viewportHeight;

		// Force structure re-render if currently visible
		// This ensures all child elements get repositioned correctly
		if (visible && m_model.isVisible()) {
			renderContent(m_model.content());
		}
	}

	void EntityInfoView::onTabChanged(const std::string& tabId) {
		// Delegate to model - it will set the flag for next refresh()
		m_model.setActiveTab(tabId);
	}

	bool EntityInfoView::handleEvent(UI::InputEvent& event) {
		if (!visible) {
			return false;
		}

		// Handle TabBar events if showing tabs
		if (m_model.showsTabs()) {
			if (auto* tabBar = getChild<UI::TabBar>(tabBarHandle)) {
				if (tabBar->handleEvent(event)) {
					return true;
				}
			}
		}

		// Only handle mouse up (click) events for interactive elements
		if (event.type != UI::InputEvent::Type::MouseUp) {
			return false;
		}

		if (event.button != engine::MouseButton::Left) {
			return false;
		}

		auto pos = event.position;

		// Check close button
		float panelY = m_viewportHeight - panelHeight;
		auto closePos = getCloseButtonPosition(panelY);

		if (pos.x >= closePos.x && pos.x <= closePos.x + kCloseButtonSize &&
			pos.y >= closePos.y && pos.y <= closePos.y + kCloseButtonSize) {
			if (onCloseCallback) {
				onCloseCallback();
			}
			event.consume();
			return true;
		}

		// Check clickable slot
		if (clickableCallback &&
			pos.x >= clickableBoundsMin.x && pos.x <= clickableBoundsMax.x &&
			pos.y >= clickableBoundsMin.y && pos.y <= clickableBoundsMax.y) {
			clickableCallback();
			event.consume();
			return true;
		}

		// Check recipe buttons
		for (size_t i = 0; i < usedRecipeCards; ++i) {
			const auto& bounds = recipeButtonBounds[i];
			if (recipeCallbacks[i] &&
				pos.x >= bounds.x && pos.x <= bounds.x + bounds.width &&
				pos.y >= bounds.y && pos.y <= bounds.y + bounds.height) {
				recipeCallbacks[i]();
				event.consume();
				return true;
			}
		}

		// Check if click is within panel bounds - consume to prevent world click
		if (pos.x >= panelX && pos.x <= panelX + panelWidth &&
			pos.y >= panelY && pos.y <= panelY + panelHeight) {
			event.consume();
			return true;
		}

		return false;
	}

} // namespace world_sim
