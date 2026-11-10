#pragma once

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

} // namespace worldview
