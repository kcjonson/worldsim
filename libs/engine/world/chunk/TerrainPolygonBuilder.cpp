#include "TerrainPolygonBuilder.h"

#include "world/chunk/TerrainPolygonBuilderDetail.h"

#include <contour/ClipRing.h>
#include <contour/MarchingSquares.h>
#include <contour/RingPins.h>
#include <contour/ScalarField.h>
#include <contour/Smoothing.h>
#include <contour/WarpField.h>
#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace engine::world::terrain_detail {

	namespace {

		constexpr int32_t kFinePerTile = static_cast<int32_t>(Builder::kTileMm / Builder::kFineCellMm);

		// Fine samples per axis: the extended region inclusive of its max edge.
		int32_t fineSamples(const Region& region) {
			return region.extendedSize * kFinePerTile + 1;
		}

		// Coarse samples added beyond the extended region on every side, repeating its
		// edge samples. They cover every read warpField and its skip test make from
		// the fine lattice (warp reach, one fine cell, one bilinear cell, the skip's
		// cell rounding), so the warp never reads the forced-land outside. Land
		// there would put the closure anywhere up to ~1.7 m inside the boundary,
		// where the synthetic-edge rule cannot see it, and would make the whole
		// boundary band pay for warp noise on every water chunk. The closure then
		// comes only from the fine lattice's forced-land border, inside one fine
		// cell of the boundary.
		constexpr int32_t kCoarsePad = static_cast<int32_t>((Builder::kMaxWarpMm + Builder::kFineCellMm) / Builder::kTileMm) + 4;

		// D5 steps 1-3: indicator at tile centers, 3x3 binomial blur (outside reads
		// land), thin-feature guard on the indicator's cardinal neighbors; then the
		// edge padding above.
		geometry::ScalarField buildCoarseField(const ExtendedGrid& grid, const Region& region) {
			constexpr int64_t	  kPadMm = static_cast<int64_t>(kCoarsePad) * Builder::kTileMm;
			const int32_t		  extent = grid.size();
			const int32_t		  size	 = extent + 2 * kCoarsePad;
			geometry::ScalarField coarse(
				{region.extMin.x + kCoarsePhaseMm.x - kPadMm, region.extMin.y + kCoarsePhaseMm.y - kPadMm}, Builder::kTileMm, size, size
			);
			auto water = [&grid](int64_t x, int64_t y) { return grid.water(static_cast<int32_t>(x), static_cast<int32_t>(y)); };
			for (int32_t y = 0; y < extent; ++y) {
				for (int32_t x = 0; x < extent; ++x) {
					coarse.at(x + kCoarsePad, y + kCoarsePad) = coarseWaterValue(water, x, y);
				}
			}
			for (int32_t y = 0; y < size; ++y) {
				for (int32_t x = 0; x < size; ++x) {
					const int32_t cx = std::clamp(x, kCoarsePad, kCoarsePad + extent - 1);
					const int32_t cy = std::clamp(y, kCoarsePad, kCoarsePad + extent - 1);
					if (cx != x || cy != y) {
						coarse.at(x, y) = coarse.at(cx, cy);
					}
				}
			}
			return coarse;
		}

		int64_t floorDiv(int64_t a, int64_t b) {
			const int64_t q = a / b;
			return (a % b != 0 && a < 0) ? q - 1 : q;
		}

		// The ring cut into runs, each from a pin up to (not including) the next,
		// starting at the lexicographically smallest pin so that every simplification
		// rung of one resampled ring splits into the same runs in the same order. A
		// ring without pins is one run.
		std::vector<std::vector<Vec2i64>> splitAtPins(const Ring& ring, const std::vector<uint8_t>& pinned) {
			const size_t n	   = ring.size();
			size_t		 start = n;
			for (size_t i = 0; i < n; ++i) {
				if (pinned[i] != 0 && (start == n || ring[i] < ring[start])) {
					start = i;
				}
			}
			if (start == n) {
				return {std::vector<Vec2i64>(ring.begin(), ring.end())};
			}
			std::vector<std::vector<Vec2i64>> runs;
			for (size_t k = 0; k < n; ++k) {
				const size_t i = (start + k) % n;
				if (pinned[i] != 0) {
					runs.emplace_back();
				}
				runs.back().push_back(ring[i]);
			}
			return runs;
		}

		// The lattice cell a run (plus the pin that ends it) lies in: every vertex is
		// in the closed cell, so the center of their bounds is inside it.
		std::pair<int64_t, int64_t> latticeCellOf(const std::vector<Vec2i64>& run, const Vec2i64& end) {
			Vec2i64 lo = end;
			Vec2i64 hi = end;
			for (const Vec2i64& v : run) {
				lo = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
				hi = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
			}
			return {floorDiv(lo.x + (hi.x - lo.x) / 2, Builder::kPinLatticeMm), floorDiv(lo.y + (hi.y - lo.y) / 2, Builder::kPinLatticeMm)};
		}

		// D6 steps 3-7 on one marched loop, pinned on the world lattice.
		std::optional<Ring> finishLoop(Ring loop, ChunkCoordinate coord) {
			geometry::chaikin(loop, Builder::kChaikinIterations);
			return pinResampleSimplifyValidate(std::move(loop), {}, {}, {}, coord, "waterline loop");
		}

		// Unit normal toward the water at vertex i. Marched rings keep water on their
		// left whatever their orientation (CCW outer, CW hole), and channel and pond
		// rings are CCW with water inside, so it is the left normal of the tangent
		// through the two neighbors.
		Vec2d waterNormal(const Ring& ring, size_t i) {
			const size_t	n	 = ring.size();
			const Vec2i64& prev = ring[(i + n - 1) % n];
			const Vec2i64& next = ring[(i + 1) % n];
			const double	tx	 = static_cast<double>(next.x - prev.x);
			const double	ty	 = static_cast<double>(next.y - prev.y);
			const double	len	 = std::sqrt(tx * tx + ty * ty);
			if (len == 0.0) {
				return {};
			}
			return {-ty / len, tx / len};
		}

		bool sideMatches(const ExtendedGrid& grid, int32_t ex, int32_t ey, SideRule rule, bool wantWater) {
			if (!grid.contains(ex, ey)) {
				return false;
			}
			if (rule == SideRule::BiomeWater) {
				return grid.water(ex, ey) == wantWater;
			}
			return wantWater || (!grid.water(ex, ey) && grid.tile(ex, ey).surface != Surface::Water);
		}

		// The tile on the wanted side of a vertex: the tile under the probe point if
		// it is on that side, else the nearest such tile center in the probe's 3x3
		// neighborhood (ties in scan order). Nullopt if none (a degenerate normal on
		// a sub-tile feature).
		std::optional<std::pair<int32_t, int32_t>> sideTile(const ExtendedGrid& grid, Vec2d probe, SideRule rule, bool wantWater) {
			const auto [px, py] = grid.tileAt(probe.x, probe.y);
			if (sideMatches(grid, px, py, rule, wantWater)) {
				return std::pair{px, py};
			}
			std::optional<std::pair<int32_t, int32_t>> best;
			double									   bestDist2 = 0.0;
			for (int32_t dy = -1; dy <= 1; ++dy) {
				for (int32_t dx = -1; dx <= 1; ++dx) {
					const int32_t tx = px + dx;
					const int32_t ty = py + dy;
					if (!sideMatches(grid, tx, ty, rule, wantWater)) {
						continue;
					}
					const auto [cx, cy] = grid.tileCenterMm(tx, ty);
					const double d2		= (cx - probe.x) * (cx - probe.x) + (cy - probe.y) * (cy - probe.y);
					if (!best || d2 < bestDist2) {
						best	  = std::pair{tx, ty};
						bestDist2 = d2;
					}
				}
			}
			return best;
		}

		Vec2d probePoint(const Vec2i64& v, Vec2d normal, double distanceMm) {
			return {static_cast<double>(v.x) + normal.x * distanceMm, static_cast<double>(v.y) + normal.y * distanceMm};
		}

		// D5: Ocean, Lake, or Wetland by majority over the ring's vertices, each vote
		// from the biome of the water-side tile next to it. Ties go to the lower enum
		// value (Ocean, then Lake, then Wetland).
		WaterKind waterKindOf(const Ring& ring, const ExtendedGrid& grid) {
			std::array<size_t, 3> votes{};
			for (size_t i = 0; i < ring.size(); ++i) {
				const Vec2d probe = probePoint(ring[i], waterNormal(ring, i), static_cast<double>(Builder::kProfileProbeMm));
				const auto	water = sideTile(grid, probe, SideRule::BiomeWater, true);
				if (!water) {
					continue;
				}
				const Biome biome = grid.tile(water->first, water->second).primaryBiome;
				++votes[biome == Biome::Ocean ? 0U : (biome == Biome::Lake ? 1U : 2U)];
			}
			const auto winner = static_cast<size_t>(std::max_element(votes.begin(), votes.end()) - votes.begin());
			return winner == 0 ? WaterKind::Ocean : (winner == 1 ? WaterKind::Lake : WaterKind::Wetland);
		}

		uint8_t toByte(float unit) {
			return static_cast<uint8_t>(std::lround(std::clamp(unit, 0.0F, 1.0F) * 255.0F));
		}

		// Concavity over an arc-length window: the vertex's offset from the chord
		// joining the ring points half a window behind and ahead of it, positive on
		// the water side. A headland bulges into the water, a bay away from it.
		// `fullScaleMm` of bulge maps to 0 or 1; 0.5 on a straight shore.
		std::vector<float> concavity(const Ring& ring, int64_t windowMm, float fullScaleMm) {
			const size_t		n = ring.size();
			std::vector<double> edgeLen(n);
			double				total = 0.0;
			for (size_t i = 0; i < n; ++i) {
				const Vec2i64& a = ring[i];
				const Vec2i64& b = ring[(i + 1) % n];
				const double	dx = static_cast<double>(b.x - a.x);
				const double	dy = static_cast<double>(b.y - a.y);
				edgeLen[i]		   = std::sqrt(dx * dx + dy * dy);
				total += edgeLen[i];
			}
			const double half = std::min(static_cast<double>(windowMm) / 2.0, total / 4.0);

			// Point at arc distance `half` from vertex i, walking forward (+1) or back.
			// half <= total / 4, so the walk never laps the ring.
			auto walk = [&](size_t i, int dir) -> Vec2d {
				double remaining = half;
				size_t cur		 = i;
				while (true) {
					const size_t   nxt = dir > 0 ? (cur + 1) % n : (cur + n - 1) % n;
					const double   len = edgeLen[dir > 0 ? cur : nxt];
					const Vec2i64& a   = ring[cur];
					const Vec2i64& b   = ring[nxt];
					if (len >= remaining) {
						const double t = len > 0.0 ? remaining / len : 0.0;
						return {
							static_cast<double>(a.x) + static_cast<double>(b.x - a.x) * t,
							static_cast<double>(a.y) + static_cast<double>(b.y - a.y) * t
						};
					}
					remaining -= len;
					cur = nxt;
				}
			};

			std::vector<float> out(n, 0.5F);
			for (size_t i = 0; i < n; ++i) {
				const Vec2d	 a	   = walk(i, -1);
				const Vec2d	 b	   = walk(i, +1);
				const double cx	   = b.x - a.x;
				const double cy	   = b.y - a.y;
				const double chord = std::sqrt(cx * cx + cy * cy);
				if (chord == 0.0) {
					continue;
				}
				const double vx	   = static_cast<double>(ring[i].x) - a.x;
				const double vy	   = static_cast<double>(ring[i].y) - a.y;
				const double bulge = (cx * vy - cy * vx) / chord; // + = water side of the chord
				out[i] = static_cast<float>(0.5 + bulge / (2.0 * static_cast<double>(fullScaleMm)));
			}
			return out;
		}

		// Per-vertex edge lengths (edge i runs from vertex i to i + 1).
		std::vector<double> edgeLengths(const Ring& ring) {
			const size_t		n = ring.size();
			std::vector<double> out(n);
			for (size_t i = 0; i < n; ++i) {
				const double dx = static_cast<double>(ring[(i + 1) % n].x - ring[i].x);
				const double dy = static_cast<double>(ring[(i + 1) % n].y - ring[i].y);
				out[i]			= std::sqrt(dx * dx + dy * dy);
			}
			return out;
		}

		// Fill every unresolved value from the nearest resolved vertices within
		// kSlopeInheritReachMm of arc either side, weighted by arc distance (the
		// nearer counts more). Nothing in reach leaves the value at zero.
		void inheritAlongRing(std::vector<float>& values, const std::vector<uint8_t>& resolved, const std::vector<double>& edgeLen) {
			const size_t	   n	  = values.size();
			const double	   reach  = Builder::kSlopeInheritReachMm;
			std::vector<float> filled = values;
			for (size_t i = 0; i < n; ++i) {
				if (resolved[i] != 0) {
					continue;
				}
				double back = 0.0;
				double fwd	= 0.0;
				std::optional<std::pair<float, double>> behind;
				std::optional<std::pair<float, double>> ahead;
				for (size_t k = 1; k < n && !behind; ++k) {
					const size_t j = (i + n - k) % n;
					back += edgeLen[j];
					if (back > reach) {
						break;
					}
					if (resolved[j] != 0) {
						behind = std::pair{values[j], back};
					}
				}
				for (size_t k = 1; k < n && !ahead; ++k) {
					fwd += edgeLen[(i + k - 1) % n];
					if (fwd > reach) {
						break;
					}
					const size_t j = (i + k) % n;
					if (resolved[j] != 0) {
						ahead = std::pair{values[j], fwd};
					}
				}
				if (behind && ahead) {
					const double total = behind->second + ahead->second;
					filled[i] = static_cast<float>(
						(static_cast<double>(behind->first) * ahead->second + static_cast<double>(ahead->first) * behind->second) / total
					);
				} else if (behind) {
					filled[i] = behind->first;
				} else if (ahead) {
					filled[i] = ahead->first;
				}
			}
			values = std::move(filled);
		}

		// Ocean fetch: open biome water along the water-side normal, in tiles, capped.
		// A ray that leaves the extended region counts as reaching the cap (the sea
		// beyond the apron is unknown here and far more often open than not).
		float fetchExposure(const Vec2i64& v, Vec2d normal, const ExtendedGrid& grid) {
			for (int k = 1; k <= Builder::kFetchCapTiles; ++k) {
				const Vec2d p			= probePoint(v, normal, static_cast<double>(k * Builder::kTileMm));
				const auto [ex, ey] = grid.tileAt(p.x, p.y);
				if (!grid.contains(ex, ey)) {
					return 1.0F;
				}
				if (!grid.water(ex, ey)) {
					return static_cast<float>(k - 1) / static_cast<float>(Builder::kFetchCapTiles);
				}
			}
			return 1.0F;
		}

		// ============ Waterline (D5, D6) ============

		void buildWaterlines(std::vector<TerrainRing>& rings, const ExtendedGrid& grid, const Region& region, uint64_t worldSeed,
							 ChunkCoordinate coord) {
			geometry::ScalarField fine = waterlineFineField(grid, region, worldSeed);
			// marchingSquares needs an all-outside border so every loop closes; the
			// closure it makes along the extended boundary is the synthetic edge (D4).
			const int32_t samples = fineSamples(region);
			for (int32_t k = 0; k < samples; ++k) {
				fine.at(k, 0)			= 0.0F;
				fine.at(k, samples - 1) = 0.0F;
				fine.at(0, k)			= 0.0F;
				fine.at(samples - 1, k) = 0.0F;
			}

			for (Ring& marched : geometry::marchingSquares(fine, Builder::kWaterlineIso)) {
				std::optional<Ring> ring = finishLoop(std::move(marched), coord);
				if (!ring || areaBelowFloor(*ring)) {
					continue;
				}
				TerrainRing terrain;
				terrain.water	 = waterKindOf(*ring, grid);
				terrain.profiles = shoreProfiles(*ring, terrain.water, grid, SideRule::BiomeWater, worldSeed);
				for (size_t i = 0; i < ring->size(); ++i) {
					if (isSyntheticEdge((*ring)[i], (*ring)[(i + 1) % ring->size()], region)) {
						terrain.profiles[i].flags |= ShoreProfile::kFlagSynthetic;
					}
				}
				terrain.ring		   = std::move(*ring);
				terrain.kind		   = TerrainRingKind::Waterline;
				terrain.blocksMovement = true;
				terrain.holeCapable	   = true;
				rings.push_back(std::move(terrain));
			}
		}

		void buildNavRings(ChunkTerrainPolygons& out, const Region& region, ChunkCoordinate coord) {
			for (const TerrainRing& terrain : out.rings) {
				for (Ring& piece : geometry::clipRingToRect(terrain.ring, region.chunkRect())) {
					if (!geometry::isSimple(piece).pass) {
						LOG_WARNING(World, "Chunk (%d, %d): dropped a non-simple clipped ring piece (%zu vertices)", coord.x, coord.y,
									piece.size());
						continue;
					}
					TerrainRing navRing;
					navRing.ring		   = std::move(piece);
					navRing.kind		   = terrain.kind;
					navRing.water		   = terrain.water;
					navRing.blocksMovement = terrain.blocksMovement;
					navRing.holeCapable	   = terrain.holeCapable;
					navRing.meanHalfWidthM = terrain.meanHalfWidthM;
					out.navRings.push_back(std::move(navRing));
				}
			}
		}

	} // namespace

	Region regionOf(ChunkCoordinate coord, int32_t apronTiles) {
		constexpr int64_t kChunkMm = static_cast<int64_t>(kChunkSize) * Builder::kTileMm;
		const int64_t	  apronMm  = static_cast<int64_t>(apronTiles) * Builder::kTileMm;
		const Vec2i64	  chunkMin{static_cast<int64_t>(coord.x) * kChunkMm, static_cast<int64_t>(coord.y) * kChunkMm};
		const Vec2i64	  chunkMax{chunkMin.x + kChunkMm, chunkMin.y + kChunkMm};
		return {
			chunkMin,
			chunkMax,
			{chunkMin.x - apronMm, chunkMin.y - apronMm},
			{chunkMax.x + apronMm, chunkMax.y + apronMm},
			apronTiles,
			kChunkSize + 2 * apronTiles
		};
	}

	WarpSeeds warpSeeds(uint64_t worldSeed) {
		return {
			purposeSeed(worldSeed, kSaltBankX), purposeSeed(worldSeed, kSaltBankY), purposeSeed(worldSeed, kSaltLowX),
			purposeSeed(worldSeed, kSaltLowY)
		};
	}

	geometry::WarpOffsetMm shoreWarpOffset(Vec2i64 worldMm, const WarpSeeds& seeds) {
		auto component = [worldMm](uint32_t bankSeed, uint32_t lowSeed) {
			const double bank = static_cast<double>(
				worldNoise(worldMm, Builder::kBankNoiseWavelengthM, bankSeed, Builder::kBankNoiseOctaves)
			);
			const double low = static_cast<double>(
				worldNoise(worldMm, Builder::kShoreLowWavelengthM, lowSeed, Builder::kShoreLowOctaves)
			);
			return static_cast<double>(Builder::kBankNoiseAmpMm) * bank + static_cast<double>(Builder::kShoreLowAmpMm) * low;
		};
		return {component(seeds.bankX, seeds.lowX), component(seeds.bankY, seeds.lowY)};
	}

	geometry::ScalarField waterlineFineField(const ExtendedGrid& grid, const Region& region, uint64_t worldSeed) {
		const geometry::ScalarField coarse = buildCoarseField(grid, region);
		const WarpSeeds				seeds  = warpSeeds(worldSeed);
		const int32_t				samples = fineSamples(region);
		return geometry::warpField(
			coarse,
			region.extMin,
			Builder::kFineCellMm,
			samples,
			samples,
			[&seeds](Vec2i64 worldMm) { return shoreWarpOffset(worldMm, seeds); },
			0.0F,
			geometry::WarpSkip{Builder::kWaterlineIso, Builder::kMaxWarpMm}
		);
	}

	float WaterlineField::valueAt(Vec2i64 worldMm) const {
		const geometry::WarpOffsetMm offset = shoreWarpOffset(worldMm, m_seeds);
		// The indicator over the 4x4 tiles the bilinear read's four blurred samples
		// cover, read once.
		const int64_t cx0 =
			geometry::latticeAxisRead(worldMm.x - kCoarsePhaseMm.x, offset.x, Builder::kTileMm).cell;
		const int64_t cy0 =
			geometry::latticeAxisRead(worldMm.y - kCoarsePhaseMm.y, offset.y, Builder::kTileMm).cell;
		std::array<std::array<bool, 4>, 4> block{};
		for (int64_t j = 0; j < 4; ++j) {
			for (int64_t i = 0; i < 4; ++i) {
				block[static_cast<size_t>(j)][static_cast<size_t>(i)] = m_biomeWater(cx0 - 1 + i, cy0 - 1 + j);
			}
		}
		auto water = [&block, cx0, cy0](int64_t x, int64_t y) {
			return block[static_cast<size_t>(y - cy0 + 1)][static_cast<size_t>(x - cx0 + 1)];
		};
		auto coarse = [&water](int64_t gx, int64_t gy) { return coarseWaterValue(water, gx, gy); };
		return geometry::warpedBilinear(coarse, kCoarsePhaseMm, Builder::kTileMm, worldMm, offset);
	}

	std::optional<Ring> pinResampleSimplifyValidate(Ring loop, std::span<const int64_t> xLines, std::span<const int64_t> yLines,
													std::span<const Vec2i64> extraPins, ChunkCoordinate coord, const char* what) {
		std::vector<uint8_t> pinned = geometry::pinAxisLineCrossings(loop, xLines, yLines, Builder::kPinLatticeMm);
		for (size_t i = 0; i < loop.size(); ++i) {
			if (std::find(extraPins.begin(), extraPins.end(), loop[i]) != extraPins.end()) {
				pinned[i] = 1;
			}
		}
		geometry::resampleRing(loop, Builder::kRingSpacingMm, pinned);

		// Every run between two pins lies in one lattice cell, and two runs can only
		// cross inside the cell they share. So the retry ladder steps per cell: a
		// cell whose runs fold at one rung takes the next (100 mm, 50 mm,
		// unsimplified) while every other cell keeps its own. A fold far from the
		// border then never changes a run near it, which the neighbor, not seeing
		// that fold, would not change either.
		constexpr size_t									kRungs = 3;
		std::array<std::vector<std::vector<Vec2i64>>, kRungs> runs;
		for (size_t rung = 0; rung < kRungs; ++rung) {
			Ring				 version = loop;
			std::vector<uint8_t> mask	 = pinned;
			if (rung == 0 || rung == 1) {
				geometry::simplifyRing(version, rung == 0 ? Builder::kRingSimplifyEpsMm : Builder::kRingSimplifyRetryEpsMm, mask);
			}
			runs[rung] = splitAtPins(version, mask);
		}
		const size_t runCount = runs[0].size();
		assert(runs[1].size() == runCount && runs[2].size() == runCount);

		std::vector<std::pair<int64_t, int64_t>> runCell(runCount);
		for (size_t r = 0; r < runCount; ++r) {
			runCell[r] = latticeCellOf(runs[0][r], runs[0][(r + 1) % runCount].front());
		}
		std::map<std::pair<int64_t, int64_t>, size_t> cellRung;
		while (true) {
			Ring				assembled;
			std::vector<size_t> runOfVertex;
			for (size_t r = 0; r < runCount; ++r) {
				const std::vector<Vec2i64>& run = runs[cellRung[runCell[r]]][r];
				assembled.insert(assembled.end(), run.begin(), run.end());
				runOfVertex.insert(runOfVertex.end(), run.size(), r);
			}
			const geometry::ConstraintResult simple = geometry::isSimple(assembled);
			if (simple.pass) {
				return assembled;
			}
			bool stepped = false;
			for (const size_t edge : {simple.vertexIndex, simple.otherIndex}) {
				size_t& rung = cellRung[runCell[runOfVertex[edge % runOfVertex.size()]]];
				if (rung + 1 < kRungs) {
					++rung;
					stepped = true;
				}
			}
			if (!stepped) {
				LOG_WARNING(World, "Chunk (%d, %d): dropped a non-simple %s (%zu vertices)", coord.x, coord.y, what, loop.size());
				return std::nullopt;
			}
			LOG_DEBUG(World, "Chunk (%d, %d): a %s folds when simplified, retrying finer in its lattice cell", coord.x, coord.y, what);
		}
	}

	bool areaBelowFloor(const Ring& ring) {
		const geometry::Int128 area2 = geometry::signedAreaDoubled(ring);
		const geometry::Int128 abs2	 = area2.sign() < 0 ? -area2 : area2;
		return abs2 < geometry::Int128(2 * Builder::kMinLoopAreaMm2);
	}

	bool isSyntheticEdge(const Vec2i64& a, const Vec2i64& b, const Region& region) {
		constexpr int64_t kBand = Builder::kFineCellMm;
		auto			  near	= [&region](const Vec2i64& v, int side) {
			   switch (side) {
				   case 0:
					   return v.x - region.extMin.x <= kBand;
				   case 1:
					   return region.extMax.x - v.x <= kBand;
				   case 2:
					   return v.y - region.extMin.y <= kBand;
				   default:
					   return region.extMax.y - v.y <= kBand;
			   }
		};
		for (int side = 0; side < 4; ++side) {
			if (near(a, side) && near(b, side)) {
				return true;
			}
		}
		return false;
	}

	bool isOnExtendedSide(const Vec2i64& a, const Vec2i64& b, const Region& region) {
		return (a.x == region.extMin.x && b.x == region.extMin.x) || (a.x == region.extMax.x && b.x == region.extMax.x) ||
			   (a.y == region.extMin.y && b.y == region.extMin.y) || (a.y == region.extMax.y && b.y == region.extMax.y);
	}

	std::vector<ShoreProfile> shoreProfiles(const Ring& ring, WaterKind water, const ExtendedGrid& grid, SideRule rule, uint64_t worldSeed) {
		const size_t			  n			  = ring.size();
		const std::vector<float>  exposureCon = concavity(ring, Builder::kExposureWindowMm, Builder::kExposureBulgeFullScaleMm);
		const std::vector<float>  bend		  = water == WaterKind::River
													? concavity(ring, Builder::kChannelBendWindowMm, Builder::kChannelBendBulgeFullScaleMm)
													: std::vector<float>{};
		const uint32_t			  slopeSeed	  = purposeSeed(worldSeed, kSaltShoreSlope);
		std::vector<ShoreProfile> profiles(n);
		std::vector<float>		  rise(n, 0.0F);
		std::vector<uint8_t>	  riseResolved(n, 0);
		const double			  probeMm = static_cast<double>(Builder::kProfileProbeMm);
		for (size_t i = 0; i < n; ++i) {
			const Vec2i64& v	  = ring[i];
			const Vec2d	   normal = waterNormal(ring, i);
			ShoreProfile&  p	  = profiles[i];

			float exposure = exposureCon[i];
			if (water == WaterKind::Ocean) {
				exposure = (1.0F - Builder::kFetchWeight) * exposure + Builder::kFetchWeight * fetchExposure(v, normal, grid);
			}
			exposure   = std::clamp(exposure, 0.0F, 1.0F);
			p.exposure = toByte(exposure);

			const auto landIdx	= sideTile(grid, probePoint(v, normal, -probeMm), rule, false);
			const auto waterIdx = sideTile(grid, probePoint(v, normal, probeMm), rule, true);
			if (landIdx) {
				const TileData& land = grid.tile(landIdx->first, landIdx->second);
				p.moisture			 = land.moisture;

				// Rise of the land kSlopeRunMm inland over the water level beside the vertex.
				const Vec2d run	   = probePoint(v, normal, -static_cast<double>(Builder::kSlopeRunMm));
				const auto [rx, ry] = grid.tileAt(run.x, run.y);
				if (waterIdx && sideMatches(grid, rx, ry, rule, false)) {
					const double levelCm = static_cast<double>(grid.tile(waterIdx->first, waterIdx->second).elevation);
					const double riseCm	 = std::max(0.0, static_cast<double>(grid.tile(rx, ry).elevation) - levelCm);
					const double grade	 = riseCm / (static_cast<double>(Builder::kSlopeRunMm) / 10.0);
					rise[i]				 = static_cast<float>(
						 static_cast<double>(Builder::kSlopeRiseWeight) * grade / (grade + static_cast<double>(Builder::kSlopeRiseHalfGrade))
					 );
					riseResolved[i] = 1;
				}

				const bool rocky = land.surface == Surface::Rock;
				const bool sandy = land.primaryBiome == Biome::Beach || land.surface == Surface::Sand;
				const bool muddy = land.surface == Surface::Mud || land.surface == Surface::Dirt;
				if (rocky) {
					p.flags |= ShoreProfile::kFlagRock;
				} else if (sandy || muddy) {
					const float base  = sandy ? Builder::kDominantSubstrateShare : 1.0F - Builder::kDominantSubstrateShare;
					const float share = std::clamp(base + Builder::kExposureSubstrateBias * (2.0F * exposure - 1.0F), 0.0F, 1.0F);
					p.sand			  = toByte(share);
					p.mud			  = static_cast<uint8_t>(255 - p.sand);
				}
			}
		}

		inheritAlongRing(rise, riseResolved, edgeLengths(ring));
		for (size_t i = 0; i < n; ++i) {
			float base = 0.0F;
			switch (water) {
				case WaterKind::Ocean:
					base = Builder::kSlopeOcean;
					break;
				case WaterKind::Lake:
					base = Builder::kSlopeLake;
					break;
				case WaterKind::Wetland:
					base = Builder::kSlopeWetland;
					break;
				case WaterKind::Pond:
					base = Builder::kSlopePond;
					break;
				case WaterKind::River:
					// bend > 0.5: the land bulges into the water, an inner bank.
					base = Builder::kSlopeChannelStraight - Builder::kSlopeChannelBendGain * (2.0F * bend[i] - 1.0F);
					break;
			}
			const float noise =
				Builder::kSlopeNoiseAmp * worldNoise(ring[i], Builder::kSlopeNoiseWavelengthM, slopeSeed, Builder::kSlopeNoiseOctaves);
			profiles[i].slope = toByte(std::clamp(base + rise[i] + noise, Builder::kSlopeMin, Builder::kSlopeMax));
		}
		return profiles;
	}

} // namespace engine::world::terrain_detail

namespace engine::world {

	bool isBiomeWater(Biome primaryBiome) {
		return isWater(primaryBiome) || primaryBiome == Biome::TemperateWetland || primaryBiome == Biome::TropicalWetland;
	}

	bool isBiomeWater(const TileData& tile) {
		return isBiomeWater(tile.primaryBiome);
	}

	ChunkTerrainPolygons TerrainPolygonBuilder::build(
		ChunkCoordinate				  coord,
		uint64_t					  worldSeed,
		const ExtendedTileFn&		  tiles,
		const BiomeWaterFn&			  biomeWater,
		std::span<const RiverSegment> riverSegments,
		std::span<const Pond>		  ponds,
		int32_t						  apronTiles
	) {
		using namespace terrain_detail;
		ChunkTerrainPolygons out;
		const Region		 region = regionOf(coord, apronTiles);
		const ExtendedGrid	 grid(tiles, biomeWater, region);
		// An all-land field marches to nothing; skip the fine lattice outright.
		if (grid.anyWater()) {
			buildWaterlines(out.rings, grid, region, worldSeed, coord);
		}
		buildPonds(out.rings, ponds, grid, region, worldSeed, coord);
		const WaterlineField waterline(biomeWater, worldSeed);
		buildChannels(out, riverSegments, ponds, waterline, grid, region, worldSeed, coord);
		buildNavRings(out, region, coord);
		return out;
	}

} // namespace engine::world
