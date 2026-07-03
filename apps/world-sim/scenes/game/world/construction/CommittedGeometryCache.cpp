#include "CommittedGeometryCache.h"

#include <assets/ConstructionRegistry.h>
#include <construction/OpeningGeometry.h>
#include <offset/WallOffset.h>

#include <algorithm>
#include <cmath>

namespace world_sim {

	namespace {

		using engine::assets::ConstructionRegistry;
		using engine::assets::StyleColor;
		namespace ec = engine::construction;

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

		Foundation::Rect boundsOfRings(const std::vector<std::vector<Foundation::Vec2>>& rings) {
			Foundation::Rect r{0.0F, 0.0F, 0.0F, 0.0F};
			bool			 first = true;
			for (const auto& ring : rings) {
				const Foundation::Rect b = boundsOf(ring);
				if (first) {
					r = b;
					first = false;
					continue;
				}
				const float minX = std::min(r.x, b.x);
				const float minY = std::min(r.y, b.y);
				const float maxX = std::max(r.x + r.width, b.x + b.width);
				const float maxY = std::max(r.y + r.height, b.y + b.height);
				r = {minX, minY, maxX - minX, maxY - minY};
			}
			return r;
		}

		std::vector<Foundation::Vec2> dequantizeRing(const geometry::Ring& ring) {
			std::vector<Foundation::Vec2> out;
			out.reserve(ring.size());
			for (const auto& v : ring) {
				out.push_back(toWorld(v));
			}
			return out;
		}

		// Segment length in mm (its two vertex positions), or 0 if either vertex is
		// missing. The opening's clear width maps to a centerline half-extent of
		// (widthMm/2) / lengthMm, so a zero-length segment yields no intervals.
		double segmentLengthMm(const ec::ConstructionWorld& world, const ec::WallSegment& seg) {
			const ec::Vertex* v0 = world.getVertex(seg.v0);
			const ec::Vertex* v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				return 0.0;
			}
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			return std::sqrt(dx * dx + dy * dy);
		}

		// The [t0,t1] centerline intervals of a segment's openings (t in [0,1]),
		// sorted ascending and clamped to [0,1]. An opening at param t with clear
		// width w on a segment of length L spans [t - (w/2)/L, t + (w/2)/L]. Stable
		// order (openings() insertion order, then t) keeps the gap render deterministic.
		std::vector<std::pair<float, float>> openingIntervalsForSegment(const ec::ConstructionWorld& world, ec::SegmentId segmentId) {
			std::vector<std::pair<float, float>> intervals;
			const ec::WallSegment*				 seg = world.getSegment(segmentId);
			if (seg == nullptr) {
				return intervals;
			}
			const double lengthMm = segmentLengthMm(world, *seg);
			if (lengthMm <= 0.0) {
				return intervals;
			}
			for (const auto& op : world.openings()) {
				if (op.segment != segmentId) {
					continue;
				}
				const auto* type = ConstructionRegistry::Get().getOpeningType(op.type);
				if (type == nullptr || type->widthMm <= 0) {
					continue;
				}
				const float half = static_cast<float>((static_cast<double>(type->widthMm) * 0.5) / lengthMm);
				const float t0 = std::clamp(op.t - half, 0.0F, 1.0F);
				const float t1 = std::clamp(op.t + half, 0.0F, 1.0F);
				if (t1 > t0) {
					intervals.emplace_back(t0, t1);
				}
			}
			std::sort(intervals.begin(), intervals.end());
			return intervals;
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

	void CommittedGeometryCache::refresh(const engine::construction::ConstructionWorld& world) {
		if (world.version() == builtVersion_) {
			return;
		}
		rebuild(world);
		builtVersion_ = world.version();
	}

	void CommittedGeometryCache::rebuild(const engine::construction::ConstructionWorld& world) {
		foundations_.clear();
		walls_.clear();
		junctions_.clear();
		openings_.clear();
		fallbackCenterlines_.clear();
		bandsFailed_ = false;

		const auto& style = ConstructionRegistry::Get().rendering();

		// --- Foundations: dequantized ring + fan triangulation --------------
		for (const auto& f : world.foundations()) {
			if (f.ring.size() < 3) {
				continue;
			}
			FoundationGeom g;
			g.ring = dequantizeRing(f.ring);
			g.fan.reserve((g.ring.size() - 2) * 3);
			for (std::size_t i = 1; i + 1 < g.ring.size(); ++i) {
				g.fan.push_back(0);
				g.fan.push_back(static_cast<uint16_t>(i));
				g.fan.push_back(static_cast<uint16_t>(i + 1));
			}
			g.aabb = boundsOf(g.ring);
			g.matColor = materialColor(f.material, style.foundation.fallbackColor);
			g.built = (f.state == ec::FoundationState::Built);
			g.entity = f.entity;
			foundations_.push_back(std::move(g));
		}

		// --- Walls: whole-graph band resolve ---------------------------------
		// Shared integer vertices mean resolveWallBands derives every junction by
		// exact-endpoint grouping. A malformed segment (missing vertex or
		// unresolved thickness preset) is SKIPPED rather than fed as a zero-length
		// placeholder, because resolveWallBands rejects any zero-length segment up
		// front and would fail the whole graph. offsetToSeg maps offsetter indices
		// (also used by junctionSegments) back to segments() indices.
		const auto&						   segs = world.segments();
		std::vector<geometry::WallSegment> offsetSegs;
		std::vector<std::size_t>		   offsetToSeg;
		offsetSegs.reserve(segs.size());
		offsetToSeg.reserve(segs.size());
		for (std::size_t i = 0; i < segs.size(); ++i) {
			const ec::WallSegment& wseg = segs[i];
			const ec::Vertex*	   v0 = world.getVertex(wseg.v0);
			const ec::Vertex*	   v1 = world.getVertex(wseg.v1);
			std::int64_t		   halfThick = 0;
			if (const auto* preset = ConstructionRegistry::Get().getThicknessPreset(wseg.material, wseg.thicknessPreset)) {
				halfThick = preset->halfThicknessMm;
			}
			if (v0 == nullptr || v1 == nullptr || halfThick <= 0) {
				continue;
			}
			offsetSegs.push_back({v0->pos, v1->pos, halfThick});
			offsetToSeg.push_back(i);
		}

		if (!offsetSegs.empty()) {
			const geometry::WallBands bands = geometry::resolveWallBands(offsetSegs, geometry::kDefaultMiterLimit);
			if (bands.status != geometry::OffsetStatus::Ok) {
				// Reject-don't-repair: a degenerate offset means the topology fed it
				// bad input; cache bare centerlines so the render still shows the
				// walls exist, never garbage bands.
				bandsFailed_ = true;
				for (const auto& wseg : segs) {
					const ec::Vertex* v0 = world.getVertex(wseg.v0);
					const ec::Vertex* v1 = world.getVertex(wseg.v1);
					if (v0 == nullptr || v1 == nullptr) {
						continue;
					}
					fallbackCenterlines_.push_back({toWorld(v0->pos), toWorld(v1->pos)});
				}
				return;
			}

			// Per-segment bands. bands[i] corresponds to offsetSegs[i], i.e.
			// segs[offsetToSeg[i]].
			for (std::size_t i = 0; i < bands.bands.size() && i < offsetToSeg.size(); ++i) {
				const ec::WallSegment& wseg = segs[offsetToSeg[i]];
				const geometry::Ring&  ring = bands.bands[i];
				if (ring.size() < 3) {
					continue;
				}

				WallBandGeom g;
				g.matColor = materialColor(wseg.material, style.wall.fallbackColor);
				g.built = (wseg.state == ec::FoundationState::Built);
				g.entity = wseg.entity;

				// A segment hosting openings can't use the single resolved band: it
				// must show a gap where each opening sits. Replace the band with solid
				// sub-bands over the centerline runs between the gaps, computed from
				// the segment's own v0->v1 centerline. This drops the whole-graph
				// junction trim on this one segment (square cuts at the gap edges, an
				// accepted interim approximation); the junction polygons still fill
				// the corners. Segments with no openings keep the trimmed band.
				const auto intervals = openingIntervalsForSegment(world, wseg.id);
				if (intervals.empty()) {
					g.rings.push_back(dequantizeRing(ring));
				} else {
					const ec::Vertex* v0 = world.getVertex(wseg.v0);
					const ec::Vertex* v1 = world.getVertex(wseg.v1);
					std::int64_t	  halfThick = 0;
					if (const auto* preset = ConstructionRegistry::Get().getThicknessPreset(wseg.material, wseg.thicknessPreset)) {
						halfThick = preset->halfThicknessMm;
					}
					if (v0 == nullptr || v1 == nullptr || halfThick <= 0) {
						continue;
					}
					auto lerpMm = [&](float t) -> geometry::Vec2i64 {
						const double ax = static_cast<double>(v0->pos.x);
						const double ay = static_cast<double>(v0->pos.y);
						const double bx = static_cast<double>(v1->pos.x);
						const double by = static_cast<double>(v1->pos.y);
						return {
							static_cast<std::int64_t>(std::llround(ax + (bx - ax) * t)),
							static_cast<std::int64_t>(std::llround(ay + (by - ay) * t)),
						};
					};
					float runStart = 0.0F;
					auto  emitRun = [&](float a, float b) {
						 if (b - a <= 1e-4F) {
							 return;
						 }
						 const geometry::Ring sub = geometry::band(lerpMm(a), lerpMm(b), halfThick);
						 if (sub.size() < 3) {
							 return;
						 }
						 g.rings.push_back(dequantizeRing(sub));
					};
					for (const auto& iv : intervals) {
						emitRun(runStart, iv.first);
						runStart = iv.second;
					}
					emitRun(runStart, 1.0F);
				}

				if (g.rings.empty()) {
					continue;
				}
				g.aabb = boundsOfRings(g.rings);
				walls_.push_back(std::move(g));
			}

			// Junction polygons fill the corner gaps the trimmed bands leave. A
			// junction reads as built only when it has incident segments and every
			// one is built (state changes bump the version, so this is safe to bake);
			// its material is the first incident segment's, so corners read as
			// continuous material on a finished structure while a blueprint's
			// corners stay blue.
			for (std::size_t j = 0; j < bands.junctions.size(); ++j) {
				const geometry::Ring& ring = bands.junctions[j];
				if (ring.size() < 3) {
					continue;
				}
				JunctionGeom g;
				g.ring = dequantizeRing(ring);
				g.aabb = boundsOf(g.ring);
				g.built = (j < bands.junctionSegments.size() && !bands.junctionSegments[j].empty());
				std::string junctionMaterial;
				if (j < bands.junctionSegments.size()) {
					for (const std::size_t offIdx : bands.junctionSegments[j]) {
						if (offIdx >= offsetToSeg.size()) {
							continue;
						}
						const ec::WallSegment& iseg = segs[offsetToSeg[offIdx]];
						if (junctionMaterial.empty()) {
							junctionMaterial = iseg.material;
						}
						if (iseg.state != ec::FoundationState::Built) {
							g.built = false;
						}
					}
				}
				g.matColor = materialColor(junctionMaterial, style.wall.fallbackColor);
				junctions_.push_back(std::move(g));
			}
		}

		// --- Openings: oriented footprint + type-derived styling inputs -----
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
	}

} // namespace world_sim
