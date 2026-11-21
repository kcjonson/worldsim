#pragma once

// What the overlays over a parts item show at a turn (docs/technical/rendering/rotatable-placed-items.md, sections
// 14.4 and 16): its use areas, its clearance with the hatching that marks it, each placed part's footprint, and the
// derived collider, all on the ground, turned with the item. The placement ghost, the asset manager, and the
// ui-sandbox's turntable draw from this; worldview::drawItemOverlays draws it.

#include "assets/AssetDefinition.h"
#include "assets/PartsAsset.h"

#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace assets {

	/// An area turned, with its fill tessellated (nonzero, holes cut out) and the point its name is drawn at.
	struct OverlayArea {
		std::string				  name; ///< empty for an unnamed area
		std::vector<AreaPolygon>  polygons;
		renderer::TessellatedMesh fill; ///< vertices only; the drawer colours them
		glm::vec2				  labelPoint{0.0F};
	};

	/// Every length in metres relative to the pivot, the item's turn applied, on the ground (no height).
	struct ItemOverlays {
		uint32_t				 turnQuanta = 0;
		std::vector<OverlayArea> useAreas;
		OverlayArea				 clearance; ///< no polygons when the item has none
		/// Lines across the clearance, from its edge to its edge, parallel to the screen's diagonal from top left to
		/// bottom right, kHatchSpacingMeters apart: they lie still on the screen as the item turns under them.
		std::vector<std::array<glm::vec2, 2>> clearanceHatch;
		std::vector<std::vector<glm::vec2>>	  footprints; ///< one a placement, its part's footprint
		std::vector<std::vector<glm::vec2>>	  collider;	  ///< the derived collider's rings
	};

	/// The perpendicular distance between two hatch lines.
	inline constexpr float kHatchSpacingMeters = 0.1F;

	/// The overlays of a parts def at a turn; empty for a def that isn't parts.
	[[nodiscard]] ItemOverlays itemOverlays(const AssetDefinition& def, uint32_t turnQuanta);

	/// The pieces of the lines x - y = c, for c every multiple of spacing * sqrt(2), that lie inside `polygons` (outer
	/// rings and holes taken together by the even-odd rule, which for a union's polygons is the nonzero one).
	[[nodiscard]] std::vector<std::array<glm::vec2, 2>> hatchLines(const std::vector<AreaPolygon>& polygons, float spacingMeters);

} // namespace assets
