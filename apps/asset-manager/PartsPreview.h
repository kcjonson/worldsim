#pragma once

// PartsPreview - a parts def's item view at one turn, fit to its box, with the overlays switched on
// (rotatable-placed-items.md, sections 6, 14.4 and 16). Drawn by worldview's item view drawing, the path the
// ui-sandbox's turntable and the placement ghost's overlays take: the use areas and clearance on the ground under
// the item, the footprints, collider, and depth strips over it. The scale fits the item and its areas at every
// turn, so a sheet of turns shares one scale and nothing jumps as the turn changes.

#include <assets/ItemOverlays.h>
#include <assets/ItemView.h>
#include <ui/Component.h>
#include <world/rendering/ItemViewDrawing.h>

#include <cstdint>
#include <optional>
#include <string>

namespace asset_manager {

	/// The overlays the detail pane's toggles switch.
	struct PartsOverlays {
		bool useAreas = true;
		bool clearance = true;
		bool footprints = false;
		bool collider = true;
		bool strips = false;
	};

	class PartsPreview : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string	  defName;
			uint32_t	  turnQuanta = 0;
			PartsOverlays overlays;
			/// A detail level by index; unset is the finest, everything the art has.
			std::optional<uint8_t> level;
			bool				   labels = true; ///< use area names
		};

		explicit PartsPreview(const Args& args);

		void setTurn(uint32_t turnQuanta);
		void setOverlays(const PartsOverlays& overlays);

		[[nodiscard]] uint32_t				  turn() const { return turnQuanta; }
		/// The view drawn last, for the pane's counts.
		[[nodiscard]] const assets::ItemView& view() const { return built; }

		[[nodiscard]] const char* typeName() const override { return "PartsPreview"; }

	  protected:
		void render() override;

	  private:
		/// Builds the view and overlays when the turn, level, or registry changed; false when there is nothing to draw.
		bool refresh(uint8_t level);

		std::string					defName;
		uint32_t					turnQuanta;
		PartsOverlays				overlays;
		std::optional<uint8_t>		fixedLevel;
		bool						labels;

		assets::ItemView	 built;
		assets::ItemOverlays geometry;
		bool				 hasView = false;
		uint64_t			 builtGeneration = 0;
		float				 fitRadiusMeters = 1.0F;
	};

	/// How far from the pivot a parts def's view and its areas reach at any turn, in metres.
	[[nodiscard]] float partsFitRadiusMeters(const std::string& defName);

	/// The level a preview draws: `level` when the def has it, else its finest.
	[[nodiscard]] uint8_t partsPreviewLevel(const assets::AssetDefinition& def, std::optional<uint8_t> level);

} // namespace asset_manager
