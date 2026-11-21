#pragma once

// What the overlays over a parts item show at a turn (docs/technical/rendering/rotatable-placed-items.md, sections
// 14.4 and 16): its use areas, its clearance with the hatching that marks it, each placed part's footprint, and the
// derived collider, all on the ground, turned with the item. The placement ghost, the asset manager, and the
// ui-sandbox's turntable draw from this; worldview::drawItemOverlays draws it.

#include "assets/AssetDefinition.h"
#include "assets/PartsAsset.h"

#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

	/// An area turned, with its fill tessellated (nonzero, holes cut out) and the point its name is drawn at.
	struct OverlayArea {
		std::string				  name; ///< empty for an unnamed area
		std::vector<AreaPolygon>  polygons;
		renderer::TessellatedMesh fill; ///< vertices only; the drawer colours them
		/// Strictly inside the largest polygon (pointInside).
		glm::vec2 labelPoint{0.0F};
	};

	/// One hatch line's piece inside an area, on the line x - y = index * step.
	struct HatchLine {
		glm::vec2 from{0.0F};
		glm::vec2 to{0.0F};
		/// Which line of the family it lies on; a drawer thins the hatching by drawing every Nth index.
		int64_t index = 0;
		/// The line nearest its polygon's middle, drawn however thin the hatching, so no polygon loses all of it.
		bool middle = false;
	};

	/// Every length in metres relative to the pivot, the item's turn applied, on the ground (no height).
	struct ItemOverlays {
		uint32_t				 turnQuanta = 0;
		std::vector<OverlayArea> useAreas;
		OverlayArea				 clearance; ///< no polygons when the item has none
		/// Lines across the clearance, from its edge to its edge, parallel to the screen's diagonal from top left to
		/// bottom right: they lie still on the screen as the item turns under them.
		std::vector<HatchLine>				clearanceHatch;
		float								hatchSpacingMeters = 0.0F;
		std::vector<std::vector<glm::vec2>> footprints; ///< one a placement, its part's footprint
		std::vector<std::vector<glm::vec2>> collider;	///< the derived collider's rings
	};

	/// The most hatch lines one polygon gets; a wider polygon has them spread further apart than the spacing asks.
	/// Ten thousand lines is a polygon a kilometre across at 0.1 m, far past any item, and still a cheap draw.
	inline constexpr int64_t kMaxHatchLinesPerPolygon = 10000;

	/// The overlays of a parts def at a turn, the clearance hatched every hatchSpacingMeters; empty for a def that isn't
	/// parts.
	[[nodiscard]] ItemOverlays itemOverlays(const AssetDefinition& def, uint32_t turnQuanta, float hatchSpacingMeters);

	/// The pieces of the lines x - y = c, for c every multiple of spacing * sqrt(2), that lie inside each polygon (its
	/// outer ring and holes by the even-odd rule). A polygon too thin to meet one of those lines gets the line through
	/// its middle, so every polygon shows hatched.
	[[nodiscard]] std::vector<HatchLine> hatchLines(const std::vector<AreaPolygon>& polygons, float spacingMeters);

	/// A point strictly inside a polygon with holes: the middle of the widest span across it on a few horizontal lines
	/// about the middle of its height, none through a vertex.
	[[nodiscard]] glm::vec2 pointInside(const AreaPolygon& polygon);

} // namespace assets
