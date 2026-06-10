#pragma once

// Deterministic noise built on integer hashing.
//
// All functions here are cross-platform bit-identical because:
//   - Lattice values come from integer hashes (no float state)
//   - Float arithmetic uses only +, -, *, / — IEEE 754 basic operations, correctly
//     rounded on all compliant implementations
//   - No transcendental calls (no sin/cos/exp/log in any path)
//
// Callers who need bit-identical results across sessions must quantize their
// input coordinates consistently (e.g. multiply world coordinates by a fixed
// reciprocal rather than dividing, then truncate to a fixed-point representation).

#include <cstdint>

namespace foundation {

// ============================================================================
// Integer hash
// ============================================================================

// Hash3: strong avalanche hash over three integers and a seed.
// Based on the lowbias32 / Murmur2 finalizer pattern — all bits of all inputs
// affect all output bits ("strict avalanche criterion").
// Output is uniform over uint32; not suitable as a noise value directly (too jagged).
//
// Initialization with FNV offset (non-zero basis) prevents the all-zero fixed point
// that would otherwise occur when seed=x=y=z=0.
inline uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    uint32_t h = 0x811C9DC5U; // FNV offset basis as non-zero start
    h ^= seed;                 h *= 0xBF58476DU; h ^= h >> 16;
    h ^= static_cast<uint32_t>(x); h *= 0x94D049BBU; h ^= h >> 16;
    h ^= static_cast<uint32_t>(y); h *= 0xBF58476DU; h ^= h >> 16;
    h ^= static_cast<uint32_t>(z); h *= 0x94D049BBU; h ^= h >> 16;
    // Two-round finalizer for strong avalanche
    h ^= h >> 15; h *= 0x85EBCA77U; h ^= h >> 13; h *= 0xC2B2AE3DU; h ^= h >> 16;
    return h;
}

// ============================================================================
// Interpolation helpers (file-internal)
// ============================================================================

namespace detail {

// Quintic fade: 6t^5 - 15t^4 + 10t^3 (C2 continuous, Perlin 2002)
inline float quintic(float t) {
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Map uint32 hash to float in [0, 1)
inline float hashToFloat(uint32_t h) {
    return static_cast<float>(h >> 8) * (1.0F / 16777216.0F); // h>>8 gives 24 bits, 2^24=16777216
}

// Fixed 16-entry gradient table (Perlin-style). Each entry is a unit vector on the
// edges of a unit cube — 12 unique directions padded to 16 for power-of-two lookup.
// Stored as (gx, gy, gz) interleaved.
inline void gradient(uint32_t h, float& gx, float& gy, float& gz) {
    // 12 directions on cube edges, repeated to fill 16 slots
    static const float kGrads[16][3] = {
        { 1, 1, 0}, {-1, 1, 0}, { 1,-1, 0}, {-1,-1, 0},
        { 1, 0, 1}, {-1, 0, 1}, { 1, 0,-1}, {-1, 0,-1},
        { 0, 1, 1}, { 0,-1, 1}, { 0, 1,-1}, { 0,-1,-1},
        { 1, 1, 0}, {-1, 1, 0}, { 0,-1, 1}, { 0,-1,-1}, // pad slots 12-15
    };
    uint32_t idx = h & 15u;
    gx = kGrads[idx][0]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    gy = kGrads[idx][1]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    gz = kGrads[idx][2]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
}

} // namespace detail

// ============================================================================
// Value noise
// ============================================================================

// ValueNoise3: trilinear-interpolated value noise.
// Lattice values from hash3; weights from quintic fade of the fractional part.
// Range: [0, 1).
inline float valueNoise3(float x, float y, float z, uint32_t seed) {
    auto ix = static_cast<int32_t>(x >= 0.0F ? static_cast<int32_t>(x) : static_cast<int32_t>(x) - 1);
    auto iy = static_cast<int32_t>(y >= 0.0F ? static_cast<int32_t>(y) : static_cast<int32_t>(y) - 1);
    auto iz = static_cast<int32_t>(z >= 0.0F ? static_cast<int32_t>(z) : static_cast<int32_t>(z) - 1);

    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);
    float fz = z - static_cast<float>(iz);

    float ux = detail::quintic(fx);
    float uy = detail::quintic(fy);
    float uz = detail::quintic(fz);

    float v000 = detail::hashToFloat(hash3(ix,   iy,   iz,   seed));
    float v100 = detail::hashToFloat(hash3(ix+1, iy,   iz,   seed));
    float v010 = detail::hashToFloat(hash3(ix,   iy+1, iz,   seed));
    float v110 = detail::hashToFloat(hash3(ix+1, iy+1, iz,   seed));
    float v001 = detail::hashToFloat(hash3(ix,   iy,   iz+1, seed));
    float v101 = detail::hashToFloat(hash3(ix+1, iy,   iz+1, seed));
    float v011 = detail::hashToFloat(hash3(ix,   iy+1, iz+1, seed));
    float v111 = detail::hashToFloat(hash3(ix+1, iy+1, iz+1, seed));

    float x00 = detail::lerp(v000, v100, ux);
    float x10 = detail::lerp(v010, v110, ux);
    float x01 = detail::lerp(v001, v101, ux);
    float x11 = detail::lerp(v011, v111, ux);
    float y0  = detail::lerp(x00,  x10,  uy);
    float y1  = detail::lerp(x01,  x11,  uy);
    return detail::lerp(y0, y1, uz);
}

// ============================================================================
// Gradient noise (Perlin-style)
// ============================================================================

// GradientNoise3: Perlin-style gradient noise with hash-selected gradients.
// Range: approximately [-1, 1]; exact bounds depend on gradient table geometry.
inline float gradientNoise3(float x, float y, float z, uint32_t seed) {
    auto ix = static_cast<int32_t>(x >= 0.0F ? static_cast<int32_t>(x) : static_cast<int32_t>(x) - 1);
    auto iy = static_cast<int32_t>(y >= 0.0F ? static_cast<int32_t>(y) : static_cast<int32_t>(y) - 1);
    auto iz = static_cast<int32_t>(z >= 0.0F ? static_cast<int32_t>(z) : static_cast<int32_t>(z) - 1);

    float fx = x - static_cast<float>(ix);
    float fy = y - static_cast<float>(iy);
    float fz = z - static_cast<float>(iz);

    float ux = detail::quintic(fx);
    float uy = detail::quintic(fy);
    float uz = detail::quintic(fz);

    auto dot = [](uint32_t h, float dx, float dy, float dz) -> float {
        float gx{}, gy{}, gz{};
        detail::gradient(h, gx, gy, gz);
        return gx * dx + gy * dy + gz * dz;
    };

    float g000 = dot(hash3(ix,   iy,   iz,   seed), fx,       fy,       fz      );
    float g100 = dot(hash3(ix+1, iy,   iz,   seed), fx-1.0F,  fy,       fz      );
    float g010 = dot(hash3(ix,   iy+1, iz,   seed), fx,       fy-1.0F,  fz      );
    float g110 = dot(hash3(ix+1, iy+1, iz,   seed), fx-1.0F,  fy-1.0F,  fz      );
    float g001 = dot(hash3(ix,   iy,   iz+1, seed), fx,       fy,       fz-1.0F );
    float g101 = dot(hash3(ix+1, iy,   iz+1, seed), fx-1.0F,  fy,       fz-1.0F );
    float g011 = dot(hash3(ix,   iy+1, iz+1, seed), fx,       fy-1.0F,  fz-1.0F );
    float g111 = dot(hash3(ix+1, iy+1, iz+1, seed), fx-1.0F,  fy-1.0F,  fz-1.0F );

    float x00 = detail::lerp(g000, g100, ux);
    float x10 = detail::lerp(g010, g110, ux);
    float x01 = detail::lerp(g001, g101, ux);
    float x11 = detail::lerp(g011, g111, ux);
    float y0  = detail::lerp(x00,  x10,  uy);
    float y1  = detail::lerp(x01,  x11,  uy);
    return detail::lerp(y0, y1, uz);
}

// ============================================================================
// Fractal / ridged noise
// ============================================================================

// FractalNoise3: fBm over gradientNoise3.
// lacunarity: frequency multiplier per octave (typically 2.0)
// gain: amplitude multiplier per octave (typically 0.5 for H=1 fBm)
inline float fractalNoise3(float x, float y, float z, uint32_t seed,
                           int octaves, float lacunarity, float gain) {
    float value = 0.0F;
    float amplitude = 1.0F;
    float frequency = 1.0F;
    float maxAmp = 0.0F;
    for (int i = 0; i < octaves; ++i) {
        value += gradientNoise3(x * frequency, y * frequency, z * frequency,
                                seed + static_cast<uint32_t>(i)) * amplitude;
        maxAmp += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return maxAmp > 0.0F ? value / maxAmp : 0.0F;
}

// RidgedNoise3: ridged multifractal — (1 - |noise|) per octave, sharpened at crests.
// Produces mountain-ridge-like features where gradientNoise3 would give smooth hills.
inline float ridgedNoise3(float x, float y, float z, uint32_t seed,
                          int octaves, float lacunarity, float gain) {
    float value = 0.0F;
    float amplitude = 1.0F;
    float frequency = 1.0F;
    float maxAmp = 0.0F;
    for (int i = 0; i < octaves; ++i) {
        float n = gradientNoise3(x * frequency, y * frequency, z * frequency,
                                 seed + static_cast<uint32_t>(i));
        float r = n < 0.0F ? 1.0F + n : 1.0F - n; // 1 - |n|
        value += r * amplitude;
        maxAmp += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return maxAmp > 0.0F ? value / maxAmp : 0.0F;
}

} // namespace foundation
