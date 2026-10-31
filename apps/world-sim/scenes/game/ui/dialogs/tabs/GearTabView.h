#pragma once

// GearTabView - the dossier's Gear tab, after the prototype's Gear panel: a three-column paperdoll (worn
// slots, the body silhouette, back, belt and hand slots), the Field Pack (a cargo meter over its stacks),
// the Tool Belt's two quick-draw slots, and the total carry load. Built once on mount; update() sets the
// kept children.

#include <ui/Component.h>
#include <ui/components/ProgressBar.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace world_sim {

	// Leaf components, in GearTabView.cpp.
	class GearSlotBox;
	class GearTextRow;
	class GearStackRow;
	class GearBeltCell;

	// One gear entry: its label, quantity and mass.
	struct GearItem {
		std::string name; // asset label (defName fallback)
		uint32_t	quantity = 0;
		float		totalKg = 0.0F;
	};

	struct GearData {
		// Hands. A two-hand item mirrors across both hands in the Inventory; the model collapses that
		// mirror into bothHands and leftHand, counted once.
		std::optional<GearItem> leftHand;
		std::optional<GearItem> rightHand;
		bool					bothHands = false;

		// Belt quick-draw tool slots.
		std::array<std::optional<GearItem>, 2> belt;

		// Backpack stacks, one per material.
		std::vector<GearItem> items;
		uint32_t			  slotCount = 0;
		uint32_t			  maxSlots = 0;

		// Cargo mass (tools excluded) against the colonist's strength-derived capacity.
		float cargoKg = 0.0F;
		float capacityKg = 0.0F;

		// Everything carried, tools included: hands, belt and pack.
		float totalKg = 0.0F;
		float beltKg = 0.0F;
	};

	class GearTabView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
		};

		void update(const GearData& data);

		[[nodiscard]] const char* typeName() const override { return "GearTabView"; }

	  protected:
		void onMount() override;

	  private:
		static constexpr size_t kPackRowPool = 7; // stack rows shown before "+N more"

		ui::Ref<GearSlotBox> bothHandsBox;
		ui::Ref<GearSlotBox> leftHandBox;
		ui::Ref<GearSlotBox> rightHandBox;

		ui::Ref<GearTextRow>							packHeader;
		ui::Ref<ui::ProgressBar>						packMeter;
		std::array<ui::Ref<GearStackRow>, kPackRowPool> packRows;
		ui::Ref<GearTextRow>							packMoreRow;
		ui::Ref<GearTextRow>							packEmptyRow;

		ui::Ref<GearTextRow>				 beltHeader;
		std::array<ui::Ref<GearBeltCell>, 2> beltCells;

		ui::Ref<ui::ProgressBar> totalMeter;
	};

} // namespace world_sim
