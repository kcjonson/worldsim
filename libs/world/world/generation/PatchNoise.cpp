#include "world/generation/PatchNoise.h"

#include <random/HashNoise.h>

namespace world::generation {

	namespace {

		constexpr uint32_t kSaltPatch = 0x9A7C0001U;

		// The first octave's lattice spacing, meters, per field; the second's is half, so each is even.
		// Gradient noise's patches run about 1.7 times smaller than those of the value noise this
		// replaced at the same spacing, so these are 1.7 to 1.9 times its wavelengths: patch sizes stay,
		// and the grass family's patch edges cover no more ground than before (tiles within 3 of another
		// surface: 73% of grassland against 75%, 28% of forest against 31%), which is what the land
		// field's cost follows.
		constexpr std::array<int32_t, static_cast<size_t>(PatchField::Count)> kWavelengthsM = {22, 12, 10, 16, 14};

	} // namespace

	PatchNoise::PatchNoise(ChunkCoordinate chunk, uint64_t seed)
		: coord(chunk),
		  worldSeed(seed) {}

	PatchNoise::Octaves PatchNoise::build(PatchField field) const {
		const int32_t  wavelength = kWavelengthsM[static_cast<size_t>(field)];
		const uint32_t seed = foundation::hash3(
			static_cast<int32_t>(kSaltPatch),
			static_cast<int32_t>(worldSeed >> 32U),
			static_cast<int32_t>(field),
			static_cast<uint32_t>(worldSeed)
		);
		const int64_t originX = static_cast<int64_t>(coord.x) * kChunkSize;
		const int64_t originY = static_cast<int64_t>(coord.y) * kChunkSize;
		// The second octave's lattice is shifted a quarter of the first's spacing, so the two
		// octaves' zeros don't coincide.
		const int32_t shift = wavelength / 4;
		return {
			.first = NoiseOctaveGrid({
				.originX = originX,
				.originY = originY,
				.side = kChunkSize,
				.wavelengthM = wavelength,
				.seed = seed,
			}),
			.second = NoiseOctaveGrid({
				.originX = originX + shift,
				.originY = originY + shift,
				.side = kChunkSize,
				.wavelengthM = wavelength / 2,
				.seed = seed + 1U,
			}),
		};
	}

} // namespace world::generation
