#pragma once

// BiomeWeights - Sparse top-4 biome blend weights.
// Stores at most 4 biome/weight pairs (Entry{ biome, weight255 }).
// weight255 is quantized to [0,255] (1/255 steps); weights sum to 255 when normalised.
// API is compatible with the old dense-array version: get/set/has/primary/secondary/
// primaryWeight/normalize/total/single all behave identically from the caller's view.

#include "world/Biome.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace engine::world {

struct BiomeWeights {
    static constexpr size_t kMaxEntries = 4;

    struct Entry {
        uint8_t biome  = 0;    // cast of Biome enum
        uint8_t weight = 0;    // 0–255
    };

    std::array<Entry, kMaxEntries> entries{};
    uint8_t count = 0;

    // ── read ────────────────────────────────────────────────────────────────

    [[nodiscard]] float get(Biome biome) const {
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].biome == static_cast<uint8_t>(biome))
                return static_cast<float>(entries[i].weight) / 255.0F;
        }
        return 0.0F;
    }

    [[nodiscard]] bool has(Biome biome) const {
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].biome == static_cast<uint8_t>(biome) && entries[i].weight > 0)
                return true;
        }
        return false;
    }

    [[nodiscard]] Biome primary() const {
        Biome best      = Biome::TemperateGrassland;
        uint8_t bestW   = 0;
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].weight > bestW) {
                bestW = entries[i].weight;
                best  = static_cast<Biome>(entries[i].biome);
            }
        }
        return best;
    }

    [[nodiscard]] Biome secondary() const {
        Biome best       = Biome::TemperateGrassland;
        Biome second     = Biome::TemperateGrassland;
        uint8_t bestW    = 0;
        uint8_t secondW  = 0;
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].weight > bestW) {
                secondW = bestW;  second = best;
                bestW   = entries[i].weight;
                best    = static_cast<Biome>(entries[i].biome);
            } else if (entries[i].weight > secondW) {
                secondW = entries[i].weight;
                second  = static_cast<Biome>(entries[i].biome);
            }
        }
        return (secondW > 0) ? second : best;
    }

    [[nodiscard]] float primaryWeight() const {
        uint8_t best = 0;
        for (uint8_t i = 0; i < count; ++i)
            if (entries[i].weight > best) best = entries[i].weight;
        return static_cast<float>(best) / 255.0F;
    }

    [[nodiscard]] float total() const {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < count; ++i) sum += entries[i].weight;
        return static_cast<float>(sum) / 255.0F;
    }

    // ── write ───────────────────────────────────────────────────────────────

    void set(Biome biome, float weight) {
        uint8_t w = static_cast<uint8_t>(weight * 255.0F + 0.5F);
        uint8_t key = static_cast<uint8_t>(biome);

        // Update existing entry.
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].biome == key) {
                entries[i].weight = w;
                return;
            }
        }

        if (w == 0) return; // Nothing to insert.

        if (count < kMaxEntries) {
            entries[count++] = { key, w };
            return;
        }

        // Evict the entry with the smallest weight.
        uint8_t minIdx = 0;
        for (uint8_t i = 1; i < kMaxEntries; ++i)
            if (entries[i].weight < entries[minIdx].weight) minIdx = i;

        if (w > entries[minIdx].weight)
            entries[minIdx] = { key, w };
    }

    void normalize() {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < count; ++i) sum += entries[i].weight;
        if (sum == 0) return;
        // Scale so weights sum to 255.
        for (uint8_t i = 0; i < count; ++i) {
            uint32_t scaled = static_cast<uint32_t>(entries[i].weight) * 255 / sum;
            entries[i].weight = static_cast<uint8_t>(scaled);
        }
    }

    // ── factories ───────────────────────────────────────────────────────────

    [[nodiscard]] static BiomeWeights single(Biome biome) {
        BiomeWeights bw;
        bw.entries[0] = { static_cast<uint8_t>(biome), 255 };
        bw.count = 1;
        return bw;
    }
};

} // namespace engine::world
