#pragma once

// ResourceStack Component
//
// Per-entity count for a loose ground pile of a bulk material (e.g. the wood a
// felled tree drops beyond the armful the colonist carried off). The pile is one
// world entity whose defName IS the material; ResourceStack holds how many units
// remain. Hauling lifts a weight-limited armful and decrements; the entity is
// removed when the count hits zero.
//
// Unlike Packaged (crafted items awaiting placement), a loose pile is spawned
// without Packaged: it is immediately haulable, not a ghost the player must site.

#include <cstdint>

namespace ecs {

/// Count of a bulk material in a loose ground pile (the haulable remainder of a fell).
struct ResourceStack {
	uint32_t quantity = 0;
};

} // namespace ecs
