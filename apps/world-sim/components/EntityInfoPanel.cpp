#include "EntityInfoPanel.h"

#include "SelectionAdapter.h"

#include <input/InputManager.h>

namespace world_sim {

	// ============================================================================
	// CachedSelection implementation
	// ============================================================================

	bool CachedSelection::matches(const Selection& selection) const {
		return std::visit(
			[this](const auto& sel) -> bool {
				using T = std::decay_t<decltype(sel)>;
				if constexpr (std::is_same_v<T, NoSelection>) {
					return type == Type::None;
				} else if constexpr (std::is_same_v<T, ColonistSelection>) {
					return type == Type::Colonist && colonistId == sel.entityId;
				} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
					return type == Type::WorldEntity && worldEntityDef == sel.defName && worldEntityPos.x == sel.position.x &&
						   worldEntityPos.y == sel.position.y;
				}
				return false;
			},
			selection
		);
	}

	void CachedSelection::update(const Selection& selection) {
		std::visit(
			[this](const auto& sel) {
				using T = std::decay_t<decltype(sel)>;
				if constexpr (std::is_same_v<T, NoSelection>) {
					type = Type::None;
					colonistId = ecs::EntityID{0};
					worldEntityDef.clear();
					worldEntityPos = {};
				} else if constexpr (std::is_same_v<T, ColonistSelection>) {
					type = Type::Colonist;
					colonistId = sel.entityId;
					worldEntityDef.clear();
					worldEntityPos = {};
				} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
					type = Type::WorldEntity;
					colonistId = ecs::EntityID{0};
					worldEntityDef = sel.defName;
					worldEntityPos = sel.position;
				}
			},
			selection
		);
	}

	EntityInfoPanel::EntityInfoPanel(const Args& args)
		: panelWidth(args.width),
		  panelX(args.position.x),
		  onCloseCallback(args.onClose),
		  onTaskListToggleCallback(args.onTaskListToggle) {

		contentWidth = panelWidth - (2.0F * kPadding);

		// Estimate max panel height (will resize dynamically based on content)
		panelHeight = 160.0F;

		// Add background panel (semi-transparent dark)
		backgroundHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = args.position,
					.size = {panelWidth, panelHeight},
					.style =
						{.fill = Foundation::Color(0.1F, 0.1F, 0.15F, 0.85F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.3F, 0.3F, 0.4F, 1.0F), .width = 1.0F}},
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
					.style =
						{.fill = Foundation::Color(0.3F, 0.2F, 0.2F, 0.9F),
						 .border = Foundation::BorderStyle{.color = Foundation::Color(0.5F, 0.3F, 0.3F, 1.0F), .width = 1.0F}},
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
							.color = Foundation::Color(0.9F, 0.6F, 0.6F, 1.0F),
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
							.color = Foundation::Color(0.9F, 0.9F, 0.95F, 1.0F),
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
								.color = Foundation::Color(0.7F, 0.7F, 0.75F, 1.0F),
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
							.color = Foundation::Color(0.8F, 0.8F, 0.85F, 1.0F),
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
								.color = Foundation::Color(0.6F, 0.8F, 0.6F, 1.0F),
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
							.color = Foundation::Color(0.5F, 0.7F, 0.9F, 1.0F), // Blue for clickable
							.fontSize = kTextFontSize,
							.hAlign = Foundation::HorizontalAlign::Left,
							.vAlign = Foundation::VerticalAlign::Top,
						},
					.zIndex = 1,
					.id = (args.id + "_clickable").c_str()
				}
			)
		);

		// Disable child sorting to preserve LayerHandle indices
		childrenNeedSorting = false;

		// Start hidden (inherited IComponent::visible defaults to true)
		visible = false;
		hideSlots();
	}

	void EntityInfoPanel::update(const ecs::World& world, const engine::assets::AssetRegistry& registry, const Selection& selection) {
		// Use adapter to convert selection to panel content
		auto content = adaptSelection(selection, world, registry, onTaskListToggleCallback);

		if (!content.has_value()) {
			// Tier 1: Visibility change - hide panel
			if (visible) {
				visible = false;
				m_cachedSelection.update(selection);
				hideSlots();
			}
			return;
		}

		// Handle close button and clickable slot clicks (only when visible)
		if (visible) {
			auto& input = engine::InputManager::Get();
			if (input.isMouseButtonReleased(engine::MouseButton::Left)) {
				auto mousePos = input.getMousePosition();

				// Check if click is within close button bounds
				float panelY = m_viewportHeight - panelHeight;
				auto  closePos = getCloseButtonPosition(panelY);

				if (mousePos.x >= closePos.x && mousePos.x <= closePos.x + kCloseButtonSize && mousePos.y >= closePos.y &&
					mousePos.y <= closePos.y + kCloseButtonSize) {
					if (onCloseCallback) {
						onCloseCallback();
					}
					return;
				}

				// Check if click is within clickable slot bounds
				if (clickableCallback && mousePos.x >= clickableBoundsMin.x && mousePos.x <= clickableBoundsMax.x &&
					mousePos.y >= clickableBoundsMin.y && mousePos.y <= clickableBoundsMax.y) {
					clickableCallback();
					return;
				}
			}
		}

		// Tier 1: Visibility change - show panel if hidden
		if (!visible) {
			visible = true;
		}

		// Check if selection identity changed
		if (m_cachedSelection.matches(selection)) {
			// Tier 3: Value-only update - same entity, just update dynamic values
			updateValues(content.value());
		} else {
			// Tier 2: Structure change - different entity, full relayout
			m_cachedSelection.update(selection);
			renderContent(content.value());
		}
	}

	void EntityInfoPanel::renderContent(const PanelContent& content) {
		// Reset slot usage counters
		usedTextSlots = 0;
		usedProgressBars = 0;
		usedListItems = 0;

		// Clear clickable slot state (will be set if content has ClickableTextSlot)
		clickableCallback = nullptr;
		clickableBoundsMin = {};
		clickableBoundsMax = {};

		// Hide all pool elements first (will show ones we use)
		hideSlots();

		// First pass: compute content height to determine panel position
		// (panel bottom aligns with viewport bottom)
		float contentHeight = kPadding + kTitleFontSize + kLineSpacing * 2.0F;
		for (const auto& slot : content.slots) {
			contentHeight += std::visit(
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
					}
					return 0.0F;
				},
				slot
			);
		}
		contentHeight += kPadding; // bottom padding

		panelHeight = contentHeight;
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

		// Render slots (each slot renderer sets visible=true on used elements)
		float yOffset = panelY + kPadding + kTitleFontSize + kLineSpacing * 2.0F;

		for (const auto& slot : content.slots) {
			yOffset += renderSlot(slot, yOffset);
		}
	}

	void EntityInfoPanel::hideSlots() {
		// Hide all children via inherited Component::children vector
		// This is O(n) but n is small (~30 elements) and avoids handle lookups
		for (auto* child : children) {
			child->visible = false;
		}
	}

	float EntityInfoPanel::renderSlot(const InfoSlot& slot, float yOffset) {
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
				}
				return 0.0F;
			},
			slot
		);
	}

	float EntityInfoPanel::renderTextSlot(const TextSlot& slot, float yOffset) {
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

	float EntityInfoPanel::renderProgressBarSlot(const ProgressBarSlot& slot, float yOffset) {
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

	float EntityInfoPanel::renderTextListSlot(const TextListSlot& slot, float yOffset) {
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

	float EntityInfoPanel::renderSpacerSlot(const SpacerSlot& slot, float /*yOffset*/) {
		return slot.height;
	}

	float EntityInfoPanel::renderClickableTextSlot(const ClickableTextSlot& slot, float yOffset) {
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

	Foundation::Vec2 EntityInfoPanel::getCloseButtonPosition(float panelY) const {
		return {panelX + panelWidth - kPadding - kCloseButtonSize, panelY + kPadding};
	}

	void EntityInfoPanel::updateValues(const PanelContent& content) {
		// Tier 3: Value-only update - same entity, just update dynamic slot values
		// Only updates progress bars since those are the main dynamic elements
		// Skips all position calculations for significant performance savings

		size_t barIndex = 0;
		for (const auto& slot : content.slots) {
			if (const auto* barSlot = std::get_if<ProgressBarSlot>(&slot)) {
				if (barIndex < progressBarHandles.size()) {
					if (auto* bar = getChild<NeedBar>(progressBarHandles[barIndex])) {
						bar->setValue(barSlot->value);
					}
				}
				++barIndex;
			}
		}
	}

	void EntityInfoPanel::setBottomLeftPosition(float x, float viewportHeight) {
		if (panelX == x && m_viewportHeight == viewportHeight) {
			return; // No change
		}

		panelX = x;
		m_viewportHeight = viewportHeight;

		// Force structure re-render on next update if currently visible
		// This ensures all child elements get repositioned correctly
		if (visible) {
			m_cachedSelection.type = CachedSelection::Type::None;
		}
	}

} // namespace world_sim
