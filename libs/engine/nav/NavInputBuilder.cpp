#include "NavInputBuilder.h"

#include "NavCoords.h"

#include "construction/OpeningGeometry.h"
#include "world/chunk/Chunk.h"

#include <contour/ClipRing.h>
#include <core/Vec2i64.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>
#include <utils/WorldHash.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace engine::nav {

	namespace {

		namespace gnav = geometry::nav;

		constexpr std::int64_t kTileMm	= geometry::kMillimetersPerMeter; // 1 tile == 1 m == 1000 mm
		constexpr std::int64_t kChunkMm = static_cast<std::int64_t>(world::kChunkSize) * kTileMm;

		std::int32_t floorDivChunk(std::int64_t mm) {
			const std::int64_t q = mm / kChunkMm;
			return static_cast<std::int32_t>((mm % kChunkMm != 0 && mm < 0) ? q - 1 : q);
		}

		// The part of `ring` inside `area`. Most rings of a 512 m chunk miss a sim
		// area entirely or sit wholly inside it, so the bounding box settles those
		// without walking the clip.
		std::vector<geometry::Ring> clipToArea(const geometry::Ring& ring, const geometry::RectMm& area) {
			geometry::Vec2i64 lo = ring.front();
			geometry::Vec2i64 hi = ring.front();
			for (const geometry::Vec2i64& p : ring) {
				lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
				hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
			}
			if (hi.x <= area.min.x || lo.x >= area.max.x || hi.y <= area.min.y || lo.y >= area.max.y) {
				return {};
			}
			if (lo.x >= area.min.x && hi.x <= area.max.x && lo.y >= area.min.y && hi.y <= area.max.y) {
				return {ring};
			}
			return geometry::clipRingToRect(ring, area);
		}

	} // namespace

	AreaChunkRange areaChunkRange(geometry::Vec2i64 areaCenterMm, std::int64_t areaRadiusMm) {
		return {
			{floorDivChunk(areaCenterMm.x - areaRadiusMm - kTileMm), floorDivChunk(areaCenterMm.y - areaRadiusMm - kTileMm)},
			{floorDivChunk(areaCenterMm.x + areaRadiusMm + kTileMm), floorDivChunk(areaCenterMm.y + areaRadiusMm + kTileMm)},
		};
	}

	TerrainPolygonsLookup readyTerrainPolygons(const world::ChunkManager& chunks) {
		return [&chunks](world::ChunkCoordinate coord) -> const world::ChunkTerrainPolygons* {
			const world::Chunk* chunk = chunks.getChunk(coord);
			return (chunk != nullptr && chunk->isReady()) ? &chunk->terrainPolygons() : nullptr;
		};
	}

	void appendWaterObstacles(geometry::Vec2i64 areaCenterMm, std::int64_t areaRadiusMm, const TerrainPolygonsLookup& polygonsOf,
							  std::vector<gnav::NavInputPolygon>& out) {
		if (areaRadiusMm <= 0) {
			return;
		}
		// navRings end at their chunk square, so a ring can reach far past the area.
		// Clipping to the area keeps the arrangement's input proportional to the
		// area; the clip lands its crossings exactly on the border ring's edges.
		const geometry::RectMm area{{areaCenterMm.x - areaRadiusMm, areaCenterMm.y - areaRadiusMm},
									{areaCenterMm.x + areaRadiusMm, areaCenterMm.y + areaRadiusMm}};
		const AreaChunkRange   range = areaChunkRange(areaCenterMm, areaRadiusMm);
		for (std::int32_t cy = range.min.y; cy <= range.max.y; ++cy) {
			for (std::int32_t cx = range.min.x; cx <= range.max.x; ++cx) {
				const world::ChunkTerrainPolygons* polygons = polygonsOf({cx, cy});
				if (polygons == nullptr) {
					continue;
				}
				for (const world::TerrainRing& terrain : polygons->navRings) {
					if (!terrain.blocksMovement || terrain.ring.size() < 3) {
						continue;
					}
					for (geometry::Ring& piece : clipToArea(terrain.ring, area)) {
						out.push_back({std::move(piece), true, kProvenanceWater, gnav::kNoOpening, terrain.holeCapable});
					}
				}
			}
		}
	}

	std::uint64_t waterSignature(geometry::Vec2i64 areaCenterMm, std::int64_t areaRadiusMm, const TerrainPolygonsLookup& polygonsOf) {
		const AreaChunkRange range = areaChunkRange(areaCenterMm, areaRadiusMm);
		std::uint64_t		 sig   = foundation::kFnvOffset;
		for (std::int32_t cy = range.min.y; cy <= range.max.y; ++cy) {
			for (std::int32_t cx = range.min.x; cx <= range.max.x; ++cx) {
				const world::ChunkTerrainPolygons* polygons = polygonsOf({cx, cy});
				const std::uint64_t				   coord	= (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)) << 32U) |
												  static_cast<std::uint64_t>(static_cast<std::uint32_t>(cy));
				sig = foundation::hashCombine(sig, coord);
				sig = foundation::hashCombine(sig, polygons != nullptr ? polygons->version : 0U);
			}
		}
		return sig;
	}

	std::optional<gnav::NavInputPolygon> floraRingFor(const assets::PlacedEntity& e, const assets::AssetRegistry& registry) {
		const assets::AssetDefinition* def = registry.getDefinition(e.defName);
		if (def == nullptr || !def->collision.blocks()) {
			return std::nullopt;
		}

		const float c  = std::cos(e.rotation);
		const float s  = std::sin(e.rotation);
		const float sc = e.scale;
		// Local-space point (meters) -> world mm: scale, rotate, translate by the
		// entity position (which is in tiles == meters). The single trig step is
		// rounded to mm at the boundary, matching the module's rounding policy.
		auto toWorldMm = [&](glm::vec2 local) -> geometry::Vec2i64 {
			const float lx = local.x * sc;
			const float ly = local.y * sc;
			const float wx = e.position.x + (lx * c - ly * s);
			const float wy = e.position.y + (lx * s + ly * c);
			return toMm({wx, wy});
		};

		geometry::Ring ring;
		if (def->collision.type == assets::CollisionShapeType::Rect) {
			// The rect's 4 corners (local meters), half-extents inflated outward by
			// the pad so the agent clears the trunk, then transformed -- so entity
			// rotation turns it into an oriented quad (OBB) rather than an AABB.
			const float		padMeters = static_cast<float>(kFloraColliderPadMm) / static_cast<float>(kTileMm);
			const glm::vec2 c		  = def->collision.offsetMeters;
			const float		hx		  = def->collision.halfExtentsMeters.x + padMeters;
			const float		hy		  = def->collision.halfExtentsMeters.y + padMeters;
			ring.reserve(4);
			ring.push_back(toWorldMm({c.x - hx, c.y - hy}));
			ring.push_back(toWorldMm({c.x + hx, c.y - hy}));
			ring.push_back(toWorldMm({c.x + hx, c.y + hy}));
			ring.push_back(toWorldMm({c.x - hx, c.y + hy}));
		} else if (def->collision.type == assets::CollisionShapeType::Polygon) {
			ring.reserve(def->collision.pointsMeters.size());
			for (const glm::vec2& p : def->collision.pointsMeters) {
				ring.push_back(toWorldMm(p));
			}
		}

		// A negative entity scale would flip winding; normalize so blocked rings
		// are CCW like the rest, and drop anything degenerate.
		if (ring.size() < 3) {
			return std::nullopt;
		}
		geometry::ensureCounterClockwise(ring);
		if (geometry::windingOrder(ring) == geometry::Winding::Degenerate) {
			return std::nullopt;
		}
		return gnav::NavInputPolygon{std::move(ring), true, kProvenanceTree};
	}

	std::vector<gnav::NavInputPolygon> extractFloraObstacles(const assets::SpatialIndex& index, const assets::AssetRegistry& registry) {
		std::vector<gnav::NavInputPolygon> out;

		// allEntities() returns copies; iterate in its stable order for determinism.
		std::vector<assets::PlacedEntity> entities = index.allEntities();
		for (const assets::PlacedEntity& e : entities) {
			std::optional<gnav::NavInputPolygon> poly = floraRingFor(e, registry);
			if (poly.has_value()) {
				out.push_back(std::move(*poly));
			}
		}
		return out;
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

	namespace {

		// True when every vertex of `ring` lies strictly outside the AABB on the SAME
		// side (all left of minMm.x, or all right of maxMm.x, or all below, or all
		// above). That is a sufficient (not exact) separation test: a ring crossing a
		// corner without any vertex inside still has vertices on both sides of an axis,
		// so it is kept. Cheap and never drops a ring that touches the area.
		bool ringEntirelyOutside(const geometry::Ring& ring, geometry::Vec2i64 minMm, geometry::Vec2i64 maxMm) {
			bool allLeft  = true;
			bool allRight = true;
			bool allBelow = true;
			bool allAbove = true;
			for (const geometry::Vec2i64& p : ring) {
				allLeft	 = allLeft && (p.x < minMm.x);
				allRight = allRight && (p.x > maxMm.x);
				allBelow = allBelow && (p.y < minMm.y);
				allAbove = allAbove && (p.y > maxMm.y);
			}
			return allLeft || allRight || allBelow || allAbove;
		}

	} // namespace

	gnav::NavMeshInput buildInput(geometry::Vec2i64 areaCenterMm, std::int64_t areaRadiusMm, engine::world::ChunkManager& chunks,
								  const assets::PlacementExecutor& placement, const assets::AssetRegistry& assetReg,
								  const construction::ConstructionWorld& world, const assets::ConstructionRegistry& cfg, bool includeFlora) {
		gnav::NavMeshInput input;

		const geometry::Vec2i64 minMm{areaCenterMm.x - areaRadiusMm, areaCenterMm.y - areaRadiusMm};
		const geometry::Vec2i64 maxMm{areaCenterMm.x + areaRadiusMm, areaCenterMm.y + areaRadiusMm};

		// 1) Border: the one unblocked rectangle covering the whole area.
		input.polygons.push_back(borderRing(minMm, maxMm));

		// 2) Water: the ready chunks' blocking terrain rings, clipped to the area.
		appendWaterObstacles(areaCenterMm, areaRadiusMm, readyTerrainPolygons(chunks), input.polygons);

		// 3) Flora (entities): the clearable tree/rock obstacles. Skipped when includeFlora is
		// false (the terrain-only placement mesh) so they don't read as off-mesh holes there.
		// Only the chunks overlapping the area AABB are visited, and within each only the entities
		// queryRect returns for the area's tile bounds -- never allEntities(). Chunks are visited
		// in (x,y) order and the per-chunk entities sorted by (position, defName) so the emission
		// order is stable regardless of queryRect's layout.
		if (includeFlora) {
			const AreaChunkRange range		  = areaChunkRange(areaCenterMm, areaRadiusMm);
			const float			 areaTileMinX = static_cast<float>(minMm.x) / static_cast<float>(kTileMm);
			const float			 areaTileMinY = static_cast<float>(minMm.y) / static_cast<float>(kTileMm);
			const float			 areaTileMaxX = static_cast<float>(maxMm.x) / static_cast<float>(kTileMm);
			const float			 areaTileMaxY = static_cast<float>(maxMm.y) / static_cast<float>(kTileMm);
			for (std::int32_t cy = range.min.y; cy <= range.max.y; ++cy) {
				for (std::int32_t cx = range.min.x; cx <= range.max.x; ++cx) {
					const assets::SpatialIndex* index = placement.getChunkIndex({cx, cy});
					if (index == nullptr) {
						continue;
					}
					std::vector<const assets::PlacedEntity*> hits =
						index->queryRect(areaTileMinX, areaTileMinY, areaTileMaxX, areaTileMaxY);
					std::sort(hits.begin(), hits.end(), [](const assets::PlacedEntity* l, const assets::PlacedEntity* r) {
						if (l->position.x != r->position.x) {
							return l->position.x < r->position.x;
						}
						if (l->position.y != r->position.y) {
							return l->position.y < r->position.y;
						}
						return l->defName < r->defName;
					});
					for (const assets::PlacedEntity* e : hits) {
						std::optional<gnav::NavInputPolygon> poly = floraRingFor(*e, assetReg);
						if (poly.has_value()) {
							input.polygons.push_back(std::move(*poly));
						}
					}
				}
			}
		}

		// 4) Walls: extract over the whole construction world, then drop bands/junctions
		// whose ring is entirely outside the area AABB. Door portals are kept as-is: the
		// later vision pass needs every opening it can see, and the portal list is small.
		std::vector<gnav::NavInputPolygon> wallPolys;
		extractWalls(world, cfg, wallPolys, input.doors);
		for (gnav::NavInputPolygon& p : wallPolys) {
			if (ringEntirelyOutside(p.ring, minMm, maxMm)) {
				continue;
			}
			input.polygons.push_back(std::move(p));
		}

		return input;
	}

} // namespace engine::nav
