#pragma once

// Opening footprint geometry shared by the interim opening render (DrawingSystem)
// and opening selection (SelectionSystem). Both need the same oriented rectangle
// an opening occupies on its host wall, so the math lives here once: the band over
// the opening's centerline sub-span [t - halfW/L, t + halfW/L], offset by the
// wall's half-thickness. Integer millimeters throughout (the topology's space).

#include <construction/ConstructionWorld.h>

#include <polygon/Polygon.h>

namespace world_sim {

	/// The oriented footprint of an opening as an integer-mm ring (CCW, 4 verts):
	/// the wall-thickness rectangle centered on the opening, sized to its clear
	/// width along the wall. Empty when geometry is missing or degenerate (unknown
	/// segment/type, zero-length segment, zero half-thickness).
	[[nodiscard]] geometry::Ring
	openingFootprint(const engine::construction::ConstructionWorld& world, const engine::construction::Opening& opening);

} // namespace world_sim
