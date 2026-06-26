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

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

namespace ecs {

/// Count of a bulk material in a loose ground pile (the haulable remainder of a fell).
struct ResourceStack {
	uint32_t quantity = 0;
};

/// Splits a dropped quantity into one loose pile per physical stack, each <= stackCap, and
/// assigns every pile a distinct ground position clustered near origin.
///
/// Returns ceil(totalQty / stackCap) entries: full caps with the remainder in the last. A
/// stackCap of 0 or UINT32_MAX means the item is unbounded/unknown, so the whole quantity
/// stays a single pile at origin. totalQty 0 yields no piles.
///
/// Positions follow a deterministic concentric-ring layout (no RNG, reproducible for tests and
/// any seeded/replay path): the first pile sits at origin, then 6 around at ~0.6 m, 12 at ~1.2 m,
/// and so on outward. Every pair lands > 0.3 m apart so piles never alias inside the 0.25 m
/// pickup epsilon. Geometry only -- no walkability check; a later pass can nudge piles onto
/// walkable tiles, but the drop origin is already walkable (a cleared stump / build site) and
/// these offsets stay close.
[[nodiscard]] inline std::vector<std::pair<glm::vec2, uint32_t>>
resourcePileDrops(uint32_t stackCap, uint32_t totalQty, glm::vec2 origin) {
	std::vector<std::pair<glm::vec2, uint32_t>> drops;
	if (totalQty == 0) {
		return drops;
	}
	if (stackCap == 0 || stackCap == UINT32_MAX) {
		drops.emplace_back(origin, totalQty);
		return drops;
	}

	const uint32_t pileCount = (totalQty + stackCap - 1) / stackCap;
	drops.reserve(pileCount);

	constexpr float kRingStep = 0.6F; // radial gap and ring-1 radius; keeps min pair distance ~0.6 m
	uint32_t		remaining = totalQty;
	uint32_t		placed	  = 0;
	for (uint32_t ring = 0; placed < pileCount; ++ring) {
		const uint32_t slotsInRing = (ring == 0) ? 1U : 6U * ring;
		const float	   radius	   = static_cast<float>(ring) * kRingStep;
		// Half-slot angular offset per ring so successive rings don't line up radially.
		const float baseAngle = (ring == 0) ? 0.0F : (3.14159265358979323846F / static_cast<float>(slotsInRing));
		for (uint32_t slot = 0; slot < slotsInRing && placed < pileCount; ++slot, ++placed) {
			const float angle = baseAngle + (2.0F * 3.14159265358979323846F * static_cast<float>(slot)
											 / static_cast<float>(slotsInRing));
			const glm::vec2 pos{origin.x + radius * std::cos(angle), origin.y + radius * std::sin(angle)};
			const uint32_t	qty = (remaining > stackCap) ? stackCap : remaining;
			drops.emplace_back(pos, qty);
			remaining -= qty;
		}
	}
	return drops;
}

} // namespace ecs
