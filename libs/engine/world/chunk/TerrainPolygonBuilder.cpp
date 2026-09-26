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
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace engine::world::terrain_detail {

	namespace {

		constexpr int32_t kFinePerTile	= static_cast<int32_t>(Builder::kTileMm / Builder::kFineCellMm);
		// Fine samples per axis: the extended region inclusive of its max edge.
		constexpr int32_t kFineSamples	= kExtendedSize * kFinePerTile + 1;

		struct WarpSeeds {
			uint32_t bankX;
			uint32_t bankY;
			uint32_t lowX;
			uint32_t lowY;
		};

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
			constexpr int64_t	  kHalfTile = Builder::kTileMm / 2;
			constexpr int64_t	  kPadMm	= static_cast<int64_t>(kCoarsePad) * Builder::kTileMm;
			constexpr int32_t	  kSize		= kExtendedSize + 2 * kCoarsePad;
			geometry::ScalarField coarse(
				{region.extMin.x + kHalfTile - kPadMm, region.extMin.y + kHalfTile - kPadMm}, Builder::kTileMm, kSize, kSize
			);
			constexpr std::array<std::array<int, 3>, 3> kBinomial = {{{1, 2, 1}, {2, 4, 2}, {1, 2, 1}}};
			for (int32_t y = 0; y < kExtendedSize; ++y) {
				for (int32_t x = 0; x < kExtendedSize; ++x) {
					int sum = 0;
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							if (grid.water(x + dx, y + dy)) {
								sum += kBinomial[static_cast<size_t>(dy + 1)][static_cast<size_t>(dx + 1)];
							}
						}
					}
					float value = static_cast<float>(sum) / 16.0F;

					const bool self = grid.water(x, y);
					int		   same = 0;
					for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
						same += grid.water(x + dx, y + dy) == self ? 1 : 0;
					}
					if (same < 2) {
						value = self ? std::max(value, Builder::kThinWaterFloor) : std::min(value, Builder::kThinLandCeil);
					}
					coarse.at(x + kCoarsePad, y + kCoarsePad) = value;
				}
			}
			for (int32_t y = 0; y < kSize; ++y) {
				for (int32_t x = 0; x < kSize; ++x) {
					const int32_t cx = std::clamp(x, kCoarsePad, kCoarsePad + kExtendedSize - 1);
					const int32_t cy = std::clamp(y, kCoarsePad, kCoarsePad + kExtendedSize - 1);
					if (cx != x || cy != y) {
						coarse.at(x, y) = coarse.at(cx, cy);
					}
				}
			}
			return coarse;
		}

		// D6 steps 3-7 on one marched loop, pinned where it crosses the chunk border.
		std::optional<Ring> finishLoop(Ring loop, const Region& region, ChunkCoordinate coord) {
			geometry::chaikin(loop, Builder::kChaikinIterations);
			const std::array<int64_t, 2> xLines = {region.chunkMin.x, region.chunkMax.x};
			const std::array<int64_t, 2> yLines = {region.chunkMin.y, region.chunkMax.y};
			std::vector<uint8_t>		 pinned = geometry::pinAxisLineCrossings(loop, xLines, yLines);
			return resampleSimplifyValidate(std::move(loop), std::move(pinned), coord, "waterline loop");
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
			if (!ExtendedGrid::contains(ex, ey)) {
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

		// Concavity over the arc-length window: the vertex's offset from the chord
		// joining the ring points half a window behind and ahead of it, positive on
		// the water side. A headland bulges into the water (exposed), a bay away
		// from it (sheltered). Maps to [0, 1], 0.5 on a straight shore.
		std::vector<float> concavityExposure(const Ring& ring) {
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
			const double half = std::min(static_cast<double>(Builder::kExposureWindowMm) / 2.0, total / 4.0);

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
				out[i] = static_cast<float>(0.5 + bulge / (2.0 * static_cast<double>(Builder::kExposureBulgeFullScaleMm)));
			}
			return out;
		}

		// Ocean fetch: open biome water along the water-side normal, in tiles, capped.
		// A ray that leaves the extended region counts as reaching the cap (the sea
		// beyond the apron is unknown here and far more often open than not).
		float fetchExposure(const Vec2i64& v, Vec2d normal, const ExtendedGrid& grid) {
			for (int k = 1; k <= Builder::kFetchCapTiles; ++k) {
				const Vec2d p			= probePoint(v, normal, static_cast<double>(k * Builder::kTileMm));
				const auto [ex, ey] = grid.tileAt(p.x, p.y);
				if (!ExtendedGrid::contains(ex, ey)) {
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
			const geometry::ScalarField coarse = buildCoarseField(grid, region);

			const WarpSeeds seeds{
				purposeSeed(worldSeed, kSaltBankX), purposeSeed(worldSeed, kSaltBankY), purposeSeed(worldSeed, kSaltLowX),
				purposeSeed(worldSeed, kSaltLowY)
			};
			geometry::ScalarField fine = geometry::warpField(
				coarse,
				region.extMin,
				Builder::kFineCellMm,
				kFineSamples,
				kFineSamples,
				[&seeds](Vec2i64 worldMm) { return shoreWarpOffset(worldMm, seeds); },
				0.0F,
				geometry::WarpSkip{Builder::kWaterlineIso, Builder::kMaxWarpMm}
			);
			// marchingSquares needs an all-outside border so every loop closes; the
			// closure it makes along the extended boundary is the synthetic edge (D4).
			for (int32_t k = 0; k < kFineSamples; ++k) {
				fine.at(k, 0)				 = 0.0F;
				fine.at(k, kFineSamples - 1) = 0.0F;
				fine.at(0, k)				 = 0.0F;
				fine.at(kFineSamples - 1, k) = 0.0F;
			}

			for (Ring& marched : geometry::marchingSquares(fine, Builder::kWaterlineIso)) {
				std::optional<Ring> ring = finishLoop(std::move(marched), region, coord);
				if (!ring || areaBelowFloor(*ring)) {
					continue;
				}
				TerrainRing terrain;
				terrain.water	 = waterKindOf(*ring, grid);
				terrain.profiles = shoreProfiles(*ring, terrain.water, grid, SideRule::BiomeWater);
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

	Region regionOf(ChunkCoordinate coord) {
		constexpr int64_t kChunkMm = static_cast<int64_t>(kChunkSize) * Builder::kTileMm;
		constexpr int64_t kApronMm = static_cast<int64_t>(kApronTiles) * Builder::kTileMm;
		constexpr int64_t kKeepMm  = kApronMm + static_cast<int64_t>(Builder::kChainKeepMarginM * kMmPerMeter);
		const Vec2i64	  chunkMin{static_cast<int64_t>(coord.x) * kChunkMm, static_cast<int64_t>(coord.y) * kChunkMm};
		const Vec2i64	  chunkMax{chunkMin.x + kChunkMm, chunkMin.y + kChunkMm};
		return {
			chunkMin,
			chunkMax,
			{chunkMin.x - kApronMm, chunkMin.y - kApronMm},
			{chunkMax.x + kApronMm, chunkMax.y + kApronMm},
			{chunkMin.x - kKeepMm, chunkMin.y - kKeepMm},
			{chunkMax.x + kKeepMm, chunkMax.y + kKeepMm}
		};
	}

	std::optional<Ring> resampleSimplifyValidate(Ring loop, std::vector<uint8_t> pinned, ChunkCoordinate coord, const char* what) {
		geometry::resampleRing(loop, Builder::kRingSpacingMm, pinned);
		for (const int64_t epsMm : {Builder::kRingSimplifyEpsMm, Builder::kRingSimplifyRetryEpsMm}) {
			Ring				 simplified = loop;
			std::vector<uint8_t> mask		= pinned;
			geometry::simplifyRing(simplified, epsMm, mask);
			if (geometry::isSimple(simplified).pass) {
				return simplified;
			}
			LOG_DEBUG(World, "Chunk (%d, %d): %s folds when simplified at %lld mm, retrying", coord.x, coord.y, what,
					  static_cast<long long>(epsMm));
		}
		if (geometry::isSimple(loop).pass) {
			LOG_DEBUG(World, "Chunk (%d, %d): keeping a %s unsimplified (%zu vertices)", coord.x, coord.y, what, loop.size());
			return loop;
		}
		LOG_WARNING(World, "Chunk (%d, %d): dropped a non-simple %s (%zu vertices)", coord.x, coord.y, what, loop.size());
		return std::nullopt;
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

	std::vector<ShoreProfile> shoreProfiles(const Ring& ring, WaterKind water, const ExtendedGrid& grid, SideRule rule) {
		const std::vector<float>  concavity = concavityExposure(ring);
		std::vector<ShoreProfile> profiles(ring.size());
		const double			  probeMm = static_cast<double>(Builder::kProfileProbeMm);
		for (size_t i = 0; i < ring.size(); ++i) {
			const Vec2i64& v	  = ring[i];
			const Vec2d	   normal = waterNormal(ring, i);
			ShoreProfile&  p	  = profiles[i];

			float exposure = concavity[i];
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

				if (waterIdx) {
					const TileData& wet	  = grid.tile(waterIdx->first, waterIdx->second);
					const auto [lx, ly] = grid.tileCenterMm(landIdx->first, landIdx->second);
					const auto [wx, wy] = grid.tileCenterMm(waterIdx->first, waterIdx->second);
					const double runCm	= std::sqrt((lx - wx) * (lx - wx) + (ly - wy) * (ly - wy)) / 10.0;
					const double riseCm = std::abs(static_cast<double>(land.elevation) - static_cast<double>(wet.elevation));
					// A probe pair in one tile (a narrow channel) has no run to measure over.
					if (runCm > 0.0) {
						p.slope = toByte(static_cast<float>(riseCm / runCm) / Builder::kSlopeFullScale);
					}
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
		return profiles;
	}

} // namespace engine::world::terrain_detail

namespace engine::world {

	bool isBiomeWater(const TileData& tile) {
		return isWater(tile.primaryBiome) || tile.primaryBiome == Biome::TemperateWetland ||
			   tile.primaryBiome == Biome::TropicalWetland;
	}

	ChunkTerrainPolygons TerrainPolygonBuilder::build(
		ChunkCoordinate				  coord,
		uint64_t					  worldSeed,
		const ExtendedTileFn&		  tiles,
		std::span<const RiverSegment> riverSegments,
		std::span<const Pond>		  ponds
	) {
		using namespace terrain_detail;
		ChunkTerrainPolygons out;
		const Region		 region = regionOf(coord);
		const ExtendedGrid	 grid(tiles, region);
		// An all-land field marches to nothing; skip the fine lattice outright.
		if (grid.anyWater()) {
			buildWaterlines(out.rings, grid, region, worldSeed, coord);
		}
		// Ponds before channels, so a channel's mouth can find a receiving pond (D7).
		buildPonds(out.rings, ponds, grid, region, worldSeed, coord);
		buildChannels(out, riverSegments, grid, region, worldSeed, coord);
		buildNavRings(out, region, coord);
		return out;
	}

} // namespace engine::world
