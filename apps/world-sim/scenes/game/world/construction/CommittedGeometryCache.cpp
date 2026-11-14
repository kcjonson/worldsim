#include "CommittedGeometryCache.h"

#include <assets/ConstructionRegistry.h>
#include <construction/OpeningGeometry.h>
#include <offset/WallOffset.h>
#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace world_sim {

	namespace {

		using assets::ConstructionRegistry;
		using assets::StyleColor;
		namespace ec = construction;

		Foundation::Color toColor(StyleColor c) {
			return {c.r, c.g, c.b, c.a};
		}

		// Material palette color (front entry) with a config fallback.
		Foundation::Color materialColor(const std::string& name, StyleColor fallback) {
			const auto* m = ConstructionRegistry::Get().getMaterial(name);
			if (m != nullptr && !m->pattern.palette.empty()) {
				const auto& c = m->pattern.palette.front();
				return {c.r / 255.0F, c.g / 255.0F, c.b / 255.0F, 1.0F};
			}
			return toColor(fallback);
		}

		Foundation::Vec2 toWorld(geometry::Vec2i64 mm) {
			const auto w = geometry::dequantize(mm);
			return {w.x, w.y};
		}

		Foundation::Rect boundsOf(const std::vector<Foundation::Vec2>& pts) {
			if (pts.empty()) {
				return {0.0F, 0.0F, 0.0F, 0.0F};
			}
			float minX = pts[0].x;
			float maxX = pts[0].x;
			float minY = pts[0].y;
			float maxY = pts[0].y;
			for (const auto& p : pts) {
				minX = std::min(minX, p.x);
				maxX = std::max(maxX, p.x);
				minY = std::min(minY, p.y);
				maxY = std::max(maxY, p.y);
			}
			return {minX, minY, maxX - minX, maxY - minY};
		}

		// Bounds of a wall's rings and its outline edges together.
		Foundation::Rect boundsOfWall(const WallBandGeom& wall) {
			std::vector<Foundation::Vec2> points;
			for (const auto& ring : wall.rings) {
				points.insert(points.end(), ring.begin(), ring.end());
			}
			for (const auto& edge : wall.outline) {
				points.insert(points.end(), edge.begin(), edge.end());
			}
			return boundsOf(points);
		}

		std::vector<Foundation::Vec2> dequantizeRing(const geometry::Ring& ring) {
			std::vector<Foundation::Vec2> out;
			out.reserve(ring.size());
			for (const auto& v : ring) {
				out.push_back(toWorld(v));
			}
			return out;
		}

		// Half the segment's thickness preset in mm, or 0 when the preset is unknown.
		std::int64_t halfThicknessMm(const ec::WallSegment& seg) {
			const auto* preset = ConstructionRegistry::Get().getThicknessPreset(seg.material, seg.thicknessPreset);
			return preset != nullptr ? preset->halfThicknessMm : 0;
		}

		// The centerline spans a segment's openings leave open, in mm from its v0
		// end: an opening at param t with clear width w on a segment of length L
		// spans t*L -/+ w/2. The offsetter clamps and merges them.
		std::vector<geometry::WallGap> openingGaps(const ec::ConstructionWorld& world, const ec::WallSegment& seg) {
			std::vector<geometry::WallGap> gaps;
			const ec::Vertex*			   v0 = world.getVertex(seg.v0);
			const ec::Vertex*			   v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				return gaps;
			}
			const double lengthMm = std::hypot(static_cast<double>(v1->pos.x - v0->pos.x), static_cast<double>(v1->pos.y - v0->pos.y));
			for (const auto& op : world.openings()) {
				if (op.segment != seg.id) {
					continue;
				}
				const auto* type = ConstructionRegistry::Get().getOpeningType(op.type);
				if (type == nullptr || type->widthMm <= 0) {
					continue;
				}
				const double center = static_cast<double>(op.t) * lengthMm;
				const double half = static_cast<double>(type->widthMm) * 0.5;
				gaps.push_back({std::llround(center - half), std::llround(center + half)});
			}
			return gaps;
		}

	} // namespace

	float footprintWidthMeters(const geometry::Ring& footprint) {
		if (footprint.size() < 4) {
			return 0.0F;
		}
		auto edgeLen = [](geometry::Vec2i64 a, geometry::Vec2i64 b) {
			const double dx = static_cast<double>(b.x - a.x);
			const double dy = static_cast<double>(b.y - a.y);
			return std::sqrt(dx * dx + dy * dy);
		};
		return static_cast<float>(
			std::max(edgeLen(footprint[0], footprint[1]), edgeLen(footprint[0], footprint[3])) / geometry::kMillimetersPerMeter
		);
	}

	void CommittedGeometryCache::refresh(const construction::ConstructionWorld& world) {
		if (world.version() == builtVersion_) {
			return;
		}
		rebuild(world);
		builtVersion_ = world.version();
	}

	void CommittedGeometryCache::rebuild(const construction::ConstructionWorld& world) {
		foundations_.clear();
		walls_.clear();
		openings_.clear();
		fallbackCenterlines_.clear();

		const auto& style = ConstructionRegistry::Get().rendering();

		// --- Foundations: dequantized ring + tessellated fill ----------------
		renderer::Tessellator	  tessellator;
		renderer::TessellatedMesh mesh;
		for (const auto& f : world.foundations()) {
			if (f.ring.size() < 3) {
				continue;
			}
			FoundationGeom g;
			g.ring = dequantizeRing(f.ring);
			// Triangulated in world space: the camera transform is affine, so the
			// triangles stay valid once the vertices are projected each frame. A ring
			// the tessellator refuses (past its 16-bit index range) is skipped, not
			// drawn wrong.
			if (!tessellator.Tessellate(renderer::VectorPath{g.ring, true}, mesh) || mesh.indices.size() < 3) {
				LOG_WARNING(Game, "Foundation #%llu (%zu vertices) failed to tessellate, not rendered", static_cast<unsigned long long>(f.id), f.ring.size());
				continue;
			}
			g.fillVertices = std::move(mesh.vertices);
			g.fillIndices = std::move(mesh.indices);
			g.aabb = boundsOf(g.ring);
			g.matColor = materialColor(f.material, style.foundation.fallbackColor);
			g.built = (f.state == ec::FoundationState::Built);
			g.entity = f.entity;
			foundations_.push_back(std::move(g));
		}

		// --- Openings: oriented footprint + type-derived styling inputs -----
		// Deliberately independent of the wall-band resolve below: openings read
		// only topology + config, so they must still render when the band
		// offsetter rejects the graph and walls fall back to centerlines.
		for (const auto& op : world.openings()) {
			const geometry::Ring footprint = ec::openingFootprint(world, op);
			if (footprint.size() < 4) {
				continue;
			}
			const auto* type = ConstructionRegistry::Get().getOpeningType(op.type);
			if (type == nullptr) {
				continue;
			}
			OpeningGeom g;
			g.footprint = dequantizeRing(footprint);
			g.aabb = boundsOf(g.footprint);
			g.matColor = materialColor(type->material, style.opening.doorFallbackColor);
			g.widthMeters = footprintWidthMeters(footprint);
			g.window = !type->pathable; // windows are not pathable; doors are
			g.built = (op.state == ec::FoundationState::Built);
			g.entity = op.entity;
			openings_.push_back(std::move(g));
		}

		// --- Walls: band resolve per connected wall graph ---------------------
		// Shared integer vertices mean resolveWallBands derives every junction by
		// exact-endpoint grouping. Each connected graph is resolved on its own, so
		// a graph the offsetter rejects falls back to centerlines without taking
		// every other wall in the world with it. A malformed segment (missing
		// vertex or unresolved thickness preset) is SKIPPED rather than fed as a
		// zero-length placeholder, which resolveWallBands would reject.
		const auto&				 segs = world.segments();
		std::vector<std::size_t> usable;
		std::vector<std::size_t> graphOf(segs.size(), 0);
		usable.reserve(segs.size());
		for (std::size_t i = 0; i < segs.size(); ++i) {
			const ec::WallSegment& wseg = segs[i];
			if (world.getVertex(wseg.v0) != nullptr && world.getVertex(wseg.v1) != nullptr &&
				halfThicknessMm(wseg) > 0) {
				graphOf[i] = i;
				usable.push_back(i);
			}
		}
		auto graphRoot = [&](std::size_t i) {
			while (graphOf[i] != i) {
				graphOf[i] = graphOf[graphOf[i]];
				i		   = graphOf[i];
			}
			return i;
		};
		std::map<geometry::Vec2i64, std::size_t> segmentAtPosition;
		for (const std::size_t i : usable) {
			for (const ec::VertexId vertexId : {segs[i].v0, segs[i].v1}) {
				const auto [it, inserted] = segmentAtPosition.emplace(world.getVertex(vertexId)->pos, i);
				if (!inserted) {
					graphOf[graphRoot(i)] = graphRoot(it->second);
				}
			}
		}
		std::map<std::size_t, std::vector<std::size_t>> graphs;
		for (const std::size_t i : usable) {
			graphs[graphRoot(i)].push_back(i);
		}

		std::set<std::vector<std::int64_t>> rejectedGraphs;
		for (const auto& [root, members] : graphs) {
			std::vector<geometry::WallSegment>			offsetSegs;
			std::vector<std::vector<geometry::WallGap>> offsetGaps;
			offsetSegs.reserve(members.size());
			offsetGaps.reserve(members.size());
			for (const std::size_t i : members) {
				const ec::WallSegment& wseg = segs[i];
				offsetSegs.push_back({world.getVertex(wseg.v0)->pos, world.getVertex(wseg.v1)->pos, halfThicknessMm(wseg)});
				offsetGaps.push_back(openingGaps(world, wseg));
			}

			const geometry::WallBands bands = geometry::resolveWallBands(offsetSegs, geometry::kDefaultMiterLimit);
			if (bands.status != geometry::OffsetStatus::Ok) {
				// Reject-don't-repair: a degenerate offset means the topology fed it
				// bad input; show this graph as bare centerlines, never garbage bands.
				// Logged once, not on every rebuild the graph survives unchanged.
				std::vector<std::int64_t> signature;
				signature.reserve(members.size() * 6);
				for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
					const geometry::WallSegment& seg = offsetSegs[memberIndex];
					signature.insert(signature.end(),
									 {static_cast<std::int64_t>(segs[members[memberIndex]].id), seg.a.x, seg.a.y, seg.b.x, seg.b.y,
									  seg.halfThicknessMm});
				}
				if (!loggedRejectedGraphs_.contains(signature)) {
					LOG_WARNING(
						Game,
						"Wall graph of %zu segment(s) rejected by the band offsetter (status %d), drawn as centerlines",
						members.size(),
						static_cast<int>(bands.status)
					);
				}
				rejectedGraphs.insert(std::move(signature));
				for (const geometry::WallSegment& seg : offsetSegs) {
					fallbackCenterlines_.push_back({toWorld(seg.a), toWorld(seg.b)});
				}
				continue;
			}
			const geometry::WallPiecesAndOutline shape = geometry::wallPiecesAndOutline(offsetSegs, offsetGaps, bands);

			// One geometry per segment: its pieces (the band cut at its openings),
			// the junctions it owns, and its share of the outline. shape.pieces and
			// the outline and junction segment indices refer to members.
			std::vector<WallBandGeom> graphWalls(members.size());
			for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
				const ec::WallSegment& wseg = segs[members[memberIndex]];
				WallBandGeom&		   wall = graphWalls[memberIndex];
				wall.matColor = materialColor(wseg.material, style.wall.fallbackColor);
				wall.built = (wseg.state == ec::FoundationState::Built);
				wall.entity = wseg.entity;
				for (const geometry::Ring& piece : shape.pieces[memberIndex]) {
					wall.rings.push_back(dequantizeRing(piece));
				}
			}
			for (const geometry::OutlineEdge& edge : shape.outline) {
				graphWalls[edge.index].outline.push_back({toWorld(edge.from), toWorld(edge.to)});
			}

			// A junction is filled as part of one incident segment, so it takes that
			// segment's material and build progress and never a style of its own. A
			// segment still being built owns it, so a corner reads finished only
			// when every wall meeting there is.
			for (std::size_t junctionIndex = 0; junctionIndex < bands.junctions.size(); ++junctionIndex) {
				const std::vector<std::size_t>& incident = bands.junctionSegments[junctionIndex];
				if (incident.empty()) {
					continue;
				}
				std::size_t owner = incident.front();
				for (const std::size_t memberIndex : incident) {
					if (segs[members[memberIndex]].state != ec::FoundationState::Built) {
						owner = memberIndex;
						break;
					}
				}
				graphWalls[owner].rings.push_back(dequantizeRing(bands.junctions[junctionIndex]));
			}

			// A segment that is all opening has no rings but can still carry outline
			// (the junction's edge facing its gap), so it is kept for that. The
			// bounds cover the outline too: an edge tagged to this segment can lie on
			// a junction another segment fills.
			for (WallBandGeom& wall : graphWalls) {
				if (wall.rings.empty() && wall.outline.empty()) {
					continue;
				}
				wall.aabb = boundsOfWall(wall);
				walls_.push_back(std::move(wall));
			}
		}
		loggedRejectedGraphs_ = std::move(rejectedGraphs);
	}

} // namespace world_sim
