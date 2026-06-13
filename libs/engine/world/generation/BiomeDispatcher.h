#pragma once

// BiomeDispatcher - Routes generation to the appropriate biome generator.
// All 21 worldgen biomes are mapped to one of the 8 existing generators.

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"

#include "world/generation/BeachGenerator.h"
#include "world/generation/DesertGenerator.h"
#include "world/generation/ForestGenerator.h"
#include "world/generation/GrasslandGenerator.h"
#include "world/generation/MountainGenerator.h"
#include "world/generation/OceanGenerator.h"
#include "world/generation/TundraGenerator.h"
#include "world/generation/WetlandGenerator.h"

namespace engine::world::generation {

class BiomeDispatcher {
public:
    [[nodiscard]] static GenerationResult generate(const GenerationContext& ctx) {
        switch (ctx.biome) {
            // ── Forest group ──────────────────────────────────────────────
            case Biome::TropicalRainforest:
            case Biome::TropicalSeasonalForest:
            case Biome::TemperateDeciduousForest:
            case Biome::TemperateRainforest:
            case Biome::BorealForest:
            case Biome::MontaneForest:
                return ForestGenerator::generate(ctx);

            // ── Grassland group ───────────────────────────────────────────
            case Biome::TropicalSavanna:
            case Biome::TemperateGrassland:
            case Biome::AlpineGrassland:
                return GrasslandGenerator::generate(ctx);

            // ── Desert group ──────────────────────────────────────────────
            case Biome::HotDesert:
            case Biome::ColdDesert:
            case Biome::SemiDesert:
            case Biome::XericShrubland:
                return DesertGenerator::generate(ctx);

            // ── Tundra / polar group ──────────────────────────────────────
            case Biome::ArcticTundra:
            case Biome::PolarDesert:
                return TundraGenerator::generate(ctx);

            // Old Mountain mapped to AlpineTundra; preserve MountainGenerator visuals.
            case Biome::AlpineTundra:
                return MountainGenerator::generate(ctx);

            // ── Wetland group ─────────────────────────────────────────────
            case Biome::TemperateWetland:
            case Biome::TropicalWetland:
                return WetlandGenerator::generate(ctx);

            // ── Beach ─────────────────────────────────────────────────────
            case Biome::Beach:
                return BeachGenerator::generate(ctx);

            // ── Ocean / Lake ──────────────────────────────────────────────
            case Biome::Ocean:
            case Biome::Lake:
                return OceanGenerator::generate(ctx);

            case Biome::Count:
                return GrasslandGenerator::generate(ctx);
        }
        return GrasslandGenerator::generate(ctx);
    }
};

} // namespace engine::world::generation
