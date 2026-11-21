#pragma once

// Bodies and clearance among placed items and walls (rotatable-placed-items.md 13, 14.2). An item's body (its
// collider) stays out of every other item's body, installed or pending, and out of every wall, built or blueprint; it
// stays out of every other item's reserved region (collider unioned with clearance), and no other body and no wall
// enters its own. Two clearances may overlap. The same pair of shapes is read whichever was placed first, so the
// placement check and the wall tool refuse alike.
//
// Two shapes meet when they share ground deeper than kFlushToleranceMm: a box flush against another, or an item
// backed onto a wall face, does not meet it, since turned shapes slid flush land on each other's edge only to within
// float rounding. A ring that is degenerate once rounded to the micrometre meets everything.

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
	enum class PlacementRefusal : std::uint8_t { OverlapsItem, WallInTheWay, NotBuildableGround, Unexplored, TooClose };

	/// The short label the ghost shows (14.4).
	[[nodiscard]] std::string_view placementRefusalLabel(PlacementRefusal refusal);

	/// How deep two shapes may share ground at their edges and still not meet.
	inline constexpr std::int64_t kFlushToleranceMm = 2;

	/// Whether two rings in metres, either winding, share ground deeper than kFlushToleranceMm. The first is taken in
	/// by the tolerance (by half of it where a part is too narrow for all of it, as it is where too narrow for that)
	/// and tested against the second exactly, in micrometres. A ring with under three distinct micrometre points, or one that crosses
	/// itself there, meets anything.
	[[nodiscard]] bool ringsMeet(const std::vector<glm::vec2>& first, const std::vector<glm::vec2>& second);

	/// `ring` in metres, either winding, taken in as ringsMeet takes it in: what a test of the ground under an item
	/// reads, so an obstacle or a wall in the mesh that only touches the item's edge doesn't refuse it.
	[[nodiscard]] std::vector<glm::vec2> takenIn(const std::vector<glm::vec2>& ring);

	/// Every wall of `world`, blueprint and built, as the offsetter outlines it (geometry::resolveWallBands over each
	/// connected wall graph: trimmed bands and the junctions that fill their corners, so an L joint's outer corner is
	/// wall), in metres. A band runs whole across its openings. A graph the offsetter rejects is its raw bands.
	[[nodiscard]] std::vector<std::vector<glm::vec2>> wallOutlines(const ConstructionWorld& world);

	/// The band a wall from `start` to `end` would have, `halfThicknessMeters` each side of the centreline, carried on
	/// by the half thickness past an end that joins another wall (`joinsAtStart`, `joinsAtEnd`), which covers the
	/// corner the joint's junction fills.
	[[nodiscard]] std::vector<glm::vec2>
	drawnWallBand(glm::vec2 start, glm::vec2 end, float halfThicknessMeters, bool joinsAtStart, bool joinsAtEnd);

	/// Rules 1 and 2 of 14.2 for an item whose outlines in the world are `held`, against every other item `items`
	/// holds (installed and pending, `ignore` aside: the crate being moved) and the walls `wallRings`
	/// (wallOutlines): OverlapsItem when its collider meets another's collider, WallInTheWay when it meets a wall.
	[[nodiscard]] std::optional<PlacementRefusal> bodyRefusal(
		const ecs::PlacedItemIndex& items, const std::vector<std::vector<glm::vec2>>& wallRings, const ecs::ItemOutlines& held, ecs::EntityID ignore
	);

	/// Rule 5: TooClose when another item's collider meets `held`'s reserved region, `held`'s collider meets another's
	/// reserved region, or a wall meets `held`'s reserved region.
	[[nodiscard]] std::optional<PlacementRefusal> clearanceRefusal(
		const ecs::PlacedItemIndex& items, const std::vector<std::vector<glm::vec2>>& wallRings, const ecs::ItemOutlines& held, ecs::EntityID ignore
	);

	/// Whether the wall band `band` (a ring in metres) meets the reserved region of any item `items` holds.
	[[nodiscard]] bool bandMeetsItems(const ecs::PlacedItemIndex& items, const std::vector<glm::vec2>& band);

} // namespace construction
