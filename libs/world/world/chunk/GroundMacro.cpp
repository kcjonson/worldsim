#include "world/chunk/GroundMacro.h"

#include <random/HashNoise.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <vector>

namespace world {

	namespace {

		constexpr uint32_t kSaltGroundMacro = 0x3AC20001U;
		// Read off the noise lattice, whose points are zeros of gradient noise and fall on even meters.
		constexpr float kNoisePhaseM = 0.5F;
		// One octave of gradient noise stays within about +-0.7; the value channels map that to 0..1.
		constexpr float kValueRange = 0.7F;
		// The mix's octaves and how far its 0.5-centered sum is stretched toward the dry and lush ends.
		constexpr float kMixCoarseWeight = 0.65F;
		constexpr float kMixFineWeight = 0.35F;
		constexpr float kMixContrast = 1.2F;

		uint32_t fieldSeed(uint64_t worldSeed, int32_t field) {
			return foundation::hash3(
				static_cast<int32_t>(kSaltGroundMacro), static_cast<int32_t>(worldSeed >> 32U), field, static_cast<uint32_t>(worldSeed)
			);
		}

		int64_t floorDiv(int64_t a, int64_t b) {
			const int64_t q = a / b;
			return (a % b != 0 && a < 0) ? q - 1 : q;
		}

		/// One octave of HashNoise's gradient noise (gradientNoise2's field) over the macro grid. A texel
		/// shares its lattice cell with its neighbors, so each corner's gradient is hashed once for the
		/// chunk instead of four times a texel.
		class OctaveGrid {
		  public:
			OctaveGrid(int64_t originX, int64_t originY, int32_t wavelength, uint32_t seed) {
				const int64_t firstX = floorDiv(originX, wavelength);
				const int64_t firstY = floorDiv(originY, wavelength);
				fillAxis(columns, originX, firstX, wavelength);
				fillAxis(rows, originY, firstY, wavelength);
				cornersWide = columns.back().cell + 2;
				const int32_t cornersHigh = rows.back().cell + 2;
				gradients.resize(static_cast<size_t>(cornersWide) * static_cast<size_t>(cornersHigh) * 2);
				for (int32_t cy = 0; cy < cornersHigh; ++cy) {
					for (int32_t cx = 0; cx < cornersWide; ++cx) {
						float		   gz = 0.0F;
						const size_t   k = (static_cast<size_t>(cy) * static_cast<size_t>(cornersWide) + static_cast<size_t>(cx)) * 2;
						const uint32_t h = foundation::hash3(static_cast<int32_t>(firstX + cx), static_cast<int32_t>(firstY + cy), 0, seed);
						foundation::detail::gradient(h, gradients[k], gradients[k + 1], gz);
					}
				}
			}

			[[nodiscard]] float at(int32_t i, int32_t j) const {
				const Axis&	 c = columns[static_cast<size_t>(i)];
				const Axis&	 r = rows[static_cast<size_t>(j)];
				const float* g00 =
					&gradients[(static_cast<size_t>(r.cell) * static_cast<size_t>(cornersWide) + static_cast<size_t>(c.cell)) * 2];
				const float* g01 = g00 + static_cast<size_t>(cornersWide) * 2;
				const float	 d00 = g00[0] * c.f + g00[1] * r.f;
				const float	 d10 = g00[2] * (c.f - 1.0F) + g00[3] * r.f;
				const float	 d01 = g01[0] * c.f + g01[1] * (r.f - 1.0F);
				const float	 d11 = g01[2] * (c.f - 1.0F) + g01[3] * (r.f - 1.0F);
				const float	 x0 = d00 + c.u * (d10 - d00);
				const float	 x1 = d01 + c.u * (d11 - d01);
				return x0 + r.u * (x1 - x0);
			}

		  private:
			struct Axis {
				int32_t cell = 0; // lattice cell, counted from the chunk's first
				float	f = 0.0F; // offset inside it, 0..1
				float	u = 0.0F; // its quintic fade
			};

			static void fillAxis(std::array<Axis, kGroundMacroSide>& axis, int64_t origin, int64_t firstCell, int32_t wavelength) {
				const float inverse = 1.0F / static_cast<float>(wavelength);
				for (int32_t t = 0; t < kGroundMacroSide; ++t) {
					const int64_t meters = origin + static_cast<int64_t>(t) * kGroundMacroMetersPerTexel;
					const int64_t cell = floorDiv(meters, wavelength);
					const float	  f = (static_cast<float>(meters - cell * wavelength) + kNoisePhaseM) * inverse;
					axis[static_cast<size_t>(t)] = {
						.cell = static_cast<int32_t>(cell - firstCell),
						.f = f,
						.u = foundation::detail::quintic(f),
					};
				}
			}

			std::array<Axis, kGroundMacroSide> columns;
			std::array<Axis, kGroundMacroSide> rows;
			int32_t							   cornersWide = 0;
			std::vector<float>				   gradients; // x, y per corner, rows of cornersWide
		};

		uint8_t unitByte(float v) {
			return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
		}

	} // namespace

	void fillGroundMacro(ChunkCoordinate coord, uint64_t worldSeed, std::span<uint8_t> rgba) {
		assert(rgba.size() == kGroundMacroBytes);
		const int64_t	 originX = static_cast<int64_t>(coord.x) * kChunkSize;
		const int64_t	 originY = static_cast<int64_t>(coord.y) * kChunkSize;
		const OctaveGrid mixCoarse(originX, originY, kGroundMacroMixWavelengthsM[0], fieldSeed(worldSeed, 0));
		const OctaveGrid mixFine(originX, originY, kGroundMacroMixWavelengthsM[1], fieldSeed(worldSeed, 1));
		const OctaveGrid mid(originX, originY, kGroundMacroMidWavelengthM, fieldSeed(worldSeed, 2));
		const OctaveGrid fine(originX, originY, kGroundMacroFineWavelengthM, fieldSeed(worldSeed, 3));

		for (int32_t j = 0; j < kGroundMacroSide; ++j) {
			uint8_t* row = rgba.data() + static_cast<size_t>(j) * kGroundMacroSide * 4;
			for (int32_t i = 0; i < kGroundMacroSide; ++i) {
				const float mix = kMixCoarseWeight * mixCoarse.at(i, j) + kMixFineWeight * mixFine.at(i, j);
				uint8_t*	texel = row + static_cast<size_t>(i) * 4;
				texel[0] = unitByte(0.5F + kMixContrast * mix);
				texel[1] = unitByte(0.5F + 0.5F * mid.at(i, j) / kValueRange);
				texel[2] = unitByte(0.5F + 0.5F * fine.at(i, j) / kValueRange);
				texel[3] = 255;
			}
		}
	}

} // namespace world
