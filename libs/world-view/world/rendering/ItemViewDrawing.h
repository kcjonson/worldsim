#pragma once

// Drawing a parts item's view and its overlays on the CPU batch (docs/technical/rendering/rotatable-placed-items.md,
// sections 6, 14.4 and 16): the one drawing path the asset manager's preview, the ui-sandbox's turntable, and the
// placement ghost share. Geometry comes from libs/assets (assets::buildItemMesh, assets::itemOverlays) and the look
// from assets/config/construction/rendering.xml (assets::ItemOverlayStyle); this maps pivot-relative metres into
// whatever space the caller draws in.

#include <assets/ConstructionRegistry.h>
#include <assets/ItemOverlays.h>
#include <assets/ItemView.h>

#include <graphics/Color.h>
#include <math/Types.h>
#include <vector/Types.h>

#include <glm/vec2.hpp>

#include <cstdint>

namespace worldview {

	/// Where pivot-relative metres land in the caller's drawing space: pivot + point * pixelsPerMeter.
	struct ItemScreenFrame {
		Foundation::Vec2 pivot{0.0F, 0.0F};
		float			 pixelsPerMeter = 1.0F;

		[[nodiscard]] Foundation::Vec2 toScreen(glm::vec2 point) const {
			return {pivot.x + point.x * pixelsPerMeter, pivot.y + point.y * pixelsPerMeter};
		}
	};

	/// Which overlays draw.
	struct ItemOverlayShown {
		bool useAreas = true;
		bool clearance = true;
		bool footprints = true;
		bool collider = true;
	};

	struct ItemDrawStyle {
		/// One logical pixel in the caller's units: line widths, dashes, labels, and the hatch's thinning are sized in it.
		float pixel = 1.0F;
		/// Opacity over everything, and the refused tint (the ghost where placement is refused).
		float	opacity = 1.0F;
		bool	refused = false;
		bool	labels = true; ///< use area names
		int32_t key = 0;
	};

	/// The look's colour as the renderer takes it.
	[[nodiscard]] Foundation::Color colorOf(const assets::StyleColor& color);

	/// A view's triangles, or any mesh about the pivot (a template), in its stored order, on the batch: each colour's
	/// alpha times the opacity, and its colour times the refused tint where refused. A mesh with no colours is white.
	void drawItemView(
		const renderer::TessellatedMesh& mesh, const ItemScreenFrame& frame, const assets::ItemOverlayStyle& look, const ItemDrawStyle& style
	);

	/// Use areas filled with a solid edge and their names, the clearance hatched with a dashed edge, each placed part's
	/// footprint as a thin line, and the collider's rings: in that order, so the lines lie over the fills. Hatch lines
	/// closer on the screen than the look's hatchMinGapPx are thinned to every Nth.
	void drawItemOverlays(
		const assets::ItemOverlays&		  overlays,
		const ItemScreenFrame&			  frame,
		const ItemOverlayShown&			  shown,
		const assets::ItemOverlayStyle&	  look,
		const ItemDrawStyle&			  style
	);

	/// A view's depth strips: each strip's screen box, and its anchor as a line across it with a dot (the flat
	/// strip's anchor has nowhere on the screen to go). Already on the screen relative to the pivot: no turn.
	void drawItemStrips(
		const assets::ItemView& view, const ItemScreenFrame& frame, float reachMeters, const assets::ItemOverlayStyle& look, const ItemDrawStyle& style
	);

} // namespace worldview
