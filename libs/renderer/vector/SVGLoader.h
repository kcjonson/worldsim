#pragma once

#include "Types.h"
#include "graphics/Color.h"

#include <string>
#include <vector>

namespace renderer {

/// A loaded SVG shape with flattened paths ready for tessellation
struct LoadedSVGShape {
	std::vector<VectorPath> paths;		 ///< Flattened polygon paths (Beziers already linearized)
	Foundation::Color		fillColor;	 ///< Solid fill color (and fallback when a gradient is ignored)
	GradientFill			gradient;	 ///< Gradient fill; type == None for solid-color shapes
	float					width{0.0F}; ///< Original SVG width
	float					height{0.0F}; ///< Original SVG height
};

/// Load an SVG file and convert to VectorPaths ready for tessellation.
///
/// Uses NanoSVG to parse the file, then flattens all cubic Bezier curves
/// using the existing flattenCubicBezier() function. The output is a set of
/// VectorPath objects that can be passed directly to Tessellator::Tessellate().
///
/// @param filepath Path to the SVG file
/// @param curveTolerance Bezier flattening tolerance (smaller = more vertices, smoother curves)
///                       Recommended: 0.5-1.0 for screen rendering
/// @param outShapes Output vector of loaded shapes with fill colors
/// @return true if loading succeeded, false on error
bool loadSVG(const std::string& filepath, float curveTolerance, std::vector<LoadedSVGShape>& outShapes);

/// Tessellate a loaded SVG shape into colored triangles, appended to outMesh.
///
/// Per-vertex colors are baked from the shape's fill: a solid color fills every vertex, while a
/// gradient is evaluated per output vertex (radial fills get an inserted centroid sample so they
/// show a center, not just the edge color). Vertices stay in the SVG's own coordinate space;
/// callers scale/position the resulting mesh afterward. Shapes are appended with index offset, so
/// the same outMesh can accumulate many shapes.
void appendShapeMesh(const LoadedSVGShape& shape, TessellatedMesh& outMesh);

} // namespace renderer
