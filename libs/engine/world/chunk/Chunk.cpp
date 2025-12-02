#include "Chunk.h"

namespace engine::world {

	Chunk::Chunk(ChunkCoordinate coord, ChunkSampleResult biomeData, uint64_t worldSeed)
		: m_coord(coord),
		  m_biomeData(std::move(biomeData)),
		  m_worldSeed(worldSeed) {
		touch();
	}

	TileData Chunk::getTile(uint16_t localX, uint16_t localY) const {
		return generateTile(localX, localY);
	}

	TileData Chunk::generateTile(uint16_t localX, uint16_t localY) const {
		TileData tile;

		// Get biome from pre-computed data
		tile.biome = m_biomeData.getTileBiome(localX, localY);

		// Get elevation from interpolation
		tile.elevation = m_biomeData.getTileElevation(localX, localY);

		// Generate deterministic properties from hash
		uint32_t hash = tileHash(m_coord, localX, localY, m_worldSeed);

		// Select ground cover based on biome
		tile.groundCover = selectGroundCover(tile.biome.primary(), hash);

		// Generate moisture (normalized hash + biome influence)
		constexpr float kNormalize = 1.0F / static_cast<float>(UINT32_MAX);
		tile.moisture = static_cast<float>(hash) * kNormalize;

		// Adjust moisture based on biome
		Biome primary = tile.biome.primary();
		if (primary == Biome::Desert) {
			tile.moisture *= 0.2F;
		} else if (primary == Biome::Wetland || primary == Biome::Ocean) {
			tile.moisture = 0.8F + tile.moisture * 0.2F;
		}

		return tile;
	}

	GroundCover Chunk::selectGroundCover(Biome biome, uint32_t hash) const {
		// Use hash to add minimal variation within biome
		// For now, keep it very uniform (~98%+ primary ground cover)
		// Real variation will come from procedural assets, not ground cover noise
		float variation = static_cast<float>(hash % 1000) / 1000.0F;

		switch (biome) {
			case Biome::Grassland:
				return (variation < 0.98F) ? GroundCover::Grass : GroundCover::Dirt;

			case Biome::Forest:
				return (variation < 0.98F) ? GroundCover::Grass : GroundCover::Dirt;

			case Biome::Desert:
				return (variation < 0.98F) ? GroundCover::Sand : GroundCover::Rock;

			case Biome::Tundra:
				return (variation < 0.95F) ? GroundCover::Snow : GroundCover::Rock;

			case Biome::Wetland:
				return (variation < 0.95F) ? GroundCover::Water : GroundCover::Grass;

			case Biome::Mountain:
				// Mountains have more visible variation (rock vs snow)
				if (variation < 0.15F) {
					return GroundCover::Snow;
				}
				return GroundCover::Rock;

			case Biome::Beach:
				return (variation < 0.98F) ? GroundCover::Sand : GroundCover::Rock;

			case Biome::Ocean:
				return GroundCover::Water;

			default:
				return GroundCover::Grass;
		}
	}

	uint32_t Chunk::tileHash(ChunkCoordinate chunk, uint16_t localX, uint16_t localY, uint64_t seed) {
		// Combine all coordinates into a deterministic hash
		uint64_t h = seed;
		h ^= static_cast<uint64_t>(chunk.x) * 0x9E3779B97F4A7C15ULL;
		h ^= static_cast<uint64_t>(chunk.y) * 0xC6A4A7935BD1E995ULL;
		h ^= static_cast<uint64_t>(localX) * 0x85EBCA6B;
		h ^= static_cast<uint64_t>(localY) * 0xC2B2AE35;
		h ^= h >> 33;
		h *= 0xFF51AFD7ED558CCDULL;
		h ^= h >> 33;
		return static_cast<uint32_t>(h);
	}

	Foundation::Color Chunk::getBiomeColor(Biome biome) {
		switch (biome) {
			case Biome::Grassland:
				return Foundation::Color(0.29F, 0.49F, 0.25F, 1.0F); // #4a7c3f

			case Biome::Forest:
				return Foundation::Color(0.18F, 0.35F, 0.12F, 1.0F); // #2d5a1f

			case Biome::Desert:
				return Foundation::Color(0.82F, 0.71F, 0.47F, 1.0F); // #d1b578

			case Biome::Tundra:
				return Foundation::Color(0.75F, 0.78F, 0.80F, 1.0F); // #c0c7cc

			case Biome::Wetland:
				return Foundation::Color(0.25F, 0.42F, 0.35F, 1.0F); // #406b59

			case Biome::Mountain:
				return Foundation::Color(0.42F, 0.42F, 0.42F, 1.0F); // #6b6b6b

			case Biome::Beach:
				return Foundation::Color(0.77F, 0.64F, 0.35F, 1.0F); // #c4a35a

			case Biome::Ocean:
				return Foundation::Color(0.10F, 0.30F, 0.48F, 1.0F); // #1a4c7a

			default:
				return Foundation::Color(0.5F, 0.5F, 0.5F, 1.0F);
		}
	}

	Foundation::Color Chunk::getGroundCoverColor(GroundCover cover) {
		switch (cover) {
			case GroundCover::Grass:
				return Foundation::Color(0.29F, 0.49F, 0.25F, 1.0F);

			case GroundCover::Dirt:
				return Foundation::Color(0.45F, 0.35F, 0.25F, 1.0F);

			case GroundCover::Sand:
				return Foundation::Color(0.82F, 0.71F, 0.47F, 1.0F);

			case GroundCover::Rock:
				return Foundation::Color(0.42F, 0.42F, 0.42F, 1.0F);

			case GroundCover::Water:
				return Foundation::Color(0.10F, 0.30F, 0.48F, 1.0F);

			case GroundCover::Snow:
				return Foundation::Color(0.95F, 0.97F, 1.0F, 1.0F);

			default:
				return Foundation::Color(0.5F, 0.5F, 0.5F, 1.0F);
		}
	}

} // namespace engine::world
