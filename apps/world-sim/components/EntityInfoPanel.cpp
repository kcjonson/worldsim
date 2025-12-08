#include "EntityInfoPanel.h"

#include "SelectionAdapter.h"

#include <input/InputManager.h>

namespace world_sim {

	EntityInfoPanel::EntityInfoPanel(const Args& args)
		: panelWidth(args.width),
		  panelPosition(args.position),
		  onCloseCallback(args.onClose) {

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
		float closeX = args.position.x + panelWidth - kPadding - kCloseButtonSize;
		float closeY = args.position.y + kPadding;
		closeButtonBgHandle = addChild(
			UI::Rectangle(
				UI::Rectangle::Args{
					.position = {closeX, closeY},
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
					.position = {closeX + kCloseButtonSize * 0.5F, closeY + kCloseButtonSize * 0.5F - 1.0F},
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

		// Create text slot pool
		textHandles.reserve(kMaxTextSlots);
		for (size_t i = 0; i < kMaxTextSlots; ++i) {
			textHandles.push_back(addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding, kHiddenY},
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

		// Create progress bar pool (using NeedBar component)
		progressBarHandles.reserve(kMaxProgressBars);
		for (size_t i = 0; i < kMaxProgressBars; ++i) {
			progressBarHandles.push_back(addChild(NeedBar(
				NeedBar::Args{
					.position = {args.position.x + kPadding, kHiddenY},
					.width = contentWidth,
					.height = kProgressBarHeight,
					.label = "",
					.id = args.id + "_bar_" + std::to_string(i)
				}
			)));
		}

		// Create list header
		listHeaderHandle = addChild(
			UI::Text(
				UI::Text::Args{
					.position = {args.position.x + kPadding, kHiddenY},
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

		// Create list item pool
		listItemHandles.reserve(kMaxListItems);
		for (size_t i = 0; i < kMaxListItems; ++i) {
			listItemHandles.push_back(addChild(
				UI::Text(
					UI::Text::Args{
						.position = {args.position.x + kPadding + 8.0F, kHiddenY},
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

		// Disable child sorting to preserve LayerHandle indices
		childrenNeedSorting = false;

		// Start hidden
		clearSlots();
	}

	void EntityInfoPanel::update(const ecs::World& world, const engine::assets::AssetRegistry& registry, const Selection& selection) {
		// Use adapter to convert selection to panel content
		auto content = adaptSelection(selection, world, registry);

		if (!content.has_value()) {
			// No selection - hide panel
			visible = false;
			clearSlots();
			return;
		}

		// Handle close button click (only when visible)
		if (visible) {
			auto& input = engine::InputManager::Get();
			if (input.isMouseButtonReleased(engine::MouseButton::Left)) {
				auto mousePos = input.getMousePosition();

				// Check if click is within close button bounds
				float closeX = panelPosition.x + panelWidth - kPadding - kCloseButtonSize;
				float closeY = panelPosition.y + kPadding;

				if (mousePos.x >= closeX && mousePos.x <= closeX + kCloseButtonSize && mousePos.y >= closeY &&
					mousePos.y <= closeY + kCloseButtonSize) {
					if (onCloseCallback) {
						onCloseCallback();
					}
					return;
				}
			}
		}

		// Show panel and render content
		visible = true;
		renderContent(content.value());
	}

	void EntityInfoPanel::renderContent(const PanelContent& content) {
		// Reset slot usage counters
		usedTextSlots = 0;
		usedProgressBars = 0;
		usedListItems = 0;

		// Clear all slots first
		clearSlots();

		// Show background at proper position
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->position.y = panelPosition.y;
		}

		// Show close button
		float closeX = panelPosition.x + panelWidth - kPadding - kCloseButtonSize;
		float closeY = panelPosition.y + kPadding;
		if (auto* closeBg = getChild<UI::Rectangle>(closeButtonBgHandle)) {
			closeBg->position = {closeX, closeY};
		}
		if (auto* closeText = getChild<UI::Text>(closeButtonTextHandle)) {
			closeText->position = {closeX + kCloseButtonSize * 0.5F, closeY + kCloseButtonSize * 0.5F - 1.0F};
		}

		// Show title
		if (auto* title = getChild<UI::Text>(titleHandle)) {
			title->position = {panelPosition.x + kPadding, panelPosition.y + kPadding};
			title->text = content.title;
		}

		// Render slots
		float yOffset = panelPosition.y + kPadding + kTitleFontSize + kLineSpacing * 2.0F;

		for (const auto& slot : content.slots) {
			yOffset += renderSlot(slot, yOffset);
		}

		// Update panel height based on content
		float newHeight = yOffset - panelPosition.y + kPadding;
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->size.y = newHeight;
		}
		panelHeight = newHeight;
	}

	void EntityInfoPanel::clearSlots() {
		// Hide background
		if (auto* bg = getChild<UI::Rectangle>(backgroundHandle)) {
			bg->position.y = kHiddenY;
		}

		// Hide close button
		if (auto* closeBg = getChild<UI::Rectangle>(closeButtonBgHandle)) {
			closeBg->position.y = kHiddenY;
		}
		if (auto* closeText = getChild<UI::Text>(closeButtonTextHandle)) {
			closeText->position.y = kHiddenY;
		}

		// Hide title
		if (auto* title = getChild<UI::Text>(titleHandle)) {
			title->position.y = kHiddenY;
		}

		// Hide all text slots
		for (auto& handle : textHandles) {
			if (auto* text = getChild<UI::Text>(handle)) {
				text->position.y = kHiddenY;
			}
		}

		// Hide all progress bars
		for (auto& handle : progressBarHandles) {
			if (auto* bar = getChild<NeedBar>(handle)) {
				bar->setPosition({panelPosition.x + kPadding, kHiddenY});
			}
		}

		// Hide list header
		if (auto* header = getChild<UI::Text>(listHeaderHandle)) {
			header->position.y = kHiddenY;
		}

		// Hide all list items
		for (auto& handle : listItemHandles) {
			if (auto* item = getChild<UI::Text>(handle)) {
				item->position.y = kHiddenY;
			}
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
			text->position = {panelPosition.x + kPadding, yOffset};
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
			bar->setPosition({panelPosition.x + kPadding, yOffset});
			bar->setValue(slot.value);

			// Update label via the label child
			// Note: NeedBar doesn't expose label updating, but we can work around
			// by setting the initial label. For dynamic labels we'd need to extend NeedBar.
			// For now, the adapter provides the correct label at construction.
		}

		++usedProgressBars;
		return kProgressBarHeight + kLineSpacing;
	}

	float EntityInfoPanel::renderTextListSlot(const TextListSlot& slot, float yOffset) {
		float height = 0.0F;

		// Render header
		if (auto* header = getChild<UI::Text>(listHeaderHandle)) {
			header->position = {panelPosition.x + kPadding, yOffset};
			header->text = slot.header + ":";
		}
		height += kTextFontSize + 2.0F;

		// Render items
		for (size_t i = 0; i < slot.items.size() && usedListItems < listItemHandles.size(); ++i) {
			if (auto* item = getChild<UI::Text>(listItemHandles[usedListItems])) {
				item->position = {panelPosition.x + kPadding + 8.0F, yOffset + height};
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

} // namespace world_sim
