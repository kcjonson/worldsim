#include "ColonistListView.h"

#include <primitives/Primitives.h>
#include <theme/PanelStyle.h>
#include <theme/Theme.h>

namespace world_sim {

ColonistListView::ColonistListView(const Args& args)
	: panelWidth(args.width),
	  itemHeight(args.itemHeight),
	  onSelectCallback(args.onColonistSelected) {

	itemHandles.reserve(kMaxColonists);
}

void ColonistListView::setPosition(float x, float y) {
	panelX = x;
	panelY = y;
}

void ColonistListView::update(ColonistListModel& model, ecs::World& world) {
	// Update model and check if data changed
	bool dataChanged = model.refresh(world);

	// Get current selection from model
	ecs::EntityID newSelectedId = model.selectedId();
	bool selectionChanged = (newSelectedId != selectedId);
	selectedId = newSelectedId;

	if (dataChanged) {
		// Full rebuild required
		rebuildUI(model.colonists());
	} else if (selectionChanged) {
		// Just update selection highlighting (cheap)
		updateSelectionHighlight(selectedId);
	}

	// Always update mood bars (values may change even if structure doesn't)
	updateMoodBars(model.colonists());
}

void ColonistListView::rebuildUI(const std::vector<adapters::ColonistData>& colonists) {
	// Clear existing items
	itemHandles.clear();

	// Content width for items
	float contentWidth = panelWidth - kPadding * 2;

	// Create layout container for items
	itemLayout = std::make_unique<UI::LayoutContainer>(UI::LayoutContainer::Args{
		.position = {panelX + kPadding, panelY + kPadding},
		.size = {contentWidth, 0.0F},  // Height determined by children
		.direction = UI::Direction::Vertical,
		.hAlign = UI::HAlign::Left
	});

	// Add colonist items
	for (size_t i = 0; i < colonists.size() && i < kMaxColonists; ++i) {
		const auto& colonist = colonists[i];

		auto handle = itemLayout->addChild(ColonistListItem(ColonistListItem::Args{
			.colonist = colonist,
			.width = contentWidth,
			.height = itemHeight - kItemSpacing,
			.isSelected = (colonist.id == selectedId),
			.itemMargin = kItemSpacing * 0.5F,
			.onSelect = onSelectCallback,
			.id = "colonist_" + std::to_string(i)
		}));

		itemHandles.push_back(handle);
	}

	// Calculate panel height from layout
	float itemsHeight = static_cast<float>(colonists.size()) * itemHeight;
	float panelHeight = kPadding * 2 + itemsHeight;

	// Create/update background panel
	if (!backgroundRect) {
		backgroundRect = std::make_unique<UI::Rectangle>();
	}
	backgroundRect->position = {panelX, panelY};
	backgroundRect->size = {panelWidth, panelHeight};
	backgroundRect->style = UI::PanelStyles::floating();
	backgroundRect->zIndex = -1;
}

void ColonistListView::updateSelectionHighlight(ecs::EntityID newSelectedId) {
	if (!itemLayout) {
		return;
	}

	for (auto& handle : itemHandles) {
		if (auto* item = itemLayout->getChild<ColonistListItem>(handle)) {
			item->setSelected(item->getEntityId() == newSelectedId);
		}
	}
}

void ColonistListView::updateMoodBars(const std::vector<adapters::ColonistData>& colonists) {
	if (!itemLayout) {
		return;
	}

	for (size_t i = 0; i < colonists.size() && i < itemHandles.size(); ++i) {
		if (auto* item = itemLayout->getChild<ColonistListItem>(itemHandles[i])) {
			item->setMood(colonists[i].mood);
		}
	}
}

bool ColonistListView::handleEvent(UI::InputEvent& event) {
	// Dispatch to layout container first
	if (itemLayout && itemLayout->handleEvent(event)) {
		return true;
	}

	// Consume clicks within the panel bounds to prevent click-through
	if (event.type == UI::InputEvent::Type::MouseDown ||
		event.type == UI::InputEvent::Type::MouseUp) {
		Foundation::Rect bounds = getBounds();
		auto pos = event.position;
		if (pos.x >= bounds.x && pos.x <= bounds.x + bounds.width &&
			pos.y >= bounds.y && pos.y <= bounds.y + bounds.height) {
			event.consume();
			return true;
		}
	}

	return false;
}

void ColonistListView::render() {
	if (!itemLayout || itemHandles.empty()) {
		return;
	}

	// Render background first
	if (backgroundRect) {
		backgroundRect->render();
	}

	// Render items via layout container
	if (itemLayout) {
		itemLayout->render();
	}
}

Foundation::Rect ColonistListView::getBounds() const {
	float itemsHeight = static_cast<float>(itemHandles.size()) * itemHeight;
	float panelHeight = kPadding * 2 + itemsHeight;
	return {panelX, panelY, panelWidth, panelHeight};
}

} // namespace world_sim
