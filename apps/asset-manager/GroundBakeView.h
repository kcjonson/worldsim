#pragma once

// The Bake view of a ground def (docs/technical/ground-textures.md, "Baker"): the def baked by the game's
// worldview::GroundTileBaker, then the selected variant's mips 0 to 6 side by side at their texel size, a
// zoom strip of what the game shows at 160 down to 3 px/m (each a 2x2 of four variants, sampled the way
// the runtime does: one LOD a draw with its +0.25 bias, trilinear between the two mips around it), and
// the bake's metrics and timings. Its update, which runs outside the frame's batch, generates every
// variant's paths at once on the pool in one frame and then renders one variant a frame, so the window
// stays live. Bakes are cached in memory by def until Reload clears them, and on disk beside the exe
// (GroundTileCache) under a key of the def's files, so editing its XML or script and pressing Reload
// bakes it again and anything else comes back from disk.

#include "TileImageStrip.h"

#include <ui/Component.h>
#include <world/rendering/GroundTileBaker.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Renderer {
	class CoordinateSystem;
}

namespace ui {
	class Text;
}

namespace asset_manager {

	class GroundBakeView : public ui::Component {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string					defName;
			Renderer::CoordinateSystem* coordinateSystem = nullptr; // the app's, put back after each offscreen render
		};

		explicit GroundBakeView(const Args& args);

		// Drop every cached bake (after an asset reload, so the next view bakes again).
		static void clearCache();

		[[nodiscard]] const char* typeName() const override { return "GroundBakeView"; }

	  protected:
		void onMount() override;
		void update(float dt) override;

	  private:
		[[nodiscard]] worldview::GroundTileBaker baker() const;
		void									 showBake();
		void									 showVariant();
		void									 showZoomStrip();
		void									 showReport();

		std::string									 defName;
		Renderer::CoordinateSystem*					 coordinateSystem;
		std::shared_ptr<worldview::GroundTileBake>	 bake;	// the cached one when done, else the one being built
		std::vector<worldview::GroundVariantPaths>	 paths; // every variant's, while their renders are pending
		uint32_t									 variantCount = 0;
		uint32_t									 shownVariant = 0;
		bool										 baking = false;
		ui::Ref<ui::Text>							 status;
		ui::Ref<TileImageStrip>						 mips;
		ui::Ref<TileImageStrip>						 zoom;
		ui::Ref<ui::Text>							 report;
	};

} // namespace asset_manager
