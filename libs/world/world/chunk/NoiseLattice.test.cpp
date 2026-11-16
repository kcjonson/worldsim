// NoiseOctaveGrid against salal's gradientNoise2, whose field it caches (NoiseLattice.h): the same
// corners hashed at the same lattice cells, negative ones included, and a sample that depends on its
// world position alone, never on the grid's origin.

#include "world/chunk/NoiseLattice.h"

#include <random/HashNoise.h>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace world {

	namespace {

		struct Origin {
			int64_t x = 0;
			int64_t y = 0;
		};

		// Around zero, both sides of it, a chunk 70 km out, its mirror, and thousands of km out.
		constexpr std::array<Origin, 6> kOrigins = {{{0, 0}, {-37, -1000}, {-512, 488}, {69632, 1024}, {-70144, -1536}, {-3000000, 2000000}}};

		uint32_t bits(float v) {
			return std::bit_cast<uint32_t>(v);
		}

	} // namespace

	// Where gradientNoise2's float position is exact (a power-of-two wavelength and a phase in its
	// 1/16ths, so the offset in a cell has few bits and the whole cell fits beside them), the grid is
	// gradientNoise2 bit for bit: same cell, same corner hashes, same float steps.
	TEST(NoiseLatticeTest, MatchesGradientNoise2BitForBit) {
		size_t compared = 0;
		for (const Origin origin : kOrigins) {
			for (const int32_t wavelength : {8, 16, 64}) {
				for (const int32_t step : {1, 2}) {
					const NoiseOctaveGrid::Args args{
						.originX = origin.x,
						.originY = origin.y,
						.side = 48,
						.stepM = step,
						.phaseM = 0.5F,
						.wavelengthM = wavelength,
						.seed = 0xC0FFEEU + static_cast<uint32_t>(wavelength),
					};
					const NoiseOctaveGrid grid(args);
					for (int32_t j = 0; j < args.side; ++j) {
						for (int32_t i = 0; i < args.side; ++i) {
							const double x = (static_cast<double>(origin.x + static_cast<int64_t>(i) * step) + 0.5) / wavelength;
							const double y = (static_cast<double>(origin.y + static_cast<int64_t>(j) * step) + 0.5) / wavelength;
							ASSERT_EQ(static_cast<double>(static_cast<float>(x)), x) << "the reference position must be exact";
							ASSERT_EQ(static_cast<double>(static_cast<float>(y)), y) << "the reference position must be exact";
							const float expected = foundation::gradientNoise2(static_cast<float>(x), static_cast<float>(y), args.seed);
							ASSERT_EQ(bits(grid.at(i, j)), bits(expected))
								<< "origin (" << origin.x << ", " << origin.y << ") wavelength " << wavelength << " step " << step << " sample (" << i
								<< ", " << j << "): " << grid.at(i, j) << " against " << expected;
							++compared;
						}
					}
				}
			}
		}
		EXPECT_EQ(compared, kOrigins.size() * 3U * 2U * 48U * 48U);
	}

	// Any other wavelength rounds the offset in a cell, so the grid, which forms it from whole meters,
	// and gradientNoise2, which takes it off a float position, part by the float position's rounding:
	// near the origin, where that is a few ulps of a few hundred cells, they agree to 1e-4.
	TEST(NoiseLatticeTest, MatchesGradientNoise2AtAnyWavelength) {
		for (const Origin origin : {Origin{0, 0}, Origin{-37, -1000}, Origin{-512, 488}}) {
			for (const int32_t wavelength : {5, 12, 22, 200}) {
				const NoiseOctaveGrid::Args args{
					.originX = origin.x,
					.originY = origin.y,
					.side = 64,
					.stepM = 1,
					.phaseM = 0.375F,
					.wavelengthM = wavelength,
					.seed = 77U,
				};
				const NoiseOctaveGrid grid(args);
				for (int32_t j = 0; j < args.side; ++j) {
					for (int32_t i = 0; i < args.side; ++i) {
						const auto x = static_cast<float>((static_cast<double>(origin.x + i) + 0.375) / wavelength);
						const auto y = static_cast<float>((static_cast<double>(origin.y + j) + 0.375) / wavelength);
						ASSERT_NEAR(grid.at(i, j), foundation::gradientNoise2(x, y, args.seed), 1e-4F)
							<< "origin (" << origin.x << ", " << origin.y << ") wavelength " << wavelength << " sample (" << i << ", " << j << ")";
					}
				}
			}
		}
	}

	// Two grids that share a world point give it the same bits, however far apart their origins and
	// on either side of zero: what lets a chunk's apron read a neighbor's tile as the neighbor does.
	TEST(NoiseLatticeTest, ASampleIgnoresTheGridsOrigin) {
		for (const Origin origin : kOrigins) {
			for (const int32_t wavelength : {5, 12, 22, 200}) {
				for (const int32_t step : {1, 2, 8}) {
					const NoiseOctaveGrid::Args a{
						.originX = origin.x,
						.originY = origin.y,
						.side = 40,
						.stepM = step,
						.phaseM = 0.5F,
						.wavelengthM = wavelength,
						.seed = 9U,
					};
					// Shifted by (13, 21) samples, so the two overlap by 27 x 19.
					NoiseOctaveGrid::Args b = a;
					b.originX = origin.x + 13 * static_cast<int64_t>(step);
					b.originY = origin.y + 21 * static_cast<int64_t>(step);
					const NoiseOctaveGrid gridA(a);
					const NoiseOctaveGrid gridB(b);
					for (int32_t j = 21; j < a.side; ++j) {
						for (int32_t i = 13; i < a.side; ++i) {
							ASSERT_EQ(bits(gridA.at(i, j)), bits(gridB.at(i - 13, j - 21)))
								<< "origin (" << origin.x << ", " << origin.y << ") wavelength " << wavelength << " step " << step << " sample (" << i
								<< ", " << j << ")";
						}
					}
				}
			}
		}
	}

} // namespace world
