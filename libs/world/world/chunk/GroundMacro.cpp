#include "world/chunk/GroundMacro.h"

#include "world/chunk/NoiseLattice.h"

#include <random/HashNoise.h>

#include <algorithm>
#include <cassert>
#include <cmath>

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

		uint8_t unitByte(float v) {
			return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
		}

	} // namespace

	void fillGroundMacro(ChunkCoordinate coord, uint64_t worldSeed, std::span<uint8_t> rgba) {
		assert(rgba.size() == kGroundMacroBytes);
		const int64_t	 originX = static_cast<int64_t>(coord.x) * kChunkSize;
		const int64_t	 originY = static_cast<int64_t>(coord.y) * kChunkSize;
		auto octave = [&](int32_t wavelengthM, int32_t field) {
			return NoiseOctaveGrid({
				.originX = originX,
				.originY = originY,
				.side = kGroundMacroSide,
				.stepM = kGroundMacroMetersPerTexel,
				.phaseM = kNoisePhaseM,
				.wavelengthM = wavelengthM,
				.seed = fieldSeed(worldSeed, field),
			});
		};
		const NoiseOctaveGrid mixCoarse = octave(kGroundMacroMixWavelengthsM[0], 0);
		const NoiseOctaveGrid mixFine = octave(kGroundMacroMixWavelengthsM[1], 1);
		const NoiseOctaveGrid mid = octave(kGroundMacroMidWavelengthM, 2);
		const NoiseOctaveGrid fine = octave(kGroundMacroFineWavelengthM, 3);

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
