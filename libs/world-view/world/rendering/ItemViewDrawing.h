#pragma once

// Drawing a parts item's view and its overlays on the CPU batch (docs/technical/rendering/rotatable-placed-items.md,
// sections 6, 14.4 and 16): the one drawing path the asset manager's preview, the ui-sandbox's turntable, and the
// placement ghost's overlays share. Geometry comes from libs/assets (assets::buildItemMesh, assets::itemOverlays);
// this maps it from pivot-relative metres into whatever space the caller draws in.

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

	/// The overlays' colours, the legend's swatches too.
	inline const Foundation::Color kUseAreaColor{0.25F, 0.85F, 0.55F, 1.0F};
	inline const Foundation::Color kClearanceColor{0.88F, 0.86F, 0.80F, 1.0F};
	inline const Foundation::Color kFootprintColor{0.96F, 0.94F, 0.88F, 1.0F};
	inline const Foundation::Color kColliderColor{0.0F, 1.0F, 1.0F, 1.0F};
	inline const Foundation::Color kStripColor{0.1F, 0.75F, 0.95F, 1.0F};
	inline const Foundation::Color kAnchorColor{0.9F, 0.2F, 0.85F, 1.0F};
	inline const Foundation::Color kRefusedColor{1.0F, 0.3F, 0.3F, 1.0F};

	struct ItemOverlayStyle {
		/// One logical pixel in the caller's units: line widths, dashes, and labels are sized in it.
		float pixel = 1.0F;
		/// Opacity over everything, and the refused tint (the ghost where placement is refused).
		float	opacity = 1.0F;
		bool	refused = false;
		bool	labels = true; ///< use area names
		int32_t key = 0;
	};

	/// A view's triangles (or any pivot-relative mesh), in its stored order, on the batch.
	void drawItemView(const renderer::TessellatedMesh& mesh, const ItemScreenFrame& frame, int32_t key = 0);

	/// Use areas filled with a solid edge and their names, the clearance hatched with a dashed edge, each placement's
	/// footprint as a thin line, and the collider's rings: in that order, so the lines lie over the fills.
	void drawItemOverlays(
		const assets::ItemOverlays& overlays, const ItemScreenFrame& frame, const ItemOverlayShown& shown, const ItemOverlayStyle& style
	);

	/// A view's depth strips: each strip's screen box, and its anchor as a line across it with a dot (the flat
	/// strip's anchor has nowhere on the screen to go). Already on the screen relative to the pivot: no turn.
	void drawItemStrips(const assets::ItemView& view, const ItemScreenFrame& frame, float reachMeters, const ItemOverlayStyle& style);

} // namespace worldview
