#pragma once

// RegionMinimapPanel - the fixed-window overview map at the top of the right stack: a compact Panel
// (title "Region", data accent, flush body) over a projected map of a fixed world window centred on the
// crash site, with the camera's viewport, colonist dots, the crash-site marker, a faint grid, the
// landing coordinates, and bearing chevrons at the inset edge for colonists outside the window. Terrain
// and click-to-navigate belong to the Minimap epic; the map is a flat inset surface, and a press on the
// panel goes no further. kWidth wide unless its size says otherwise.

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <theme/Panel.h>

#include <graphics/Rect.h>
#include <math/Types.h>

#include <vector>

namespace world_sim {

	class RegionMinimapPanel : public theme::Panel {
	  public:
		static constexpr float kWidth = 232.0F;

		struct Args {
			UI_COMMON_ARGS
		};

		RegionMinimapPanel();

		// Once: the map window's centre, and the landing coordinates its label shows.
		void setContext(Foundation::Vec2 crashSite, double latDeg, double lonDeg);
		// Each frame: the camera's visible rect and the colonists, in world metres.
		void updateData(const Foundation::Rect& visibleRect, const std::vector<adapters::ColonistData>& colonists);

		[[nodiscard]] const char* typeName() const override { return "RegionMinimapPanel"; }

	  protected:
		void onMount() override;

	  private:
		class Map;

		ui::Ref<Map> map;
	};

} // namespace world_sim
