#include "TerrainPolygonBuilder.h"

#include "world/chunk/ChunkSampleResult.h"

#include <contour/CatmullRom.h>
#include <contour/ClipRing.h>
#include <contour/MarchingSquares.h>
#include <contour/RingPins.h>
#include <contour/ScalarField.h>
#include <contour/Smoothing.h>
#include <contour/Stroke.h>
#include <contour/WarpField.h>
#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <math/DeterministicMath.h>
#include <offset/WallOffset.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>
#include <random/HashNoise.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace engine::world {

	bool isBiomeWater(const TileData& tile) {
		return isWater(tile.primaryBiome) || tile.primaryBiome == Biome::TemperateWetland ||
			   tile.primaryBiome == Biome::TropicalWetland;
	}

	namespace {

		using geometry::Ring;
		using geometry::Vec2d;
		using geometry::Vec2i64;
		using Builder = TerrainPolygonBuilder;

		constexpr int32_t kFinePerTile	= static_cast<int32_t>(Builder::kTileMm / Builder::kFineCellMm);
		// Fine samples per axis: the extended region inclusive of its max edge.
		constexpr int32_t kFineSamples	= kExtendedSize * kFinePerTile + 1;
		constexpr double  kMmPerMeter	= static_cast<double>(geometry::kMillimetersPerMeter);

		// The longest river sub-segment the gather emits (RiverNetwork2D's trunk
		// step). The kept region must stop a full step short of the gather margin,
		// so the chain-end Catmull-Rom span, which uses a phantom point, is trimmed.
		constexpr double kGatherStepM = 20.0;
		static_assert(
			static_cast<double>(kApronTiles) + Builder::kChainKeepMarginM + kGatherStepM <= kRiverGatherMarginM,
			"trimmed chains must end before the span the gather cut distorts"
		);

		// Per-purpose salts mixed with the world seed, so each noise term has its own
		// stream and no other system that hashes the world seed lines up with them.
		constexpr uint32_t kSaltBankX		  = 0x5A17B001U;
		constexpr uint32_t kSaltBankY		  = 0x5A17B002U;
		constexpr uint32_t kSaltLowX		  = 0x5A17B003U;
		constexpr uint32_t kSaltLowY		  = 0x5A17B004U;
		constexpr uint32_t kSaltLeftBankFine  = 0x5A17B005U;
		constexpr uint32_t kSaltLeftBankLow	  = 0x5A17B006U;
		constexpr uint32_t kSaltRightBankFine = 0x5A17B007U;
		constexpr uint32_t kSaltRightBankLow  = 0x5A17B008U;
		constexpr uint32_t kSaltPondRim		  = 0x5A17B009U;

		uint32_t purposeSeed(uint64_t worldSeed, uint32_t salt) {
			return foundation::hash3(static_cast<int32_t>(salt), static_cast<int32_t>(worldSeed >> 32U), 0,
									 static_cast<uint32_t>(worldSeed));
		}

		struct WarpSeeds {
			uint32_t bankX;
			uint32_t bankY;
			uint32_t lowX;
			uint32_t lowY;
		};

		// fBm at a world position, clamped to [-1, 1] (gradient noise can overshoot
		// slightly, and kMaxWarpMm must be a true bound). The input is formed from the
		// exact integer mm alone, never a chunk-relative value, so every chunk feeds
		// the noise bit-identical inputs for the same world point. fractalNoise2 is
		// fractalNoise3 at z = 0 bit for bit, at half the hashing.
		float worldNoise(Vec2i64 worldMm, double wavelengthM, uint32_t seed, int octaves) {
			const auto	x = static_cast<float>(static_cast<double>(worldMm.x) / kMmPerMeter / wavelengthM);
			const auto	y = static_cast<float>(static_cast<double>(worldMm.y) / kMmPerMeter / wavelengthM);
			const float n = foundation::fractalNoise2(x, y, seed, octaves, Builder::kNoiseLacunarity, Builder::kNoiseGain);
			return std::clamp(n, -1.0F, 1.0F);
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

		Vec2i64 toMm(const Vec2d& meters) {
			return {std::llround(meters.x * kMmPerMeter), std::llround(meters.y * kMmPerMeter)};
		}

		Vec2d toMeters(const Vec2i64& mm) {
			return {static_cast<double>(mm.x) / kMmPerMeter, static_cast<double>(mm.y) / kMmPerMeter};
		}

		// Spelled out rather than std::lerp, whose rounding is left to each standard
		// library (D14).
		double lerp(double a, double b, double t) {
			return a + (b - a) * t;
		}

		double smoothstep(double x) {
			const double t = std::clamp(x, 0.0, 1.0);
			return t * t * (3.0 - 2.0 * t);
		}

		// The chunk square and the extended region (chunk plus apron), world mm, plus
		// the wider box river centerlines are kept in.
		struct Region {
			Vec2i64 chunkMin;
			Vec2i64 chunkMax;
			Vec2i64 extMin;
			Vec2i64 extMax;
			Vec2i64 keepMin;
			Vec2i64 keepMax;

			[[nodiscard]] geometry::RectMm chunkRect() const { return {chunkMin, chunkMax}; }
			[[nodiscard]] geometry::RectMm extendedRect() const { return {extMin, extMax}; }

			[[nodiscard]] bool inExtended(const Vec2i64& p) const {
				return p.x >= extMin.x && p.x <= extMax.x && p.y >= extMin.y && p.y <= extMax.y;
			}
			[[nodiscard]] bool inKept(const Vec2i64& p) const {
				return p.x >= keepMin.x && p.x <= keepMax.x && p.y >= keepMin.y && p.y <= keepMax.y;
			}
		};

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

		// Extended-region tiles plus the biome water indicator, read once.
		class ExtendedGrid {
		  public:
			ExtendedGrid(const Builder::ExtendedTileFn& tiles, const Region& region)
				: m_tiles(tiles),
				  m_extMin(region.extMin),
				  m_water(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kExtendedSize)) {
				for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
					for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
						m_water[index(ex, ey)] = isBiomeWater(tiles(ex, ey)) ? 1 : 0;
						m_anyWater			   = m_anyWater || m_water[index(ex, ey)] != 0;
					}
				}
			}

			[[nodiscard]] bool anyWater() const { return m_anyWater; }

			[[nodiscard]] static bool contains(int32_t ex, int32_t ey) {
				return ex >= 0 && ey >= 0 && ex < kExtendedSize && ey < kExtendedSize;
			}

			// Outside the extended region is land (D4).
			[[nodiscard]] bool water(int32_t ex, int32_t ey) const { return contains(ex, ey) && m_water[index(ex, ey)] != 0; }

			[[nodiscard]] const TileData& tile(int32_t ex, int32_t ey) const { return m_tiles(ex, ey); }

			// Extended tile containing world point (mm, fractional allowed).
			[[nodiscard]] std::pair<int32_t, int32_t> tileAt(double xMm, double yMm) const {
				const double tile = static_cast<double>(Builder::kTileMm);
				return {
					static_cast<int32_t>(std::floor((xMm - static_cast<double>(m_extMin.x)) / tile)),
					static_cast<int32_t>(std::floor((yMm - static_cast<double>(m_extMin.y)) / tile))
				};
			}

			[[nodiscard]] std::pair<double, double> tileCenterMm(int32_t ex, int32_t ey) const {
				const double half = static_cast<double>(Builder::kTileMm) / 2.0;
				return {
					static_cast<double>(m_extMin.x + static_cast<int64_t>(ex) * Builder::kTileMm) + half,
					static_cast<double>(m_extMin.y + static_cast<int64_t>(ey) * Builder::kTileMm) + half
				};
			}

		  private:
			static size_t index(int32_t ex, int32_t ey) {
				return static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex);
			}

			const Builder::ExtendedTileFn& m_tiles;
			Vec2i64						   m_extMin;
			std::vector<uint8_t>		   m_water;
			bool						   m_anyWater = false;
		};

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

		// The shared tail of every ring (D6 steps 4-7): resample between the pins,
		// then simplify, validating after the last change. The retry ladder only
		// loosens the simplification, never the shape: a per-chunk shape change
		// would break the seam, since the neighbor would not make the same choice.
		// Returns nullopt when every rung folds.
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

		// D6 steps 3-7 on one marched loop, pinned where it crosses the chunk border.
		std::optional<Ring> finishLoop(Ring loop, const Region& region, ChunkCoordinate coord) {
			geometry::chaikin(loop, Builder::kChaikinIterations);
			const std::array<int64_t, 2> xLines = {region.chunkMin.x, region.chunkMax.x};
			const std::array<int64_t, 2> yLines = {region.chunkMin.y, region.chunkMax.y};
			std::vector<uint8_t>		 pinned = geometry::pinAxisLineCrossings(loop, xLines, yLines);
			return resampleSimplifyValidate(std::move(loop), std::move(pinned), coord, "waterline loop");
		}

		bool areaBelowFloor(const Ring& ring) {
			const geometry::Int128 area2 = geometry::signedAreaDoubled(ring);
			const geometry::Int128 abs2	 = area2.sign() < 0 ? -area2 : area2;
			return abs2 < geometry::Int128(2 * Builder::kMinLoopAreaMm2);
		}

		// D4: an edge whose endpoints both lie within one fine cell of the same side
		// of the extended rectangle is the closure the forced-land boundary makes.
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

		// D4 for clipped vector rings: clipRingToRect closes a ring exactly along the
		// extended rectangle, so an edge is synthetic when both ends lie on one side.
		bool isOnExtendedSide(const Vec2i64& a, const Vec2i64& b, const Region& region) {
			return (a.x == region.extMin.x && b.x == region.extMin.x) || (a.x == region.extMax.x && b.x == region.extMax.x) ||
				   (a.y == region.extMin.y && b.y == region.extMin.y) || (a.y == region.extMax.y && b.y == region.extMax.y);
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

		// Which tiles count as the land side and the water side of a ring vertex.
		// Waterline rings split on the biome water indicator. Channel and pond
		// water is not biome water, and the tile raster paints it Surface::Water
		// (with a 0.8 m half-width floor, so a probe past a narrow bank can still
		// land on it): their land side is a tile that is neither, their water side
		// whatever tile the probe falls in.
		enum class SideRule : uint8_t { BiomeWater, VectorWater };

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

		// D15 per-vertex profile. Every scale is a named constant on the builder.
		// Flags other than rock are the caller's (synthetic and fordable-cut edges
		// are found differently per ring kind).
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

		// ============ Vector rings: ponds and channels (D7, D8) ============

		// Clip a pond or channel ring to the extended region, pin it on the chunk
		// border and extended boundary lines (and at `extraPins`, the fordable cut
		// vertices), then the shared tail. The extended boundary is pinned too so
		// resampling cannot chamfer the corner where a bank meets its closure,
		// leaving an unflagged sliver of closure inside the region.
		std::vector<Ring> finishVectorRing(const Ring& ring, std::span<const Vec2i64> extraPins, const Region& region,
										   ChunkCoordinate coord, const char* what) {
			std::vector<Ring> out;
			if (!geometry::isSimple(ring).pass) {
				// The radius clamp keeps the inner bank under the local radius, which
				// is enough while the half-width stays under the bend's tightest
				// radius. Past that, or where the centerline comes back within its
				// own width, the banks cross beyond any one sample's reach.
				LOG_WARNING(World, "Chunk (%d, %d): dropped a self-overlapping %s (%zu vertices)", coord.x, coord.y, what, ring.size());
				return out;
			}
			const std::array<int64_t, 4> xLines = {region.extMin.x, region.chunkMin.x, region.chunkMax.x, region.extMax.x};
			const std::array<int64_t, 4> yLines = {region.extMin.y, region.chunkMin.y, region.chunkMax.y, region.extMax.y};
			for (Ring& piece : geometry::clipRingToRect(ring, region.extendedRect())) {
				std::vector<uint8_t> pinned = geometry::pinAxisLineCrossings(piece, xLines, yLines);
				for (size_t i = 0; i < piece.size(); ++i) {
					if (std::find(extraPins.begin(), extraPins.end(), piece[i]) != extraPins.end()) {
						pinned[i] = 1;
					}
				}
				std::optional<Ring> finished = resampleSimplifyValidate(std::move(piece), std::move(pinned), coord, what);
				if (finished && !areaBelowFloor(*finished)) {
					out.push_back(std::move(*finished));
				}
			}
			return out;
		}

		TerrainRing vectorTerrainRing(Ring ring, TerrainRingKind kind, WaterKind water, std::span<const Vec2i64> cutVertices,
									  const ExtendedGrid& grid, const Region& region) {
			TerrainRing terrain;
			terrain.profiles = shoreProfiles(ring, water, grid, SideRule::VectorWater);
			for (size_t i = 0; i < ring.size(); ++i) {
				if (isOnExtendedSide(ring[i], ring[(i + 1) % ring.size()], region)) {
					terrain.profiles[i].flags |= ShoreProfile::kFlagSynthetic;
				}
				if (std::find(cutVertices.begin(), cutVertices.end(), ring[i]) != cutVertices.end()) {
					terrain.profiles[i].flags |= ShoreProfile::kFlagFordableCut;
				}
			}
			terrain.ring		   = std::move(ring);
			terrain.kind		   = kind;
			terrain.water		   = water;
			terrain.blocksMovement = true;
			terrain.holeCapable	   = false;
			return terrain;
		}

		// D8: the rim at kPondRimSpacingM arc spacing from theta 0, each vertex's
		// radius perturbed by world-space noise read at the unperturbed rim point. A
		// radial perturbation of a star-shaped rim stays simple.
		Ring pondRim(const Builder::Pond& pond, uint32_t seed) {
			constexpr double kTwoPi		   = 2.0 * std::numbers::pi;
			const double	 circumference = kTwoPi * static_cast<double>(pond.radius);
			const auto		 count		   = std::max<size_t>(8, static_cast<size_t>(std::ceil(circumference / Builder::kPondRimSpacingM)));
			const double	 noiseAmpM	   = static_cast<double>(Builder::kPondRimNoiseAmpMm) / kMmPerMeter;
			Ring			 ring(count);
			for (size_t k = 0; k < count; ++k) {
				const double theta	= kTwoPi * static_cast<double>(k) / static_cast<double>(count);
				const double radius = worldgen::PondNetwork2D::rimRadiusAt(pond, theta);
				const double c		= foundation::det_math::cos(theta);
				const double s		= foundation::det_math::sin(theta);
				const Vec2d	 rim{pond.cx + radius * c, pond.cy + radius * s};
				const double noise = noiseAmpM * static_cast<double>(worldNoise(
													 toMm(rim), Builder::kBankNoiseWavelengthM, seed, Builder::kBankNoiseOctaves
												 ));
				ring[k] = toMm({pond.cx + (radius + noise) * c, pond.cy + (radius + noise) * s});
			}
			return ring;
		}

		void buildPonds(std::vector<TerrainRing>& rings, std::span<const Builder::Pond> ponds, const ExtendedGrid& grid,
						const Region& region, uint64_t worldSeed, ChunkCoordinate coord) {
			const uint32_t seed = purposeSeed(worldSeed, kSaltPondRim);
			for (const Builder::Pond& pond : ponds) {
				for (Ring& ring : finishVectorRing(pondRim(pond, seed), {}, region, coord, "pond ring")) {
					rings.push_back(vectorTerrainRing(std::move(ring), TerrainRingKind::Pond, WaterKind::Pond, {}, grid, region));
				}
			}
		}

		// ---- Chains: gathered segments joined end to end (D7 step 1) ----

		struct ChainNode {
			Vec2i64 mm;
			double	halfWidthM = 0.0;
		};
		using Chain = std::vector<ChainNode>;

		// Segments joined by exact endpoint match after quantizing to the mm, which
		// absorbs the float noise between a coarse-tile pair's last point and the
		// next pair's first. A node that is not one-in, one-out ends every chain
		// through it. A node's position is its mm key, so every chunk that has the
		// node uses the same point; its half-width is the incoming segment's (the
		// outgoing one's at a chain start), which every chunk that has the node as
		// a chain interior agrees on.
		std::vector<Chain> joinChains(std::span<const Builder::RiverSegment> segments) {
			struct Edge {
				Vec2i64 a;
				Vec2i64 b;
				double	hw0 = 0.0;
				double	hw1 = 0.0;
			};
			std::vector<Edge> edges;
			edges.reserve(segments.size());
			for (const Builder::RiverSegment& s : segments) {
				const Vec2i64 a = toMm({s.x0, s.y0});
				const Vec2i64 b = toMm({s.x1, s.y1});
				if (a != b) {
					edges.push_back({a, b, static_cast<double>(s.halfWidth0), static_cast<double>(s.halfWidth1)});
				}
			}
			std::sort(edges.begin(), edges.end(), [](const Edge& l, const Edge& r) {
				return std::tie(l.a, l.b, l.hw0, l.hw1) < std::tie(r.a, r.b, r.hw0, r.hw1);
			});
			edges.erase(
				std::unique(edges.begin(), edges.end(), [](const Edge& l, const Edge& r) { return l.a == r.a && l.b == r.b; }),
				edges.end()
			);

			struct Node {
				int	   in	   = 0;
				int	   out	   = 0;
				size_t outEdge = 0;
			};
			std::map<Vec2i64, Node> nodes;
			for (size_t i = 0; i < edges.size(); ++i) {
				Node& from = nodes[edges[i].a];
				++from.out;
				from.outEdge = i;
				++nodes[edges[i].b].in;
			}
			auto through = [&nodes](const Vec2i64& v) {
				const Node& node = nodes.at(v);
				return node.in == 1 && node.out == 1;
			};

			std::vector<Chain>	 chains;
			std::vector<uint8_t> used(edges.size(), 0);
			auto				 walk = [&](size_t first) {
				Chain  chain{{edges[first].a, edges[first].hw0}};
				size_t e = first;
				while (true) {
					used[e] = 1;
					chain.push_back({edges[e].b, edges[e].hw1});
					if (!through(edges[e].b)) {
						break;
					}
					const size_t next = nodes.at(edges[e].b).outEdge;
					if (used[next] != 0) {
						break; // a closed loop of through nodes
					}
					e = next;
				}
				chains.push_back(std::move(chain));
			};
			for (size_t i = 0; i < edges.size(); ++i) {
				if (!through(edges[i].a)) {
					walk(i);
				}
			}
			for (size_t i = 0; i < edges.size(); ++i) {
				if (used[i] == 0) {
					walk(i);
				}
			}
			return chains;
		}

		// Drops nodes that sit closer than kMinChainSpanM to their predecessor (the
		// original one, so the choice is per node pair, not a running state). At the
		// chain's end the node before the last goes instead, keeping the end point.
		// Returns false when the chain collapses to a stub.
		bool dropShortSpans(Chain& chain) {
			static constexpr double kMinMm = Builder::kMinChainSpanM * kMmPerMeter;
			auto					shortSpan = [&chain](size_t i) {
				   const double dx = static_cast<double>(chain[i].mm.x - chain[i - 1].mm.x);
				   const double dy = static_cast<double>(chain[i].mm.y - chain[i - 1].mm.y);
				   return std::sqrt(dx * dx + dy * dy) < kMinMm;
			};
			const size_t		 n = chain.size();
			std::vector<uint8_t> drop(n, 0);
			for (size_t i = 1; i + 1 < n; ++i) {
				drop[i] = shortSpan(i) ? 1 : 0;
			}
			if (n > 2 && shortSpan(n - 1)) {
				drop[n - 2] = 1;
			}
			if (n == 2 && shortSpan(1)) {
				return false;
			}
			Chain kept;
			kept.reserve(n);
			for (size_t i = 0; i < n; ++i) {
				if (drop[i] == 0) {
					kept.push_back(chain[i]);
				}
			}
			chain = std::move(kept);
			return chain.size() >= 2;
		}

		// ---- Reaches: the stretches of a chain's centerline that become ribbons ----

		enum class Ground : uint8_t { Unknown, Land, Water };

		// Waterline and pond rings, for the mouth test. Waterline rings count by
		// even-odd parity (islands are CW holes), ponds as solid.
		class WaterBodies {
		  public:
			explicit WaterBodies(const std::vector<TerrainRing>& rings) {
				for (const TerrainRing& ring : rings) {
					Entry entry{&ring.ring, ring.ring.front(), ring.ring.front(), ring.holeCapable};
					for (const Vec2i64& v : ring.ring) {
						entry.lo = {std::min(entry.lo.x, v.x), std::min(entry.lo.y, v.y)};
						entry.hi = {std::max(entry.hi.x, v.x), std::max(entry.hi.y, v.y)};
					}
					m_entries.push_back(entry);
				}
			}

			[[nodiscard]] bool contains(const Vec2i64& p) const {
				bool parity = false;
				for (const Entry& e : m_entries) {
					if (p.x < e.lo.x || p.x > e.hi.x || p.y < e.lo.y || p.y > e.hi.y) {
						continue;
					}
					if (geometry::pointInPolygon(p, *e.ring) == geometry::PointInPolygon::Outside) {
						continue;
					}
					if (!e.evenOdd) {
						return true;
					}
					parity = !parity;
				}
				return parity;
			}

		  private:
			struct Entry {
				const Ring* ring;
				Vec2i64		lo;
				Vec2i64		hi;
				bool		evenOdd;
			};
			std::vector<Entry> m_entries;
		};

		struct ReachPoint {
			Vec2d  position;			  // world meters
			double rawHalfWidthM = 0.0;	  // the segment half-width, which fordability reads
			double flare		 = 1.0;	  // mouth widening, a factor on the half-width
			double asymmetry	 = 1.0;	  // share of the bend asymmetry kept, faded out over a flare
			float  widthRatio	 = 1.0F;  // ThalwegPath::widthRatio
		};

		struct Reach {
			std::vector<ReachPoint> points;
			// Ends where the chain was trimmed to the kept region: a butt cap, well
			// outside the extended region, never seen.
			bool trimmedStart = false;
			bool trimmedEnd	  = false;
		};

		// hw / mean hw over the samples within half a window each way (arc length,
		// summed outward from the sample so it never depends on the chain's start).
		// Raw segment widths: the riffle/pool modulation is the signal (R4); a mouth
		// flare is not a riffle.
		std::vector<float> widthRatios(const std::vector<geometry::CenterlineSample>& samples) {
			const size_t	   n = samples.size();
			std::vector<float> out(n, 1.0F);
			for (size_t i = 0; i < n; ++i) {
				const double half  = 0.5 * Builder::kThalwegWindowW * 2.0 * samples[i].halfWidthM;
				double		 sum   = samples[i].halfWidthM;
				int			 count = 1;
				double		 d	   = 0.0;
				for (size_t k = i; k > 0; --k) {
					d += geometry::length(samples[k].position - samples[k - 1].position);
					if (d > half) {
						break;
					}
					sum += samples[k - 1].halfWidthM;
					++count;
				}
				d = 0.0;
				for (size_t k = i; k + 1 < n; ++k) {
					d += geometry::length(samples[k + 1].position - samples[k].position);
					if (d > half) {
						break;
					}
					sum += samples[k + 1].halfWidthM;
					++count;
				}
				const double mean = sum / static_cast<double>(count);
				out[i]			  = mean > 0.0 ? static_cast<float>(samples[i].halfWidthM / mean) : 1.0F;
			}
			return out;
		}

		// One run of kept samples [first, last] of a chain into reaches, handling
		// river mouths (D7). Samples are Land or Water inside the extended region
		// and Unknown outside it. Where the centerline passes from Land into Water
		// (an inflow mouth at s_m), the channel flares over [s_m - 2 w, s_m] and
		// runs on 1 w into the water body, then stops; where it passes from Water
		// onto Land (a lake outlet), it starts 1 w inside the water. Water with no
		// Land transition in view is dropped: it lies inside the receiving body.
		// Every decision is a function of samples near the transition and of the
		// receiving rings, never of where the chain starts or ends, so neighbors
		// agree wherever they see the same transition.
		void splitRun(const std::vector<geometry::CenterlineSample>& samples, const std::vector<float>& ratios,
					  const std::vector<Ground>& ground, size_t first, size_t last, std::vector<Reach>& out) {
			const size_t		 count = last - first + 1;
			std::vector<uint8_t> include(count, 1);
			std::vector<double>	 flare(count, 1.0);
			std::vector<double>	 asymmetry(count, 1.0);
			auto				 pos = [&samples](size_t i) { return samples[i].position; };
			auto				 hw	 = [&samples](size_t i) { return samples[i].halfWidthM; };

			std::optional<double> extensionM; // past the chain's true end, into the water body

			for (size_t a = first; a <= last;) {
				if (ground[a] != Ground::Water) {
					++a;
					continue;
				}
				size_t b = a;
				while (b < last && ground[b + 1] == Ground::Water) {
					++b;
				}
				for (size_t k = a; k <= b; ++k) {
					include[k - first] = 0;
				}

				const bool inflow  = a > first && ground[a - 1] == Ground::Land;
				const bool outflow = b < last && ground[b + 1] == Ground::Land;
				size_t	   inEnd   = a;
				size_t	   outFrom = b;
				if (inflow) {
					const double w		 = 2.0 * hw(a);
					const double extendM = Builder::kMouthExtendW * w;
					double		 d		 = 0.0;
					include[a - first]	 = 1;
					while (inEnd < b) {
						const double step = geometry::length(pos(inEnd + 1) - pos(inEnd));
						if (d + step > extendM) {
							break;
						}
						d += step;
						++inEnd;
						include[inEnd - first] = 1;
					}
					if (inEnd == b && b == samples.size() - 1 && d < extendM) {
						extensionM = extendM - d;
					}
					for (size_t k = a; k <= inEnd; ++k) {
						flare[k - first]	 = Builder::kMouthFlare;
						asymmetry[k - first] = 0.0;
					}
					const double flareM = Builder::kMouthFlareW * w;
					double		 up		= 0.0;
					for (size_t k = a; k > first; --k) {
						up += geometry::length(pos(k) - pos(k - 1));
						if (up >= flareM) {
							break;
						}
						const double ramp	 = smoothstep(1.0 - up / flareM);
						const size_t idx	 = k - 1 - first;
						flare[idx]			 = std::max(flare[idx], 1.0 + (Builder::kMouthFlare - 1.0) * ramp);
						asymmetry[idx]		 = std::min(asymmetry[idx], 1.0 - ramp);
					}
				}
				if (outflow) {
					const double extendM = Builder::kMouthExtendW * 2.0 * hw(b);
					double		 d		 = 0.0;
					include[b - first]	 = 1;
					while (outFrom > a) {
						const double step = geometry::length(pos(outFrom) - pos(outFrom - 1));
						if (d + step > extendM) {
							break;
						}
						d += step;
						--outFrom;
						include[outFrom - first] = 1;
					}
				}
				// A crossing narrower than the two overlaps is kept whole.
				if (inflow && outflow && outFrom <= inEnd + 1) {
					for (size_t k = a; k <= b; ++k) {
						include[k - first] = 1;
					}
				}
				a = b + 1;
			}

			for (size_t i = first; i <= last;) {
				if (include[i - first] == 0) {
					++i;
					continue;
				}
				Reach reach;
				reach.trimmedStart = i == first && first > 0;
				size_t j		   = i;
				while (j <= last && include[j - first] != 0) {
					reach.points.push_back({pos(j), hw(j), flare[j - first], asymmetry[j - first], ratios[j]});
					++j;
				}
				const size_t end = j - 1;
				reach.trimmedEnd = end == last && last + 1 < samples.size();
				if (extensionM && end == samples.size() - 1 && end > 0) {
					const Vec2d	 chord = pos(end) - pos(end - 1);
					const Vec2d	 dir   = chord * (1.0 / geometry::length(chord));
					const auto	 steps = std::max<size_t>(1, static_cast<size_t>(std::ceil(*extensionM / Builder::kCenterlineSpacingM)));
					for (size_t q = 1; q <= steps; ++q) {
						const double along = *extensionM * static_cast<double>(q) / static_cast<double>(steps);
						reach.points.push_back({pos(end) + dir * along, hw(end), Builder::kMouthFlare, 0.0, 1.0F});
					}
				}
				if (reach.points.size() >= 2) {
					out.push_back(std::move(reach));
				}
				i = j;
			}
		}

		std::vector<Reach> extractReaches(const std::vector<geometry::CenterlineSample>& samples, const Region& region,
										  const WaterBodies& water) {
			const size_t		 n = samples.size();
			std::vector<uint8_t> kept(n);
			std::vector<Ground>	 ground(n);
			for (size_t i = 0; i < n; ++i) {
				const Vec2i64 mm = toMm(samples[i].position);
				kept[i]			 = region.inKept(mm) ? 1 : 0;
				ground[i]		 = !region.inExtended(mm) ? Ground::Unknown : (water.contains(mm) ? Ground::Water : Ground::Land);
			}
			const std::vector<float> ratios = widthRatios(samples);
			std::vector<Reach>		 reaches;
			for (size_t i = 0; i < n;) {
				if (kept[i] == 0) {
					++i;
					continue;
				}
				size_t j = i;
				while (j + 1 < n && kept[j + 1] != 0) {
					++j;
				}
				splitRun(samples, ratios, ground, i, j, reaches);
				i = j + 1;
			}
			return reaches;
		}

		// ---- Ribbons: bank offsets, the fordable split, the stroke ----

		struct ChannelSeeds {
			uint32_t leftFine;
			uint32_t leftLow;
			uint32_t rightFine;
			uint32_t rightLow;
		};

		// One centerline point with everything the stroke and the thalweg need.
		struct RibbonPoint {
			Vec2d  position;
			double rawHalfWidthM = 0.0;
			double halfWidthM	 = 0.0; // bankfull, flared
			double kappa		 = 0.0; // signed, 1/m, positive turning left
			double radiusM		 = std::numeric_limits<double>::infinity();
			double asymmetryM	 = 0.0; // a: the outer bank's extra offset
			double leftM		 = 0.0;
			double rightM		 = 0.0;
			float  widthRatio	 = 1.0F;
			bool   cut			 = false; // a fordability cut, shared by the pieces either side
		};

		bool blocks(double rawHalfWidthM) {
			return 2.0 * rawHalfWidthM >= Builder::kFordableWidthM;
		}

		// D7 steps 3-4 at one point: bankfull half-width, bend asymmetry, bank noise
		// at the point's world position, then the inner bank clamped under the local
		// radius of curvature.
		void bankOffsets(RibbonPoint& p, double flare, double asymmetryShare, const ChannelSeeds& seeds) {
			const double hw	   = p.rawHalfWidthM * flare;
			const double kAbs  = std::abs(p.kappa);
			const double a	   = std::min(Builder::kAsymmetryMaxFrac, Builder::kAsymmetryCurvatureGain * kAbs * hw) * hw * asymmetryShare;
			p.halfWidthM	   = hw;
			p.asymmetryM	   = a;
			const double outer = hw + a;
			const double inner = hw + Builder::kInnerBankAsymmetryShare * a;
			// Positive curvature turns left: the left bank is on the inside.
			double left	 = p.kappa > 0.0 ? inner : outer;
			double right = p.kappa > 0.0 ? outer : inner;

			const Vec2i64 mm	 = toMm(p.position);
			const double  turn	 = kAbs * Builder::kChannelBankNoiseWavelengthM;
			const double  damp	 = std::max(Builder::kNoiseDampFloor, 1.0 - turn / Builder::kNoiseDampTurnRad);
			auto		  noise	 = [&mm, hw](uint32_t fineSeed, uint32_t lowSeed) {
				 const double fine = static_cast<double>(worldNoise(
					 mm, Builder::kChannelBankNoiseWavelengthM, fineSeed, Builder::kChannelBankNoiseOctaves
				 ));
				 const double low = static_cast<double>(worldNoise(
					 mm, Builder::kChannelLowNoiseWavelengthM, lowSeed, Builder::kChannelLowNoiseOctaves
				 ));
				 return (Builder::kChannelBankNoiseFrac * fine + Builder::kChannelLowNoiseFrac * low) * hw;
			};
			left += damp * noise(seeds.leftFine, seeds.leftLow);
			right += damp * noise(seeds.rightFine, seeds.rightLow);

			const double limit = Builder::kRadiusClampFrac * p.radiusM;
			if (p.kappa > 0.0) {
				left = std::min(left, limit);
			} else if (p.kappa < 0.0) {
				right = std::min(right, limit);
			}
			p.leftM	 = std::max(0.0, left);
			p.rightM = std::max(0.0, right);
		}

		// The reach's centerline with the fordable cuts in place and every offset
		// computed once, so the two pieces either side of a cut read the same
		// numbers for it.
		std::vector<RibbonPoint> ribbonPoints(const Reach& reach, const ChannelSeeds& seeds) {
			const std::vector<ReachPoint>& pts = reach.points;
			const size_t				   n   = pts.size();

			std::vector<double> kappa(n, 0.0);
			std::vector<double> radius(n, std::numeric_limits<double>::infinity());
			for (size_t k = 1; k + 1 < n; ++k) {
				const double r = geometry::localRadiusOfCurvatureM(pts[k - 1].position, pts[k].position, pts[k + 1].position);
				if (!std::isfinite(r) || r == 0.0) {
					continue;
				}
				const Vec2d	 a	   = pts[k].position - pts[k - 1].position;
				const Vec2d	 b	   = pts[k + 1].position - pts[k].position;
				const double cross = a.x * b.y - a.y * b.x;
				kappa[k]		   = (cross > 0.0 ? 1.0 : -1.0) / r;
				radius[k]		   = r;
			}

			std::vector<RibbonPoint> out;
			out.reserve(n + 4);
			auto push = [&](size_t k) {
				RibbonPoint p;
				p.position		= pts[k].position;
				p.rawHalfWidthM = pts[k].rawHalfWidthM;
				p.kappa			= kappa[k];
				p.radiusM		= radius[k];
				p.widthRatio	= pts[k].widthRatio;
				bankOffsets(p, pts[k].flare, pts[k].asymmetry, seeds);
				out.push_back(p);
			};

			static constexpr double kFordHalfM = Builder::kFordableWidthM / 2.0;
			bool					cutNext	   = false;
			for (size_t k = 0; k < n; ++k) {
				push(k);
				out.back().cut = cutNext;
				cutNext		   = false;
				if (k + 1 == n || blocks(pts[k].rawHalfWidthM) == blocks(pts[k + 1].rawHalfWidthM)) {
					continue;
				}
				// Half-width is linear in the span parameter between two samples of one
				// span, so the crossing is linear between them too (D7 step 5).
				const double h0 = pts[k].rawHalfWidthM;
				const double h1 = pts[k + 1].rawHalfWidthM;
				const double u	= (kFordHalfM - h0) / (h1 - h0);
				if (u <= 0.0) {
					out.back().cut = true;
					continue;
				}
				if (u >= 1.0) {
					cutNext = true;
					continue;
				}
				RibbonPoint p;
				p.position = pts[k].position + (pts[k + 1].position - pts[k].position) * u;
				if (p.position == pts[k].position || p.position == pts[k + 1].position) {
					(p.position == pts[k].position ? out.back().cut : cutNext) = true;
					continue;
				}
				p.rawHalfWidthM = kFordHalfM;
				p.kappa			= lerp(kappa[k], kappa[k + 1], u);
				p.radiusM		= p.kappa != 0.0 ? 1.0 / std::abs(p.kappa) : std::numeric_limits<double>::infinity();
				p.widthRatio	= static_cast<float>(lerp(pts[k].widthRatio, pts[k + 1].widthRatio, u));
				p.cut			= true;
				bankOffsets(p, lerp(pts[k].flare, pts[k + 1].flare, u), lerp(pts[k].asymmetry, pts[k + 1].asymmetry, u), seeds);
				out.push_back(p);
			}

			// A cut at a reach end, or one whose two sides block alike (the width
			// touched the threshold at a single sample), splits nothing.
			const size_t m = out.size();
			for (size_t i = 0; i < m; ++i) {
				if (!out[i].cut) {
					continue;
				}
				if (i == 0 || i + 1 == m || blocks(out[i - 1].rawHalfWidthM) == blocks(out[i + 1].rawHalfWidthM)) {
					out[i].cut = false;
				}
			}
			return out;
		}

		// Both bank points strokePolyline places at a cut offset along `normal`.
		std::array<Vec2i64, 2> cutVertices(const RibbonPoint& p, const Vec2d& normal) {
			return {geometry::strokeBankPoint(p.position, normal, -p.rightM), geometry::strokeBankPoint(p.position, normal, p.leftM)};
		}

		struct ChannelPiece {
			Ring				 ring;
			std::vector<Vec2i64> cuts; // this piece's fordable cut vertices
			bool				 blocksMovement = true;
			float				 meanHalfWidthM = 0.0F;
		};

		// Every non-cut point of a piece lies on one side of the threshold, and a
		// piece always has one (cuts are never adjacent, nor at a reach end).
		bool pieceBlocks(const std::vector<RibbonPoint>& pts, size_t s, size_t e) {
			for (size_t i = s; i <= e; ++i) {
				if (!pts[i].cut) {
					return blocks(pts[i].rawHalfWidthM);
				}
			}
			return true;
		}

		std::vector<ChannelPiece> strokeReach(const Reach& reach, const std::vector<RibbonPoint>& pts) {
			std::vector<Vec2d> centerline(pts.size());
			for (size_t i = 0; i < pts.size(); ++i) {
				centerline[i] = pts[i].position;
			}
			std::vector<size_t> bounds = {0};
			for (size_t i = 1; i + 1 < pts.size(); ++i) {
				if (pts[i].cut) {
					bounds.push_back(i);
				}
			}
			bounds.push_back(pts.size() - 1);

			std::vector<ChannelPiece> pieces;
			for (size_t b = 0; b + 1 < bounds.size(); ++b) {
				const size_t s = bounds[b];
				const size_t e = bounds[b + 1];

				std::vector<double> left(e - s + 1);
				std::vector<double> right(e - s + 1);
				double				hwSum = 0.0;
				for (size_t i = s; i <= e; ++i) {
					left[i - s]	 = pts[i].leftM;
					right[i - s] = pts[i].rightM;
					hwSum += pts[i].halfWidthM;
				}

				ChannelPiece piece;
				piece.blocksMovement = pieceBlocks(pts, s, e);
				piece.meanHalfWidthM = static_cast<float>(hwSum / static_cast<double>(e - s + 1));

				geometry::StrokeArgs args;
				args.centerline	  = std::span<const Vec2d>(centerline.data() + s, e - s + 1);
				args.leftOffsetM  = left;
				args.rightOffsetM = right;
				args.capSpacingM  = Builder::kCapSpacingM;
				if (pts[s].cut) {
					args.startCap	 = geometry::StrokeCap::Butt;
					args.startNormal = geometry::strokeNormal(centerline, s);
					const auto v	 = cutVertices(pts[s], *args.startNormal);
					piece.cuts.insert(piece.cuts.end(), v.begin(), v.end());
				} else {
					args.startCap = s == 0 && reach.trimmedStart ? geometry::StrokeCap::Butt : geometry::StrokeCap::Round;
				}
				if (pts[e].cut) {
					args.endCap	   = geometry::StrokeCap::Butt;
					args.endNormal = geometry::strokeNormal(centerline, e);
					const auto v   = cutVertices(pts[e], *args.endNormal);
					piece.cuts.insert(piece.cuts.end(), v.begin(), v.end());
				} else {
					args.endCap = e + 1 == pts.size() && reach.trimmedEnd ? geometry::StrokeCap::Butt : geometry::StrokeCap::Round;
				}
				piece.ring = geometry::strokePolyline(args);
				pieces.push_back(std::move(piece));
			}
			return pieces;
		}

		// D2 thalweg: the centerline offset toward the outer bank by the asymmetry,
		// one path per stretch inside the extended region.
		void appendThalwegs(const std::vector<RibbonPoint>& pts, const Region& region, std::vector<ThalwegPath>& out) {
			std::vector<Vec2d> centerline(pts.size());
			for (size_t i = 0; i < pts.size(); ++i) {
				centerline[i] = pts[i].position;
			}
			ThalwegPath path;
			auto		flush = [&out, &path]() {
				   if (path.points.size() >= 2) {
					   out.push_back(std::move(path));
				   }
				   path = {};
			};
			for (size_t i = 0; i < pts.size(); ++i) {
				const RibbonPoint& p	   = pts[i];
				const double	   towards = p.kappa > 0.0 ? -p.asymmetryM : (p.kappa < 0.0 ? p.asymmetryM : 0.0);
				const Vec2i64	   mm	   = toMm(p.position + geometry::strokeNormal(centerline, i) * towards);
				if (!region.inExtended(toMm(p.position))) {
					flush();
					continue;
				}
				path.points.push_back(mm);
				path.halfWidthM.push_back(static_cast<float>(p.halfWidthM));
				path.widthRatio.push_back(p.widthRatio);
				path.curvature.push_back(static_cast<float>(p.kappa));
			}
			flush();
		}

		void buildChannels(ChunkTerrainPolygons& out, std::span<const Builder::RiverSegment> segments, const ExtendedGrid& grid,
						   const Region& region, uint64_t worldSeed, ChunkCoordinate coord) {
			if (segments.empty()) {
				return;
			}
			const ChannelSeeds seeds{
				purposeSeed(worldSeed, kSaltLeftBankFine), purposeSeed(worldSeed, kSaltLeftBankLow),
				purposeSeed(worldSeed, kSaltRightBankFine), purposeSeed(worldSeed, kSaltRightBankLow)
			};
			const WaterBodies		 water(out.rings);
			std::vector<TerrainRing> channels;

			for (Chain& chain : joinChains(segments)) {
				if (!dropShortSpans(chain)) {
					continue;
				}
				std::vector<Vec2d>	points(chain.size());
				std::vector<double> halfWidths(chain.size());
				for (size_t i = 0; i < chain.size(); ++i) {
					points[i]	  = toMeters(chain[i].mm);
					halfWidths[i] = chain[i].halfWidthM;
				}
				const std::vector<geometry::CenterlineSample> samples =
					geometry::sampleCatmullRom(points, halfWidths, Builder::kCenterlineSpacingM);

				for (const Reach& reach : extractReaches(samples, region, water)) {
					const std::vector<RibbonPoint> ribbon = ribbonPoints(reach, seeds);
					appendThalwegs(ribbon, region, out.thalwegs);
					for (ChannelPiece& piece : strokeReach(reach, ribbon)) {
						for (Ring& ring : finishVectorRing(piece.ring, piece.cuts, region, coord, "channel ring")) {
							TerrainRing terrain = vectorTerrainRing(
								std::move(ring), TerrainRingKind::Channel, WaterKind::River, piece.cuts, grid, region
							);
							terrain.blocksMovement = piece.blocksMovement;
							terrain.meanHalfWidthM = piece.meanHalfWidthM;
							channels.push_back(std::move(terrain));
						}
					}
				}
			}
			for (TerrainRing& channel : channels) {
				out.rings.push_back(std::move(channel));
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

	ChunkTerrainPolygons TerrainPolygonBuilder::build(
		ChunkCoordinate				  coord,
		uint64_t					  worldSeed,
		const ExtendedTileFn&		  tiles,
		std::span<const RiverSegment> riverSegments,
		std::span<const Pond>		  ponds
	) {
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
