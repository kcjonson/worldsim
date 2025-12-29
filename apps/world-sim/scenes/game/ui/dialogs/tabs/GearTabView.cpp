#include "GearTabView.h"
#include "TabStyles.h"

#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <sstream>

namespace world_sim {

void GearTabView::create(const Foundation::Rect& contentBounds) {
	using namespace tabs;

	auto layout = UI::LayoutContainer(UI::LayoutContainer::Args{
		.position = {contentBounds.x, contentBounds.y},
		.size = {contentBounds.width, contentBounds.height},
		.direction = UI::Direction::Vertical,
		.id = "gear_content"
	});

	// Attire section
	layout.addChild(UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Attire",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 4.0F
	}));

	// Attire slots
	const char* slots[] = {"Head", "Chest", "Legs", "Feet", "Hands"};
	for (const char* slot : slots) {
		layout.addChild(UI::Text(UI::Text::Args{
			.height = kBodySize,
			.text = std::string(slot) + ": (empty)",
			.style = {.color = mutedColor(), .fontSize = kBodySize},
			.margin = 1.0F
		}));
	}

	// Inventory section - store handle for dynamic updates
	auto inventoryHeader = UI::Text(UI::Text::Args{
		.height = kLabelSize,
		.text = "Inventory: 0/0 slots",
		.style = {.color = labelColor(), .fontSize = kLabelSize},
		.margin = 6.0F
	});
	inventoryHeaderHandle = layout.addChild(std::move(inventoryHeader));

	// Empty state / items list - store handle for dynamic updates
	auto itemsText = UI::Text(UI::Text::Args{
		.height = kBodySize,
		.text = "Empty",
		.style = {.color = mutedColor(), .fontSize = kBodySize},
		.margin = 2.0F
	});
	itemsTextHandle = layout.addChild(std::move(itemsText));

	layoutHandle = addChild(std::move(layout));
}

void GearTabView::update(const GearData& gear) {
	auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
	if (layout == nullptr) return;

	// Update inventory header using stored handle
	if (auto* text = layout->getChild<UI::Text>(inventoryHeaderHandle)) {
		std::ostringstream ss;
		ss << "Inventory: " << gear.slotCount << "/" << gear.maxSlots << " slots";
		text->text = ss.str();
	}

	// Update items text using stored handle
	if (auto* text = layout->getChild<UI::Text>(itemsTextHandle)) {
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

} // namespace world_sim
