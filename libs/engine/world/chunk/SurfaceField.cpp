#include "SurfaceField.h"

#include <debug/Tunables.h>
#include <random/HashNoise.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

namespace engine::world {

	namespace {

		constexpr uint8_t kWater = static_cast<uint8_t>(Surface::Water);
		constexpr uint8_t kSand	 = static_cast<uint8_t>(Surface::Sand);

		// D13, lowest first.
		constexpr std::array<uint8_t, 9> kSurfaceByLevel = {
			static_cast<uint8_t>(Surface::Mud),
			static_cast<uint8_t>(Surface::Sand),
			static_cast<uint8_t>(Surface::Dirt),
			static_cast<uint8_t>(Surface::GrassShort),
			static_cast<uint8_t>(Surface::Grass),
			static_cast<uint8_t>(Surface::GrassMeadow),
			static_cast<uint8_t>(Surface::GrassTall),
			static_cast<uint8_t>(Surface::Rock),
			static_cast<uint8_t>(Surface::Snow),
		};

		struct EdgeDefaults {
			float fineM;
			float lowM;
		};

		// By surface id. Grass and soil edges wobble at the bank scale, rock
		// outcrops keep a harder, lower-amplitude edge. The waterline's 0.25 m at 6 m
		// (D6) barely bends a 2-4 tile straight run of a small patch; 0.55 m at 4 m
		// does, and the low term moves whole edges less than the shore's 1.2 m.
		constexpr std::array<EdgeDefaults, kSurfaceCount> kEdgeDefaults = {{
			{0.55F, 0.7F}, // Grass
			{0.55F, 0.7F}, // Dirt
			{0.55F, 0.7F}, // Sand
			{0.2F, 0.4F},  // Rock
			{0.0F, 0.0F},  // Water, never painted
			{0.45F, 0.7F}, // Snow
			{0.55F, 0.7F}, // Mud
			{0.55F, 0.7F}, // GrassTall
			{0.55F, 0.7F}, // GrassShort
			{0.55F, 0.7F}, // GrassMeadow
		}};
		// The scalar defaults are SurfaceFieldParams' own initializers.
		constexpr SurfaceFieldParams kDefaults{};
		constexpr int32_t			 kMaxWavelengthM = 4096;

		// Distinct from the waterline's salts, so a land edge never wobbles in step
		// with the shore beside it (D15).
		constexpr uint32_t kSaltFine = 0x1A4D0001U;
		constexpr uint32_t kSaltLow	 = 0x1A4D0002U;

		uint32_t purposeSeed(uint64_t worldSeed, uint32_t salt) {
			return foundation::hash3(static_cast<int32_t>(salt), static_cast<int32_t>(worldSeed >> 32U), 0, static_cast<uint32_t>(worldSeed));
		}

		// Tile-center lattice: index k sits at world k + 0.5, so a point T + f lies in
		// the cell starting at T - 1 when f < 0.5, else at T. Exact in float.
		struct LatticeRead {
			int64_t cell;
			float	t;
		};

		LatticeRead latticeRead(int64_t tile, float f) {
			return f < 0.5F ? LatticeRead{tile - 1, f + 0.5F} : LatticeRead{tile, f - 0.5F};
		}

		// Spelled out rather than std::lerp, so land.glsl can write the same expression.
		float lerp(float a, float b, float t) {
			return a + (b - a) * t;
		}

		float bilinear(float v00, float v10, float v01, float v11, float tx, float ty) {
			return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
		}

		// The paint surfaces of the 4x4 tiles (cell - 1 .. cell + 2 per axis) a bilinear
		// read in lattice cell (cellX, cellY) blurs, row-major.
		using Block = std::array<uint8_t, 16>;

		Block surfaceBlock(const RenderTileView& tiles, int64_t cellX, int64_t cellY) {
			Block block{};
			for (int j = 0; j < 4; ++j) {
				for (int i = 0; i < 4; ++i) {
					block[static_cast<size_t>(j * 4 + i)] = tiles.at(cellX - 1 + i, cellY - 1 + j).surfaceId;
				}
			}
			return block;
		}

		// D16 step 1 at block tile (i, j), each 1 or 2: the indicator of `s` blurred
		// with the 3x3 binomial, then the thin-feature guard. A tile is thin when both
		// its neighbors along either axis are on the other side of the indicator, which
		// covers every tile with fewer than two same-side cardinal neighbors and the
		// tiles of a straight 1-wide run (two, opposite), whose blur is exactly 0.5.
		float guardedBlur(const Block& block, uint8_t s, int i, int j, float thinFloor, float thinCeil) {
			static constexpr int kBinomial[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
			auto is = [&block, s](int x, int y) { return block[static_cast<size_t>(y * 4 + x)] == s; };
			int	 sum = 0;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (is(i + dx, j + dy)) {
						sum += kBinomial[dy + 1][dx + 1];
					}
				}
			}
			const float blur = static_cast<float>(sum) * 0.0625F;
			const bool	in	 = is(i, j);
			const bool	thin = (is(i - 1, j) != in && is(i + 1, j) != in) || (is(i, j - 1) != in && is(i, j + 1) != in);
			if (!thin) {
				return blur;
			}
			return in ? std::max(blur, thinFloor) : std::min(blur, thinCeil);
		}

		float blockField(const Block& block, uint8_t s, float tx, float ty, float thinFloor, float thinCeil) {
			return bilinear(
				guardedBlur(block, s, 1, 1, thinFloor, thinCeil),
				guardedBlur(block, s, 2, 1, thinFloor, thinCeil),
				guardedBlur(block, s, 1, 2, thinFloor, thinCeil),
				guardedBlur(block, s, 2, 2, thinFloor, thinCeil),
				tx,
				ty
			);
		}

	} // namespace

	int surfaceStackLevel(uint8_t surfaceId) {
		for (size_t level = 0; level < kSurfaceByLevel.size(); ++level) {
			if (kSurfaceByLevel[level] == surfaceId) {
				return static_cast<int>(level) + 1;
			}
		}
		return 0;
	}

	TilePoint tilePointOf(double worldX, double worldY) {
		const double wholeX = std::floor(worldX);
		const double wholeY = std::floor(worldY);
		TilePoint	 point{static_cast<int64_t>(wholeX), static_cast<int64_t>(wholeY), static_cast<float>(worldX - wholeX), static_cast<float>(worldY - wholeY)};
		// A remainder a hair under 1 can round up to 1 in float.
		if (point.fx >= 1.0F) {
			point.fx = 0.0F;
			++point.x;
		}
		if (point.fy >= 1.0F) {
			point.fy = 0.0F;
			++point.y;
		}
		return point;
	}

	SurfaceFieldSeeds surfaceFieldSeeds(uint64_t worldSeed) {
		return {purposeSeed(worldSeed, kSaltFine), purposeSeed(worldSeed, kSaltLow)};
	}

	int32_t landWavelength(float meters) {
		return static_cast<int32_t>(std::clamp(std::lround(meters), 1L, static_cast<long>(kMaxWavelengthM)));
	}

	const SurfaceFieldTunables& SurfaceFieldTunables::get() {
		static const SurfaceFieldTunables tunables = [] {
			static constexpr float kNoWarp	= 0.0F;
			Foundation::Tunables&  registry = Foundation::Tunables::instance();
			auto				   add		= [&registry](const std::string& name, float value) {
				  return registry.add(name, std::span<const float>(&value, 1));
			};
			SurfaceFieldTunables out;
			for (size_t id = 0; id < kSurfaceCount; ++id) {
				if (id == kWater) {
					out.warpFineM[id] = &kNoWarp;
					out.warpLowM[id]  = &kNoWarp;
					continue;
				}
				const std::string surface = surfaceToString(static_cast<Surface>(id));
				out.warpFineM[id]		  = add("terrain/land/warpFine/" + surface, kEdgeDefaults[id].fineM);
				out.warpLowM[id]		  = add("terrain/land/warpLow/" + surface, kEdgeDefaults[id].lowM);
			}
			out.warpFineWavelengthM = add("terrain/land/warpFineWavelength", static_cast<float>(kDefaults.warpFineWavelengthM));
			out.warpLowWavelengthM	= add("terrain/land/warpLowWavelength", static_cast<float>(kDefaults.warpLowWavelengthM));
			out.warpFineGain		= add("terrain/land/warpFineGain", kDefaults.warpFineGain);
			out.thinFloor			= add("terrain/land/thinFloor", kDefaults.thinFloor);
			out.thinCeil			= add("terrain/land/thinCeil", kDefaults.thinCeil);
			return out;
		}();
		return tunables;
	}

	SurfaceFieldParams SurfaceFieldTunables::params(uint64_t worldSeed) const {
		SurfaceFieldParams out;
		for (size_t id = 0; id < kSurfaceCount; ++id) {
			out.warpFineM[id] = *warpFineM[id];
			out.warpLowM[id]  = *warpLowM[id];
		}
		out.warpFineWavelengthM = landWavelength(*warpFineWavelengthM);
		out.warpLowWavelengthM	= landWavelength(*warpLowWavelengthM);
		out.warpFineGain		= *warpFineGain;
		out.thinFloor			= *thinFloor;
		out.thinCeil			= *thinCeil;
		out.seeds				= surfaceFieldSeeds(worldSeed);
		return out;
	}

	SurfaceFieldParams defaultSurfaceFieldParams(uint64_t worldSeed) {
		SurfaceFieldParams out;
		for (size_t id = 0; id < kSurfaceCount; ++id) {
			out.warpFineM[id] = kEdgeDefaults[id].fineM;
			out.warpLowM[id]  = kEdgeDefaults[id].lowM;
		}
		out.seeds = surfaceFieldSeeds(worldSeed);
		return out;
	}

	SurfaceFieldSample evaluateSurfaceField(const RenderTileView& tiles, TilePoint point, const SurfaceFieldParams& params) {
		const TileRenderData& own = tiles.at(point.x, point.y);
		if ((own.edge & kRenderInteriorBit) != 0) {
			return {.surface = static_cast<Surface>(own.surfaceId)};
		}

		// Warp amplitudes: bilinear over the tile centers around the point, each
		// tile's from its edge surface, so the character changes smoothly.
		const LatticeRead ax	 = latticeRead(point.x, point.fx);
		const LatticeRead ay	 = latticeRead(point.y, point.fy);
		auto			  edgeAt = [&tiles, &ax, &ay](int64_t dx, int64_t dy) {
			 const size_t id = tiles.at(ax.cell + dx, ay.cell + dy).edge & kRenderEdgeSurfaceMask;
			 assert(id < kSurfaceCount);
			 return id;
		};
		const size_t e00	 = edgeAt(0, 0);
		const size_t e10	 = edgeAt(1, 0);
		const size_t e01	 = edgeAt(0, 1);
		const size_t e11	 = edgeAt(1, 1);
		const auto&	 fine	 = params.warpFineM;
		const auto&	 low	 = params.warpLowM;
		const float	 fineAmp = bilinear(fine[e00], fine[e10], fine[e01], fine[e11], ax.t, ay.t);
		const float	 lowAmp	 = bilinear(low[e00], low[e10], low[e01], low[e11], ax.t, ay.t);

		// The noise reads the point kWarpNoisePhaseM off the tile grid, carried into
		// the whole part so the offset stays in [0, 1) (exact in float).
		const float	  phasedX = point.fx + kWarpNoisePhaseM;
		const float	  phasedY = point.fy + kWarpNoisePhaseM;
		const int64_t noiseX  = point.x + (phasedX >= 1.0F ? 1 : 0);
		const int64_t noiseY  = point.y + (phasedY >= 1.0F ? 1 : 0);
		const float	  noiseFx = phasedX >= 1.0F ? phasedX - 1.0F : phasedX;
		const float	  noiseFy = phasedY >= 1.0F ? phasedY - 1.0F : phasedY;
		// Each term's noise pair is the offset's x and y.
		auto noise = [&](int32_t wavelength, uint32_t seed, int octaves, float gain) {
			const foundation::NoisePair n =
				foundation::fractalNoise2SplitPair(noiseX, noiseY, noiseFx, noiseFy, wavelength, seed, octaves, gain);
			return foundation::NoisePair{std::clamp(n.first, -1.0F, 1.0F), std::clamp(n.second, -1.0F, 1.0F)};
		};
		const foundation::NoisePair fineNoise = noise(params.warpFineWavelengthM, params.seeds.fine, kWarpFineOctaves, params.warpFineGain);
		const foundation::NoisePair lowNoise  = noise(params.warpLowWavelengthM, params.seeds.low, kWarpLowOctaves, kWarpLowGain);
		const float warpX = std::clamp(fineAmp * fineNoise.first + lowAmp * lowNoise.first, -kMaxLandWarpM, kMaxLandWarpM);
		const float warpY = std::clamp(fineAmp * fineNoise.second + lowAmp * lowNoise.second, -kMaxLandWarpM, kMaxLandWarpM);

		// The warped read position, still a whole tile plus an offset.
		const float		  sx = point.fx + warpX;
		const float		  sy = point.fy + warpY;
		const float		  wx = std::floor(sx);
		const float		  wy = std::floor(sy);
		const LatticeRead qx = latticeRead(point.x + static_cast<int64_t>(wx), sx - wx);
		const LatticeRead qy = latticeRead(point.y + static_cast<int64_t>(wy), sy - wy);
		const Block		  block = surfaceBlock(tiles, qx.cell, qy.cell);

		uint32_t present = 0;
		for (const uint8_t id : block) {
			present |= 1U << id;
		}

		// Low to high: the highest surface whose field passes 0.5 paints; where none
		// does (a junction of three or more), the largest field, ties to the higher.
		SurfaceFieldSample sample;
		sample.warpXM		 = warpX;
		sample.warpYM		 = warpY;
		float	margin		 = std::numeric_limits<float>::max();
		bool	passed		 = false;
		uint8_t painted		 = 0;
		float	paintedField = 0.0F;
		uint8_t best		 = 0;
		float	bestField	 = -1.0F;
		float	secondField	 = -1.0F;
		for (const uint8_t s : kSurfaceByLevel) {
			if ((present & (1U << s)) == 0) {
				continue;
			}
			const float f = blockField(block, s, qx.t, qy.t, params.thinFloor, params.thinCeil);
			margin		  = std::min(margin, std::abs(f - 0.5F));
			if (f > 0.5F) {
				passed		 = true;
				painted		 = s;
				paintedField = f;
			}
			if (f >= bestField) {
				secondField = bestField;
				bestField	= f;
				best		= s;
			} else if (f > secondField) {
				secondField = f;
			}
		}
		if (!passed) {
			painted		 = best;
			paintedField = bestField;
			margin		 = std::min(margin, bestField - secondField);
		}
		sample.surface = static_cast<Surface>(painted);
		sample.field   = paintedField;
		sample.margin  = margin;
		return sample;
	}

	float surfaceFieldValue(const RenderTileView& tiles, TilePoint at, uint8_t surfaceId, float thinFloor, float thinCeil) {
		const LatticeRead ax = latticeRead(at.x, at.fx);
		const LatticeRead ay = latticeRead(at.y, at.fy);
		return blockField(surfaceBlock(tiles, ax.cell, ay.cell), surfaceId, ax.t, ay.t, thinFloor, thinCeil);
	}

	std::vector<uint8_t> paintSurfaces(std::span<const uint8_t> surfaces, int32_t width, int32_t height) {
		constexpr int32_t kReach	  = kBedReachTiles;
		const int32_t	  grownWidth  = width + 2 * kReach;
		const int32_t	  grownHeight = height + 2 * kReach;
		assert(surfaces.size() == static_cast<size_t>(grownWidth) * static_cast<size_t>(grownHeight));
		auto surfaceAt = [&surfaces, grownWidth](int32_t gx, int32_t gy) {
			return surfaces[static_cast<size_t>(gy) * static_cast<size_t>(grownWidth) + static_cast<size_t>(gx)];
		};

		// Offsets within reach, nearest first, ties by dy then dx: world y, then world x.
		struct Offset {
			int32_t dx;
			int32_t dy;
		};
		static const std::vector<Offset> kOffsets = [] {
			std::vector<Offset> out;
			for (int32_t dy = -kReach; dy <= kReach; ++dy) {
				for (int32_t dx = -kReach; dx <= kReach; ++dx) {
					if (dx != 0 || dy != 0) {
						out.push_back({dx, dy});
					}
				}
			}
			std::sort(out.begin(), out.end(), [](const Offset& a, const Offset& b) {
				const int32_t da = a.dx * a.dx + a.dy * a.dy;
				const int32_t db = b.dx * b.dx + b.dy * b.dy;
				if (da != db) {
					return da < db;
				}
				return a.dy != b.dy ? a.dy < b.dy : a.dx < b.dx;
			});
			return out;
		}();

		// Land anywhere within reach, a separable OR, so open water skips the search.
		std::vector<uint8_t> landInRow(static_cast<size_t>(width) * static_cast<size_t>(grownHeight), 0);
		for (int32_t gy = 0; gy < grownHeight; ++gy) {
			for (int32_t x = 0; x < width; ++x) {
				uint8_t any = 0;
				for (int32_t k = 0; k <= 2 * kReach && any == 0; ++k) {
					any = surfaceAt(x + k, gy) != kWater ? 1 : 0;
				}
				landInRow[static_cast<size_t>(gy) * static_cast<size_t>(width) + static_cast<size_t>(x)] = any;
			}
		}

		std::vector<uint8_t> paint(static_cast<size_t>(width) * static_cast<size_t>(height));
		for (int32_t y = 0; y < height; ++y) {
			for (int32_t x = 0; x < width; ++x) {
				const int32_t gx	  = x + kReach;
				const int32_t gy	  = y + kReach;
				uint8_t&	  out	  = paint[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
				const uint8_t surface = surfaceAt(gx, gy);
				if (surface != kWater) {
					out = surface;
					continue;
				}
				bool landNear = false;
				for (int32_t k = 0; k <= 2 * kReach && !landNear; ++k) {
					landNear = landInRow[static_cast<size_t>(y + k) * static_cast<size_t>(width) + static_cast<size_t>(x)] != 0;
				}
				out = kSand;
				if (!landNear) {
					continue;
				}
				for (const Offset& o : kOffsets) {
					const uint8_t candidate = surfaceAt(gx + o.dx, gy + o.dy);
					if (candidate != kWater) {
						out = candidate;
						break;
					}
				}
			}
		}
		return paint;
	}

	std::vector<TileRenderData> buildRenderTiles(std::span<const uint8_t> paint, int32_t width, int32_t height) {
		constexpr int32_t kGrow		  = kInteriorReachTiles;
		const int32_t	  grownWidth  = width + 2 * kGrow;
		const int32_t	  grownHeight = height + 2 * kGrow;
		assert(paint.size() == static_cast<size_t>(grownWidth) * static_cast<size_t>(grownHeight));
		static_assert(kEdgeReachTiles <= kInteriorReachTiles);

		std::vector<uint8_t> level(paint.size());
		for (size_t i = 0; i < paint.size(); ++i) {
			level[i] = static_cast<uint8_t>(surfaceStackLevel(paint[i]));
			assert(level[i] > 0 && "paint surfaces are never Water");
		}

		// Sliding extremes along each grown row, then down each window column: the
		// highest level within kEdgeReachTiles, and the least and greatest surface id
		// within kInteriorReachTiles (the same id at both ends means one surface).
		const size_t		 rowCells = static_cast<size_t>(width) * static_cast<size_t>(grownHeight);
		std::vector<uint8_t> rowLevel(rowCells);
		std::vector<uint8_t> rowMin(rowCells);
		std::vector<uint8_t> rowMax(rowCells);
		for (int32_t gy = 0; gy < grownHeight; ++gy) {
			const size_t row = static_cast<size_t>(gy) * static_cast<size_t>(grownWidth);
			for (int32_t x = 0; x < width; ++x) {
				// Grown column x + kGrow is window column x; the windows start left of it.
				const size_t edgeFirst = row + static_cast<size_t>(x + kGrow - kEdgeReachTiles);
				uint8_t		 top	   = 0;
				for (size_t k = 0; k <= 2 * static_cast<size_t>(kEdgeReachTiles); ++k) {
					top = std::max(top, level[edgeFirst + k]);
				}
				const size_t interiorFirst = row + static_cast<size_t>(x);
				uint8_t		 lo			   = paint[interiorFirst];
				uint8_t		 hi			   = lo;
				for (size_t k = 1; k <= 2 * static_cast<size_t>(kGrow); ++k) {
					const uint8_t id = paint[interiorFirst + k];
					lo				 = std::min(lo, id);
					hi				 = std::max(hi, id);
				}
				const size_t cell = static_cast<size_t>(gy) * static_cast<size_t>(width) + static_cast<size_t>(x);
				rowLevel[cell]	  = top;
				rowMin[cell]	  = lo;
				rowMax[cell]	  = hi;
			}
		}

		std::vector<TileRenderData> out(static_cast<size_t>(width) * static_cast<size_t>(height));
		for (int32_t y = 0; y < height; ++y) {
			for (int32_t x = 0; x < width; ++x) {
				auto cell = [width, x](int32_t gy) {
					return static_cast<size_t>(gy) * static_cast<size_t>(width) + static_cast<size_t>(x);
				};
				const int32_t gy  = y + kGrow;
				uint8_t		  top = 0;
				for (int32_t d = -kEdgeReachTiles; d <= kEdgeReachTiles; ++d) {
					top = std::max(top, rowLevel[cell(gy + d)]);
				}
				uint8_t lo = rowMin[cell(gy - kGrow)];
				uint8_t hi = rowMax[cell(gy - kGrow)];
				for (int32_t d = -kGrow + 1; d <= kGrow; ++d) {
					lo = std::min(lo, rowMin[cell(gy + d)]);
					hi = std::max(hi, rowMax[cell(gy + d)]);
				}
				const uint8_t surface = paint[static_cast<size_t>(gy) * static_cast<size_t>(grownWidth) + static_cast<size_t>(x + kGrow)];
				out[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = {
					surface,
					static_cast<uint8_t>(kSurfaceByLevel[top - 1] | (lo == hi ? kRenderInteriorBit : 0))
				};
			}
		}
		return out;
	}

} // namespace engine::world
