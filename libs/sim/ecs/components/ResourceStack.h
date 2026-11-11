#pragma once

// ResourceStack Component
//
// Per-entity count for a loose ground pile (the wood a felled tree drops beyond the
// armful the colonist carried off, a tool dropped when the hands had to be freed, a
// cancelled site's staged materials). The pile is one world entity whose defName IS
// the item; ResourceStack holds how many units remain. Hauling lifts a weight-limited
// armful and decrements; the entity is removed when the count hits zero.
//
// Unlike Packaged (crafted furniture awaiting placement), a loose pile is spawned
// without Packaged: it is immediately haulable, not a ghost the player must site.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

namespace ecs {

/// Count of an item in a loose ground pile.
struct ResourceStack {
	uint32_t quantity = 0;
};

// Ground-drop spacing. Tuning is deferred; these are the only knobs.
inline constexpr float	  kPileMergeRadius = 1.0F;	   // a same-item pile this close to the drop is topped up
inline constexpr float	  kGroundClearanceGap = 0.1F; // kept between the footprints of two things on the ground
inline constexpr uint32_t kWalkableSearchRings = 12;  // rings checked against isGround before clearance alone decides

/// Something already on the ground near a drop: the center and radius of its drawn footprint,
/// and, for a pile, what it holds and whether it is the dropped item (only a same-item pile can
/// be topped up; a crate or any other pile only takes up room).
struct GroundOccupant {
	glm::vec2 position{0.0F, 0.0F};
	float	  radius = 0.0F;
	uint32_t  quantity = 0;
	bool	  sameDef = false;
};

/// What a drop does to the ground: units added to existing piles (index into the `nearby`
/// list, units added) and new piles to spawn (position, quantity).
struct PileDropPlan {
	std::vector<std::pair<std::size_t, uint32_t>> topUps;
	std::vector<std::pair<glm::vec2, uint32_t>>	   spawns;
};

/// Lays `totalQty` units of one item onto the ground at `origin`, given what is already there.
///
/// Same-item piles within kPileMergeRadius of the origin are topped up to `stackCap` first,
/// nearest first. The rest spawns as new stack-capped piles on the first clear spots of a
/// deterministic spiral (no RNG): the origin, then rings of 6, 12, 18... spots stepped by the
/// dropped item's own footprint, so overflow piles of one item sit side by side. A spot is
/// clear when its distance to every occupant (and every pile this drop already placed) is at
/// least the two footprint radii plus kGroundClearanceGap, so dissimilar items never overlay.
/// For the first kWalkableSearchRings rings a spot must also pass `isGround` (when given);
/// past them clearance alone decides, so a drop on a tiny patch of ground still lands.
/// Before ring N (N >= 1) opens a new pile, same-item piles out to that ring's radius are
/// topped up, nearest first, so repeated drops fill the overflow piles earlier drops left.
/// A spot where `kept` holds (ground a site will be built on) is never used, on any ring.
///
/// A stackCap of 0 or UINT32_MAX means unbounded: everything lands in one pile.
[[nodiscard]] inline PileDropPlan resourcePileDrops(
	uint32_t							   stackCap,
	uint32_t							   totalQty,
	glm::vec2							   origin,
	float								   footprintRadius,
	const std::vector<GroundOccupant>&	   nearby,
	const std::function<bool(glm::vec2)>& isGround = {},
	const std::function<bool(glm::vec2)>& kept = {}
) {
	PileDropPlan plan;
	if (totalQty == 0) {
		return plan;
	}
	const bool unbounded = stackCap == 0 || stackCap == UINT32_MAX;
	uint32_t   remaining = totalQty;

	auto distSq = [](glm::vec2 a, glm::vec2 b) {
		const float dx = a.x - b.x;
		const float dy = a.y - b.y;
		return dx * dx + dy * dy;
	};

	// Same-item piles nearest the origin first; each is topped up at most once.
	std::vector<std::size_t> sameDefByDistance;
	for (std::size_t i = 0; i < nearby.size(); ++i) {
		if (nearby[i].sameDef) {
			sameDefByDistance.push_back(i);
		}
	}
	std::sort(sameDefByDistance.begin(), sameDefByDistance.end(), [&](std::size_t a, std::size_t b) {
		const float da = distSq(nearby[a].position, origin);
		const float db = distSq(nearby[b].position, origin);
		return da != db ? da < db : a < b;
	});
	std::size_t topUpCursor = 0;
	auto		topUpWithin = [&](float reach) {
		for (; topUpCursor < sameDefByDistance.size() && remaining > 0; ++topUpCursor) {
			const std::size_t i = sameDefByDistance[topUpCursor];
			if (distSq(nearby[i].position, origin) > reach * reach) {
				return;
			}
			const uint32_t room = unbounded ? remaining : (nearby[i].quantity < stackCap ? stackCap - nearby[i].quantity : 0U);
			const uint32_t added = room < remaining ? room : remaining;
			if (added > 0) {
				plan.topUps.emplace_back(i, added);
				remaining -= added;
			}
		}
	};
	topUpWithin(kPileMergeRadius);

	std::vector<GroundOccupant> occupied = nearby;
	constexpr float				kPi = 3.14159265358979323846F;
	constexpr float				kMinStep = 0.05F;		   // a zero-size footprint still steps outward
	constexpr float				kEpsilon = 1e-4F;		   // float slack on a ring whose spacing is exactly the step
	constexpr float				kRingReachEpsilon = 1e-3F; // a pile an earlier drop put on this ring still counts as on it
	const float					step = std::max(2.0F * footprintRadius + kGroundClearanceGap, kMinStep);

	for (uint32_t ring = 0; remaining > 0; ++ring) {
		const uint32_t slotsInRing = (ring == 0) ? 1U : 6U * ring;
		const float	   radius = static_cast<float>(ring) * step;
		// Fill the partial piles earlier drops left out to this ring before opening a new one.
		if (ring > 0) {
			topUpWithin(radius + kRingReachEpsilon);
			if (remaining == 0) {
				break;
			}
		}
		// Half-slot angular offset per ring so successive rings don't line up radially.
		const float baseAngle = (ring == 0) ? 0.0F : (kPi / static_cast<float>(slotsInRing));
		for (uint32_t slot = 0; slot < slotsInRing && remaining > 0; ++slot) {
			const float		angle = baseAngle + (2.0F * kPi * static_cast<float>(slot) / static_cast<float>(slotsInRing));
			const glm::vec2 pos{origin.x + radius * std::cos(angle), origin.y + radius * std::sin(angle)};

			bool clear = true;
			for (const GroundOccupant& other : occupied) {
				const float minDist = footprintRadius + other.radius + kGroundClearanceGap - kEpsilon;
				if (distSq(pos, other.position) < minDist * minDist) {
					clear = false;
					break;
				}
			}
			if (!clear || (isGround && ring < kWalkableSearchRings && !isGround(pos)) || (kept && kept(pos))) {
				continue;
			}
			const uint32_t qty = (unbounded || remaining < stackCap) ? remaining : stackCap;
			plan.spawns.emplace_back(pos, qty);
			occupied.push_back({pos, footprintRadius, qty, true});
			remaining -= qty;
		}
	}
	return plan;
}

} // namespace ecs
