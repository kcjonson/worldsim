#include "NavInputBuilder.h"

#include "NavCoords.h"

#include "construction/OpeningGeometry.h"

#include <core/Vec2i64.h>
#include <nav/LimitPolygon.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <utility>

namespace nav {

	namespace {

		namespace gnav = geometry::nav;

		constexpr std::int64_t kTileMm = geometry::kMillimetersPerMeter; // 1 tile == 1 m == 1000 mm

	} // namespace

	gnav::NavInputPolygon meshLimit(const geometry::Vec2i64& centreMm, std::int64_t rangeMm) {
		return {gnav::limitPolygon(centreMm, rangeMm), false, kProvenanceBorder};
	}

	void appendChunkWater(const world::ChunkTerrainPolygons& polygons, std::vector<gnav::NavInputPolygon>& out) {
		for (const world::TerrainRing& terrain : polygons.navRings) {
			if (terrain.ring.size() < 3) {
				continue;
			}
			if (terrain.blocksMovement) {
				out.push_back({terrain.ring, true, kProvenanceWater, gnav::kNoOpening, terrain.holeCapable});
			} else {
				out.push_back({.ring = terrain.ring, .blocked = true, .provenanceId = kProvenanceFord, .coverOnly = true});
			}
		}
	}

	bool faceInFord(const gnav::PatchableMesh& mesh, std::int32_t tri) {
		const std::vector<gnav::RingId>& cover = mesh.cover(tri);
		return std::any_of(cover.begin(), cover.end(), [&mesh](gnav::RingId ring) {
			return ring != gnav::kGroundToken && mesh.ring(ring).polygon.provenanceId == kProvenanceFord;
		});
	}

	std::optional<gnav::NavInputPolygon> placedObstacleRing(const assets::PlacedEntity& e, const assets::AssetRegistry& registry) {
		geometry::Ring ring;
		if (!placedObstacleOutline(e, registry, ring)) {
			return std::nullopt;
		}
		return gnav::NavInputPolygon{std::move(ring), true, kProvenancePlaced};
	}

	std::optional<OrientedRect> placedObstacleBox(const assets::PlacedEntity& e, const assets::CollisionShape& collision, glm::vec2 frameOrigin) {
		if (collision.type != assets::CollisionShapeType::Rect) {
			return std::nullopt;
		}
		const float		c	= std::cos(e.rotation);
		const float		s	= std::sin(e.rotation);
		const float		pad = static_cast<float>(kPlacedObstaclePadMm) / static_cast<float>(kTileMm);
		const glm::vec2 off = (frameOrigin + collision.offsetMeters) * e.scale;
		OrientedRect	box;
		box.center		= e.position + glm::vec2{off.x * c - off.y * s, off.x * s + off.y * c};
		box.axisX		= {c, s};
		box.axisY		= {-s, c};
		box.halfExtents = std::abs(e.scale) * (collision.halfExtentsMeters + glm::vec2{pad, pad});
		return box;
	}

	bool placedObstacleOutline(const assets::PlacedEntity& e, const assets::AssetRegistry& registry, geometry::Ring& ring) {
		ring.clear();
		const assets::AssetDefinition* def = registry.getDefinition(e.defName);
		if (def == nullptr) {
			return false;
		}
		const assets::CollisionShape& collision = def->collisionFor(e.traits.ageClass);
		if (!collision.blocks()) {
			return false;
		}
		const glm::vec2 frameOrigin = registry.collisionFrameOrigin(e.templateId);

		if (collision.type == assets::CollisionShapeType::Rect) {
			const OrientedRect box = *placedObstacleBox(e, collision, frameOrigin);
			ring.reserve(4);
			for (const glm::vec2 corner : {glm::vec2{-1.0F, -1.0F}, glm::vec2{1.0F, -1.0F}, glm::vec2{1.0F, 1.0F}, glm::vec2{-1.0F, 1.0F}}) {
				ring.push_back(toMm(box.center + box.axisX * (corner.x * box.halfExtents.x) + box.axisY * (corner.y * box.halfExtents.y)));
			}
		} else if (collision.type == assets::CollisionShapeType::Polygon) {
			const float c  = std::cos(e.rotation);
			const float s  = std::sin(e.rotation);
			const float sc = e.scale;
			ring.reserve(collision.pointsMeters.size());
			for (const glm::vec2& p : collision.pointsMeters) {
				const glm::vec2 l = (frameOrigin + p) * sc;
				ring.push_back(toMm({e.position.x + (l.x * c - l.y * s), e.position.y + (l.x * s + l.y * c)}));
			}
		}

		// A negative entity scale would flip winding; normalize so blocked rings
		// are CCW like the rest, and drop anything degenerate.
		if (ring.size() < 3) {
			return false;
		}
		geometry::ensureCounterClockwise(ring);
		return geometry::windingOrder(ring) != geometry::Winding::Degenerate;
	}

	void extractWalls(const construction::ConstructionWorld& world, const assets::ConstructionRegistry& registry,
					  std::vector<gnav::NavInputPolygon>& outPolys, std::vector<gnav::DoorPortal>& outDoors,
					  std::vector<WallJunction>* outJunctions) {
		// The BUILT segments as the offsetter takes them, with a parallel list of their ids for
		// door spans and provenance.
		std::vector<geometry::WallSegment>		   geoSegments;
		std::vector<construction::SegmentId>	   segmentIds;
		for (const construction::WallSegment& seg : world.segments()) {
			if (!construction::isBuilt(seg)) {
				continue;
			}
			const construction::Vertex* v0 = world.getVertex(seg.v0);
			const construction::Vertex* v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const auto* preset = registry.getThicknessPreset(seg.material, seg.thicknessPreset);
			const std::int64_t half = (preset != nullptr) ? preset->halfThicknessMm : 0;
			if (half <= 0) {
				continue;
			}
			geoSegments.push_back({v0->pos, v1->pos, half});
			segmentIds.push_back(seg.id);
		}

		if (geoSegments.empty()) {
			return;
		}

		std::unordered_map<construction::SegmentId, std::size_t> segIndexById;
		for (std::size_t i = 0; i < segmentIds.size(); ++i) {
			segIndexById[segmentIds[i]] = i;
		}
		std::vector<std::uint8_t> hostsDoor(geoSegments.size(), 0);

		for (const construction::Opening& op : world.openings()) {
			if (op.state != construction::FoundationState::Built) {
				continue;
			}
			const auto* type = registry.getOpeningType(op.type);
			if (type == nullptr) {
				continue;
			}
			// The footprint is the band over the opening's clear-width sub-span,
			// CCW with 4 verts walked aLeft, bLeft, bRight, aRight (geometry::band).
			// Its two SHORT edges are the jambs: aLeft-aRight and bLeft-bRight. The
			// jamb POINTS that bound the passage are the centerline-crossing midpoints
			// of those short edges, which sit on the wall centerline. Computed for
			// windows too so every opening is spatially addressable.
			const geometry::Ring footprint = construction::openingFootprint(world, op);
			if (footprint.size() < 4) {
				continue;
			}
			const geometry::Vec2i64 aLeft	= footprint[0];
			const geometry::Vec2i64 bLeft	= footprint[1];
			const geometry::Vec2i64 bRight	= footprint[2];
			const geometry::Vec2i64 aRight	= footprint[3];
			const geometry::Vec2i64 jambA{(aLeft.x + aRight.x) / 2, (aLeft.y + aRight.y) / 2};
			const geometry::Vec2i64 jambB{(bLeft.x + bRight.x) / 2, (bLeft.y + bRight.y) / 2};

			if (!type->pathable) {
				// Window: leave the band solid, emit a zero-clearance portal carrying
				// the jamb points so a later vision pass can locate the opening.
				outDoors.push_back({static_cast<std::int64_t>(op.id), jambA, jambB, 0});
				continue;
			}
			auto idxIt = segIndexById.find(op.segment);
			if (idxIt == segIndexById.end()) {
				continue; // door on a non-built / missing segment: nothing to cut
			}
			const geometry::WallSegment& host = geoSegments[idxIt->second];
			if (host.a == host.b) {
				continue;
			}
			hostsDoor[idxIt->second] = 1;
			outDoors.push_back({static_cast<std::int64_t>(op.id), jambA, jambB, type->widthMm});
		}

		// Each connected wall graph is resolved on its own, so a graph the offsetter rejects
		// (degenerate or self-intersecting band) changes no other. It falls back to its raw
		// per-segment bands, door segments whole, with no junctions: a blocked obstacle ring is
		// safer than a hole in the navmesh. Bands come out in segment order, then the junctions.
		struct Junction {
			geometry::Ring						 ring;
			std::int64_t						 provenance = kProvenanceJunction;
			std::vector<construction::SegmentId> walls;
		};
		std::vector<std::vector<gnav::NavInputPolygon>> polygonsOfSegment(geoSegments.size());
		std::vector<Junction>							junctions;
		for (const std::vector<std::size_t>& graph : geometry::connectedWallGraphs(geoSegments)) {
			std::vector<geometry::WallSegment> members;
			members.reserve(graph.size());
			for (const std::size_t i : graph) {
				members.push_back(geoSegments[i]);
			}
			const geometry::WallBands bands = geometry::resolveWallBands(members, geometry::kDefaultMiterLimit);
			if (bands.status != geometry::OffsetStatus::Ok || bands.bands.size() != members.size()) {
				for (const std::size_t i : graph) {
					geometry::Ring ring = geometry::band(geoSegments[i].a, geoSegments[i].b, geoSegments[i].halfThicknessMm);
					if (ring.size() >= 3) {
						polygonsOfSegment[i].push_back({std::move(ring), true, static_cast<std::int64_t>(segmentIds[i])});
					}
				}
				continue;
			}

			// A segment hosting doors is its trimmed band in spans: the door spans are blocked faces
			// carrying their opening (truth -> walkable, belief -> gated at query time), the solid
			// spans carry none. Windows leave the band solid.
			std::vector<std::vector<geometry::WallGap>>			gaps(members.size());
			std::vector<std::vector<construction::OpeningId>> gapOpenings(members.size());
			bool												anyDoor = false;
			for (std::size_t k = 0; k < graph.size(); ++k) {
				if (hostsDoor[graph[k]] == 0) {
					continue;
				}
				construction::OpeningGaps doors =
					construction::openingGaps(world, *world.getSegment(segmentIds[graph[k]]), {.doorsOnly = true, .builtOnly = true});
				gaps[k]		   = std::move(doors.gaps);
				gapOpenings[k] = std::move(doors.openings);
				anyDoor		   = anyDoor || !gaps[k].empty();
			}
			const std::vector<std::vector<geometry::WallSpan>> spans =
				anyDoor ? geometry::wallSpans(members, gaps, bands) : std::vector<std::vector<geometry::WallSpan>>{};

			for (std::size_t k = 0; k < graph.size(); ++k) {
				const std::int64_t						 provenance = static_cast<std::int64_t>(segmentIds[graph[k]]);
				std::vector<gnav::NavInputPolygon>& polygons = polygonsOfSegment[graph[k]];
				if (gaps[k].empty()) {
					if (bands.bands[k].size() >= 3) {
						polygons.push_back({bands.bands[k], true, provenance});
					}
					continue;
				}
				for (const geometry::WallSpan& span : spans[k]) {
					const std::int64_t opening =
						span.gap == geometry::kSolidSpan ? gnav::kNoOpening : static_cast<std::int64_t>(gapOpenings[k][span.gap]);
					polygons.push_back({span.ring, true, provenance, opening});
				}
			}

			// Each junction is tagged with its smallest incident wall id, so belief gates it by
			// knowing one of its walls, and lists every wall meeting there.
			for (std::size_t junctionIndex = 0; junctionIndex < bands.junctions.size(); ++junctionIndex) {
				if (bands.junctions[junctionIndex].size() < 3) {
					continue;
				}
				Junction junction{.ring = bands.junctions[junctionIndex]};
				if (junctionIndex < bands.junctionSegments.size()) {
					for (const std::size_t k : bands.junctionSegments[junctionIndex]) {
						if (k >= graph.size()) {
							continue;
						}
						const construction::SegmentId wall = segmentIds[graph[k]];
						junction.walls.push_back(wall);
						const auto id = static_cast<std::int64_t>(wall);
						if (junction.provenance == kProvenanceJunction || id < junction.provenance) {
							junction.provenance = id;
						}
					}
				}
				junctions.push_back(std::move(junction));
			}
		}

		for (std::vector<gnav::NavInputPolygon>& polygons : polygonsOfSegment) {
			std::move(polygons.begin(), polygons.end(), std::back_inserter(outPolys));
		}
		for (Junction& junction : junctions) {
			if (outJunctions != nullptr) {
				outJunctions->push_back({outPolys.size(), std::move(junction.walls)});
			}
			outPolys.push_back({std::move(junction.ring), true, junction.provenance, gnav::kNoOpening});
		}
	}

	gnav::NavInputPolygon borderRing(geometry::Vec2i64 minMm, geometry::Vec2i64 maxMm) {
		geometry::Ring ring = {
			{minMm.x, minMm.y},
			{maxMm.x, minMm.y},
			{maxMm.x, maxMm.y},
			{minMm.x, maxMm.y},
		};
		geometry::ensureCounterClockwise(ring);
		return {std::move(ring), false, kProvenanceBorder};
	}

} // namespace nav
