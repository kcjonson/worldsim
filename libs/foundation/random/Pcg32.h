#pragma once

// PCG32 — O'Neill (2014) "PCG: A Family of Simple Fast Space-Efficient Statistically Good
// Algorithms for Random Number Generation", pcg_oneseq_64_xsh_rr_32 variant.
//
// This is THE rng for world-generation code. std::mt19937 is banned in worldgen because:
//   - its 624-word state blooms per-task memory usage in parallel generation
//   - seeding semantics differ across implementations (MSVC vs libstdc++ warming loops)
//   - PCG32 passes all BigCrush tests with 8 bytes of state and ~1.5× the throughput
//
// Cross-platform determinism: all operations are standard unsigned integer arithmetic;
// identical output on x64, ARM, WASM, etc. at any optimization level.

#include "SplitMix64.h"
#include <bit>
#include <cstdint>

namespace foundation {

class Pcg32 {
  public:
    // Seed from a uint64 via SplitMix64 so any bit pattern (including 0) gives a
    // well-distributed initial state.
    explicit constexpr Pcg32(uint64_t seed) {
        state = 0;
        nextUInt32();
        state += splitMix64(seed);
        nextUInt32();
    }

    constexpr uint32_t nextUInt32() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    // Returns a float in [0, 1). Uses 23 explicit mantissa bits so there are no gaps
    // from imprecise conversion of large uint32 values.
    constexpr float nextFloat() {
        uint32_t bits = 0x3F800000U | (nextUInt32() >> 9);
        return std::bit_cast<float>(bits) - 1.0F;
    }

    // Returns a double in [0, 1) using 52 explicit mantissa bits.
    constexpr double nextDouble() {
        uint64_t hi = nextUInt32();
        uint64_t lo = nextUInt32();
        uint64_t mantissa = (hi << 20) | (lo >> 12); // 52 bits
        uint64_t bits = 0x3FF0000000000000ULL | mantissa;
        return std::bit_cast<double>(bits) - 1.0;
    }

    // Lemire (2018) nearly-divisionless range: uniform in [0, bound) with at most one
    // division per call. The rare slow path kicks in only when m < bound (< 1/2^31 of calls).
    constexpr uint32_t nextRange(uint32_t bound) {
        if (bound == 0) return 0;
        uint64_t m = static_cast<uint64_t>(nextUInt32()) * static_cast<uint64_t>(bound);
        uint32_t lo = static_cast<uint32_t>(m);
        if (lo < bound) {
            uint32_t threshold = (0u - bound) % bound;
            while (lo < threshold) {
                m = static_cast<uint64_t>(nextUInt32()) * static_cast<uint64_t>(bound);
                lo = static_cast<uint32_t>(m);
            }
        }
        return static_cast<uint32_t>(m >> 32);
    }

  private:
    uint64_t state{};
};

} // namespace foundation
