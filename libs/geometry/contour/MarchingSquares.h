#pragma once

#include "../polygon/Polygon.h"
#include "ScalarField.h"

#include <vector>

namespace geometry {

	// Isoline extraction on the dual grid (terrain-polygons D3/D5). Samples >= iso
	// are inside. Each crossing is interpolated linearly along its lattice edge from
	// the edge's world-mm endpoints and rounded to the nearest mm, so the same world
	// edge yields the same vertex in any field that contains it. Saddle cells
	// connect their two inside corners when the four-sample average is >= iso and
	// separate them otherwise.
	//
	// Rounded crossings are kept at least 1 mm inside their edge. Every vertex then
	// lies strictly inside a distinct lattice edge and every segment strictly
	// inside one cell, so each output ring is simple and no two rings touch, even
	// where a sample sits within rounding distance of iso.
	//
	// Output rings keep the inside on their left: outer boundaries CCW, holes CW.
	// Loop order and each loop's start vertex follow the row-major scan order
	// (bottom row first) of the loop's first crossing, so a field padded with extra
	// outside samples returns the same rings in the same order.
	//
	// Preconditions: cellMm >= 2, and every sample on the field's border is < iso
	// so every loop closes inside the field (asserted; in release a violating
	// field returns no rings). Callers pad with an outside sample.
	std::vector<Ring> marchingSquares(const ScalarField& field, float iso);

} // namespace geometry
