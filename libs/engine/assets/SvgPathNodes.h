#pragma once

// Parse an SVG path `d` attribute into its on-curve anchor nodes (the points you see as
// editable nodes in Illustrator/Inkscape), in the path's own user-space coordinates, in
// document order. This is what `find_node(part, index)` indexes into: a pivot is the Nth
// authored node of a part's path. We parse the AUTHORED `d` here, never the tessellated mesh
// (tessellation flattens beziers and adds interior vertices, destroying node identity).
//
// Handles M/L/H/V/C/S/Q/T/Z, absolute and relative. Control points of curves are skipped;
// only the on-curve endpoint of each command is recorded. Arc (A/a) endpoints are recorded
// (the arc sweep is ignored, which is fine for pivots). Malformed input yields a best-effort
// prefix rather than throwing.

#include <string>
#include <vector>

#include <glm/vec2.hpp>

namespace engine::assets {

	std::vector<glm::vec2> parseSvgPathNodes(const std::string& d);

} // namespace engine::assets
