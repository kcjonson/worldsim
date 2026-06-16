#pragma once

#include "worldgen/grid/SphereGrid.h"

#include <atomic>
#include <vector>

namespace worldgen {

// Priority-flood depression routing (Barnes 2014). Fills every pit up to the lowest
// lip over which it spills to the sea, and routes each land tile one step toward that
// spill outlet. Shared by PrecipitationStage (climate-weighted drainage + lakes) and
// ErosionStage (provisional drainage for stream-power incision) so there is one
// implementation of the routing.
//
// Deterministic: a min-heap pops the globally lowest filled cell, ties broken by
// ascending TileId, so the traversal -- and therefore filled[] and receiver[] -- is
// bit-identical at any thread count.
//
// Ocean is elevation < seaLevel: ocean tiles are the outlets, fixed at their own
// elevation; land tiles enter the heap only when first reached from an already-spilled
// cell.
//
// Outputs (sized to elevation.size()):
//   filled[t]   = water-surface level a parcel at t must rise to in order to spill out
//                 (== terrain where t drains freely, > terrain under a lake surface).
//   receiver[t] = the neighbor one step toward the spill outlet. kInvalidTile for ocean
//                 tiles and for endorheic land the flood never reached from any ocean
//                 (a genuine sink). Because a tile's receiver was popped before it, in
//                 (filled ascending, then TileId ascending) order the receiver always
//                 precedes the tile -- a ready-made drainage-stack order.
//
// `cancel` is polled periodically; routeDepressions throws CancelledException when set.
void routeDepressions(const SphereGrid& grid,
                      const std::vector<float>& elevation,
                      float seaLevel,
                      std::vector<float>& filled,
                      std::vector<TileId>& receiver,
                      const std::atomic<bool>& cancel);

} // namespace worldgen
