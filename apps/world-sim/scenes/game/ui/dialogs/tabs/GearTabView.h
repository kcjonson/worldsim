#pragma once

#include <component/Container.h>
#include <graphics/Rect.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace UI {
class LayoutContainer;
}

namespace world_sim {

// Leaf widgets defined in GearTabView.cpp
struct GearSlotBox;
struct GearTextRow;
struct GearMeterRow;
struct GearStackRow;
struct GearBeltCell;

/// One resolved gear entry: display label + quantity + mass, ready to render.
struct GearItem {
	std::string name; // asset label (defName fallback)
	uint32_t	quantity = 0;
	float		totalKg = 0.0F;
};

/// Data for Gear tab
struct GearData {
	// Hands. A two-hand item mirrors across both hands in the Inventory; the model
	// collapses that mirror into bothHands + leftHand (counted once).
	std::optional<GearItem> leftHand;
	std::optional<GearItem> rightHand;
	bool					bothHands = false;

	// Belt quick-draw tool slots
	std::array<std::optional<GearItem>, 2> belt;

	// Backpack stacks, aggregated per material
	std::vector<GearItem> items;
	uint32_t			  slotCount = 0;
	uint32_t			  maxSlots = 0;

	// Cargo mass (tools excluded) vs the colonist's strength-derived capacity
	float cargoKg = 0.0F;
	float capacityKg = 0.0F;

	// Everything carried including tools: hands + belt + pack
	float totalKg = 0.0F;
	float beltKg = 0.0F;
};

/// Gear tab content for ColonistDetailsDialog.
///
/// Mirrors the prototype's Gear panel: a 3-column paperdoll (worn-slot stubs,
/// body silhouette, back/belt/hand slots), then a Field Pack section (cargo
/// meter + stack rows), a Tool Belt slot grid, and a total carry-load meter.
/// Built once on create() as a LayoutContainer tree; update() mutates pooled
/// leaf widgets (no per-frame child churn).
class GearTabView : public UI::Container {
  public:
	/// Create the tab view with content bounds from parent dialog
	void create(const Foundation::Rect& contentBounds);

	/// Update content from model data
	void update(const GearData& data);

  private:
	static constexpr size_t kPackRowPool = 7; // stack rows shown before "+N more"

	// Containers whose measured size can change on update (visibility, text)
	UI::LayoutContainer* root = nullptr;
	UI::LayoutContainer* paperdollRow = nullptr;
	UI::LayoutContainer* rightCol = nullptr;
	UI::LayoutContainer* packSection = nullptr;
	UI::LayoutContainer* packList = nullptr;

	// Right-column slots (hands are real; back/belt are stubs for now)
	GearSlotBox* bothHandsBox = nullptr;
	GearSlotBox* leftHandBox = nullptr;
	GearSlotBox* rightHandBox = nullptr;

	// Field Pack
	GearTextRow*						  packHeader = nullptr;
	GearMeterRow*						  packMeter = nullptr;
	std::array<GearStackRow*, kPackRowPool> packRows{};
	GearTextRow*						  packMoreRow = nullptr;
	GearTextRow*						  packEmptyRow = nullptr;

	// Tool Belt
	GearTextRow*				 beltHeader = nullptr;
	std::array<GearBeltCell*, 2> beltCells{};

	// Total carry load
	GearMeterRow* totalMeter = nullptr;
};

} // namespace world_sim
