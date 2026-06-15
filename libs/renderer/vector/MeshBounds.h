#pragma once

// Geometry helpers for fitting a tessellated mesh into a target rectangle.
// Used by the asset render path to frame an asset with no world/camera inputs.

#include "Types.h"
#include "graphics/Rect.h"

namespace renderer {

	// Axis-aligned bounds of a mesh's vertices. Returns a zero rect for an empty mesh.
	Foundation::Rect computeBounds(const TessellatedMesh& mesh);

	// Scale and translate mesh vertices in place so the geometry bounded by `src`
	// fits centered within `dst`, preserving aspect ratio (uniform scale).
	// No-op if `src` has zero extent.
	void fitToRect(TessellatedMesh& mesh, const Foundation::Rect& src, const Foundation::Rect& dst);

} // namespace renderer
