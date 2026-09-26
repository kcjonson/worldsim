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

		constexpr double  kMmPerM	 = 1000.0;
		constexpr int64_t kSdfNearMm = static_cast<int64_t>(Field::kSdfNearM * kMmPerM);
		constexpr int64_t kCellMm	 = 4000;
		// The farthest any stored texel center lies outside the chunk square: the
		// far level's gutter, half a far texel out.
		constexpr int64_t kQueryPadMm = Field::kFarTexelMm / 2;
		// A near tile is allocated within kSdfNearM plus one texel of the shoreline.
		constexpr double kNearTileReachMm = static_cast<double>(kSdfNearMm + Field::kSdfNearTexelMm);
		// Consecutive thalweg segments bucketed as one entry: a 0.5 m thalweg under
		// a 55 m river would otherwise land each segment in thousands of cells.
		constexpr uint32_t kThalwegRunSegments = 16;
		// Slack on every pruning bound, well above its rounding error, so pruning
		// never drops a candidate that could win or tie.
		constexpr double kPruneSlackMm = 1.0;

		int64_t floorDiv(int64_t a, int64_t b) {
			const int64_t q = a / b;
			return (a % b != 0 && a < 0) ? q - 1 : q;
		}

		int64_t ceilDiv(int64_t a, int64_t b) {
			return -floorDiv(-a, b);
		}

		// How far v lies outside [lo, hi]; zero inside.
		int64_t outside(int64_t v, int64_t lo, int64_t hi) {
			return v < lo ? lo - v : (v > hi ? v - hi : 0);
		}

		double outside(double v, double lo, double hi) {
			return v < lo ? lo - v : (v > hi ? v - hi : 0.0);
		}

		// glm rounds to nearest with ties away from zero (IEEE would round ties to
		// even); either way the result is a pure function of the float.
		uint16_t toHalf(double v) {
			return glm::packHalf1x16(static_cast<float>(v));
		}

		double lerp(double a, double b, double t) {
			return a + (b - a) * t;
		}

		struct DPoint {
			double x;
			double y;
		};

		DPoint along(const Vec2i64& a, const Vec2i64& b, double t) {
			return {static_cast<double>(a.x) + static_cast<double>(b.x - a.x) * t, static_cast<double>(a.y) + static_cast<double>(b.y - a.y) * t};
		}

		struct SegmentPoint {
			double distanceMm;
			double t;
		};

		// Closest point to p on the part [t0, t1] of segment [a, b]. Differences of
		// integer mm are exact in double, so the result depends only on the inputs.
		SegmentPoint closestOnSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, double t0 = 0.0, double t1 = 1.0) {
			const double abx  = static_cast<double>(b.x - a.x);
			const double aby  = static_cast<double>(b.y - a.y);
			const double apx  = static_cast<double>(p.x - a.x);
			const double apy  = static_cast<double>(p.y - a.y);
			const double len2 = abx * abx + aby * aby;
			const double t	  = len2 > 0.0 ? std::clamp((apx * abx + apy * aby) / len2, t0, t1) : t0;
			const double dx	  = apx - abx * t;
			const double dy	  = apy - aby * t;
			return {std::sqrt(dx * dx + dy * dy), t};
		}

		double pointSegmentDistance(DPoint p, DPoint a, DPoint b) {
			const double abx  = b.x - a.x;
			const double aby  = b.y - a.y;
			const double len2 = abx * abx + aby * aby;
			const double t	  = len2 > 0.0 ? std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / len2, 0.0, 1.0) : 0.0;
			return std::hypot(p.x - a.x - abx * t, p.y - a.y - aby * t);
		}

		// Distance between segment [a, b] and the closed box [lo, hi]: zero when
		// they meet, else attained at a segment endpoint or a box corner.
		double segmentBoxDistanceMm(DPoint a, DPoint b, DPoint lo, DPoint hi) {
			// Liang-Barsky: does the segment enter the box?
			const double dx	  = b.x - a.x;
			const double dy	  = b.y - a.y;
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
			clip(-dx, a.x - lo.x);
			clip(dx, hi.x - a.x);
			clip(-dy, a.y - lo.y);
			clip(dy, hi.y - a.y);
			if (hit && tMin <= tMax) {
				return 0.0;
			}
			auto pointBox = [&lo, &hi](DPoint p) { return std::hypot(outside(p.x, lo.x, hi.x), outside(p.y, lo.y, hi.y)); };
			double best = std::min(pointBox(a), pointBox(b));
			for (const DPoint corner : {lo, DPoint{hi.x, lo.y}, hi, DPoint{lo.x, hi.y}}) {
				best = std::min(best, pointSegmentDistance(corner, a, b));
			}
			return best;
		}

		// Items binned into kCellMm cells over a fixed area, each into every cell
		// its box touches (clipped to the area), in item order, so each cell's list
		// is the same whatever else a chunk holds.
		class BucketGrid {
		  public:
			BucketGrid(Vec2i64 areaOrigin, int32_t cellsX, int32_t cellsY)
				: origin(areaOrigin),
				  width(cellsX),
				  height(cellsY) {}

			void add(uint32_t item, Vec2i64 lo, Vec2i64 hi) {
				const auto [x0, y0, x1, y1] = cellRange(lo, hi);
				for (int64_t y = y0; y <= y1; ++y) {
					for (int64_t x = x0; x <= x1; ++x) {
						pairs.emplace_back(static_cast<uint32_t>(y * width + x), item);
					}
				}
			}

			// Counting sort by cell; stable, so each cell keeps item order.
			void finish() {
				const size_t cells = static_cast<size_t>(width) * static_cast<size_t>(height);
				start.assign(cells + 1, 0);
				for (const auto& [cell, item] : pairs) {
					++start[cell + 1];
				}
				for (size_t c = 0; c < cells; ++c) {
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
				const int64_t x = floorDiv(p.x - origin.x, kCellMm);
				const int64_t y = floorDiv(p.y - origin.y, kCellMm);
				if (start.empty() || x < 0 || y < 0 || x >= width || y >= height) {
					return {};
				}
				return cell(static_cast<size_t>(y * width + x));
			}

			// Every item in the cells the box touches, sorted and unique.
			void gather(Vec2i64 lo, Vec2i64 hi, std::vector<uint32_t>& out) const {
				out.clear();
				const auto [x0, y0, x1, y1] = cellRange(lo, hi);
				for (int64_t y = y0; y <= y1; ++y) {
					for (int64_t x = x0; x <= x1; ++x) {
						const std::span<const uint32_t> c = cell(static_cast<size_t>(y * width + x));
						out.insert(out.end(), c.begin(), c.end());
					}
				}
				std::sort(out.begin(), out.end());
				out.erase(std::unique(out.begin(), out.end()), out.end());
			}

		  private:
			// Clipped cell range; empty (x0 > x1) when the box misses the area.
			[[nodiscard]] std::tuple<int64_t, int64_t, int64_t, int64_t> cellRange(Vec2i64 lo, Vec2i64 hi) const {
				return {
					std::max<int64_t>(floorDiv(lo.x - origin.x, kCellMm), 0), std::max<int64_t>(floorDiv(lo.y - origin.y, kCellMm), 0),
					std::min<int64_t>(floorDiv(hi.x - origin.x, kCellMm), width - 1),
					std::min<int64_t>(floorDiv(hi.y - origin.y, kCellMm), height - 1)
				};
			}

			[[nodiscard]] std::span<const uint32_t> cell(size_t c) const { return {items.data() + start[c], items.data() + start[c + 1]}; }

			Vec2i64										 origin;
			int32_t										 width;
			int32_t										 height;
			std::vector<std::pair<uint32_t, uint32_t>> pairs{};
			std::vector<uint32_t>						 start{};
			std::vector<uint32_t>						 items{};
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

		// ============ Rings: shoreline pieces, vertices, containment ============

		// Part [t0, t1] of ring edge [a, b], on the boundary of the water union.
		struct ShorePiece {
			Vec2i64 a;
			Vec2i64 b;
			double	t0;
			double	t1;
			Vec2i64 lo; // the whole edge's box: a lower bound on distance to the piece
			Vec2i64 hi;
			uint8_t water;
		};

		struct ShoreVertex {
			Vec2i64	  p;
			ByteTexel profile;
		};

		struct RingEdge {
			Vec2i64	 a;
			Vec2i64	 b;
			uint32_t ring;
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
			Rings(const std::vector<TerrainRing>& rings, Vec2i64 chunkOrigin)
				: pieceGrid(areaOrigin(chunkOrigin), areaCells(), areaCells()),
				  vertexGrid(areaOrigin(chunkOrigin), areaCells(), areaCells()) {
				std::vector<const TerrainRing*> kept;
				Vec2i64							lo{INT64_MAX, INT64_MAX};
				Vec2i64							hi{INT64_MIN, INT64_MIN};
				for (const TerrainRing& ring : rings) {
					const size_t n = ring.ring.size();
					if (n < 3 || ring.profiles.size() != n) {
						continue;
					}
					const geometry::Int128 area2 = geometry::signedAreaDoubled(ring.ring);
					info.push_back({area2.sign() < 0 ? -area2 : area2, static_cast<uint8_t>(ring.water), !ring.holeCapable});
					kept.push_back(&ring);
					for (size_t i = 0; i < n; ++i) {
						const Vec2i64& v = ring.ring[i];
						lo				 = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
						hi				 = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
						edges.push_back({v, ring.ring[(i + 1) % n], static_cast<uint32_t>(kept.size() - 1)});
					}
				}
				if (kept.empty()) {
					return;
				}
				buildBands(lo.y, hi.y);
				buildShoreline(kept, chunkOrigin);
			}

			[[nodiscard]] bool empty() const { return info.empty(); }
			[[nodiscard]] bool hasShoreVertices() const { return !vertices.empty(); }
			[[nodiscard]] size_t ringCount() const { return info.size(); }
			[[nodiscard]] const RingInfo& ring(uint32_t slot) const { return info[slot]; }
			[[nodiscard]] const std::vector<ShorePiece>& shoreline() const { return pieces; }

			// Every ring edge crossing the row y = yMm (half-open in y, so each closed
			// ring crosses it an even number of times), sorted by threshold. A texel
			// center (x, yMm) has crossed a crossing when x >= threshold, so its
			// crossing count per ring is the exact integer point-in-polygon test off
			// the boundary.
			void crossingsAt(int64_t yMm, std::vector<Crossing>& out) const {
				out.clear();
				for (const uint32_t index : band(static_cast<double>(yMm))) {
					const RingEdge& e = edges[index];
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

			// Nearest shoreline piece within kSdfNearM of p; ties go to the lower
			// WaterKind. `hint` is the previous texel's nearest piece: trying it first
			// tightens the bound the rest are pruned against (by their box, exact in
			// integers), without changing the answer.
			[[nodiscard]] std::optional<Nearest> nearestShore(const Vec2i64& p, uint32_t& hint) const {
				std::optional<Nearest> best;
				double				   bound	 = static_cast<double>(kSdfNearMm);
				uint32_t			   bestIndex = hint;
				auto				   tryPiece	 = [&](uint32_t index) {
					  const ShorePiece& s	  = pieces[index];
					  const int64_t		ox	  = outside(p.x, s.lo.x, s.hi.x);
					  const int64_t		oy	  = outside(p.y, s.lo.y, s.hi.y);
					  const double		reach = bound + kPruneSlackMm;
					  if (static_cast<double>(ox * ox + oy * oy) > reach * reach) {
						  return;
					  }
					  const double d = closestOnSegment(p, s.a, s.b, s.t0, s.t1).distanceMm;
					  if (d > static_cast<double>(kSdfNearMm)) {
						  return;
					  }
					  if (!best || d < best->distanceMm || (d == best->distanceMm && s.water < best->water)) {
						  best		= Nearest{d, s.water};
						  bound		= d;
						  bestIndex = index;
					  }
				};
				if (hint < pieces.size()) {
					tryPiece(hint);
				}
				for (const uint32_t index : pieceGrid.at(p)) {
					if (index != hint) {
						tryPiece(index);
					}
				}
				hint = bestIndex;
				return best;
			}

			// Profile of the nearest shoreline vertex within kSdfNearM of p, exact in
			// integer mm^2; ties by position, then profile bytes.
			[[nodiscard]] ByteTexel nearestProfile(const Vec2i64& p) const {
				constexpr int64_t  kReach2 = kSdfNearMm * kSdfNearMm;
				const ShoreVertex* best	   = nullptr;
				int64_t			   bestD2  = 0;
				auto			   key	   = [](const ShoreVertex& v) {
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

		  private:
			// The area the texel queries fall in: the chunk square plus the gutter,
			// less one cell so a point on the far edge still has one.
			static Vec2i64 areaOrigin(Vec2i64 chunkOrigin) { return {chunkOrigin.x - kQueryPadMm, chunkOrigin.y - kQueryPadMm}; }
			static int32_t areaCells() { return static_cast<int32_t>((Field::kChunkMm + 2 * kQueryPadMm) / kCellMm) + 1; }

			void buildBands(int64_t minY, int64_t maxY) {
				bandOrigin				 = minY;
				bandCount				 = floorDiv(maxY - minY, kCellMm) + 1;
				const auto bands		 = static_cast<size_t>(bandCount);
				std::vector<std::pair<uint32_t, uint32_t>> bandPairs;
				for (uint32_t k = 0; k < edges.size(); ++k) {
					const RingEdge& e = edges[k];
					if (e.a.y == e.b.y) {
						continue;
					}
					// An edge crosses row y when minY <= y < maxY.
					const int64_t b0 = floorDiv(std::min(e.a.y, e.b.y) - bandOrigin, kCellMm);
					const int64_t b1 = floorDiv(std::max(e.a.y, e.b.y) - 1 - bandOrigin, kCellMm);
					for (int64_t b = b0; b <= b1; ++b) {
						bandPairs.emplace_back(static_cast<uint32_t>(b), k);
					}
				}
				bandStart.assign(bands + 1, 0);
				for (const auto& [b, edge] : bandPairs) {
					++bandStart[b + 1];
				}
				for (size_t b = 0; b < bands; ++b) {
					bandStart[b + 1] += bandStart[b];
				}
				bandEdges.resize(bandPairs.size());
				std::vector<uint32_t> cursor(bandStart.begin(), bandStart.end() - 1);
				for (const auto& [b, edge] : bandPairs) {
					bandEdges[cursor[b]++] = edge;
				}
			}

			[[nodiscard]] std::span<const uint32_t> band(double y) const {
				const double b = std::floor((y - static_cast<double>(bandOrigin)) / static_cast<double>(kCellMm));
				if (b < 0.0 || b >= static_cast<double>(bandCount)) {
					return {};
				}
				const auto i = static_cast<size_t>(b);
				return {bandEdges.data() + bandStart[i], bandEdges.data() + bandStart[i + 1]};
			}

			// Is a point on ring `self`'s boundary inside water the other rings make,
			// such that both sides of `self` there are water? A Waterline edge flips
			// the even-odd parity, so its sides differ unless a solid ring covers the
			// point. A solid ring's edge borders water only where no other solid ring
			// covers the point and the Waterline parity there is even.
			[[nodiscard]] bool submerged(DPoint p, uint32_t self, std::vector<uint8_t>& inside) const {
				std::fill(inside.begin(), inside.end(), uint8_t{0});
				for (const uint32_t index : band(p.y)) {
					const RingEdge& e = edges[index];
					if (e.ring == self || (static_cast<double>(e.a.y) > p.y) == (static_cast<double>(e.b.y) > p.y)) {
						continue;
					}
					const double x = static_cast<double>(e.a.x) + (p.y - static_cast<double>(e.a.y)) *
																	   static_cast<double>(e.b.x - e.a.x) /
																	   static_cast<double>(e.b.y - e.a.y);
					if (x > p.x) {
						inside[e.ring] ^= 1U;
					}
				}
				bool	solidOther = false;
				uint8_t parity	   = 0;
				for (uint32_t slot = 0; slot < inside.size(); ++slot) {
					if (inside[slot] == 0) {
						continue;
					}
					if (info[slot].solid) {
						solidOther = true;
					} else {
						parity ^= 1U;
					}
				}
				return solidOther || (info[self].solid && parity != 0);
			}

			// The shoreline: every edge that is neither synthetic nor a fordable cut,
			// split where other rings' edges cross it (exact integer orientation
			// tests; the split parameter is the one rounding), keeping the pieces
			// whose midpoint is not submerged. Only edges within reach of a stored
			// texel are considered.
			void buildShoreline(const std::vector<const TerrainRing*>& rings, Vec2i64 chunkOrigin) {
				const int64_t reach = kSdfNearMm + kCellMm;
				const Vec2i64 areaLo{chunkOrigin.x - kQueryPadMm - reach, chunkOrigin.y - kQueryPadMm - reach};
				const auto	  cells = static_cast<int32_t>((Field::kChunkMm + 2 * (kQueryPadMm + reach)) / kCellMm) + 1;
				BucketGrid	  edgeGrid(areaLo, cells, cells);
				for (uint32_t k = 0; k < edges.size(); ++k) {
					const RingEdge& e = edges[k];
					edgeGrid.add(k, {std::min(e.a.x, e.b.x), std::min(e.a.y, e.b.y)}, {std::max(e.a.x, e.b.x), std::max(e.a.y, e.b.y)});
				}
				edgeGrid.finish();

				const DPoint queryLo{static_cast<double>(chunkOrigin.x - kQueryPadMm), static_cast<double>(chunkOrigin.y - kQueryPadMm)};
				const DPoint queryHi{
					static_cast<double>(chunkOrigin.x + Field::kChunkMm + kQueryPadMm), static_cast<double>(chunkOrigin.y + Field::kChunkMm + kQueryPadMm)
				};
				std::vector<uint8_t>  inside(info.size());
				std::vector<uint32_t> candidates;
				std::vector<double>	  splits;
				uint32_t			  edgeIndex = 0;
				for (uint32_t slot = 0; slot < rings.size(); ++slot) {
					const TerrainRing&	 ring = *rings[slot];
					const size_t		 n	  = ring.ring.size();
					std::vector<uint8_t> startKept(n, 0); // a kept piece starts at vertex i (t0 = 0 on edge i)
					std::vector<uint8_t> endKept(n, 0);	  // a kept piece ends at vertex i + 1 (t1 = 1 on edge i)
					for (size_t i = 0; i < n; ++i, ++edgeIndex) {
						const RingEdge& e = edges[edgeIndex];
						if (isSyntheticEdge(ring, i) || isCutEdge(ring, i)) {
							continue;
						}
						const Vec2i64 lo{std::min(e.a.x, e.b.x), std::min(e.a.y, e.b.y)};
						const Vec2i64 hi{std::max(e.a.x, e.b.x), std::max(e.a.y, e.b.y)};
						if (segmentBoxDistanceMm(along(e.a, e.b, 0.0), along(e.a, e.b, 1.0), queryLo, queryHi) >
							static_cast<double>(kSdfNearMm) + kPruneSlackMm) {
							continue;
						}
						splits.assign({0.0, 1.0});
						edgeGrid.gather(lo, hi, candidates);
						for (const uint32_t other : candidates) {
							if (edges[other].ring != slot) {
								addCrossing(e, edges[other], splits);
							}
						}
						std::sort(splits.begin(), splits.end());
						splits.erase(std::unique(splits.begin(), splits.end()), splits.end());
						for (size_t k = 0; k + 1 < splits.size(); ++k) {
							const double t0 = splits[k];
							const double t1 = splits[k + 1];
							if (submerged(along(e.a, e.b, 0.5 * (t0 + t1)), slot, inside)) {
								continue;
							}
							const auto index = static_cast<uint32_t>(pieces.size());
							pieces.push_back({e.a, e.b, t0, t1, lo, hi, static_cast<uint8_t>(ring.water)});
							const DPoint pa = along(e.a, e.b, t0);
							const DPoint pb = along(e.a, e.b, t1);
							pieceGrid.add(
								index,
								{static_cast<int64_t>(std::floor(std::min(pa.x, pb.x))) - kSdfNearMm,
								 static_cast<int64_t>(std::floor(std::min(pa.y, pb.y))) - kSdfNearMm},
								{static_cast<int64_t>(std::ceil(std::max(pa.x, pb.x))) + kSdfNearMm,
								 static_cast<int64_t>(std::ceil(std::max(pa.y, pb.y))) + kSdfNearMm}
							);
							startKept[i]  = startKept[i] != 0 || t0 == 0.0 ? 1 : 0;
							endKept[i]	  = endKept[i] != 0 || t1 == 1.0 ? 1 : 0;
						}
					}
					// Vertices on the shoreline (an end of a kept piece), except those of
					// synthetic edges.
					for (size_t i = 0; i < n; ++i) {
						const size_t prev = (i + n - 1) % n;
						if (isSyntheticEdge(ring, i) || isSyntheticEdge(ring, prev) || (startKept[i] == 0 && endKept[prev] == 0)) {
							continue;
						}
						const Vec2i64&		p	  = ring.ring[i];
						const ShoreProfile& pr	  = ring.profiles[i];
						const auto			index = static_cast<uint32_t>(vertices.size());
						vertices.push_back({p, {pr.slope, pr.exposure, pr.sand, pr.mud}});
						vertexGrid.add(index, {p.x - kSdfNearMm, p.y - kSdfNearMm}, {p.x + kSdfNearMm, p.y + kSdfNearMm});
					}
				}
				pieceGrid.finish();
				vertexGrid.finish();
			}

			// Where `other` crosses or touches the interior of `e`, as e's parameter.
			// Collinear overlaps are not split (they never occur between the
			// builder's rings except at shared cut edges, which are not shoreline).
			static void addCrossing(const RingEdge& e, const RingEdge& other, std::vector<double>& splits) {
				const Vec2i64		   ab = e.b - e.a;
				const Vec2i64		   cd = other.b - other.a;
				const geometry::Int128 o1 = geometry::cross(ab, other.a - e.a);
				const geometry::Int128 o2 = geometry::cross(ab, other.b - e.a);
				if (o1.sign() * o2.sign() > 0 || (o1.sign() == 0 && o2.sign() == 0)) {
					return;
				}
				const geometry::Int128 o3 = geometry::cross(cd, e.a - other.a);
				const geometry::Int128 o4 = geometry::cross(cd, e.b - other.a);
				if (o3.sign() * o4.sign() > 0 || o3 == o4) {
					return;
				}
				const double t = o3.toDouble() / (o3 - o4).toDouble();
				if (t > 0.0 && t < 1.0) {
					splits.push_back(t);
				}
			}

			std::vector<RingInfo>	 info{};
			std::vector<RingEdge>	 edges{};
			std::vector<ShorePiece>	 pieces{};
			std::vector<ShoreVertex> vertices{};
			BucketGrid				 pieceGrid;
			BucketGrid				 vertexGrid;
			int64_t					 bandOrigin = 0;
			int64_t					 bandCount	= 0;
			std::vector<uint32_t>	 bandStart{};
			std::vector<uint32_t>	 bandEdges{};
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

		struct ThalwegHit {
			double	 ratio;
			uint32_t path;
			uint32_t seg;
			double	 t;
		};

		class Thalwegs {
		  public:
			Thalwegs(const std::vector<ThalwegPath>& paths, Vec2i64 chunkOrigin)
				: paths(paths),
				  grid(
					  {chunkOrigin.x - kQueryPadMm, chunkOrigin.y - kQueryPadMm},
					  static_cast<int32_t>((Field::kChunkMm + 2 * kQueryPadMm) / kCellMm) + 1,
					  static_cast<int32_t>((Field::kChunkMm + 2 * kQueryPadMm) / kCellMm) + 1
				  ) {
				for (uint32_t p = 0; p < paths.size(); ++p) {
					const ThalwegPath& path = paths[p];
					const size_t	   n	= path.points.size();
					if (n < 2 || path.halfWidthM.size() != n || path.widthRatio.size() != n || path.curvature.size() != n ||
						path.arcLengthM.size() != n) {
						continue;
					}
					const auto segs = static_cast<uint32_t>(n - 1);
					for (uint32_t first = 0; first < segs; first += kThalwegRunSegments) {
						const uint32_t count = std::min(kThalwegRunSegments, segs - first);
						Run run{p, first, count, path.points[first], path.points[first], path.points[first], path.points[first + count]};
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
					   const Run&	 run   = runs[runIndex];
					   const double ox	   = static_cast<double>(outside(p.x, run.lo.x, run.hi.x));
					   const double oy	   = static_cast<double>(outside(p.y, run.lo.y, run.hi.y));
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

			// channelFrame texel of a hit: arc coordinate mod kArcWrapM, width ratio,
			// curvature x hw.
			[[nodiscard]] HalfTexel frame(const ThalwegHit& hit) const {
				const ThalwegPath& path = paths[hit.path];
				const size_t	   k	= hit.seg;
				auto			   at	= [&hit, k](const auto& values) {
					  return lerp(static_cast<double>(values[k]), static_cast<double>(values[k + 1]), hit.t);
				};
				double s = std::fmod(at(path.arcLengthM), Field::kArcWrapM);
				if (s < 0.0) {
					s += Field::kArcWrapM;
				}
				if (s >= Field::kArcWrapM) {
					s = 0.0;
				}
				return {toHalf(s), toHalf(at(path.widthRatio)), toHalf(at(path.curvature) * at(path.halfWidthM))};
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

			const std::vector<ThalwegPath>& paths;
			std::vector<Run>				runs{};
			BucketGrid						grid;
		};

		// ============ The bake ============

		// One row of terrainSdf texels on a lattice of spacing texelMm: lattice
		// texels first .. first + out.size() - 1 of row `row` (both relative to the
		// chunk origin, so -1 is the gutter); only the texels `needed` marks (all
		// when empty) are written.
		void sdfRow(const Rings& rings, const Thalwegs& thalwegs, Vec2i64 origin, int64_t texelMm, int32_t first, int32_t row,
					std::span<const uint8_t> needed, std::span<HalfTexel> out, std::vector<Crossing>& crossings) {
			const int64_t yMm = origin.y + static_cast<int64_t>(row) * texelMm + texelMm / 2;
			rings.crossingsAt(yMm, crossings);
			RowContainment containment(rings, crossings);
			uint32_t	   shoreHint   = UINT32_MAX;
			uint32_t	   thalwegHint = UINT32_MAX;
			for (size_t idx = 0; idx < out.size(); ++idx) {
				if (!needed.empty() && needed[idx] == 0) {
					continue;
				}
				const int64_t xMm = origin.x + (static_cast<int64_t>(first) + static_cast<int64_t>(idx)) * texelMm + texelMm / 2;
				const Vec2i64 p{xMm, yMm};
				containment.advanceTo(xMm);
				const bool	  water	  = containment.water();
				const auto	  nearest = rings.nearestShore(p, shoreHint);
				const double  d		  = nearest ? nearest->distanceMm / kMmPerM : Field::kSdfNearM;
				const uint8_t kind	  = nearest ? nearest->water : (water ? containment.containingWater() : 0);
				double		  g		  = Field::kNoThalweg;
				if (!thalwegs.empty()) {
					if (const auto hit = thalwegs.nearest(p, thalwegHint)) {
						g = hit->ratio;
					}
				}
				out[idx] = {toHalf(water ? -d : d), toHalf(g), toHalf(static_cast<double>(kind))};
			}
		}

		// Near tiles within kSdfNearM plus one texel of the shoreline, row-major.
		std::vector<uint16_t> allocateNearTiles(const Rings& rings, Vec2i64 origin, uint16_t& count) {
			std::vector<uint8_t> want(static_cast<size_t>(Field::kTilesPerSide) * Field::kTilesPerSide, 0);
			const auto			 reach = static_cast<int64_t>(kNearTileReachMm) + 1;
			for (const ShorePiece& s : rings.shoreline()) {
				const DPoint  a	  = along(s.a, s.b, s.t0);
				const DPoint  b	  = along(s.a, s.b, s.t1);
				const int64_t tx0 = std::max<int64_t>(floorDiv(s.lo.x - reach - origin.x, Field::kNearTileMm), 0);
				const int64_t ty0 = std::max<int64_t>(floorDiv(s.lo.y - reach - origin.y, Field::kNearTileMm), 0);
				const int64_t tx1 = std::min<int64_t>(floorDiv(s.hi.x + reach - origin.x, Field::kNearTileMm), Field::kTilesPerSide - 1);
				const int64_t ty1 = std::min<int64_t>(floorDiv(s.hi.y + reach - origin.y, Field::kNearTileMm), Field::kTilesPerSide - 1);
				for (int64_t ty = ty0; ty <= ty1; ++ty) {
					for (int64_t tx = tx0; tx <= tx1; ++tx) {
						uint8_t& w = want[static_cast<size_t>(ty * Field::kTilesPerSide + tx)];
						if (w != 0) {
							continue;
						}
						const DPoint lo{static_cast<double>(origin.x + tx * Field::kNearTileMm), static_cast<double>(origin.y + ty * Field::kNearTileMm)};
						const DPoint hi{lo.x + static_cast<double>(Field::kNearTileMm), lo.y + static_cast<double>(Field::kNearTileMm)};
						w = segmentBoxDistanceMm(a, b, lo, hi) <= kNearTileReachMm ? 1 : 0;
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
			constexpr int32_t kLattice	  = Field::kTilesPerSide * Field::kNearTileTexels;
			constexpr int32_t kRowTexels = kLattice + 2; // lattice texels -1 .. kLattice
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
			for (int32_t j = -1; j <= kLattice; ++j) {
				std::fill(needed.begin(), needed.end(), uint8_t{0});
				rowTiles.clear();
				const auto tyCenter = static_cast<int32_t>(floorDiv(j, Field::kNearTileTexels));
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
				sdfRow(rings, thalwegs, field.originMm, Field::kSdfNearTexelMm, -1, j, needed, rowBuffer, crossings);
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
		field.originMm = {static_cast<int64_t>(coord.x) * kChunkMm, static_cast<int64_t>(coord.y) * kChunkMm};
		field.version  = polygons.version;
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
		field.farTexels.resize(static_cast<size_t>(kFarStride) * kFarStride);
		for (int32_t j = -1; j <= kFarTexels; ++j) {
			sdfRow(rings, thalwegs, field.originMm, kFarTexelMm, -1, j, {},
				   std::span(field.farTexels).subspan(static_cast<size_t>(j + 1) * kFarStride, kFarStride), crossings);
		}
		clearIfUniform(field.farTexels, kLandSdfTexel);

		if (rings.hasShoreVertices()) {
			field.shoreProfile.resize(static_cast<size_t>(kDetailStride) * kDetailStride);
		}
		if (!thalwegs.empty()) {
			field.channelFrame.resize(static_cast<size_t>(kDetailStride) * kDetailStride);
		}
		for (int32_t j = -1; j <= kDetailTexels && (!field.shoreProfile.empty() || !field.channelFrame.empty()); ++j) {
			uint32_t hint = UINT32_MAX;
			for (int32_t i = -1; i <= kDetailTexels; ++i) {
				const Vec2i64 p{
					field.originMm.x + static_cast<int64_t>(i) * kDetailTexelMm + kDetailTexelMm / 2,
					field.originMm.y + static_cast<int64_t>(j) * kDetailTexelMm + kDetailTexelMm / 2
				};
				const size_t index = gutteredIndex(i, j, kDetailStride);
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
