#include "TerrainDistanceField.h"

#include <core/Int128.h>
#include <polygon/Polygon.h>

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace engine::world {

	namespace {

		using geometry::Vec2i64;
		using Field = TerrainDistanceField;

		constexpr double  kMmPerM	  = 1000.0;
		constexpr int64_t kSdfNearMm  = static_cast<int64_t>(Field::kSdfNearM * kMmPerM);
		constexpr int64_t kBucketMm	  = 4000;
		// One extra cell per side, so a gutter texel half a near texel outside the
		// region still finds every edge within kSdfNearM of it.
		constexpr int32_t kBucketsPerSide = static_cast<int32_t>(Field::kRegionMm / kBucketMm) + 2;
		constexpr int32_t kNearLatticeSize = Field::kTilesPerSide * Field::kNearTileTexels;
		// A near tile is allocated within kSdfNearM plus one texel of a shore edge.
		constexpr double kNearTileReachMm = static_cast<double>(kSdfNearMm + Field::kNearTexelMm);
		// Consecutive thalweg segments bucketed as one entry: a 0.5 m thalweg under
		// a 55 m river would otherwise land each segment in thousands of cells.
		constexpr uint32_t kThalwegRunSegments = 16;
		// Slack on the thalweg run bound, well above the bound's rounding error, so
		// pruning never drops a candidate that could win or tie.
		constexpr double kPruneSlackMm = 1.0;

		int64_t floorDiv(int64_t a, int64_t b) {
			const int64_t q = a / b;
			return (a % b != 0 && a < 0) ? q - 1 : q;
		}

		int64_t ceilDiv(int64_t a, int64_t b) {
			return -floorDiv(-a, b);
		}

		// glm rounds to nearest with ties away from zero (IEEE would round ties to
		// even); either way the result is a pure function of the float.
		uint16_t toHalf(double v) {
			return glm::packHalf1x16(static_cast<float>(v));
		}

		// How far v lies outside [lo, hi]; zero inside.
		int64_t outside(int64_t v, int64_t lo, int64_t hi) {
			return v < lo ? lo - v : (v > hi ? v - hi : 0);
		}

		double lerp(double a, double b, double t) {
			return a + (b - a) * t;
		}

		struct SegmentPoint {
			double distanceMm;
			double t;
		};

		// Closest point of [a, b] to p. Differences of integer mm are exact in
		// double, so the result depends only on the three points.
		SegmentPoint closestOnSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			const double abx  = static_cast<double>(b.x - a.x);
			const double aby  = static_cast<double>(b.y - a.y);
			const double apx  = static_cast<double>(p.x - a.x);
			const double apy  = static_cast<double>(p.y - a.y);
			const double len2 = abx * abx + aby * aby;
			const double t	  = len2 > 0.0 ? std::clamp((apx * abx + apy * aby) / len2, 0.0, 1.0) : 0.0;
			const double dx	  = apx - abx * t;
			const double dy	  = apy - aby * t;
			return {std::sqrt(dx * dx + dy * dy), t};
		}

		// Exact-enough distance between segment [a, b] and the closed box [lo, hi]:
		// zero when they meet, else attained at a segment endpoint or a box corner.
		double segmentBoxDistanceMm(const Vec2i64& a, const Vec2i64& b, const Vec2i64& lo, const Vec2i64& hi) {
			// Liang-Barsky: does the segment enter the box?
			const double dx	  = static_cast<double>(b.x - a.x);
			const double dy	  = static_cast<double>(b.y - a.y);
			double		 tMin = 0.0;
			double		 tMax = 1.0;
			bool		 hit  = true;
			auto		 clip = [&](double p, double q) {
				if (p == 0.0) {
					hit = hit && q >= 0.0;
					return;
				}
				const double r = q / p;
				if (p < 0.0) {
					tMin = std::max(tMin, r);
				} else {
					tMax = std::min(tMax, r);
				}
			};
			clip(-dx, static_cast<double>(a.x - lo.x));
			clip(dx, static_cast<double>(hi.x - a.x));
			clip(-dy, static_cast<double>(a.y - lo.y));
			clip(dy, static_cast<double>(hi.y - a.y));
			if (hit && tMin <= tMax) {
				return 0.0;
			}
			auto pointBox = [&lo, &hi](const Vec2i64& p) {
				const double ox = static_cast<double>(outside(p.x, lo.x, hi.x));
				const double oy = static_cast<double>(outside(p.y, lo.y, hi.y));
				return std::sqrt(ox * ox + oy * oy);
			};
			double best = std::min(pointBox(a), pointBox(b));
			for (const Vec2i64& corner : {lo, Vec2i64{hi.x, lo.y}, hi, Vec2i64{lo.x, hi.y}}) {
				best = std::min(best, closestOnSegment(corner, a, b).distanceMm);
			}
			return best;
		}

		// Items binned into kBucketMm cells over the bake region plus one cell per
		// side, each into every cell its box touches, in item order, so each cell's
		// list is the same whatever else a chunk holds.
		class BucketGrid {
		  public:
			explicit BucketGrid(Vec2i64 regionOrigin)
				: origin{regionOrigin.x - kBucketMm, regionOrigin.y - kBucketMm} {}

			void add(uint32_t item, Vec2i64 lo, Vec2i64 hi) {
				const int64_t x0 = floorDiv(lo.x - origin.x, kBucketMm);
				const int64_t y0 = floorDiv(lo.y - origin.y, kBucketMm);
				const int64_t x1 = floorDiv(hi.x - origin.x, kBucketMm);
				const int64_t y1 = floorDiv(hi.y - origin.y, kBucketMm);
				if (x1 < 0 || y1 < 0 || x0 >= kBucketsPerSide || y0 >= kBucketsPerSide) {
					return;
				}
				for (int64_t y = std::max<int64_t>(y0, 0); y <= std::min<int64_t>(y1, kBucketsPerSide - 1); ++y) {
					for (int64_t x = std::max<int64_t>(x0, 0); x <= std::min<int64_t>(x1, kBucketsPerSide - 1); ++x) {
						pairs.emplace_back(static_cast<uint32_t>(y * kBucketsPerSide + x), item);
					}
				}
			}

			// Counting sort by cell; stable, so each cell keeps item order.
			void finish() {
				constexpr size_t kCells = static_cast<size_t>(kBucketsPerSide) * kBucketsPerSide;
				start.assign(kCells + 1, 0);
				for (const auto& [cell, item] : pairs) {
					++start[cell + 1];
				}
				for (size_t c = 0; c < kCells; ++c) {
					start[c + 1] += start[c];
				}
				items.resize(pairs.size());
				std::vector<uint32_t> cursor(start.begin(), start.end() - 1);
				for (const auto& [cell, item] : pairs) {
					items[cursor[cell]++] = item;
				}
				pairs = {};
			}

			[[nodiscard]] std::span<const uint32_t> at(const Vec2i64& p) const {
				const int64_t x = floorDiv(p.x - origin.x, kBucketMm);
				const int64_t y = floorDiv(p.y - origin.y, kBucketMm);
				if (x < 0 || y < 0 || x >= kBucketsPerSide || y >= kBucketsPerSide) {
					return {};
				}
				const size_t cell = static_cast<size_t>(y * kBucketsPerSide + x);
				return {items.data() + start[cell], items.data() + start[cell + 1]};
			}

		  private:
			Vec2i64									 origin;
			std::vector<std::pair<uint32_t, uint32_t>> pairs{};
			std::vector<uint32_t>					 start{};
			std::vector<uint32_t>					 items{};
		};

		bool flagged(const TerrainRing& ring, size_t i, uint8_t flag) {
			return (ring.profiles[i].flags & flag) != 0;
		}

		bool isSyntheticEdge(const TerrainRing& ring, size_t i) {
			return flagged(ring, i, ShoreProfile::kFlagSynthetic);
		}

		// D7 step 6: the butt edge between the two cut vertices is inside the river.
		bool isCutEdge(const TerrainRing& ring, size_t i) {
			return flagged(ring, i, ShoreProfile::kFlagFordableCut) &&
				   flagged(ring, (i + 1) % ring.ring.size(), ShoreProfile::kFlagFordableCut);
		}

		// ============ Rings: shore edges, vertices, containment ============

		struct ShoreEdge {
			Vec2i64 a; // a < b, so an edge's distance never depends on its direction
			Vec2i64 b;
			uint8_t water;
		};

		struct ShoreVertex {
			Vec2i64	  p;
			ByteTexel profile;
		};

		struct Crossing {
			int64_t	 threshold; // smallest texel x at or right of the crossing
			uint32_t ring;
		};

		struct RingInfo {
			geometry::Int128 area2;
			uint8_t			 water;
			bool			 solid;
		};

		class Rings {
		  public:
			Rings(const std::vector<TerrainRing>& rings, Vec2i64 regionOrigin)
				: edgeGrid(regionOrigin),
				  vertexGrid(regionOrigin),
				  bandOrigin(regionOrigin.y - kBucketMm) {
				constexpr int32_t kBands = kBucketsPerSide;
				std::vector<std::pair<uint32_t, uint32_t>> bandPairs;
				for (uint32_t r = 0; r < rings.size(); ++r) {
					const TerrainRing& ring = rings[r];
					const size_t	   n	= ring.ring.size();
					if (n < 3 || ring.profiles.size() != n) {
						continue;
					}
					const geometry::Int128 area2 = geometry::signedAreaDoubled(ring.ring);
					info.push_back({area2.sign() < 0 ? -area2 : area2, static_cast<uint8_t>(ring.water), !ring.holeCapable});
					const auto slot = static_cast<uint32_t>(info.size() - 1);
					for (size_t i = 0; i < n; ++i) {
						const Vec2i64& a = ring.ring[i];
						const Vec2i64& b = ring.ring[(i + 1) % n];
						if (a.y != b.y) {
							const int64_t lo = floorDiv(std::min(a.y, b.y) - bandOrigin, kBucketMm);
							const int64_t hi = floorDiv(std::max(a.y, b.y) - 1 - bandOrigin, kBucketMm);
							for (int64_t band = std::max<int64_t>(lo, 0); band <= std::min<int64_t>(hi, kBands - 1); ++band) {
								bandPairs.emplace_back(static_cast<uint32_t>(band), static_cast<uint32_t>(crossEdges.size()));
							}
							crossEdges.push_back({a, b, slot});
						}
						if (!isSyntheticEdge(ring, i) && !isCutEdge(ring, i)) {
							const auto index = static_cast<uint32_t>(edges.size());
							edges.push_back({std::min(a, b), std::max(a, b), static_cast<uint8_t>(ring.water)});
							edgeGrid.add(
								index,
								{std::min(a.x, b.x) - kSdfNearMm, std::min(a.y, b.y) - kSdfNearMm},
								{std::max(a.x, b.x) + kSdfNearMm, std::max(a.y, b.y) + kSdfNearMm}
							);
						}
						if (!isSyntheticEdge(ring, i) && !isSyntheticEdge(ring, (i + n - 1) % n)) {
							const ShoreProfile& p	  = ring.profiles[i];
							const auto			index = static_cast<uint32_t>(vertices.size());
							vertices.push_back({a, {p.slope, p.exposure, p.sand, p.mud}});
							vertexGrid.add(index, {a.x - kSdfNearMm, a.y - kSdfNearMm}, {a.x + kSdfNearMm, a.y + kSdfNearMm});
						}
					}
				}
				edgeGrid.finish();
				vertexGrid.finish();

				bandStart.assign(static_cast<size_t>(kBands) + 1, 0);
				for (const auto& [band, edge] : bandPairs) {
					++bandStart[band + 1];
				}
				for (size_t b = 0; b < static_cast<size_t>(kBands); ++b) {
					bandStart[b + 1] += bandStart[b];
				}
				bandEdges.resize(bandPairs.size());
				std::vector<uint32_t> cursor(bandStart.begin(), bandStart.end() - 1);
				for (const auto& [band, edge] : bandPairs) {
					bandEdges[cursor[band]++] = edge;
				}
			}

			[[nodiscard]] bool empty() const { return info.empty(); }
			[[nodiscard]] bool hasShoreVertices() const { return !vertices.empty(); }
			[[nodiscard]] size_t ringCount() const { return info.size(); }
			[[nodiscard]] const RingInfo& ring(uint32_t slot) const { return info[slot]; }

			// Every ring edge crossing the row y = yMm (half-open in y, so each closed
			// ring crosses it an even number of times), sorted by threshold. A texel
			// center (x, yMm) has crossed a crossing when x >= threshold, so its
			// crossing count per ring is the exact integer point-in-polygon test off
			// the boundary.
			void crossingsAt(int64_t yMm, std::vector<Crossing>& out) const {
				out.clear();
				const int64_t band = floorDiv(yMm - bandOrigin, kBucketMm);
				if (band < 0 || band >= kBucketsPerSide) {
					return;
				}
				for (uint32_t k = bandStart[static_cast<size_t>(band)]; k < bandStart[static_cast<size_t>(band) + 1]; ++k) {
					const CrossEdge& e = crossEdges[bandEdges[k]];
					if ((e.a.y > yMm) == (e.b.y > yMm)) {
						continue;
					}
					int64_t num = (yMm - e.a.y) * (e.b.x - e.a.x);
					int64_t den = e.b.y - e.a.y;
					if (den < 0) {
						num = -num;
						den = -den;
					}
					out.push_back({e.a.x + ceilDiv(num, den), e.ring});
				}
				std::sort(out.begin(), out.end(), [](const Crossing& l, const Crossing& r) {
					return std::tie(l.threshold, l.ring) < std::tie(r.threshold, r.ring);
				});
			}

			struct Nearest {
				double	distanceMm;
				uint8_t water;
			};

			// Nearest shore edge within kSdfNearM of p; ties go to the lower WaterKind.
			// `hint` is the previous texel's nearest edge: trying it first tightens the
			// bound the rest are pruned against (by their box, exact in integers),
			// without changing the answer.
			[[nodiscard]] std::optional<Nearest> nearestEdge(const Vec2i64& p, uint32_t& hint) const {
				std::optional<Nearest> best;
				double				   bound	 = static_cast<double>(kSdfNearMm);
				uint32_t			   bestIndex = hint;
				auto				   tryEdge	 = [&](uint32_t index) {
					  const ShoreEdge& e  = edges[index];
					  const int64_t	   ox = outside(p.x, e.a.x, e.b.x); // a < b, so a.x <= b.x
					  const int64_t	   oy = outside(p.y, std::min(e.a.y, e.b.y), std::max(e.a.y, e.b.y));
					  const double	   reach = bound + kPruneSlackMm;
					  if (static_cast<double>(ox * ox + oy * oy) > reach * reach) {
						  return;
					  }
					  const double d = closestOnSegment(p, e.a, e.b).distanceMm;
					  if (d > static_cast<double>(kSdfNearMm)) {
						  return;
					  }
					  if (!best || d < best->distanceMm || (d == best->distanceMm && e.water < best->water)) {
						  best		= Nearest{d, e.water};
						  bound		= d;
						  bestIndex = index;
					  }
				};
				if (hint < edges.size()) {
					tryEdge(hint);
				}
				for (const uint32_t index : edgeGrid.at(p)) {
					if (index != hint) {
						tryEdge(index);
					}
				}
				hint = bestIndex;
				return best;
			}

			// Profile of the nearest shore vertex within kSdfNearM of p, exact in
			// integer mm^2; ties by position, then profile bytes.
			[[nodiscard]] ByteTexel nearestProfile(const Vec2i64& p) const {
				constexpr int64_t kReach2 = kSdfNearMm * kSdfNearMm;
				const ShoreVertex* best	  = nullptr;
				int64_t			   bestD2 = 0;
				auto			   key	  = [](const ShoreVertex& v) {
					   return std::tuple(v.p, v.profile.r, v.profile.g, v.profile.b, v.profile.a);
				};
				for (const uint32_t index : vertexGrid.at(p)) {
					const ShoreVertex& v  = vertices[index];
					const int64_t	   dx = v.p.x - p.x;
					const int64_t	   dy = v.p.y - p.y;
					const int64_t	   d2 = dx * dx + dy * dy;
					if (d2 > kReach2) {
						continue;
					}
					if (best == nullptr || d2 < bestD2 || (d2 == bestD2 && key(v) < key(*best))) {
						best   = &v;
						bestD2 = d2;
					}
				}
				return best == nullptr ? ByteTexel{} : best->profile;
			}

			[[nodiscard]] const std::vector<ShoreEdge>& shoreEdges() const { return edges; }

		  private:
			struct CrossEdge {
				Vec2i64	 a;
				Vec2i64	 b;
				uint32_t ring;
			};

			std::vector<RingInfo>			info{};
			std::vector<ShoreEdge>			edges{};
			std::vector<ShoreVertex>		vertices{};
			std::vector<CrossEdge>			crossEdges{};
			BucketGrid						edgeGrid;
			BucketGrid						vertexGrid;
			int64_t							bandOrigin;
			std::vector<uint32_t>			bandStart{};
			std::vector<uint32_t>			bandEdges{};
		};

		// Walks one row's crossings left to right, tracking which rings contain the
		// current texel center: even-odd over all Waterline rings, union over the
		// solid Channel and Pond rings (D9's classification).
		class RowContainment {
		  public:
			RowContainment(const Rings& rings, const std::vector<Crossing>& crossings)
				: rings(rings),
				  crossings(crossings),
				  inside(rings.ringCount(), 0) {}

			void advanceTo(int64_t xMm) {
				while (next < crossings.size() && crossings[next].threshold <= xMm) {
					const uint32_t slot = crossings[next].ring;
					inside[slot] ^= 1U;
					if (rings.ring(slot).solid) {
						solidInside += inside[slot] != 0 ? 1 : -1;
					} else {
						waterlineParity ^= 1U;
					}
					kindDirty = true;
					++next;
				}
			}

			[[nodiscard]] bool water() const { return solidInside > 0 || waterlineParity != 0; }

			// WaterKind of the containing ring: the smallest solid ring when in one,
			// else the smallest Waterline ring (the innermost, whose parity makes the
			// point water). Ties go to the lower WaterKind.
			[[nodiscard]] uint8_t containingWater() {
				if (!kindDirty) {
					return kind;
				}
				kindDirty				  = false;
				const bool		wantSolid = solidInside > 0;
				const RingInfo* best	  = nullptr;
				for (uint32_t slot = 0; slot < inside.size(); ++slot) {
					const RingInfo& r = rings.ring(slot);
					if (inside[slot] == 0 || r.solid != wantSolid) {
						continue;
					}
					if (best == nullptr || r.area2 < best->area2 || (r.area2 == best->area2 && r.water < best->water)) {
						best = &r;
					}
				}
				kind = best == nullptr ? 0 : best->water;
				return kind;
			}

		  private:
			const Rings&				 rings;
			const std::vector<Crossing>& crossings;
			std::vector<uint8_t>		 inside;
			size_t						 next			 = 0;
			int							 solidInside	 = 0;
			uint8_t						 waterlineParity = 0;
			bool						 kindDirty		 = true;
			uint8_t						 kind			 = 0;
		};

		// ============ Thalwegs: G and the channel frame ============

		struct Anchor {
			uint32_t seg;
			double	 t;
		};

		// Per path, what the arc-length anchoring needs (see bake() in the header).
		struct PathArc {
			std::vector<double>	  segLenM{};
			std::vector<Anchor>	  anchors{};	  // sorted by (seg, t)
			std::vector<uint32_t> segAnchorStart{}; // anchors on seg k: [segAnchorStart[k], segAnchorStart[k + 1])
			std::vector<double>	  fromAnchorM{};  // per vertex: arc since the last anchor (or the path start)
			std::vector<double>	  toAnchorM{};	  // per vertex: arc to the next anchor (unused past the last)
		};

		PathArc buildPathArc(const ThalwegPath& path) {
			const auto&	 pts  = path.points;
			const size_t segs = pts.size() - 1;
			PathArc		 arc;
			arc.segLenM.resize(segs);
			arc.segAnchorStart.assign(segs + 1, 0);
			std::vector<double> ts;
			for (size_t k = 0; k < segs; ++k) {
				const Vec2i64& a = pts[k];
				const Vec2i64& b = pts[k + 1];
				arc.segLenM[k]	 = std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y)) / kMmPerM;
				ts.clear();
				// Lines strictly past the lower end and up to the upper end, so a
				// crossing is counted once however the path meets the line.
				auto lines = [&ts](int64_t from, int64_t to) {
					const int64_t lo = std::min(from, to);
					const int64_t hi = std::max(from, to);
					for (int64_t m = floorDiv(lo, Field::kArcAnchorSpacingMm) + 1; m <= floorDiv(hi, Field::kArcAnchorSpacingMm); ++m) {
						const int64_t line = m * Field::kArcAnchorSpacingMm;
						ts.push_back(static_cast<double>(line - from) / static_cast<double>(to - from));
					}
				};
				lines(a.x, b.x);
				lines(a.y, b.y);
				std::sort(ts.begin(), ts.end());
				for (const double t : ts) {
					arc.anchors.push_back({static_cast<uint32_t>(k), t});
				}
				arc.segAnchorStart[k + 1] = static_cast<uint32_t>(arc.anchors.size());
			}

			arc.fromAnchorM.assign(pts.size(), 0.0);
			for (size_t k = 0; k < segs; ++k) {
				const bool anchored	   = arc.segAnchorStart[k + 1] > arc.segAnchorStart[k];
				arc.fromAnchorM[k + 1] = anchored ? (1.0 - arc.anchors[arc.segAnchorStart[k + 1] - 1].t) * arc.segLenM[k]
												  : arc.fromAnchorM[k] + arc.segLenM[k];
			}
			arc.toAnchorM.assign(pts.size(), 0.0);
			for (size_t k = segs; k-- > 0;) {
				const bool anchored = arc.segAnchorStart[k + 1] > arc.segAnchorStart[k];
				arc.toAnchorM[k]	= anchored ? arc.anchors[arc.segAnchorStart[k]].t * arc.segLenM[k] : arc.toAnchorM[k + 1] + arc.segLenM[k];
			}
			return arc;
		}

		struct ThalwegHit {
			double	 ratio;
			uint32_t path;
			uint32_t seg;
			double	 t;
		};

		class Thalwegs {
		  public:
			Thalwegs(const std::vector<ThalwegPath>& paths, Vec2i64 regionOrigin)
				: paths(paths),
				  grid(regionOrigin) {
				arcs.resize(paths.size());
				for (uint32_t p = 0; p < paths.size(); ++p) {
					const ThalwegPath& path = paths[p];
					if (path.points.size() < 2 || path.halfWidthM.size() != path.points.size() ||
						path.widthRatio.size() != path.points.size() || path.curvature.size() != path.points.size()) {
						continue;
					}
					arcs[p]			  = buildPathArc(path);
					const auto segs	  = static_cast<uint32_t>(path.points.size() - 1);
					for (uint32_t first = 0; first < segs; first += kThalwegRunSegments) {
						const uint32_t count = std::min(kThalwegRunSegments, segs - first);
						Run			   run{p, first, count, path.points[first], path.points[first], path.points[first], path.points[first + count]};
						for (uint32_t k = first; k <= first + run.count; ++k) {
							const Vec2i64& v = path.points[k];
							run.lo			 = {std::min(run.lo.x, v.x), std::min(run.lo.y, v.y)};
							run.hi			 = {std::max(run.hi.x, v.x), std::max(run.hi.y, v.y)};
							run.hwMaxMm		 = std::max(run.hwMaxMm, static_cast<double>(path.halfWidthM[k]) * kMmPerM);
							run.deviationMm	 = std::max(run.deviationMm, closestOnSegment(v, run.chordA, run.chordB).distanceMm);
						}
						run.deviationMm += kPruneSlackMm;
						if (run.hwMaxMm <= 0.0) {
							continue;
						}
						const auto reach = static_cast<int64_t>(std::ceil(Field::kNoThalweg * run.hwMaxMm));
						grid.add(static_cast<uint32_t>(runs.size()), {run.lo.x - reach, run.lo.y - reach}, {run.hi.x + reach, run.hi.y + reach});
						runs.push_back(run);
					}
				}
				grid.finish();
			}

			[[nodiscard]] bool empty() const { return runs.empty(); }

			// Min over thalweg segments of distance / half-width at the closest point,
			// among those under kNoThalweg; ties go to the lexicographically smaller
			// segment. `hint` is the run that won the previous texel: trying it first
			// tightens the bound the rest are pruned against, without changing the
			// answer.
			[[nodiscard]] std::optional<ThalwegHit> nearest(const Vec2i64& p, uint32_t& hint) const {
				std::optional<ThalwegHit> best;
				double					  bestRatio = Field::kNoThalweg;
				uint32_t				  bestRun	= hint;
				auto					  tryRun	= [&](uint32_t runIndex) {
					   const Run&	 run = runs[runIndex];
					   const double ox	= static_cast<double>(outside(p.x, run.lo.x, run.hi.x));
					   const double oy	= static_cast<double>(outside(p.y, run.lo.y, run.hi.y));
					   const double reach = bestRatio * run.hwMaxMm + kPruneSlackMm;
					   if (ox * ox + oy * oy > reach * reach ||
						   closestOnSegment(p, run.chordA, run.chordB).distanceMm - run.deviationMm > reach) {
						   return;
					   }
					   const ThalwegPath& path = paths[run.path];
					   for (uint32_t k = run.first; k < run.first + run.count; ++k) {
						   const Vec2i64& a		 = path.points[k];
						   const Vec2i64& b		 = path.points[k + 1];
						   const double	  hw0	 = static_cast<double>(path.halfWidthM[k]);
						   const double	  hw1	 = static_cast<double>(path.halfWidthM[k + 1]);
						   const double	  sx	 = static_cast<double>(outside(p.x, std::min(a.x, b.x), std::max(a.x, b.x)));
						   const double	  sy	 = static_cast<double>(outside(p.y, std::min(a.y, b.y), std::max(a.y, b.y)));
						   const double	  segCap = bestRatio * std::max(hw0, hw1) * kMmPerM + kPruneSlackMm;
						   if (sx * sx + sy * sy > segCap * segCap) {
							   continue;
						   }
						   const SegmentPoint sp = closestOnSegment(p, a, b);
						   const double		  hw = lerp(hw0, hw1, sp.t) * kMmPerM;
						   if (hw <= 0.0) {
							   continue;
						   }
						   const double ratio = sp.distanceMm / hw;
						   if (ratio >= Field::kNoThalweg) {
							   continue;
						   }
						   if (best) {
							   const ThalwegPath& bestPath = paths[best->path];
							   const auto		  bestKey  = std::tie(bestPath.points[best->seg], bestPath.points[best->seg + 1]);
							   if (ratio > best->ratio || (ratio == best->ratio && !(std::tie(a, b) < bestKey))) {
								   continue;
							   }
						   }
						   best		 = ThalwegHit{ratio, run.path, k, sp.t};
						   bestRatio = ratio;
						   bestRun	 = runIndex;
					   }
				};
				if (hint < runs.size()) {
					tryRun(hint);
				}
				for (const uint32_t runIndex : grid.at(p)) {
					if (runIndex != hint) {
						tryRun(runIndex);
					}
				}
				hint = bestRun;
				return best;
			}

			// channelFrame texel of a hit: arc length, width ratio, curvature x hw.
			[[nodiscard]] HalfTexel frame(const ThalwegHit& hit) const {
				const ThalwegPath& path = paths[hit.path];
				const size_t	   k	= hit.seg;
				const double	   hw	= lerp(static_cast<double>(path.halfWidthM[k]), static_cast<double>(path.halfWidthM[k + 1]), hit.t);
				const double ratio = lerp(static_cast<double>(path.widthRatio[k]), static_cast<double>(path.widthRatio[k + 1]), hit.t);
				const double curvature = lerp(static_cast<double>(path.curvature[k]), static_cast<double>(path.curvature[k + 1]), hit.t);
				double		 s		   = std::fmod(Field::kArcAnchorValueM + arcLengthM(hit), Field::kArcWrapM);
				if (s < 0.0) {
					s += Field::kArcWrapM;
				}
				if (s >= Field::kArcWrapM) {
					s = 0.0;
				}
				return {toHalf(s), toHalf(ratio), toHalf(curvature * hw), 0};
			}

		  private:
			// kThalwegRunSegments consecutive segments. Every point of the run lies
			// within deviationMm of its chord (the capsule around a segment is convex),
			// so distance to the chord minus deviationMm bounds the run from below.
			struct Run {
				uint32_t path;
				uint32_t first;
				uint32_t count;
				Vec2i64	 lo;
				Vec2i64	 hi;
				Vec2i64	 chordA;
				Vec2i64	 chordB;
				double	 hwMaxMm	 = 0.0;
				double	 deviationMm = 0.0;
			};

			// Unwrapped arc length at a hit (header, bake()): exact from the nearer
			// anchor near either end of a reach, stretched in between.
			[[nodiscard]] double arcLengthM(const ThalwegHit& hit) const {
				const PathArc& arc = arcs[hit.path];
				const uint32_t k   = hit.seg;
				const double   len = arc.segLenM[k];
				uint32_t	   piece = arc.segAnchorStart[k];
				while (piece < arc.segAnchorStart[k + 1] && arc.anchors[piece].t <= hit.t) {
					++piece;
				}
				const Anchor* up   = piece > 0 ? &arc.anchors[piece - 1] : nullptr;
				const Anchor* down = piece < arc.anchors.size() ? &arc.anchors[piece] : nullptr;

				const double fromUp = up != nullptr && up->seg == k ? (hit.t - up->t) * len : arc.fromAnchorM[k] + hit.t * len;
				if (down == nullptr) {
					return fromUp;
				}
				const double toDown = down->seg == k ? (down->t - hit.t) * len : arc.toAnchorM[k + 1] + (1.0 - hit.t) * len;
				if (up == nullptr) {
					return -toDown;
				}
				const double reach = up->seg == down->seg ? (down->t - up->t) * arc.segLenM[up->seg]
														  : arc.fromAnchorM[down->seg] + down->t * arc.segLenM[down->seg];
				const double wraps = std::round(reach / Field::kArcWrapM);
				if (wraps == 0.0) {
					return fromUp <= toDown ? fromUp : -toDown;
				}
				if (fromUp <= Field::kArcAnchorHoldM) {
					return fromUp;
				}
				if (toDown <= Field::kArcAnchorHoldM) {
					return -toDown;
				}
				const double stretch = wraps * Field::kArcWrapM - reach;
				return fromUp + stretch * (fromUp - Field::kArcAnchorHoldM) / (reach - 2.0 * Field::kArcAnchorHoldM);
			}

			const std::vector<ThalwegPath>& paths;
			std::vector<PathArc>			arcs{};
			std::vector<Run>				runs{};
			BucketGrid						grid;
		};

		// ============ The bake ============

		// One row of terrainSdf texels on a lattice of spacing texelMm, texel index
		// `first` onward (relative to the bake origin), `count` of them; only the
		// texels `needed` marks (all when empty) are written.
		void sdfRow(const Rings& rings, const Thalwegs& thalwegs, Vec2i64 origin, int64_t texelMm, int32_t first, int32_t row,
					std::span<const uint8_t> needed, std::span<HalfTexel> out, std::vector<Crossing>& crossings) {
			const int64_t yMm = origin.y + static_cast<int64_t>(row) * texelMm + texelMm / 2;
			rings.crossingsAt(yMm, crossings);
			RowContainment containment(rings, crossings);
			uint32_t	   edgeHint	   = UINT32_MAX;
			uint32_t	   thalwegHint = UINT32_MAX;
			for (size_t idx = 0; idx < out.size(); ++idx) {
				if (!needed.empty() && needed[idx] == 0) {
					continue;
				}
				const int64_t xMm = origin.x + (static_cast<int64_t>(first) + static_cast<int64_t>(idx)) * texelMm + texelMm / 2;
				const Vec2i64 p{xMm, yMm};
				containment.advanceTo(xMm);
				const bool	  water	  = containment.water();
				const auto	  nearest = rings.nearestEdge(p, edgeHint);
				const double  d		  = nearest ? nearest->distanceMm / kMmPerM : Field::kSdfNearM;
				const uint8_t kind	  = nearest ? nearest->water : (water ? containment.containingWater() : 0);
				double		  g		  = Field::kNoThalweg;
				if (!thalwegs.empty()) {
					if (const auto hit = thalwegs.nearest(p, thalwegHint)) {
						g = hit->ratio;
					}
				}
				out[idx] = {toHalf(water ? -d : d), toHalf(g), toHalf(static_cast<double>(kind)), 0};
			}
		}

		// Near tiles within kSdfNearM plus one texel of a shore edge, row-major.
		std::vector<uint16_t> allocateNearTiles(const Rings& rings, Vec2i64 origin, uint16_t& count) {
			std::vector<uint8_t> want(static_cast<size_t>(Field::kTilesPerSide) * Field::kTilesPerSide, 0);
			const auto			 reach = static_cast<int64_t>(kNearTileReachMm);
			for (const ShoreEdge& e : rings.shoreEdges()) {
				const int64_t tx0 = std::max<int64_t>(floorDiv(std::min(e.a.x, e.b.x) - reach - origin.x, Field::kNearTileMm), 0);
				const int64_t ty0 = std::max<int64_t>(floorDiv(std::min(e.a.y, e.b.y) - reach - origin.y, Field::kNearTileMm), 0);
				const int64_t tx1 =
					std::min<int64_t>(floorDiv(std::max(e.a.x, e.b.x) + reach - origin.x, Field::kNearTileMm), Field::kTilesPerSide - 1);
				const int64_t ty1 =
					std::min<int64_t>(floorDiv(std::max(e.a.y, e.b.y) + reach - origin.y, Field::kNearTileMm), Field::kTilesPerSide - 1);
				for (int64_t ty = ty0; ty <= ty1; ++ty) {
					for (int64_t tx = tx0; tx <= tx1; ++tx) {
						uint8_t& w = want[static_cast<size_t>(ty * Field::kTilesPerSide + tx)];
						if (w != 0) {
							continue;
						}
						const Vec2i64 lo{origin.x + tx * Field::kNearTileMm, origin.y + ty * Field::kNearTileMm};
						const Vec2i64 hi{lo.x + Field::kNearTileMm, lo.y + Field::kNearTileMm};
						w = segmentBoxDistanceMm(e.a, e.b, lo, hi) <= kNearTileReachMm ? 1 : 0;
					}
				}
			}
			std::vector<uint16_t> map(want.size(), Field::kNoNearTile);
			count = 0;
			for (size_t i = 0; i < want.size(); ++i) {
				if (want[i] != 0) {
					map[i] = count++;
				}
			}
			return map;
		}

		void bakeNear(Field& field, const Rings& rings, const Thalwegs& thalwegs, uint16_t tileCount) {
			constexpr int32_t kRowTexels = kNearLatticeSize + 2; // lattice texels -1 .. kNearLatticeSize
			field.nearTexels.resize(static_cast<size_t>(tileCount) * Field::kNearTileTexelCount);
			std::vector<HalfTexel> rowBuffer(kRowTexels);
			std::vector<uint8_t>   needed(kRowTexels);
			std::vector<Crossing>  crossings;
			// The stored rows this lattice row fills: a tile's own row or a gutter row.
			struct RowTarget {
				int32_t	 tx;
				uint16_t tile;
				int32_t	 v;
			};
			std::vector<RowTarget> rowTiles;
			for (int32_t j = -1; j <= kNearLatticeSize; ++j) {
				std::fill(needed.begin(), needed.end(), uint8_t{0});
				rowTiles.clear();
				const int32_t tyCenter = static_cast<int32_t>(floorDiv(j, Field::kNearTileTexels));
				for (int32_t ty = std::max(tyCenter - 1, 0); ty <= std::min(tyCenter + 1, Field::kTilesPerSide - 1); ++ty) {
					const int32_t v = j - ty * Field::kNearTileTexels + 1;
					if (v < 0 || v >= Field::kNearTileStride) {
						continue;
					}
					for (int32_t tx = 0; tx < Field::kTilesPerSide; ++tx) {
						const uint16_t tile = field.nearTileAt(tx, ty);
						if (tile == Field::kNoNearTile) {
							continue;
						}
						rowTiles.push_back({tx, tile, v});
						std::fill_n(needed.begin() + tx * Field::kNearTileTexels, Field::kNearTileStride, uint8_t{1});
					}
				}
				if (rowTiles.empty()) {
					continue;
				}
				sdfRow(rings, thalwegs, field.originMm, Field::kNearTexelMm, -1, j, needed, rowBuffer, crossings);
				for (const RowTarget& target : rowTiles) {
					std::copy_n(
						rowBuffer.begin() + target.tx * Field::kNearTileTexels,
						Field::kNearTileStride,
						field.nearTexels.begin() + static_cast<std::ptrdiff_t>(
													   static_cast<size_t>(target.tile) * Field::kNearTileTexelCount +
													   static_cast<size_t>(target.v) * Field::kNearTileStride
												   )
					);
				}
			}
		}

		template <typename T>
		void clearIfUniform(std::vector<T>& texels, const T& value) {
			if (std::all_of(texels.begin(), texels.end(), [&value](const T& t) { return t == value; })) {
				texels = {};
			}
		}

	} // namespace

	TerrainDistanceField TerrainDistanceField::bake(const ChunkTerrainPolygons& polygons, ChunkCoordinate coord) {
		TerrainDistanceField field;
		field.originMm = {
			static_cast<int64_t>(coord.x) * kChunkSize * 1000 - kMarginMm, static_cast<int64_t>(coord.y) * kChunkSize * 1000 - kMarginMm
		};
		field.version = polygons.version;
		field.tileMap.assign(static_cast<size_t>(kTilesPerSide) * kTilesPerSide, kNoNearTile);
		if (polygons.rings.empty() && polygons.thalwegs.empty()) {
			return field;
		}

		const Rings	   rings(polygons.rings, field.originMm);
		const Thalwegs thalwegs(polygons.thalwegs, field.originMm);

		uint16_t tileCount = 0;
		field.tileMap	   = allocateNearTiles(rings, field.originMm, tileCount);
		bakeNear(field, rings, thalwegs, tileCount);

		// Far level: every texel exact at its center; past kSdfNearM the value is
		// the clamp, so no propagation pass is needed.
		std::vector<Crossing> crossings;
		field.farTexels.resize(static_cast<size_t>(kFarSize) * kFarSize);
		for (int32_t j = 0; j < kFarSize; ++j) {
			sdfRow(rings, thalwegs, field.originMm, kFarTexelMm, 0, j, {},
				   std::span(field.farTexels).subspan(static_cast<size_t>(j) * kFarSize, kFarSize), crossings);
		}
		clearIfUniform(field.farTexels, kLandSdfTexel);

		if (rings.hasShoreVertices()) {
			field.shoreProfile.resize(static_cast<size_t>(kDetailSize) * kDetailSize);
		}
		if (!thalwegs.empty()) {
			field.channelFrame.resize(static_cast<size_t>(kDetailSize) * kDetailSize);
		}
		for (int32_t j = 0; j < kDetailSize && (!field.shoreProfile.empty() || !field.channelFrame.empty()); ++j) {
			uint32_t hint = UINT32_MAX;
			for (int32_t i = 0; i < kDetailSize; ++i) {
				const Vec2i64 p{
					field.originMm.x + static_cast<int64_t>(i) * kDetailTexelMm + kDetailTexelMm / 2,
					field.originMm.y + static_cast<int64_t>(j) * kDetailTexelMm + kDetailTexelMm / 2
				};
				const size_t index = detailIndex(i, j, kDetailSize);
				if (!field.shoreProfile.empty()) {
					field.shoreProfile[index] = rings.nearestProfile(p);
				}
				if (!field.channelFrame.empty()) {
					if (const auto hit = thalwegs.nearest(p, hint)) {
						field.channelFrame[index] = thalwegs.frame(*hit);
					}
				}
			}
		}
		clearIfUniform(field.shoreProfile, ByteTexel{});
		clearIfUniform(field.channelFrame, HalfTexel{});
		return field;
	}

} // namespace engine::world
