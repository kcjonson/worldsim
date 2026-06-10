#pragma once

#include <cstdint>

namespace foundation {

// SplitMix64 — Steele et al. (2014) "Fast Splittable Pseudorandom Number Generators".
// Single-step mix: advances state by the golden-ratio increment then applies two
// multiply-xorshift finalizers with strong avalanche constants.
//
// Primary use: derive independent per-stage seeds from a world seed without any
// sequential correlation between stages. Do not use for sampling; use Pcg32 for that.
constexpr uint64_t splitMix64(uint64_t state) {
    state += 0x9E3779B97F4A7C15ULL;
    state = (state ^ (state >> 30)) * 0xBF58476D1CE4E5B9ULL;
    state = (state ^ (state >> 27)) * 0x94D049BB133111EBULL;
    return state ^ (state >> 31);
}

// Derives an independent sub-seed for a given (worldSeed, streamId) pair.
// Each (worldSeed, streamId) combination produces a statistically independent
// bit stream — safe to use for per-stage, per-tile-region, or per-feature seeding.
// determinism: pure integer math, identical on all platforms.
constexpr uint64_t deriveSeed(uint64_t worldSeed, uint64_t streamId) {
    // Mix worldSeed first so that streamId=0 doesn't return worldSeed verbatim,
    // then fold streamId in with a second mix so adjacent stream ids are uncorrelated.
    uint64_t s = splitMix64(worldSeed);
    s ^= streamId * 0x9E3779B97F4A7C15ULL;
    return splitMix64(s);
}

} // namespace foundation
