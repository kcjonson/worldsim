#include "ItemClearance.h"

#include <assets/ConstructionRegistry.h>
#include <boolean/RingBoolean.h>
#include <core/Vec2i64.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace construction {

	namespace {

		// The exact tests read integer coordinates, and a face counts as shared ground only with a lattice point strictly
		// inside it, so they run in micrometres: in millimetres a sliver up to 2 mm wide would read as no overlap at all.
		constexpr double kUnitsPerMeter = 1.0e6;
		constexpr double kUnitsPerMillimeter = 1.0e3;
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

		std::optional<Box> boxOfAll(const std::vector<std::vector<glm::vec2>>& rings) {
			std::optional<Box> box;
			for (const std::vector<glm::vec2>& ring : rings) {
				if (ring.empty()) {
					continue;
				}
				const Box next = boxOf(ring);
				box = box.has_value() ? Box{glm::min(box->lower, next.lower), glm::max(box->upper, next.upper)} : next;
			}
			return box;
		}

		// `ring` in micrometres, counter-clockwise, consecutive repeats dropped; nothing when that leaves fewer than three
		// points, no area, or a ring that crosses itself.
		std::optional<geometry::Ring> toLattice(const std::vector<glm::vec2>& ring) {
			geometry::Ring out;
			out.reserve(ring.size());
			for (const glm::vec2 point : ring) {
				const geometry::Vec2i64 rounded{
					std::llround(static_cast<double>(point.x) * kUnitsPerMeter), std::llround(static_cast<double>(point.y) * kUnitsPerMeter)
				};
				if (out.empty() || out.back() != rounded) {
					out.push_back(rounded);
				}
			}
			while (out.size() > 1 && out.back() == out.front()) {
				out.pop_back();
			}
			if (out.size() < 3 || geometry::windingOrder(out) == geometry::Winding::Degenerate || !geometry::isSimple(out).pass) {
				return std::nullopt;
			}
			geometry::ensureCounterClockwise(out);
			return out;
		}

		// `ring` in metres, from integer units `unitsPerMeter` to the metre.
		std::vector<glm::vec2> toMeters(const geometry::Ring& ring, double unitsPerMeter) {
			std::vector<glm::vec2> out;
			out.reserve(ring.size());
			for (const geometry::Vec2i64& point : ring) {
				out.push_back({static_cast<float>(static_cast<double>(point.x) / unitsPerMeter), static_cast<float>(static_cast<double>(point.y) / unitsPerMeter)});
			}
			return out;
		}

		// `ring` (counter-clockwise, micrometres) with every edge moved inward by `inset`: each corner where its two moved
		// edges cross, the miter held to kMiterLimit insets. Nothing when the result is not a simple ring.
		std::optional<geometry::Ring> insetBy(const geometry::Ring& ring, double inset) {
			const std::size_t		count = ring.size();
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
				glm::dvec2		 miter = (before + after) * (inset / std::max(1.0 + glm::dot(before, after), 1e-9));
				const double	 miterLength = glm::length(miter);
				if (miterLength > kMiterLimit * inset) {
					miter *= kMiterLimit * inset / miterLength;
				}
				const geometry::Vec2i64 moved{
					std::llround(static_cast<double>(ring[index].x) + miter.x), std::llround(static_cast<double>(ring[index].y) + miter.y)
				};
				if (out.empty() || out.back() != moved) {
					out.push_back(moved);
				}
			}
			if (out.size() < 3 || geometry::windingOrder(out) != geometry::Winding::CounterClockwise || !geometry::isSimple(out).pass) {
				return std::nullopt;
			}
			return out;
		}

		// The ring taken in by the flush tolerance, by half of it where a part is too narrow for all of it, and as it is
		// where too narrow for that: such a ring meets what touches it.
		geometry::Ring takenInLattice(const geometry::Ring& ring) {
			for (const double inset : {static_cast<double>(kFlushToleranceMm) * kUnitsPerMillimeter, static_cast<double>(kFlushToleranceMm) * kUnitsPerMillimeter * 0.5}) {
				if (std::optional<geometry::Ring> taken = insetBy(ring, inset)) {
					return std::move(*taken);
				}
			}
			return ring;
		}

		bool anyMeet(const std::vector<std::vector<glm::vec2>>& first, const std::vector<std::vector<glm::vec2>>& second) {
			return std::ranges::any_of(first, [&second](const std::vector<glm::vec2>& ring) {
				return std::ranges::any_of(second, [&ring](const std::vector<glm::vec2>& other) { return ringsMeet(ring, other); });
			});
		}

		std::vector<const ecs::PlacedItemIndex::Entry*>
		othersNear(const ecs::PlacedItemIndex& items, const ecs::ItemOutlines& held, ecs::EntityID ignore) {
			std::vector<const ecs::PlacedItemIndex::Entry*> near;
			const std::optional<Box>						 box = boxOfAll(held.reserved.empty() ? held.collider : held.reserved);
			if (!box.has_value()) {
				return near;
			}
			items.query(box->lower, box->upper, near);
			if (ignore != ecs::kInvalidEntity) {
				std::erase_if(near, [ignore](const ecs::PlacedItemIndex::Entry* entry) { return entry->entity == ignore; });
			}
			return near;
		}

		bool wallsMeet(const std::vector<std::vector<glm::vec2>>& wallRings, const std::vector<std::vector<glm::vec2>>& rings) {
			const std::optional<Box> box = boxOfAll(rings);
			return box.has_value() && std::ranges::any_of(wallRings, [&](const std::vector<glm::vec2>& wall) {
					   return !wall.empty() && boxOf(wall).meets(*box) && anyMeet({wall}, rings);
				   });
		}

	} // namespace

	std::string_view placementRefusalLabel(PlacementRefusal refusal) {
		switch (refusal) {
			case PlacementRefusal::OverlapsItem:
				return "Overlaps an item";
			case PlacementRefusal::WallInTheWay:
				return "A wall is in the way";
			case PlacementRefusal::NotBuildableGround:
				return "Not buildable ground";
			case PlacementRefusal::Unexplored:
				return "Unexplored";
			case PlacementRefusal::TooClose:
				return "Too close";
		}
		return {};
	}

	bool ringsMeet(const std::vector<glm::vec2>& first, const std::vector<glm::vec2>& second) {
		if (first.empty() || second.empty() || !boxOf(first).meets(boxOf(second))) {
			return false;
		}
		const std::optional<geometry::Ring> firstLattice = toLattice(first);
		const std::optional<geometry::Ring> secondLattice = toLattice(second);
		if (!firstLattice.has_value() || !secondLattice.has_value()) {
			return true;
		}
		return geometry::ringsInteriorOverlap(takenInLattice(*firstLattice), *secondLattice);
	}

	std::vector<glm::vec2> takenIn(const std::vector<glm::vec2>& ring) {
		const std::optional<geometry::Ring> lattice = toLattice(ring);
		return lattice.has_value() ? toMeters(takenInLattice(*lattice), kUnitsPerMeter) : ring;
	}

	std::vector<std::vector<glm::vec2>> wallOutlines(const ConstructionWorld& world) {
		std::vector<geometry::WallSegment> segments;
		for (const WallSegment& segment : world.segments()) {
			const Vertex*					first = world.getVertex(segment.v0);
			const Vertex*					second = world.getVertex(segment.v1);
			const assets::ThicknessPreset* preset = assets::ConstructionRegistry::Get().getThicknessPreset(segment.material, segment.thicknessPreset);
			if (first == nullptr || second == nullptr || preset == nullptr || preset->halfThicknessMm <= 0 || first->pos == second->pos) {
				continue;
			}
			segments.push_back({first->pos, second->pos, preset->halfThicknessMm});
		}
		std::vector<std::vector<glm::vec2>> outlines;
		for (const std::vector<std::size_t>& graph : geometry::connectedWallGraphs(segments)) {
			std::vector<geometry::WallSegment> members;
			members.reserve(graph.size());
			for (const std::size_t index : graph) {
				members.push_back(segments[index]);
			}
			const geometry::WallBands bands = geometry::resolveWallBands(members, geometry::kDefaultMiterLimit);
			if (!geometry::bandsAccepted(bands)) {
				for (const geometry::WallSegment& member : members) {
					outlines.push_back(toMeters(geometry::band(member.a, member.b, member.halfThicknessMm), static_cast<double>(geometry::kMillimetersPerMeter)));
				}
				continue;
			}
			for (const std::vector<geometry::Ring>* rings : {&bands.bands, &bands.junctions}) {
				for (const geometry::Ring& ring : *rings) {
					outlines.push_back(toMeters(ring, static_cast<double>(geometry::kMillimetersPerMeter)));
				}
			}
		}
		return outlines;
	}

	std::vector<glm::vec2> drawnWallBand(glm::vec2 start, glm::vec2 end, float halfThicknessMeters, bool joinsAtStart, bool joinsAtEnd) {
		const float length = glm::distance(start, end);
		if (length <= 0.0F) {
			return {};
		}
		const glm::vec2 along = (end - start) / length;
		const glm::vec2 from = joinsAtStart ? start - along * halfThicknessMeters : start;
		const glm::vec2 to = joinsAtEnd ? end + along * halfThicknessMeters : end;
		const glm::vec2 side = glm::vec2{-along.y, along.x} * halfThicknessMeters;
		return {from - side, to - side, to + side, from + side};
	}

	std::optional<PlacementRefusal> bodyRefusal(
		const ecs::PlacedItemIndex& items, const std::vector<std::vector<glm::vec2>>& wallRings, const ecs::ItemOutlines& held, ecs::EntityID ignore
	) {
		for (const ecs::PlacedItemIndex::Entry* other : othersNear(items, held, ignore)) {
			if (anyMeet(held.collider, other->world.collider)) {
				return PlacementRefusal::OverlapsItem;
			}
		}
		if (wallsMeet(wallRings, held.collider)) {
			return PlacementRefusal::WallInTheWay;
		}
		return std::nullopt;
	}

	std::optional<PlacementRefusal> clearanceRefusal(
		const ecs::PlacedItemIndex& items, const std::vector<std::vector<glm::vec2>>& wallRings, const ecs::ItemOutlines& held, ecs::EntityID ignore
	) {
		for (const ecs::PlacedItemIndex::Entry* other : othersNear(items, held, ignore)) {
			if (anyMeet(other->world.collider, held.reserved) || anyMeet(held.collider, other->world.reserved)) {
				return PlacementRefusal::TooClose;
			}
		}
		if (wallsMeet(wallRings, held.reserved)) {
			return PlacementRefusal::TooClose;
		}
		return std::nullopt;
	}

	bool bandMeetsItems(const ecs::PlacedItemIndex& items, const std::vector<glm::vec2>& band) {
		if (band.empty()) {
			return false;
		}
		const Box										box = boxOf(band);
		std::vector<const ecs::PlacedItemIndex::Entry*> near;
		items.query(box.lower, box.upper, near);
		return std::ranges::any_of(near, [&band](const ecs::PlacedItemIndex::Entry* entry) { return anyMeet({band}, entry->world.reserved); });
	}

} // namespace construction
