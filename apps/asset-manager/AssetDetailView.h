#pragma once

// The right-hand detail pane: a header (category kicker, name, type badge), then two columns. Left, a
// large preview: one framed thumbnail for a simple asset, or a 2x3 grid of seeded variants under a
// Randomize button for a procedural one (each cell a different generator seed) or an SVG ground def (each
// cell an SVG, Randomize stepping through them), the collider drawn over the first. A ground tile shows
// its first variant tiled 3x3 beside the grid, so seams show. Right, the
// asset's facts, its validation issues, and its source XML in a scrolling well. A ground def also gets a
// Vectors / Bake switch; Bake swaps both columns for its GroundBakeView.
//
// A parts def (rotatable-placed-items.md, section 16) shows its item view at a turn instead: step buttons, a
// degrees entry, and Left / Right (Alt for 1 degree) once the preview has focus; overlay toggles that are the
// legend; a level picker; and a sheet of the item every 15 degrees, a click on one turning the large view to it.
// Its right column lists its counts, every drawing use with what it maps onto, its size in metres and its
// proportion check, and the validation issues by part.

#include "AssetThumbnail.h"
#include "PartsPreview.h"

#include <ui/Component.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Renderer {
	class CoordinateSystem;
}

namespace ui {
	class TextInput;
}

namespace asset_manager {

	class AssetDetailView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			Renderer::CoordinateSystem* coordinateSystem = nullptr; // the app's, for the ground bake's offscreen renders
		};

		explicit AssetDetailView(const Args& args);

		void setAsset(const std::string& defName); // empty shows a placeholder

		/// A parts def's preview turn, as the turn entry and the keys set it.
		void		   setPartsTurn(uint32_t turnQuanta);
		[[nodiscard]] uint32_t partsTurn() const { return partsTurnQuanta; }

		[[nodiscard]] const char* typeName() const override { return "AssetDetailView"; }

	  protected:
		void onMount() override;

	  private:
		void build();
		void preview(ui::Ref<ui::Component> column);
		void info(ui::Ref<ui::Component> column);
		void partsPreview(ui::Ref<ui::Component> column);
		void partsInfo(ui::Ref<ui::Component> column);
		void definitionWell(ui::Ref<ui::Component> column, bool fill);
		void applyOverlays();
		void applyVariantSeeds();
		void randomize(); // advance to the next set of variant seeds

		static constexpr int kVariants = 6;

		std::string									   defName;
		Renderer::CoordinateSystem*					   coordinateSystem;
		bool										   procedural = false;
		bool										   parts = false;
		bool										   ground = false;
		bool										   bakeMode = false; // kept across selections
		uint32_t									   variantBase = 1U;
		ui::Ref<ui::Component>						   content;
		std::array<ui::Ref<AssetThumbnail>, kVariants> variants;
		ui::Ref<AssetThumbnail>						   tiling; // a ground tile's first variant, 3x3

		// A parts def's preview; the turn, the overlays, and the level are kept across selections.
		uint32_t						 partsTurnQuanta = 0;
		PartsOverlays					 partsOverlays;
		std::optional<uint8_t>			 partsLevel;
		ui::Ref<PartsPreview>			 largePreview;
		std::vector<ui::Ref<PartsPreview>> sheetPreviews;
		ui::Ref<ui::TextInput>			 turnEntry;
	};

} // namespace asset_manager
