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
#include <optional>
#include <unordered_map>
#include <utility>

namespace nav {

	namespace {

		namespace gnav = geometry::nav;

		constexpr std::int64_t kTileMm = geometry::kMillimetersPerMeter; // 1 tile == 1 m == 1000 mm

	} // namespace

	gnav::NavInputPolygon visibilityLimit(const geometry::Vec2i64& centreMm, std::int64_t rangeMm) {
		return {gnav::limitPolygon(centreMm, rangeMm), false, kProvenanceBorder};
	}

	void appendChunkWater(const world::ChunkTerrainPolygons& polygons, std::vector<gnav::NavInputPolygon>& out) {
		for (const world::TerrainRing& terrain : polygons.navRings) {
			if (!terrain.blocksMovement || terrain.ring.size() < 3) {
				continue;
			}
			out.push_back({terrain.ring, true, kProvenanceWater, gnav::kNoOpening, terrain.holeCapable});
		}
	}

	std::optional<gnav::NavInputPolygon> placedObstacleRing(const assets::PlacedEntity& e, const assets::AssetRegistry& registry) {
		geometry::Ring ring;
		if (!placedObstacleOutline(e, registry, ring)) {
			return std::nullopt;
		}
		return gnav::NavInputPolygon{std::move(ring), true, kProvenancePlaced};
	}

	std::optional<OrientedRect> placedObstacleBox(const assets::PlacedEntity& e, const assets::CollisionShape& collision) {
		if (collision.type != assets::CollisionShapeType::Rect) {
			return std::nullopt;
		}
		const float		c	= std::cos(e.rotation);
		const float		s	= std::sin(e.rotation);
		const float		pad = static_cast<float>(kPlacedObstaclePadMm) / static_cast<float>(kTileMm);
		const glm::vec2 off = collision.offsetMeters * e.scale;
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

		if (collision.type == assets::CollisionShapeType::Rect) {
			const OrientedRect box = *placedObstacleBox(e, collision);
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
				const glm::vec2 l = p * sc;
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
					  std::vector<gnav::NavInputPolygon>& outPolys, std::vector<gnav::DoorPortal>& outDoors) {
		// Gather the BUILT segments and their geometry::WallSegment in a stable order.
		// resolveWallBands returns one band per input segment in this same order, so
		// we keep a parallel index back to each SegmentId for door-gap replacement
		// and provenance tagging.
		std::vector<geometry::WallSegment>		   geoSegments;
		std::vector<construction::SegmentId>	   segmentIds;
		for (const construction::WallSegment& seg : world.segments()) {
			if (seg.state != construction::FoundationState::Built) {
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

		const geometry::WallBands bands = geometry::resolveWallBands(geoSegments, geometry::kDefaultMiterLimit);
		// On a rejected offset (degenerate/self-intersecting band) fall back to the
		// raw per-segment bands rather than dropping the walls entirely: a blocked
		// obstacle ring is better for safety than a hole in the navmesh.
		const bool bandsOk = bands.status == geometry::OffsetStatus::Ok && bands.bands.size() == geoSegments.size();

		// Map each BUILT pathable opening to the index of its hosting segment in our
		// parallel arrays, so we can swap that segment's solid band for two flanking
		// gap rings. A segment may host several openings; we cut them in turn.
		std::unordered_map<construction::SegmentId, std::size_t> segIndexById;
		for (std::size_t i = 0; i < segmentIds.size(); ++i) {
			segIndexById[segmentIds[i]] = i;
		}

		// Per-segment-index list of door cut spans (each cut splits the band along
		// the centerline). We collect, then build the flanking rings once per segment.
		struct DoorCut {
			construction::OpeningId openingId;
			std::int64_t			clearWidthMm;
			geometry::Vec2i64		jambA; // the two jamb points spanning the passage
			geometry::Vec2i64		jambB;
			float					t0; // centerline parameter range of the gap
			float					t1;
		};
		std::unordered_map<std::size_t, std::vector<DoorCut>> cutsBySeg;

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

			// Centerline parameter range of the gap, recomputed the same way the
			// footprint did (clear-width half-extent over segment length), so the
			// flanking bands meet the footprint exactly.
			const construction::WallSegment* seg = world.getSegment(op.segment);
			const construction::Vertex*		 v0	 = (seg != nullptr) ? world.getVertex(seg->v0) : nullptr;
			const construction::Vertex*		 v1	 = (seg != nullptr) ? world.getVertex(seg->v1) : nullptr;
			if (v0 == nullptr || v1 == nullptr) {
				continue;
			}
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			const double lengthMm = std::sqrt(dx * dx + dy * dy);
			if (lengthMm <= 0.0) {
				continue;
			}
			const float halfExtent = static_cast<float>((static_cast<double>(type->widthMm) * 0.5) / lengthMm);
			const float t0 = std::clamp(op.t - halfExtent, 0.0F, 1.0F);
			const float t1 = std::clamp(op.t + halfExtent, 0.0F, 1.0F);

			cutsBySeg[idxIt->second].push_back({op.id, type->widthMm, jambA, jambB, t0, t1});
			outDoors.push_back({static_cast<std::int64_t>(op.id), jambA, jambB, type->widthMm});
		}

		// Emit bands (with door gaps applied) and junction rings.
		for (std::size_t i = 0; i < geoSegments.size(); ++i) {
			const geometry::WallSegment& gs = geoSegments[i];
			const std::int64_t			 prov = static_cast<std::int64_t>(segmentIds[i]);

			auto cutIt = cutsBySeg.find(i);
			if (!bandsOk || cutIt == cutsBySeg.end()) {
				// No door cut on this segment: emit the trimmed band (or, on offset
				// failure, the raw band).
				geometry::Ring ring = bandsOk ? bands.bands[i] : geometry::band(gs.a, gs.b, gs.halfThicknessMm);
				if (ring.size() >= 3) {
					outPolys.push_back({std::move(ring), true, prov});
				}
				continue;
			}

			// Door(s) on this segment: emit the FULL band footprint, split into solid
			// sub-spans and door-span sub-spans, all tagged with this segment's id.
			// Belief gating happens at query time, not here: the door span is a blocked
			// face carrying its openingId (truth -> walkable, belief -> gated), the
			// solid spans carry no opening (always block when the wall is known). The
			// gap is no longer physically cut, so an agent who has not seen this wall
			// can path straight through its whole footprint. Sub-bands share the exact
			// rounded jamb cross-edges (same centerline direction + half-thickness), so
			// they tile the segment band with no gap and no overlap.
			std::vector<DoorCut> cuts = cutIt->second;
			std::sort(cuts.begin(), cuts.end(), [](const DoorCut& l, const DoorCut& r) { return l.t0 < r.t0; });

			auto lerp = [&](float t) -> geometry::Vec2i64 {
				const double ax = static_cast<double>(gs.a.x);
				const double ay = static_cast<double>(gs.a.y);
				const double bx = static_cast<double>(gs.b.x);
				const double by = static_cast<double>(gs.b.y);
				return {static_cast<std::int64_t>(std::llround(ax + (bx - ax) * t)),
						static_cast<std::int64_t>(std::llround(ay + (by - ay) * t))};
			};

			auto emitSpan = [&](float ta, float tb, std::int64_t openingId) {
				if (tb <= ta + 1e-6F) {
					return;
				}
				geometry::Ring ring = geometry::band(lerp(ta), lerp(tb), gs.halfThicknessMm);
				if (ring.size() >= 3 && geometry::windingOrder(ring) != geometry::Winding::Degenerate) {
					outPolys.push_back({std::move(ring), true, prov, openingId});
				}
			};

			float cursor = 0.0F;
			for (const DoorCut& cut : cuts) {
				emitSpan(cursor, cut.t0, gnav::kNoOpening);						// solid flank/between-door span
				emitSpan(cut.t0, cut.t1, static_cast<std::int64_t>(cut.openingId)); // pathable door span
				cursor = std::max(cursor, cut.t1);
			}
			emitSpan(cursor, 1.0F, gnav::kNoOpening); // trailing solid span
		}

		if (bandsOk) {
			// Tag each junction with a representative INCIDENT wall segment id (the
			// smallest, chosen deterministically) so belief gates the junction by
			// knowing one of its walls -- a junction the agent has not seen is then
			// absent like its walls, consistent with the wall rule. junctionSegments[i]
			// is aligned with junctions[i] and holds caller indices into segmentIds.
			for (std::size_t ji = 0; ji < bands.junctions.size(); ++ji) {
				const geometry::Ring& j = bands.junctions[ji];
				if (j.size() < 3) {
					continue;
				}
				std::int64_t prov = kProvenanceJunction; // fallback: always-block sentinel
				if (ji < bands.junctionSegments.size()) {
					for (std::size_t segIdx : bands.junctionSegments[ji]) {
						if (segIdx < segmentIds.size()) {
							const std::int64_t id = static_cast<std::int64_t>(segmentIds[segIdx]);
							if (prov == kProvenanceJunction || id < prov) {
								prov = id;
							}
						}
					}
				}
				outPolys.push_back({j, true, prov, gnav::kNoOpening});
			}
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
