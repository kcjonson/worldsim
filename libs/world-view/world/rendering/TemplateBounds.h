#pragma once

#include "assets/placement/WorldDepthSort.h"
#include "graphics/Rect.h"
#include "vector/MeshBounds.h"
#include "vector/Types.h"

// Template-local bounds (meters) of an asset's tessellated template, the box every layout
// that sizes or centers a sprite measures. A missing template is a zero rect, which the
// layouts treat as a zero-size sprite at its anchor.

namespace worldview {

	[[nodiscard]] inline Foundation::Rect templateBounds(const renderer::TessellatedMesh* mesh) {
		return (mesh != nullptr) ? renderer::computeBounds(*mesh) : Foundation::Rect{0.0F, 0.0F, 0.0F, 0.0F};
	}

	/// The depth a dynamic entity drawn from one template sorts by: its feet, or a footprint's lower edge (a tree's
	/// trunk foot), from its def and its template's bounds. DynamicEntityRenderSystem draws by it and selection ranks
	/// a click by it.
	[[nodiscard]] inline float dynamicAnchorY(const assets::AssetDefinition* def, uint8_t ageClass, const Foundation::Rect& bounds, float positionY, float scale) {
		const assets::MeshYExtent meshY{.minY = bounds.y, .maxY = bounds.y + bounds.height}; // maxY: the feet
		return assets::computeAnchorY(positionY, assets::anchorLocalY(def, ageClass, meshY, 0.0F), scale);
	}

} // namespace worldview
