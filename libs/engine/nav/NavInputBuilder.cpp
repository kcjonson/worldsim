#include "NavInputBuilder.h"

#include "NavCoords.h"

#include "construction/OpeningGeometry.h"

#include <core/Vec2i64.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

namespace engine::nav {

	namespace {

		namespace gnav = geometry::nav;

		constexpr std::int64_t kTileMm		   = geometry::kMillimetersPerMeter; // 1 tile == 1 m == 1000 mm
		constexpr std::int64_t kSimplifyEpsMm  = 500;						   // collinear-run collapse tolerance for water loops
		constexpr std::int64_t kMinLoopAreaMm2 = kTileMm * kTileMm / 4;		   // drop sub-tile slivers (< 1/4 tile)

		// --- Marching-squares loop stitching ------------------------------------

		// A directed boundary edge between a water tile and a non-water neighbor,
		// oriented so the water cell is on its LEFT. Walking these head-to-tail
		// closes into loops; outer water boundaries come out CCW, holes (land
		// islands) come out CW, with no extra orientation work.
		struct DirEdge {
			geometry::Vec2i64 from;
			geometry::Vec2i64 to;
		};

		// Doubled signed area of a tile-space ring (shoelace). int64 is ample at
		// tile resolution (a 512-wide chunk maxes at ~512*512 per term).
		std::int64_t signedAreaDoubledTiles(const std::vector<geometry::Vec2i64>& ring) {
			std::int64_t acc = 0;
			const std::size_t n = ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				const geometry::Vec2i64& a = ring[i];
				const geometry::Vec2i64& b = ring[(i + 1) % n];
				acc += a.x * b.y - b.x * a.y;
			}
			return acc;
		}

		// Stitch directed edges into closed loops. Each vertex has exactly one
		// outgoing edge on a clean water boundary (cells are unit squares, no
		// shared corners with ambiguity once edges carry direction), so a simple
		// from->to chain walk recovers every loop deterministically.
		std::vector<std::vector<geometry::Vec2i64>> stitchLoops(std::vector<DirEdge>& edges) {
			std::map<geometry::Vec2i64, std::vector<std::size_t>> outgoing; // from -> edge indices
			for (std::size_t i = 0; i < edges.size(); ++i) {
				outgoing[edges[i].from].push_back(i);
			}

			std::vector<bool>							 used(edges.size(), false);
			std::vector<std::vector<geometry::Vec2i64>>	 loops;

			for (std::size_t start = 0; start < edges.size(); ++start) {
				if (used[start]) {
					continue;
				}
				std::vector<geometry::Vec2i64> loop;
				std::size_t					   cur = start;
				bool						   closed = false;
				while (!used[cur]) {
					used[cur] = true;
					loop.push_back(edges[cur].from);
					const geometry::Vec2i64 next = edges[cur].to;

					// Find an unused outgoing edge from `next`. The saddle case (two
					// outgoing edges at a shared corner) is resolved by preferring the
					// edge that turns to keep water consistently on the left: pick the
					// one whose direction continues the boundary without crossing into
					// the diagonal-opposite cell. With unit-square edges that reduces
					// to "take the first unused", which is deterministic given the
					// stable edge ordering and never strands an edge.
					auto it = outgoing.find(next);
					std::size_t pick = edges.size();
					if (it != outgoing.end()) {
						for (std::size_t cand : it->second) {
							if (!used[cand]) {
								pick = cand;
								break;
							}
						}
					}
					if (pick == edges.size()) {
						closed = (next == loop.front());
						break;
					}
					cur = pick;
				}
				// Keep only loops that closed back to their start. An open chain (a
				// walk that stranded before returning) is not a valid ring and would
				// give meaningless area/orientation downstream.
				if (closed && loop.size() >= 3) {
					loops.push_back(std::move(loop));
				}
			}
			return loops;
		}

	} // namespace

	std::vector<gnav::NavInputPolygon> extractWaterObstacles(int width, int height, const std::function<bool(int, int)>& isWater,
															 geometry::Vec2i64 originMm) {
		auto water = [&](int x, int y) -> bool {
			if (x < 0 || y < 0 || x >= width || y >= height) {
				return false; // out of bounds is land: closes loops at the grid edge
			}
			return isWater(x, y);
		};

		// Emit the unit boundary edges of every water tile, oriented water-on-left
		// (the single-tile CCW boundary). Tile (x,y) covers [x,x+1]x[y,y+1] in tile
		// space, +y up.
		std::vector<DirEdge> edges;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (!water(x, y)) {
					continue;
				}
				const std::int64_t x0 = x;
				const std::int64_t y0 = y;
				const std::int64_t x1 = x + 1;
				const std::int64_t y1 = y + 1;
				if (!water(x, y - 1)) {
					edges.push_back({{x0, y0}, {x1, y0}}); // bottom: ->+x
				}
				if (!water(x + 1, y)) {
					edges.push_back({{x1, y0}, {x1, y1}}); // right: ->+y
				}
				if (!water(x, y + 1)) {
					edges.push_back({{x1, y1}, {x0, y1}}); // top: ->-x
				}
				if (!water(x - 1, y)) {
					edges.push_back({{x0, y1}, {x0, y0}}); // left: ->-y
				}
			}
		}

		std::vector<std::vector<geometry::Vec2i64>> tileLoops = stitchLoops(edges);

		std::vector<gnav::NavInputPolygon> out;
		for (std::vector<geometry::Vec2i64>& tileLoop : tileLoops) {
			// Drop sub-tile slivers before mapping to mm.
			const std::int64_t area2 = std::llabs(signedAreaDoubledTiles(tileLoop)) * (kTileMm * kTileMm);
			if (area2 / 2 < kMinLoopAreaMm2) {
				continue;
			}

			// Tile coords -> world mm. The winding established in tile space (CCW
			// outer, CW hole) is preserved by an axis-aligned positive scale.
			geometry::Ring ring;
			ring.reserve(tileLoop.size());
			for (const geometry::Vec2i64& t : tileLoop) {
				ring.push_back({originMm.x + t.x * kTileMm, originMm.y + t.y * kTileMm});
			}
			geometry::simplifyRing(ring, kSimplifyEpsMm);
			if (ring.size() < 3) {
				continue;
			}
			// holeCapable: water bodies emit a CCW outer boundary plus CW land-island
			// holes as separate blocked rings; face classification uses even-odd
			// containment parity so a land island inside water stays walkable floor.
			out.push_back({std::move(ring), true, kProvenanceWater, gnav::kNoOpening, true});
		}
		return out;
	}

	std::vector<gnav::NavInputPolygon> extractWaterObstacles(const world::Chunk& chunk) {
		const geometry::Vec2i64 originMm = chunkOriginMm(chunk.coordinate());
		auto					isWater	 = [&chunk](int x, int y) -> bool {
			   const world::TileData& tile = chunk.getTile(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
			   return tile.surface == world::Surface::Water || world::isWater(tile.primaryBiome);
		};
		return extractWaterObstacles(world::kChunkSize, world::kChunkSize, isWater, originMm);
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

		// Area tile span (mm -> tiles, floor min / ceil max). The marching-squares pass
		// runs over [tileMin-1, tileMax+1) so water touching the area edge still closes
		// its loop against the land just outside (out-of-grid reads as land).
		const std::int64_t tileMinX = static_cast<std::int64_t>(std::floor(static_cast<double>(minMm.x) / static_cast<double>(kTileMm))) - 1;
		const std::int64_t tileMinY = static_cast<std::int64_t>(std::floor(static_cast<double>(minMm.y) / static_cast<double>(kTileMm))) - 1;
		const std::int64_t tileMaxX = static_cast<std::int64_t>(std::ceil(static_cast<double>(maxMm.x) / static_cast<double>(kTileMm))) + 1;
		const std::int64_t tileMaxY = static_cast<std::int64_t>(std::ceil(static_cast<double>(maxMm.y) / static_cast<double>(kTileMm))) + 1;
		const int width	 = static_cast<int>(tileMaxX - tileMinX);
		const int height = static_cast<int>(tileMaxY - tileMinY);

		// 2) Water: drive the area-generic marching-squares extractor over the tile
		// span. The predicate maps a local (x,y) into a GLOBAL tile, finds its chunk via
		// worldToChunk, and reads the tile; a missing or not-ready chunk reads as land so
		// the loop still closes (no obstacle invented over unloaded terrain).
		if (width > 0 && height > 0) {
			auto isWater = [&](int x, int y) -> bool {
				const std::int64_t gx = tileMinX + static_cast<std::int64_t>(x);
				const std::int64_t gy = tileMinY + static_cast<std::int64_t>(y);
				const engine::world::WorldPosition wp{static_cast<float>(gx), static_cast<float>(gy)};
				const engine::world::ChunkCoordinate coord = engine::world::worldToChunk(wp);
				const engine::world::Chunk*			 chunk = chunks.getChunk(coord);
				if (chunk == nullptr || !chunk->isReady()) {
					return false;
				}
				const auto [lx, ly] = engine::world::worldToLocalTile(wp);
				const engine::world::TileData& tile = chunk->getTile(lx, ly);
				return tile.surface == engine::world::Surface::Water || engine::world::isWater(tile.primaryBiome);
			};
			const geometry::Vec2i64 originMm{tileMinX * kTileMm, tileMinY * kTileMm};
			std::vector<gnav::NavInputPolygon> waterPolys = extractWaterObstacles(width, height, isWater, originMm);
			for (gnav::NavInputPolygon& p : waterPolys) {
				input.polygons.push_back(std::move(p));
			}
		}

		// 3) Flora (entities): the clearable tree/rock obstacles. Skipped when includeFlora is
		// false (the terrain-only placement mesh) so they don't read as off-mesh holes there.
		// Only the chunks overlapping the area AABB are visited, and within each only the entities
		// queryRect returns for the area's tile bounds -- never allEntities(). Chunks are visited
		// in (x,y) order and the per-chunk entities sorted by (position, defName) so the emission
		// order is stable regardless of queryRect's layout.
		if (includeFlora) {
			const engine::world::ChunkCoordinate cMin =
				engine::world::worldToChunk({static_cast<float>(tileMinX), static_cast<float>(tileMinY)});
			const engine::world::ChunkCoordinate cMax =
				engine::world::worldToChunk({static_cast<float>(tileMaxX), static_cast<float>(tileMaxY)});
			const float areaTileMinX = static_cast<float>(minMm.x) / static_cast<float>(kTileMm);
			const float areaTileMinY = static_cast<float>(minMm.y) / static_cast<float>(kTileMm);
			const float areaTileMaxX = static_cast<float>(maxMm.x) / static_cast<float>(kTileMm);
			const float areaTileMaxY = static_cast<float>(maxMm.y) / static_cast<float>(kTileMm);
			for (std::int32_t cy = cMin.y; cy <= cMax.y; ++cy) {
				for (std::int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
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
