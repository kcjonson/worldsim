#pragma once

// Bodies and clearance among placed items and walls (rotatable-placed-items.md 13, 14.2 rules 3 and 4). An item's
// body (its collider) stays out of every other item's reserved region (collider unioned with clearance), installed
// or pending, and so does a wall's band, built or blueprint; two clearances may overlap. The same pair of shapes is
// read whichever was placed first, so the placement check and the wall tool refuse alike.
//
// Shapes meeting only along an edge or at a point don't meet: a box flush against another, or an item backed onto a
// wall face, is allowed. Turned shapes slid flush land on each other's edge only to within float and millimetre
// rounding, so a shape is taken in by kFlushToleranceMm before the exact interior test.

#include "ConstructionWorld.h"

#include <ecs/EntityID.h>
#include <ecs/PlacedItemIndex.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace construction {

	/// Why an item can't go where the ghost stands (rotatable-placed-items 14.2), in the order the checks run.
	enum class PlacementRefusal : std::uint8_t { NotBuildableGround, Unexplored, OverlapsItem, TooClose };

	/// The short label the ghost shows (14.4).
	[[nodiscard]] std::string_view placementRefusalLabel(PlacementRefusal refusal);

	/// How far two shapes may share ground at their edges and still not meet.
	inline constexpr std::int64_t kFlushToleranceMm = 2;

	/// Whether two rings in metres, either winding, share ground deeper than kFlushToleranceMm.
	[[nodiscard]] bool ringsMeet(const std::vector<glm::vec2>& first, const std::vector<glm::vec2>& second);

	/// `ring` in metres, either winding, taken in by kFlushToleranceMm: what a test of the ground under an item reads,
	/// so an obstacle or a wall in the mesh that only touches the item's edge doesn't refuse it. The ring as it is when
	/// a part of it is too narrow to take in.
	[[nodiscard]] std::vector<glm::vec2> takenIn(const std::vector<glm::vec2>& ring);

	/// A wall's band, the centreline from `start` to `end` grown by `halfThicknessMeters` each side, as a ring.
	[[nodiscard]] std::vector<glm::vec2> bandRing(glm::vec2 start, glm::vec2 end, float halfThicknessMeters);

	/// Rules 3 and 4 for an item whose outlines in the world are `held`, against every other item `items` holds
	/// (installed and pending, `ignore` aside: the crate being moved) and every wall of `walls` (none when null):
	/// OverlapsItem when its collider meets another's collider; TooClose when another's collider meets its reserved
	/// region, its collider meets another's reserved region, or a wall's band meets its reserved region.
	[[nodiscard]] std::optional<PlacementRefusal>
	itemsRefusal(const ecs::PlacedItemIndex& items, const ConstructionWorld* walls, const ecs::ItemOutlines& held, ecs::EntityID ignore);

	/// Whether the wall band `band` (a ring in metres) meets the reserved region of any item `items` holds.
	[[nodiscard]] bool bandMeetsItems(const ecs::PlacedItemIndex& items, const std::vector<glm::vec2>& band);

} // namespace construction
