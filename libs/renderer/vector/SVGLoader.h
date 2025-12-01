#pragma once

#include "Types.h"
#include "graphics/Color.h"

#include <string>
#include <vector>

namespace renderer {

/// A loaded SVG shape with flattened paths ready for tessellation
struct LoadedSVGShape {
	std::vector<VectorPath> paths;		 ///< Flattened polygon paths (Beziers already linearized)
	Foundation::Color		fillColor;	 ///< Fill color from SVG
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

} // namespace renderer
