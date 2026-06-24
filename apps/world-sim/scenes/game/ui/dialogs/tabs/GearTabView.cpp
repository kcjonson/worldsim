#include "GearTabView.h"
#include "TabStyles.h"

#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <sstream>

namespace world_sim {

	void GearTabView::create(const Foundation::Rect& contentBounds) {
		using namespace tabs;

		auto layout = UI::LayoutContainer(
			UI::LayoutContainer::Args{
				.position = {contentBounds.x, contentBounds.y},
				.size = {contentBounds.width, contentBounds.height},
				.direction = UI::Direction::Vertical,
				.id = "gear_content"
			}
		);

		// Attire section
		layout.addChild(
			UI::Text(
				UI::Text::Args{
					.height = kLabelSize, .text = "Attire", .style = {.color = labelColor(), .fontSize = kLabelSize}, .margin = 4.0F
				}
			)
		);

		// Attire slots
		const char* slots[] = {"Head", "Chest", "Legs", "Feet", "Hands"};
		for (const char* slot : slots) {
			layout.addChild(
				UI::Text(
					UI::Text::Args{
						.height = kBodySize,
						.text = std::string(slot) + ": (empty)",
						.style = {.color = mutedColor(), .fontSize = kBodySize},
						.margin = 1.0F
					}
				)
			);
		}

		// Holding section - what's in colonist's hands
		layout.addChild(
			UI::Text(
				UI::Text::Args{
					.height = kLabelSize, .text = "Holding", .style = {.color = labelColor(), .fontSize = kLabelSize}, .margin = 6.0F
				}
			)
		);

		auto handsText = UI::Text(
			UI::Text::Args{.height = kBodySize, .text = "(empty)", .style = {.color = mutedColor(), .fontSize = kBodySize}, .margin = 2.0F}
		);
		handsTextHandle = layout.addChild(std::move(handsText));

		// Belt section - quick-draw tool slots
		layout.addChild(
			UI::Text(
				UI::Text::Args{
					.height = kLabelSize, .text = "Belt", .style = {.color = labelColor(), .fontSize = kLabelSize}, .margin = 6.0F
				}
			)
		);
		auto beltText = UI::Text(
			UI::Text::Args{.height = kBodySize, .text = "(empty)", .style = {.color = mutedColor(), .fontSize = kBodySize}, .margin = 2.0F}
		);
		beltTextHandle = layout.addChild(std::move(beltText));

		// Inventory section - store handle for dynamic updates
		auto inventoryHeader = UI::Text(
			UI::Text::Args{
				.height = kLabelSize,
				.text = "Inventory: 0/0 slots",
				.style = {.color = labelColor(), .fontSize = kLabelSize},
				.margin = 6.0F
			}
		);
		inventoryHeaderHandle = layout.addChild(std::move(inventoryHeader));

		// Carry-weight readout (cargo only; held/belted tools don't count)
		auto carryText = UI::Text(
			UI::Text::Args{.height = kBodySize, .text = "Carry: 0 / 0 kg", .style = {.color = mutedColor(), .fontSize = kBodySize}, .margin = 2.0F}
		);
		carryTextHandle = layout.addChild(std::move(carryText));

		// Empty state / items list - store handle for dynamic updates
		auto itemsText = UI::Text(
			UI::Text::Args{.height = kBodySize, .text = "Empty", .style = {.color = mutedColor(), .fontSize = kBodySize}, .margin = 2.0F}
		);
		itemsTextHandle = layout.addChild(std::move(itemsText));

		layoutHandle = addChild(std::move(layout));
	}

	void GearTabView::update(const GearData& gear) {
		using namespace tabs;

		auto* layout = getChild<UI::LayoutContainer>(layoutHandle);
		if (layout == nullptr)
			return;

		// Update hands display
		if (auto* text = layout->getChild<UI::Text>(handsTextHandle)) {
			bool hasLeft = gear.leftHand.has_value();
			bool hasRight = gear.rightHand.has_value();

			if (!hasLeft && !hasRight) {
				text->text = "(empty)";
				text->style.color = mutedColor();
			} else if (hasLeft && hasRight && gear.leftHand->defName == gear.rightHand->defName) {
				// Same item in both hands: a two-hand armful (counted once)
				text->text = gear.leftHand->defName + " x" + std::to_string(gear.leftHand->quantity) + " (both hands)";
				text->style.color = bodyColor();
			} else {
				std::ostringstream ss;
				ss << "L: " << (hasLeft ? gear.leftHand->defName : "(empty)");
				ss << "  R: " << (hasRight ? gear.rightHand->defName : "(empty)");
				text->text = ss.str();
				text->style.color = bodyColor();
			}
		}

		// Update belt display
		if (auto* text = layout->getChild<UI::Text>(beltTextHandle)) {
			std::ostringstream ss;
			bool				any = false;
			for (const auto& slot : gear.belt) {
				if (slot.has_value()) {
					if (any) {
						ss << ", ";
					}
					ss << slot->defName;
					any = true;
				}
			}
			text->text = any ? ss.str() : "(empty)";
			text->style.color = any ? bodyColor() : mutedColor();
		}

		// Update inventory header using stored handle
		if (auto* text = layout->getChild<UI::Text>(inventoryHeaderHandle)) {
			std::ostringstream ss;
			ss << "Inventory: " << gear.slotCount << "/" << gear.maxSlots << " slots";
			text->text = ss.str();
		}

		// Update carry-weight readout
		if (auto* text = layout->getChild<UI::Text>(carryTextHandle)) {
			std::ostringstream ss;
			ss << "Carry: " << static_cast<int>(gear.carriedKg + 0.5F) << " / " << static_cast<int>(gear.capacityKg + 0.5F) << " kg";
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
