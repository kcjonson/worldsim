#include "EntityInfoView.h"

#include "scenes/game/ui/adapters/SelectionAdapter.h"

#include <ecs/components/Needs.h>
#include <theme/PanelStyle.h>
#include <theme/Theme.h>
#include <utils/Log.h>

namespace world_sim {

	EntityInfoView::EntityInfoView(const Args& args)
		: panelWidth(args.width),
		  panelX(args.position.x),
		  onCloseCallback(args.onClose),
		  onDetailsCallback(args.onDetails),
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
					.id = (args.id + "_close_text").c_str()
				}
			)
		);

		// Title text (used for single-column layout)
		titleHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, args.position.y + kPadding},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textTitle,
							.fontSize = kNameFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.id = (args.id + "_title").c_str()
				}
			)
		);

		// ========== Colonist header elements (two-column layout) ==========

		// Portrait placeholder (gray rectangle)
		portraitHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = {args.position.x + kPadding, args.position.y + kPadding},
					.size = {kPortraitSize, kPortraitSize},
					.style =
						{.fill = Foundation::Color(0.20F, 0.20F, 0.25F, 1.0F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.30F, 0.30F, 0.35F, 1.0F), .width = 1.0F}},
					.id = (args.id + "_portrait").c_str()
				}
			)
		);

		// Header name "Sarah Chen, 28"
		headerNameHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding + kPortraitSize + kSectionGap, args.position.y + kPadding},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textTitle,
							.fontSize = kNameFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.id = (args.id + "_header_name").c_str()
				}
			)
		);

		// Header mood bar - uses NeedBar component for consistent color gradient
		// No label (label is rendered separately on the right side)
		headerMoodBarHandle = addChild(NeedBar(
			NeedBar::Args{
				.position =
					{args.position.x + kPadding + kPortraitSize + kSectionGap, args.position.y + kPadding + kNameFontSize + kItemGap},
				.width = kHeaderMoodBarWidth,
				.height = kHeaderMoodBarHeight,
				.size = NeedBarSize::Compact,
				.label = "", // No label - we render "72% Content" separately on the right
				.id = args.id + "_mood_bar"
			}
		));

		// Header mood label "72% Content"
		headerMoodLabelHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position =
						{args.position.x + kPadding + kPortraitSize + kSectionGap + kHeaderMoodBarWidth + kIconLabelGap,
						 args.position.y + kPadding + kNameFontSize + kItemGap},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textSecondary,
							.fontSize = kLabelFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.id = (args.id + "_mood_label").c_str()
				}
			)
		);

		// "Needs:" section header (right column)
		needsLabelHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, args.position.y},
					.text = "Needs:",
					.style =
						{
							.color = UI::Theme::Colors::textHeader,
							.fontSize = kHeaderFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.id = (args.id + "_needs_label").c_str()
				}
			)
		);

		// ========== Single-column layout elements (items/flora) ==========

		// Centered icon placeholder
		centeredIconHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = {args.position.x + (panelWidth - kEntityIconSize) * 0.5F, args.position.y + kPadding},
					.size = {kEntityIconSize, kEntityIconSize},
					.style =
						{.fill = Foundation::Color(0.25F, 0.25F, 0.30F, 1.0F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.35F, 0.35F, 0.40F, 1.0F), .width = 1.0F}},
					.id = (args.id + "_centered_icon").c_str()
				}
			)
		);

		// Centered entity label
		centeredLabelHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + panelWidth * 0.5F, args.position.y + kPadding + kEntityIconSize + kIconLabelGap},
					.text = "",
					.style =
						{
							.color = UI::Theme::Colors::textTitle,
							.fontSize = kNameFontSize,
							.hAlign = Foundation::HorizontalAlign::Center,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.id = (args.id + "_centered_label").c_str()
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
								.fontSize = kLabelFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
						.id = (args.id + "_text_" + std::to_string(i)).c_str()
					}
				)
			));
		}

		// Create progress bar pool for needs (positions set when shown via renderContent)
		// Labels come from ecs::needLabel() - single source of truth with bounds checking
		progressBarHandles.reserve(kMaxProgressBars);
		for (size_t i = 0; i < kMaxProgressBars; ++i) {
			// Use actual need label for first N needs, empty for extras
			const char* label = (i < static_cast<size_t>(ecs::NeedType::Count)) ? ecs::needLabel(static_cast<ecs::NeedType>(i)) : "";
			progressBarHandles.push_back(addChild(NeedBar(
				NeedBar::Args{
					.position = {args.position.x + kPadding, args.position.y},
					.width = contentWidth,
					.height = kNeedBarHeight,
					.label = label,
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
							.fontSize = kLabelFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
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
								.fontSize = kLabelFontSize,
								.hAlign = Foundation::HorizontalAlign::Left,
								.vAlign = Foundation::VerticalAlign::Top,
							},
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
							.fontSize = kLabelFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
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
						.id = (args.id + "_recipe_btn_text_" + std::to_string(i)).c_str()
					}
				)
			);

			recipeCardHandles.push_back(card);
		}

		// Create details button icon (hidden initially, shown for colonists)
		// Icon: "open in new window" - rectangle outline + arrow
		auto detailsPos = getDetailsButtonPosition(args.position.y);
		detailsButtonBgHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = detailsPos,
					.size = {kDetailsButtonSize, kDetailsButtonSize},
					.style = UI::PanelStyles::actionButton(),
					.id = (args.id + "_details_bg").c_str()
				}
			)
		);

		// Create icon line elements (positions set by updateDetailsIcon)
		Foundation::Color iconColor = UI::Theme::Colors::actionButtonText;
		constexpr float	  lineWidth = 1.5F;
		auto			  createLine = [&]() {
			 return addChild(
				 UI::Line(UI::Line::Args{.start = {0.0F, 0.0F}, .end = {0.0F, 0.0F}, .style = {.color = iconColor, .width = lineWidth}})
			 );
		};
		detailsIconLine1Handle = createLine();
		detailsIconLine2Handle = createLine();
		detailsIconLine3Handle = createLine();
		detailsIconLine4Handle = createLine();
		detailsIconLine5Handle = createLine();
		detailsIconLine6Handle = createLine();

		// Set initial positions (icon starts hidden)
		updateDetailsIcon(false, detailsPos);

		// Disable child sorting to preserve LayerHandle indices
		childrenNeedSorting = false;

		// Start hidden (inherited IComponent::visible defaults to true)
		visible = false;
		hideSlots();
	}

	void EntityInfoView::update(
		const ecs::World&					  world,
		const engine::assets::AssetRegistry&  assetRegistry,
		const engine::assets::RecipeRegistry& recipeRegistry,
		const Selection&					  selection
	) {
		// Prepare callbacks for model
		EntityInfoModel::Callbacks callbacks{
			.onDetails = onDetailsCallback,
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
				renderContent(m_model.content());
				break;

			case EntityInfoModel::UpdateType::Structure:
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

		// Fixed panel height for all entity types - ensures visual consistency
		// Header: kPadding(12) + kPortraitSize(64) + kSectionGap(12) = 88px
		//         Name text and mood bar are positioned within the portrait band
		// Column: kHeaderFontSize(12) + kItemGap(4) + 8 needs * (kNeedBarHeight(16) + kItemGap(4)) = 16 + 160 = 176px
		// Bottom: kPadding(12) = 12px
		// Total = 88 + 176 + 12 = 276px, plus 4px extra padding for breathing room -> 280px
		constexpr float kFixedPanelHeight = 280.0F;
		float			totalHeight = kFixedPanelHeight;

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

		// Dispatch to appropriate layout renderer
		if (content.layout == PanelLayout::TwoColumn) {
			renderTwoColumnLayout(content, panelY);
		} else {
			renderSingleColumnLayout(content, panelY);
		}
	}

	void EntityInfoView::renderSingleColumnLayout(const PanelContent& content, float panelY) {
		// Hide colonist-specific header elements
		if (auto* portrait = getChild<UI::Rectangle>(portraitHandle)) {
			portrait->visible = false;
		}
		if (auto* headerName = getChild<UI::Text>(headerNameHandle)) {
			headerName->visible = false;
		}
		if (auto* moodBar = getChild<NeedBar>(headerMoodBarHandle)) {
			moodBar->visible = false;
		}
		if (auto* moodLabel = getChild<UI::Text>(headerMoodLabelHandle)) {
			moodLabel->visible = false;
		}
		if (auto* needsLabel = getChild<UI::Text>(needsLabelHandle)) {
			needsLabel->visible = false;
		}
		if (auto* title = getChild<UI::Text>(titleHandle)) {
			title->visible = false;
		}

		// Hide details button for non-colonist selections
		if (auto* detailsBg = getChild<UI::Rectangle>(detailsButtonBgHandle)) {
			detailsBg->visible = false;
		}
		// Hide all icon lines
		if (auto* line = getChild<UI::Line>(detailsIconLine1Handle))
			line->visible = false;
		if (auto* line = getChild<UI::Line>(detailsIconLine2Handle))
			line->visible = false;
		if (auto* line = getChild<UI::Line>(detailsIconLine3Handle))
			line->visible = false;
		if (auto* line = getChild<UI::Line>(detailsIconLine4Handle))
			line->visible = false;
		if (auto* line = getChild<UI::Line>(detailsIconLine5Handle))
			line->visible = false;
		if (auto* line = getChild<UI::Line>(detailsIconLine6Handle))
			line->visible = false;

		// Show centered icon placeholder
		float iconX = panelX + (panelWidth - kEntityIconSize) * 0.5F;
		if (auto* icon = getChild<UI::Rectangle>(centeredIconHandle)) {
			icon->visible = true;
			icon->position = {iconX, panelY + kPadding};
		}

		// Get entity name from first IconSlot if present
		std::string entityName = content.title;
		for (const auto& slot : content.slots) {
			if (const auto* iconSlot = std::get_if<IconSlot>(&slot)) {
				entityName = iconSlot->label;
				break;
			}
		}

		// Show centered entity label below icon
		if (auto* label = getChild<UI::Text>(centeredLabelHandle)) {
			label->visible = true;
			label->position = {panelX + panelWidth * 0.5F, panelY + kPadding + kEntityIconSize + kItemGap};
			label->text = entityName;
		}

		// Render remaining slots below the centered icon/label
		float yOffset = panelY + kPadding + kEntityIconSize + kItemGap + kNameFontSize + kSectionGap;
		for (const auto& slot : content.slots) {
			// Skip IconSlot (already rendered as centered icon)
			if (std::holds_alternative<IconSlot>(slot)) {
				continue;
			}
			yOffset += renderSlot(slot, yOffset, 0.0F, 0.0F);
		}
	}

	void EntityInfoView::renderTwoColumnLayout(const PanelContent& content, float panelY) {
		// Hide single-column elements
		if (auto* centeredIcon = getChild<UI::Rectangle>(centeredIconHandle)) {
			centeredIcon->visible = false;
		}
		if (auto* centeredLabel = getChild<UI::Text>(centeredLabelHandle)) {
			centeredLabel->visible = false;
		}
		if (auto* title = getChild<UI::Text>(titleHandle)) {
			title->visible = false;
		}

		// ========== HEADER AREA ==========
		// Portrait placeholder (64×64)
		if (auto* portrait = getChild<UI::Rectangle>(portraitHandle)) {
			portrait->visible = true;
			portrait->position = {panelX + kPadding, panelY + kPadding};
		}

		// Name to right of portrait: "Sarah Chen"
		float headerTextX = panelX + kPadding + kPortraitSize + kSectionGap;
		if (auto* headerName = getChild<UI::Text>(headerNameHandle)) {
			headerName->visible = true;
			headerName->position = {headerTextX, panelY + kPadding};
			headerName->text = content.header.name;
		}

		// Compact mood bar (8px height) below name with spacing
		// Uses NeedBar component which handles color gradient automatically
		float moodBarY = panelY + kPadding + kNameFontSize + kHeaderMoodBarOffset;
		if (auto* moodBar = getChild<NeedBar>(headerMoodBarHandle)) {
			moodBar->visible = true;
			moodBar->setPosition({headerTextX, moodBarY});
			moodBar->setValue(content.header.moodValue); // NeedBar handles color gradient
		}

		// Mood label: "72% Content" - vertically centered with mood bar
		// Pre-allocate string to avoid temporary allocations
		std::string moodText;
		{
			auto valueStr = std::to_string(static_cast<int>(content.header.moodValue));
			moodText.reserve(valueStr.size() + 2U + content.header.moodLabel.size());
			moodText.append(valueStr);
			moodText.append("% ");
			moodText.append(content.header.moodLabel);
		}
		if (auto* moodLabel = getChild<UI::Text>(headerMoodLabelHandle)) {
			moodLabel->visible = true;
			// Center text with bar: compute offset from bar height and font size
			const float moodLabelVerticalOffset = (kHeaderMoodBarHeight - kMoodLabelFontSize) * 0.5F;
			moodLabel->position = {headerTextX + kHeaderMoodBarWidth + kIconLabelGap, moodBarY + moodLabelVerticalOffset};
			moodLabel->text = std::move(moodText);
		}

		// Details icon button at top-right (only for colonists - check if callback is set)
		bool showDetailsButton = (content.onDetails != nullptr);
		auto detailsPos = getDetailsButtonPosition(panelY);
		if (auto* detailsBg = getChild<UI::Rectangle>(detailsButtonBgHandle)) {
			detailsBg->visible = showDetailsButton;
			detailsBg->position = detailsPos;
		}
		updateDetailsIcon(showDetailsButton, detailsPos);

		// ========== TWO-COLUMN CONTENT AREA ==========
		float columnsY = panelY + kPadding + kPortraitSize + kSectionGap;

		// Column widths (left is fixed, right fills remaining)
		float rightColumnWidth = contentWidth - kLeftColumnWidth - kColumnGap;
		float rightColumnX = kLeftColumnWidth + kColumnGap;

		// LEFT COLUMN: Current task, Next task, Gear list (may be empty for world entities)
		float leftY = columnsY;
		for (const auto& slot : content.leftColumn) {
			leftY += renderSlot(slot, leftY, 0.0F, kLeftColumnWidth);
		}

		// RIGHT COLUMN: "Needs:" header + need bars (only if has content)
		float rightY = columnsY;
		bool  hasNeedsContent = !content.rightColumn.empty();

		// "Needs:" section header (only show if we have needs)
		if (auto* needsLabel = getChild<UI::Text>(needsLabelHandle)) {
			needsLabel->visible = hasNeedsContent;
			if (hasNeedsContent) {
				needsLabel->position = {panelX + kPadding + rightColumnX, rightY};
			}
		}
		if (hasNeedsContent) {
			rightY += kHeaderFontSize + kItemGap;
		}

		// Need bars
		for (const auto& slot : content.rightColumn) {
			rightY += renderSlot(slot, rightY, rightColumnX, rightColumnWidth);
		}
	}

	void EntityInfoView::hideSlots() {
		// Hide all children via inherited Component::children vector
		// This is O(n) but n is small (~30 elements) and avoids handle lookups
		for (auto* child : children) {
			child->visible = false;
		}
	}

	float EntityInfoView::renderSlot(const InfoSlot& slot, float yOffset, float xOffset, float maxWidth) {
		return std::visit(
			[this, yOffset, xOffset, maxWidth](const auto& s) -> float {
				using T = std::decay_t<decltype(s)>;
				if constexpr (std::is_same_v<T, TextSlot>) {
					return renderTextSlot(s, yOffset, xOffset);
				} else if constexpr (std::is_same_v<T, ProgressBarSlot>) {
					return renderProgressBarSlot(s, yOffset, xOffset, maxWidth);
				} else if constexpr (std::is_same_v<T, TextListSlot>) {
					return renderTextListSlot(s, yOffset, xOffset);
				} else if constexpr (std::is_same_v<T, SpacerSlot>) {
					return renderSpacerSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, ClickableTextSlot>) {
					return renderClickableTextSlot(s, yOffset, xOffset);
				} else if constexpr (std::is_same_v<T, RecipeSlot>) {
					return renderRecipeSlot(s, yOffset);
				} else if constexpr (std::is_same_v<T, IconSlot>) {
					return renderIconSlot(s, yOffset);
				}
				return 0.0F;
			},
			slot
		);
	}

	float EntityInfoView::renderTextSlot(const TextSlot& slot, float yOffset, float xOffset) {
		if (usedTextSlots >= textHandles.size()) {
			return 0.0F;
		}

		if (auto* text = getChild<UI::Text>(textHandles[usedTextSlots])) {
			text->visible = true;
			text->position = {panelX + kPadding + xOffset, yOffset};
			// Pre-allocate string to avoid temporary allocations
			std::string combined;
			combined.reserve(slot.label.size() + 2U + slot.value.size());
			combined.append(slot.label);
			combined.append(": ");
			combined.append(slot.value);
			text->text = std::move(combined);
		}

		++usedTextSlots;
		return kLabelFontSize + kItemGap;
	}

	float EntityInfoView::renderProgressBarSlot(const ProgressBarSlot& slot, float yOffset, float xOffset, float maxWidth) {
		if (usedProgressBars >= progressBarHandles.size()) {
			return 0.0F;
		}

		float barWidth = (maxWidth > 0.0F) ? maxWidth : contentWidth;

		if (auto* bar = getChild<NeedBar>(progressBarHandles[usedProgressBars])) {
			bar->visible = true;
			bar->setPosition({panelX + kPadding + xOffset, yOffset});
			bar->setWidth(barWidth);
			bar->setValue(slot.value);
			bar->setLabel(slot.label);
		}

		++usedProgressBars;
		return kNeedBarHeight + kItemGap;
	}

	float EntityInfoView::renderTextListSlot(const TextListSlot& slot, float yOffset, float xOffset) {
		float height = 0.0F;

		// Render header
		if (auto* header = getChild<UI::Text>(listHeaderHandle)) {
			header->visible = true;
			header->position = {panelX + kPadding + xOffset, yOffset};
			header->text = slot.header + ":";
		}
		height += kLabelFontSize + 2.0F;

		// Render items
		// TODO: Replace text dash with small item icon rects once we have item icons
		for (size_t i = 0; i < slot.items.size() && usedListItems < listItemHandles.size(); ++i) {
			if (auto* item = getChild<UI::Text>(listItemHandles[usedListItems])) {
				item->visible = true;
				item->position = {panelX + kPadding + xOffset + 8.0F, yOffset + height};
				item->text = "- " + slot.items[i];
			}
			++usedListItems;
			height += kLabelFontSize + 2.0F;
		}

		return height + kItemGap;
	}

	float EntityInfoView::renderSpacerSlot(const SpacerSlot& slot, float /*yOffset*/) {
		return slot.height;
	}

	float EntityInfoView::renderClickableTextSlot(const ClickableTextSlot& slot, float yOffset, float xOffset) {
		if (auto* text = getChild<UI::Text>(clickableTextHandle)) {
			text->visible = true;
			text->position = {panelX + kPadding + xOffset, yOffset};
			text->text = slot.label + ": " + slot.value;

			// Store callback and bounds for click handling
			clickableCallback = slot.onClick;
			clickableBoundsMin = {panelX + kPadding + xOffset, yOffset};
			clickableBoundsMax = {panelX + contentWidth, yOffset + kLabelFontSize};
		}
		return kLabelFontSize + kItemGap;
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
		recipeButtonBounds[usedRecipeCards] = Foundation::Rect{buttonX, buttonY, kRecipeQueueButtonSize, kRecipeQueueButtonSize};

		++usedRecipeCards;
		return kRecipeCardHeight + kRecipeCardSpacing;
	}

	float EntityInfoView::renderIconSlot(const IconSlot& slot, float yOffset) {
		// IconSlot is primarily rendered via centeredIconHandle/centeredLabelHandle in
		// renderSingleColumnLayout. This method returns the height consumed for layout.
		// The centered icon is already positioned there; this just returns height for
		// any additional rendering in a slot list context.
		return slot.size + kLabelFontSize + kSectionGap;
	}

	Foundation::Vec2 EntityInfoView::getCloseButtonPosition(float panelY) const {
		return {panelX + panelWidth - kPadding - kCloseButtonSize, panelY + kPadding};
	}

	Foundation::Vec2 EntityInfoView::getDetailsButtonPosition(float panelY) const {
		// Position to left of close button with a small gap
		return {panelX + panelWidth - kPadding - kCloseButtonSize - kButtonGap - kDetailsButtonSize, panelY + kPadding};
	}

	void EntityInfoView::updateValues(const PanelContent& content) {
		// Tier 3: Value-only update - same entity, just update dynamic slot values
		// Updates progress bars, text slots, and header mood bar
		// Skips all position calculations for significant performance savings

		// Update header mood bar for colonists (NeedBar handles color gradient)
		if (content.layout == PanelLayout::TwoColumn) {
			if (auto* moodBar = getChild<NeedBar>(headerMoodBarHandle)) {
				moodBar->setValue(content.header.moodValue);
			}

			// Update mood label with pre-allocated string
			std::string moodText;
			{
				auto valueStr = std::to_string(static_cast<int>(content.header.moodValue));
				moodText.reserve(valueStr.size() + 2U + content.header.moodLabel.size());
				moodText.append(valueStr);
				moodText.append("% ");
				moodText.append(content.header.moodLabel);
			}
			if (auto* moodLabel = getChild<UI::Text>(headerMoodLabelHandle)) {
				moodLabel->text = std::move(moodText);
			}
		}

		size_t barIndex = 0;
		size_t textIndex = 0;

		// Helper to update slots from a vector
		auto updateSlots = [&](const std::vector<InfoSlot>& slots) {
			for (const auto& slot : slots) {
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
							// Pre-reserve to avoid multiple allocations
							std::string combined;
							combined.reserve(textSlot->label.size() + 2U + textSlot->value.size());
							combined.append(textSlot->label);
							combined.append(": ");
							combined.append(textSlot->value);
							text->text = std::move(combined);
						}
					}
					++textIndex;
				}
			}
		};

		// Update header slots
		updateSlots(content.slots);

		// Update column slots (for two-column layout)
		if (content.layout == PanelLayout::TwoColumn) {
			updateSlots(content.leftColumn);
			updateSlots(content.rightColumn);
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

	bool EntityInfoView::handleEvent(UI::InputEvent& event) {
		if (!visible) {
			return false;
		}

		// Only handle mouse up (click) events for interactive elements
		if (event.type != UI::InputEvent::Type::MouseUp) {
			return false;
		}

		if (event.button != engine::MouseButton::Left) {
			return false;
		}

		auto  pos = event.position;
		float panelY = m_viewportHeight - panelHeight;

		// Check close button
		auto closePos = getCloseButtonPosition(panelY);
		if (pos.x >= closePos.x && pos.x <= closePos.x + kCloseButtonSize && pos.y >= closePos.y &&
			pos.y <= closePos.y + kCloseButtonSize) {
			if (onCloseCallback) {
				onCloseCallback();
			}
			event.consume();
			return true;
		}

		// Check details button (only visible for colonists)
		if (m_model.isColonist()) {
			auto detailsPos = getDetailsButtonPosition(panelY);
			if (pos.x >= detailsPos.x && pos.x <= detailsPos.x + kDetailsButtonSize && pos.y >= detailsPos.y &&
				pos.y <= detailsPos.y + kDetailsButtonSize) {
				if (onDetailsCallback) {
					onDetailsCallback();
				}
				event.consume();
				return true;
			}
		}

		// Check clickable slot
		if (clickableCallback && pos.x >= clickableBoundsMin.x && pos.x <= clickableBoundsMax.x && pos.y >= clickableBoundsMin.y &&
			pos.y <= clickableBoundsMax.y) {
			clickableCallback();
			event.consume();
			return true;
		}

		// Check recipe buttons
		for (size_t i = 0; i < usedRecipeCards; ++i) {
			const auto& bounds = recipeButtonBounds[i];
			if (recipeCallbacks[i] && pos.x >= bounds.x && pos.x <= bounds.x + bounds.width && pos.y >= bounds.y &&
				pos.y <= bounds.y + bounds.height) {
				recipeCallbacks[i]();
				event.consume();
				return true;
			}
		}

		// Check if click is within panel bounds - consume to prevent world click
		if (pos.x >= panelX && pos.x <= panelX + panelWidth && pos.y >= panelY && pos.y <= panelY + panelHeight) {
			event.consume();
			return true;
		}

		return false;
	}

	void EntityInfoView::updateDetailsIcon(bool visible, const Foundation::Vec2& buttonPos) {
		// Icon geometry: "open in new window" symbol
		// Rectangle with missing top-right corner + diagonal arrow pointing out
		constexpr float iconPad = 3.0F;
		float			iconSize = kDetailsButtonSize - 2.0F * iconPad;
		float			ix = buttonPos.x + iconPad;
		float			iy = buttonPos.y + iconPad;

		// Left side of rectangle (top to bottom)
		if (auto* line = getChild<UI::Line>(detailsIconLine1Handle)) {
			line->visible = visible;
			line->start = {ix, iy};
			line->end = {ix, iy + iconSize};
		}

		// Bottom of rectangle (left to right, partial)
		if (auto* line = getChild<UI::Line>(detailsIconLine2Handle)) {
			line->visible = visible;
			line->start = {ix, iy + iconSize};
			line->end = {ix + iconSize * 0.6F, iy + iconSize};
		}

		// Top of rectangle (left to middle, partial)
		if (auto* line = getChild<UI::Line>(detailsIconLine3Handle)) {
			line->visible = visible;
			line->start = {ix, iy};
			line->end = {ix + iconSize * 0.4F, iy};
		}

		// Diagonal arrow (from center to top-right)
		float arrowStartX = ix + iconSize * 0.35F;
		float arrowStartY = iy + iconSize * 0.65F;
		float arrowEndX = ix + iconSize;
		float arrowEndY = iy;

		if (auto* line = getChild<UI::Line>(detailsIconLine4Handle)) {
			line->visible = visible;
			line->start = {arrowStartX, arrowStartY};
			line->end = {arrowEndX, arrowEndY};
		}

		// Arrow head - horizontal part
		if (auto* line = getChild<UI::Line>(detailsIconLine5Handle)) {
			line->visible = visible;
			line->start = {arrowEndX, arrowEndY};
			line->end = {arrowEndX - iconSize * 0.3F, arrowEndY};
		}

		// Arrow head - vertical part
		if (auto* line = getChild<UI::Line>(detailsIconLine6Handle)) {
			line->visible = visible;
			line->start = {arrowEndX, arrowEndY};
			line->end = {arrowEndX, arrowEndY + iconSize * 0.3F};
		}
	}

} // namespace world_sim
