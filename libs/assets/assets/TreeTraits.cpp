#include "TreeTraits.h"

#include "assets/AssetDefinition.h"
#include "assets/placement/DecimeterGrid.h"
#include "world/generation/GroveField.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace assets {

	namespace {

		constexpr uint64_t kAgeFieldSalt = 0xA6EF1E1D5EEDULL;
		constexpr uint64_t kAgeHashSalt = 0x7EE5A9E5C0FFEEULL;
		constexpr uint64_t kVariantSalt = 0x5A71A27B10CCULL;

		// Share of a tree's age value that comes from the age field; the rest is its own hash,
		// so a patch leans one way without every tree in it being the same age.
		constexpr float kFieldShare = 0.6F;

		// In a thicket core the age value is scaled by (1 - this), so a thicket is a dense
		// stand of young trees rather than veterans packed trunk to trunk.
		constexpr float kThicketYoungLean = 0.6F;

		// world::grove::warpedNoise at the quantiles i/16 (its distribution is the same at any feature
		// size and seed), measured over 2.56 million samples across 64 seeds; the ends are its
		// range. Mapping through them makes the field uniform, so the age weights hold.
		constexpr std::array<float, 17> kFieldQuantiles{
			0.0F,	   0.25495F, 0.31032F, 0.35091F, 0.38527F, 0.41610F, 0.44502F, 0.47274F, 0.5F,
			0.52728F, 0.55495F, 0.58388F, 0.61491F, 0.64926F, 0.68992F, 0.74540F, 1.0F,
		};

		uint64_t mix64(uint64_t z) {
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			return z ^ (z >> 31);
		}

		// FNV-1a: std::hash differs between standard libraries, and traits must not.
		uint64_t nameHash(std::string_view name) {
			uint64_t h = 0xCBF29CE484222325ULL;
			for (const char c : name) {
				h ^= static_cast<unsigned char>(c);
				h *= 0x100000001B3ULL;
			}
			return h;
		}

		uint64_t cellHash(DecimeterCell cell, uint64_t salt) {
			const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(cell.x)) << 32) | static_cast<uint32_t>(cell.y);
			return mix64(salt ^ mix64(packed));
		}

		// [0,1)
		float unitFloat(uint64_t h) {
			return static_cast<float>(static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0));
		}

		float uniformFromField(float field) {
			constexpr size_t kSteps = kFieldQuantiles.size() - 1;
			for (size_t i = 0; i < kSteps; ++i) {
				if (field < kFieldQuantiles[i + 1]) {
					const float t = (field - kFieldQuantiles[i]) / (kFieldQuantiles[i + 1] - kFieldQuantiles[i]);
					return (static_cast<float>(i) + t) / static_cast<float>(kSteps);
				}
			}
			return 1.0F;
		}

		// The CDF of a * U1 + b * U2 for independent uniforms U1 and U2, with a = kFieldShare,
		// b = 1 - a, and a >= b: turns the blend of field and hash back into a uniform value.
		float uniformFromBlend(float x) {
			constexpr float a = kFieldShare;
			constexpr float b = 1.0F - kFieldShare;
			if (x <= b) {
				return x * x / (2.0F * a * b);
			}
			if (x <= a) {
				return (2.0F * x - b) / (2.0F * a);
			}
			const float r = 1.0F - x;
			return 1.0F - r * r / (2.0F * a * b);
		}

	} // namespace

	TreeTraits computeTreeTraits(const AssetDefinition& def, glm::vec2 worldPosition, uint64_t worldSeed) {
		const DecimeterCell cell = decimeterCell(worldPosition);
		const uint64_t		defSalt = worldSeed ^ nameHash(def.defName);

		TreeTraits traits;
		if (def.ageClasses.size() > 1) {
			const float x = static_cast<float>(cell.x) * 0.1F;
			const float y = static_cast<float>(cell.y) * 0.1F;
			const float field = uniformFromField(world::grove::warpedNoise(x, y, kAgeFeatureMeters, worldSeed ^ kAgeFieldSalt));
			const float own = unitFloat(cellHash(cell, defSalt ^ kAgeHashSalt));
			const float thicket = world::grove::thicketWeight(world::grove::groveValue(x, y, worldSeed));
			const float age = uniformFromBlend(kFieldShare * field + (1.0F - kFieldShare) * own) * (1.0F - kThicketYoungLean * thicket);

			const size_t last = def.ageClasses.size() - 1;
			size_t		 index = last;
			float		 cumulative = 0.0F;
			for (size_t i = 0; i < last; ++i) {
				cumulative += def.ageClasses[i].weight;
				if (age < cumulative) {
					index = i;
					break;
				}
			}
			traits.ageClass = static_cast<uint8_t>(index);
		}

		const uint32_t variants = def.ageClasses.empty() ? def.variantCount : def.ageClasses[traits.ageClass].variants;
		if (variants > 1) {
			traits.variant = static_cast<uint16_t>(cellHash(cell, defSalt ^ kVariantSalt) % variants);
		}
		return traits;
	}

} // namespace assets
