#include "ItemClearance.h"

#include "WallSides.h"

#include <boolean/RingBoolean.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace construction {

	namespace {

		constexpr double kMillimetersPerMeter = 1000.0;
		// A miter at a sharp corner is held to this many insets, so the taken-in ring keeps near its outline.
		constexpr double kMiterLimit = 4.0;

		struct Box {
			glm::vec2 lower{0.0F, 0.0F};
			glm::vec2 upper{0.0F, 0.0F};

			[[nodiscard]] bool meets(const Box& other) const {
				return lower.x <= other.upper.x && upper.x >= other.lower.x && lower.y <= other.upper.y && upper.y >= other.lower.y;
			}
		};

		Box boxOf(const std::vector<glm::vec2>& ring) {
			Box box{ring.front(), ring.front()};
			for (const glm::vec2 point : ring) {
				box.lower = glm::min(box.lower, point);
				box.upper = glm::max(box.upper, point);
			}
			return box;
		}

		Box boxOfAll(const std::vector<std::vector<glm::vec2>>& rings) {
			Box box = boxOf(rings.front());
			for (const std::vector<glm::vec2>& ring : rings) {
				const Box next = boxOf(ring);
				box.lower = glm::min(box.lower, next.lower);
				box.upper = glm::max(box.upper, next.upper);
			}
			return box;
		}

		geometry::Ring toMillimeters(const std::vector<glm::vec2>& ring) {
			geometry::Ring out;
			out.reserve(ring.size());
			for (const glm::vec2 point : ring) {
				out.push_back({std::llround(static_cast<double>(point.x) * kMillimetersPerMeter), std::llround(static_cast<double>(point.y) * kMillimetersPerMeter)});
			}
			geometry::ensureCounterClockwise(out);
			return out;
		}

		// `ring` (counter-clockwise, mm) with every edge moved inward by `insetMm`: each corner where its two moved
		// edges cross, the miter held to kMiterLimit insets. Nothing when the result is not a simple ring (a part
		// narrower than twice the inset), so the caller tests the ring as it is.
		std::optional<geometry::Ring> insetRing(const geometry::Ring& ring, double insetMm) {
			const std::size_t count = ring.size();
			std::vector<glm::dvec2> inward(count);
			for (std::size_t index = 0; index < count; ++index) {
				const geometry::Vec2i64& from = ring[index];
				const geometry::Vec2i64& to = ring[(index + 1) % count];
				const glm::dvec2		 along{static_cast<double>(to.x - from.x), static_cast<double>(to.y - from.y)};
				const double			 length = glm::length(along);
				inward[index] = length > 0.0 ? glm::dvec2{-along.y, along.x} / length : glm::dvec2{0.0, 0.0};
			}
			geometry::Ring out;
			out.reserve(count);
			for (std::size_t index = 0; index < count; ++index) {
				const glm::dvec2 before = inward[(index + count - 1) % count];
				const glm::dvec2 after = inward[index];
				const double	 cosine = glm::dot(before, after);
				glm::dvec2		 miter = (before + after) * (insetMm / std::max(1.0 + cosine, 1e-9));
				const double	 miterLength = glm::length(miter);
				if (miterLength > kMiterLimit * insetMm) {
					miter *= kMiterLimit * insetMm / miterLength;
				}
				out.push_back({std::llround(static_cast<double>(ring[index].x) + miter.x), std::llround(static_cast<double>(ring[index].y) + miter.y)});
			}
			if (geometry::windingOrder(out) != geometry::Winding::CounterClockwise || !geometry::isSimple(out).pass) {
				return std::nullopt;
			}
			return out;
		}

		bool anyMeet(const std::vector<std::vector<glm::vec2>>& first, const std::vector<std::vector<glm::vec2>>& second) {
			return std::ranges::any_of(first, [&second](const std::vector<glm::vec2>& ring) {
				return std::ranges::any_of(second, [&ring](const std::vector<glm::vec2>& other) { return ringsMeet(ring, other); });
			});
		}

		bool wallBandsMeet(const ConstructionWorld& walls, const std::vector<std::vector<glm::vec2>>& reserved) {
			const Box box = boxOfAll(reserved);
			for (const WallSegment& segment : walls.segments()) {
				const std::optional<WallBand> band = wallBand(walls, segment);
				if (!band.has_value()) {
					continue;
				}
				const std::vector<glm::vec2> ring = bandRing(band->start, band->end, band->halfThicknessMeters);
				if (boxOf(ring).meets(box) && anyMeet({ring}, reserved)) {
					return true;
				}
			}
			return false;
		}

	} // namespace

	std::string_view placementRefusalLabel(PlacementRefusal refusal) {
		switch (refusal) {
			case PlacementRefusal::NotBuildableGround:
				return "Not buildable ground";
			case PlacementRefusal::Unexplored:
				return "Unexplored";
			case PlacementRefusal::OverlapsItem:
				return "Overlaps an item";
			case PlacementRefusal::TooClose:
				return "Too close";
		}
		return {};
	}

	bool ringsMeet(const std::vector<glm::vec2>& first, const std::vector<glm::vec2>& second) {
		if (first.size() < 3 || second.size() < 3 || !boxOf(first).meets(boxOf(second))) {
			return false;
		}
		const geometry::Ring				firstMm = toMillimeters(first);
		const std::optional<geometry::Ring> taken = insetRing(firstMm, static_cast<double>(kFlushToleranceMm));
		return geometry::ringsInteriorOverlap(taken.has_value() ? *taken : firstMm, toMillimeters(second));
	}

	std::vector<glm::vec2> takenIn(const std::vector<glm::vec2>& ring) {
		if (ring.size() < 3) {
			return ring;
		}
		const std::optional<geometry::Ring> taken = insetRing(toMillimeters(ring), static_cast<double>(kFlushToleranceMm));
		if (!taken.has_value()) {
			return ring;
		}
		std::vector<glm::vec2> out;
		out.reserve(taken->size());
		for (const geometry::Vec2i64& point : *taken) {
			out.push_back({static_cast<float>(static_cast<double>(point.x) / kMillimetersPerMeter), static_cast<float>(static_cast<double>(point.y) / kMillimetersPerMeter)});
		}
		return out;
	}

	std::vector<glm::vec2> bandRing(glm::vec2 start, glm::vec2 end, float halfThicknessMeters) {
		const float length = glm::distance(start, end);
		if (length <= 0.0F) {
			return {};
		}
		const glm::vec2 along = (end - start) / length;
		const glm::vec2 side = glm::vec2{-along.y, along.x} * halfThicknessMeters;
		return {start - side, end - side, end + side, start + side};
	}

	std::optional<PlacementRefusal>
	itemsRefusal(const ecs::PlacedItemIndex& items, const ConstructionWorld* walls, const ecs::ItemOutlines& held, ecs::EntityID ignore) {
		if (held.empty()) {
			return std::nullopt;
		}
		const Box box = boxOfAll(held.reserved.empty() ? held.collider : held.reserved);
		std::vector<const ecs::PlacedItemIndex::Entry*> near;
		items.query(box.lower, box.upper, near);
		std::erase_if(near, [ignore](const ecs::PlacedItemIndex::Entry* entry) { return entry->entity == ignore; });
		for (const ecs::PlacedItemIndex::Entry* other : near) {
			if (anyMeet(held.collider, other->world.collider)) {
				return PlacementRefusal::OverlapsItem;
			}
		}
		for (const ecs::PlacedItemIndex::Entry* other : near) {
			if (anyMeet(other->world.collider, held.reserved) || anyMeet(held.collider, other->world.reserved)) {
				return PlacementRefusal::TooClose;
			}
		}
		if (walls != nullptr && !held.reserved.empty() && wallBandsMeet(*walls, held.reserved)) {
			return PlacementRefusal::TooClose;
		}
		return std::nullopt;
	}

	bool bandMeetsItems(const ecs::PlacedItemIndex& items, const std::vector<glm::vec2>& band) {
		if (band.size() < 3) {
			return false;
		}
		const Box										box = boxOf(band);
		std::vector<const ecs::PlacedItemIndex::Entry*> near;
		items.query(box.lower, box.upper, near);
		return std::ranges::any_of(near, [&band](const ecs::PlacedItemIndex::Entry* entry) { return anyMeet({band}, entry->world.reserved); });
	}

} // namespace construction
