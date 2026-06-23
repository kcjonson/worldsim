#pragma once

// Monotone-region triangulation of the swept mesh. Ported from libtess2
// (tessMeshTessellateMonoRegion / tessMeshTessellateInterior in tess.c), SGI Free
// Software License B 2.0. After Sweep::computeInterior has marked interior
// regions, each is a monotone face; this splits each into triangles in place.

#include "Mesh.h"

namespace renderer::tess {

	// Triangulate every face marked "inside" the polygon. Each must be monotone.
	void tessellateInterior(Mesh& mesh);

} // namespace renderer::tess
