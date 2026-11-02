#include "world/chunk/TileBiomes.h"

#include "world/chunk/TerrainPolygonBuilder.h"

#include <random/HashNoise.h>

#include <algorithm>

namespace world {

	namespace {

		struct Octave {
			int32_t wavelengthM = 0;
			int32_t shiftM = 0;		  ///< moves the octave's lattice off its neighbors', whose zeros would otherwise coincide
			float	amplitude = 0.0F; ///< in biome weight
		};

		// Across a planet blend band (500 m each side of a cell edge) a biome's weight changes 0.001 a
		// meter, so a weight difference changes 0.002 and a unit of noise difference moves the boundary
		// about amplitude / 0.002 m: tens of meters at 200 m, a few at 12 m.
		constexpr std::array<Octave, 2> kCoarseOctaves = {{{200, 0, 0.23F}, {50, 23, 0.06F}}};
		constexpr Octave				kFineOctave = {12, 7, 0.02F};
		// The ecotone: one octave shared by every pair, deciding a land pair whose edge scores lie within
		// its amplitude, so islands and bays of 3 to 10 m stand off the edge. A finer octave adds no
		// islands, only fragments of a tile or two.
		constexpr Octave kEcotoneOctave = {8, 0, 0.08F};

		// The coarse octaves change little over a few meters, so they are summed at every 8th tile and
		// read bilinearly; the error is under 1e-3 of a weight.
		constexpr int32_t kCoarseStep = 8;
		constexpr int32_t kCoarseSide = kChunkSize / kCoarseStep + 1;
		static_assert(kChunkSize % kCoarseStep == 0, "a chunk's coarse samples must line up with its neighbors'");

		// Gradient noise stays within [-1, 1] (gradientNoise2's corners are at most sqrt(2) long), so an
		// octave moves a score by at most its amplitude, and a bilinear read of octaves by at most theirs.
		constexpr float kCoarseReach = kCoarseOctaves[0].amplitude + kCoarseOctaves[1].amplitude;
		constexpr float kFineReach = kFineOctave.amplitude;
		constexpr float kEcotoneReach = kEcotoneOctave.amplitude;

		constexpr uint32_t kSaltBiomeEdge = 0xB10E0001U;
		constexpr int32_t  kEcotoneField = 0x7FFF;

		uint32_t fieldSeed(uint64_t worldSeed, int32_t field) {
			return foundation::hash3(
				static_cast<int32_t>(kSaltBiomeEdge), static_cast<int32_t>(worldSeed >> 32U), field, static_cast<uint32_t>(worldSeed)
			);
		}

		int32_t edgeField(Biome biome, int32_t octave) {
			return static_cast<int32_t>(biome) * 8 + octave;
		}

		uint8_t weightByte(float weight) {
			return static_cast<uint8_t>(std::clamp(weight * 255.0F + 0.5F, 0.0F, 255.0F));
		}

	} // namespace

	ChunkBiomes::ChunkBiomes(ChunkCoordinate coord, const std::array<BiomeWeights, 4>& corners, uint64_t worldSeed)
		: originX(static_cast<int64_t>(coord.x) * kChunkSize),
		  originY(static_cast<int64_t>(coord.y) * kChunkSize) {
		for (const BiomeWeights& weights : corners) {
			for (uint8_t e = 0; e < weights.count; ++e) {
				const auto biome = static_cast<Biome>(weights.entries[e].biome);
				if (weights.entries[e].weight == 0 ||
					std::any_of(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(count), [biome](const Candidate& c) {
						return c.biome == biome;
					})) {
					continue;
				}
				candidates[count++] = {.biome = biome, .water = isBiomeWater(biome)};
			}
		}
		// Ties go to the lower biome, whichever corner listed it first.
		std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(count), [](const Candidate& a, const Candidate& b) {
			return a.biome < b.biome;
		});
		for (size_t k = 0; k < count; ++k) {
			for (size_t c = 0; c < corners.size(); ++c) {
				candidates[k].corner[c] = corners[c].get(candidates[k].biome);
			}
			hasWater = hasWater || candidates[k].water;
		}
		if (count < 2) {
			return;
		}

		coarseNoise.assign(count * static_cast<size_t>(kCoarseSide) * kCoarseSide, 0.0F);
		fineNoise.reserve(count);
		for (size_t k = 0; k < count; ++k) {
			float* coarse = &coarseNoise[k * static_cast<size_t>(kCoarseSide) * kCoarseSide];
			for (size_t o = 0; o < kCoarseOctaves.size(); ++o) {
				// Sample (i, j) is tile (8i, 8j)'s center; the last row and column are the neighbors' first.
				const NoiseOctaveGrid octave({
					.originX = originX + kCoarseOctaves[o].shiftM,
					.originY = originY + kCoarseOctaves[o].shiftM,
					.side = kCoarseSide,
					.stepM = kCoarseStep,
					.phaseM = 0.5F,
					.wavelengthM = kCoarseOctaves[o].wavelengthM,
					.seed = fieldSeed(worldSeed, edgeField(candidates[k].biome, static_cast<int32_t>(o))),
				});
				for (int32_t j = 0; j < kCoarseSide; ++j) {
					for (int32_t i = 0; i < kCoarseSide; ++i) {
						coarse[static_cast<size_t>(j) * kCoarseSide + static_cast<size_t>(i)] +=
							kCoarseOctaves[o].amplitude * octave.at(i, j);
					}
				}
			}
			fineNoise.emplace_back(
				NoiseOctaveGrid::Args{
					.originX = originX + kFineOctave.shiftM,
					.originY = originY + kFineOctave.shiftM,
					.side = kChunkSize,
					.stepM = 1,
					.phaseM = 0.5F,
					.wavelengthM = kFineOctave.wavelengthM,
					.seed = fieldSeed(worldSeed, edgeField(candidates[k].biome, static_cast<int32_t>(kCoarseOctaves.size()))),
				}
			);
		}
		if (std::count_if(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(count), [](const Candidate& c) {
				return !c.water;
			}) >= 2) {
			ecotoneNoise.emplace(
				NoiseOctaveGrid::Args{
					.originX = originX + kEcotoneOctave.shiftM,
					.originY = originY + kEcotoneOctave.shiftM,
					.side = kChunkSize,
					.stepM = 1,
					.phaseM = 0.5F,
					.wavelengthM = kEcotoneOctave.wavelengthM,
					.seed = fieldSeed(worldSeed, kEcotoneField),
				}
			);
		}
	}

	TileBiome ChunkBiomes::at(uint16_t localX, uint16_t localY) const {
		if (count == 0) {
			return {};
		}
		if (count == 1) {
			return {.primary = candidates[0].biome, .secondary = candidates[0].biome, .blend = 255};
		}

		// The corner weights at the tile's center: a chunk's last tile and its neighbor's first sit half
		// a tile either side of the corners they share, so the weights run on across the border.
		constexpr float					  kInverseChunk = 1.0F / static_cast<float>(kChunkSize);
		const float						  u = (static_cast<float>(localX) + 0.5F) * kInverseChunk;
		const float						  v = (static_cast<float>(localY) + 0.5F) * kInverseChunk;
		std::array<float, kMaxCandidates> score;
		for (size_t k = 0; k < count; ++k) {
			const std::array<float, 4>& c = candidates[k].corner;
			const float					top = c[0] * (1.0F - u) + c[1] * u;
			const float					bottom = c[2] * (1.0F - u) + c[3] * u;
			score[k] = top * (1.0F - v) + bottom * v;
		}
		const std::array<float, kMaxCandidates> weight = score;

		// The best and runner-up of `score` among `active`, ties to the lower biome.
		uint32_t active = (1U << count) - 1U;
		size_t	 lead = 0;
		size_t	 next = 0;
		auto	 rank = [&]() {
			lead = kMaxCandidates;
			next = kMaxCandidates;
			for (size_t k = 0; k < count; ++k) {
				if ((active & (1U << k)) == 0) {
					continue;
				}
				if (lead == kMaxCandidates || score[k] > score[lead]) {
					next = lead;
					lead = k;
				} else if (next == kMaxCandidates || score[k] > score[next]) {
					next = k;
				}
			}
		};

		// A biome the noise can't lift to within the ecotone's reach of the leader plays no part, and
		// a stage ends the pick once what's left can't change the winner or bring a land pair within
		// the ecotone's reach. Both tests compare one float expression, the leader's lead: written
		// another way (b + reach <= a) a rounding tie could prune the runner-up of a pair `settled`
		// had just called open, leaving `rank` without one.
		auto leadOver = [&](size_t k) {
			return score[lead] - score[k];
		};
		auto settled = [&](float reachLeft) {
			return leadOver(next) >= 2.0F * reachLeft + (candidates[lead].water ? 0.0F : kEcotoneReach);
		};
		rank();
		if (settled(kCoarseReach + kFineReach)) {
			return {.primary = candidates[lead].biome, .secondary = candidates[next].biome, .blend = weightByte(weight[lead])};
		}
		const float reach = 2.0F * (kCoarseReach + kFineReach) + kEcotoneReach;
		for (size_t k = 0; k < count; ++k) {
			if (leadOver(k) >= reach) {
				active &= ~(1U << k);
			}
		}

		const int32_t ci = localX / kCoarseStep;
		const int32_t cj = localY / kCoarseStep;
		const float	  fx = static_cast<float>(localX % kCoarseStep) * (1.0F / kCoarseStep);
		const float	  fy = static_cast<float>(localY % kCoarseStep) * (1.0F / kCoarseStep);
		for (size_t k = 0; k < count; ++k) {
			if ((active & (1U << k)) != 0) {
				const float* c00 = &coarseNoise[(k * kCoarseSide + static_cast<size_t>(cj)) * kCoarseSide + static_cast<size_t>(ci)];
				const float* c01 = c00 + kCoarseSide;
				const float	 top = c00[0] + fx * (c00[1] - c00[0]);
				const float	 bottom = c01[0] + fx * (c01[1] - c01[0]);
				score[k] += top + fy * (bottom - top);
			}
		}
		rank();
		if (!settled(kFineReach)) {
			for (size_t k = 0; k < count; ++k) {
				if ((active & (1U << k)) != 0) {
					score[k] += kFineOctave.amplitude * fineNoise[k].at(localX, localY);
				}
			}
			rank();
		}

		const float gap = score[lead] - score[next];
		if (!candidates[lead].water && !candidates[next].water && gap < kEcotoneReach) {
			// The lower biome of the pair wins where its score less the other's, plus the ecotone noise,
			// is at least zero.
			const float noise = kEcotoneOctave.amplitude * ecotoneNoise->at(localX, localY);
			const bool	leadIsLower = candidates[lead].biome < candidates[next].biome;
			if (leadIsLower ? gap + noise < 0.0F : noise - gap >= 0.0F) {
				std::swap(lead, next);
			}
		}
		return {.primary = candidates[lead].biome, .secondary = candidates[next].biome, .blend = weightByte(weight[lead])};
	}

} // namespace world
