#pragma once

#include "Types.h"

#include <span>

namespace renderer {

	struct TessellatorOptions {
		// Fill rule: true = nonzero, false = even-odd.
		bool useNonZeroFillRule = true;

		// Tolerance for curve flattening (smaller = more vertices). Reserved; curves are
		// flattened before tessellation today.
		float curveFlatteningTolerance = 0.5F;

		// For a single convex contour, insert a centroid vertex and fan from it. This gives an
		// interior sample point so per-vertex gradient fills (especially radial) can show their
		// center, instead of fanning from a perimeter vertex where every vertex is the edge color.
		// Ignored for concave or multi-contour input (those go through the sweep).
		bool fanFromCentroid = false;
	};

	// Converts vector contours into a triangle mesh. Convex single contours take an O(n) fan
	// fast path; everything else (concave, self-intersecting, holes) goes through a sweep-line
	// tessellator over a half-edge mesh (libtess-style), so winding rules and intersections
	// are handled. Output positions may include new vertices (intersection/Steiner points);
	// callers that bake per-vertex data should do so per output vertex.
	class Tessellator {
	  public:
		Tessellator() = default;
		~Tessellator() = default;

		Tessellator(const Tessellator&) = delete;
		Tessellator& operator=(const Tessellator&) = delete;
		Tessellator(Tessellator&&) = delete;
		Tessellator& operator=(Tessellator&&) = delete;

		// Tessellate a single closed contour. Returns false on error.
		bool Tessellate(const VectorPath& path, TessellatedMesh& outMesh, const TessellatorOptions& options = {});

		// Tessellate multiple contours resolved together by the winding rule (outer boundary
		// plus holes). Returns false on error.
		bool Tessellate(std::span<const VectorPath> contours, TessellatedMesh& outMesh,
						const TessellatorOptions& options = {});
	};

} // namespace renderer
