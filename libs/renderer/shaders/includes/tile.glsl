// tile.glsl - Tile rendering with procedural edge variation
//
// Provides tile rendering with adjacency-based edge/corner darkening
// and procedural variation for organic-looking tile boundaries.

// ============================================================================
// PROCEDURAL NOISE FUNCTIONS
// ============================================================================

// 2D hash function for better spatial distribution
// Uses multiple primes to avoid axis-aligned patterns
vec2 tileHash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return fract(sin(p) * 43758.5453);
}

// Single float hash from 2D input
float tileHash2to1(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Value noise with 2D input - smoother, more organic than 1D
float tileNoise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    // Smooth interpolation (quintic for C2 continuity)
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    // Sample corners
    float a = tileHash2to1(i);
    float b = tileHash2to1(i + vec2(1.0, 0.0));
    float c = tileHash2to1(i + vec2(0.0, 1.0));
    float d = tileHash2to1(i + vec2(1.0, 1.0));

    // Bilinear interpolation
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractal Brownian Motion - layer multiple octaves for organic appearance
// This breaks up the obvious patterns of single-frequency noise
float tileFBM(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    float maxValue = 0.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * tileNoise2D(p * frequency);
        maxValue += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value / maxValue;  // Normalize to [0, 1]
}

// ============================================================================
// TILE RENDERING FUNCTION
// ============================================================================

/// Render a tile with procedural edge variation.
/// @param uv Normalized UV coordinates [0,1] within the tile
/// @param tileX World tile X coordinate (integer)
/// @param tileY World tile Y coordinate (integer)
/// @param edgeMask Bitmask for edges requiring darkening (bits: N=0, E=1, S=2, W=3)
/// @param cornerMask Bitmask for corners requiring darkening (bits: NW=0, NE=1, SE=2, SW=3)
/// @param hardEdgeMask Bitmask for hard edges (different surface family)
/// @return Darkening factor to multiply with color (1.0 = no darkening)
float computeTileEdgeDarkening(
    vec2 uv,
    int tileX,
    int tileY,
    uint edgeMask,
    uint cornerMask,
    uint hardEdgeMask
) {
    // ========== PROCEDURAL EDGE PARAMETERS ==========
    // Significantly increased for visibility at normal zoom levels
    const float kBaseEdgeWidth = 0.09;        // Base edge width (9% of tile) - thicker
    const float kWidthVariation = 0.04;       // ±4% width variation
    const float kWaveAmplitude = 0.03;        // ±3% wave displacement
    const float kNoiseScale = 3.0;            // Scale for FBM noise sampling
    const float kEdgeDarkenFactor = 0.75;     // Darkening multiplier for soft edges (closer to base color)
    const float kHardEdgeDarkenFactor = 0.70; // Slightly stronger darkening for hard edges

    // World positions for deterministic variation
    float worldX = float(tileX) + uv.x;
    float worldY = float(tileY) + uv.y;

    // ========== PROCEDURAL EDGE WIDTHS ==========
    // Each edge uses 2D FBM noise for organic, non-repeating patterns.
    // The key insight: use the world coordinate along the edge + a perpendicular
    // offset unique to that edge position, ensuring adjacent tiles match.

    // North edge: at y=0, varies along X
    // Sample noise along the line y=tileY (north boundary)
    float nNoise = tileFBM(vec2(worldX * kNoiseScale, float(tileY) * 0.73), 3);
    float nThreshold = kBaseEdgeWidth + (nNoise - 0.5) * kWidthVariation * 2.0;
    // Add gentle waviness using a different noise sample
    nThreshold += (tileFBM(vec2(worldX * 1.7, float(tileY) * 0.91), 2) - 0.5) * kWaveAmplitude * 2.0;

    // South edge: at y=1, must match north edge of tile (tileX, tileY+1)
    float southWorldY = float(tileY + 1);
    float sNoise = tileFBM(vec2(worldX * kNoiseScale, southWorldY * 0.73), 3);
    float sThreshold = kBaseEdgeWidth + (sNoise - 0.5) * kWidthVariation * 2.0;
    sThreshold += (tileFBM(vec2(worldX * 1.7, southWorldY * 0.91), 2) - 0.5) * kWaveAmplitude * 2.0;

    // East edge: at x=1, must match west edge of tile (tileX+1, tileY)
    float eastWorldX = float(tileX + 1);
    float eNoise = tileFBM(vec2(eastWorldX * 0.73, worldY * kNoiseScale), 3);
    float eThreshold = kBaseEdgeWidth + (eNoise - 0.5) * kWidthVariation * 2.0;
    eThreshold += (tileFBM(vec2(eastWorldX * 0.91, worldY * 1.7), 2) - 0.5) * kWaveAmplitude * 2.0;

    // West edge: at x=0, varies along Y
    float wNoise = tileFBM(vec2(float(tileX) * 0.73, worldY * kNoiseScale), 3);
    float wThreshold = kBaseEdgeWidth + (wNoise - 0.5) * kWidthVariation * 2.0;
    wThreshold += (tileFBM(vec2(float(tileX) * 0.91, worldY * 1.7), 2) - 0.5) * kWaveAmplitude * 2.0;

    // ========== EDGE DETECTION ==========
    // Determine if pixel is in each edge region (with procedural variation)
    bool inN = uv.y < nThreshold;
    bool inS = (1.0 - uv.y) < sThreshold;
    bool inE = (1.0 - uv.x) < eThreshold;
    bool inW = uv.x < wThreshold;

    // ========== EDGE DARKENING ==========
    // Edge bits: 0=N, 1=E, 2=S, 3=W
    // Hard edge mask uses different bit layout for 8 directions
    float darkenFactor = 1.0;

    if (inN) {
        if ((hardEdgeMask & 0x80u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x1u) != 0u) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }
    if (inE) {
        if ((hardEdgeMask & 0x20u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x2u) != 0u) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }
    if (inS) {
        if ((hardEdgeMask & 0x08u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x4u) != 0u) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }
    if (inW) {
        if ((hardEdgeMask & 0x02u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x8u) != 0u) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }

    // ========== ROUNDED CORNER DARKENING ==========
    // Interior corners are quarter-circles that smoothly connect adjacent edges.
    // The corner radius uses the average of the two edge widths meeting at that corner.
    // Corner bits: 0=NW, 1=NE, 2=SE, 3=SW

    // NW corner (top-left): connects north edge (at x=0) and west edge (at y=0)
    if ((cornerMask & 0x1u) != 0u) {
        // Get edge widths at the corner point (uv near 0,0)
        float nwRadius = (nThreshold + wThreshold) * 0.5;
        float distNW = length(uv);  // distance from corner (0,0)
        if (distNW < nwRadius) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }

    // NE corner (top-right): connects north edge (at x=1) and east edge (at y=0)
    if ((cornerMask & 0x2u) != 0u) {
        float neRadius = (nThreshold + eThreshold) * 0.5;
        float distNE = length(vec2(1.0 - uv.x, uv.y));  // distance from corner (1,0)
        if (distNE < neRadius) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }

    // SE corner (bottom-right): connects south edge (at x=1) and east edge (at y=1)
    if ((cornerMask & 0x4u) != 0u) {
        float seRadius = (sThreshold + eThreshold) * 0.5;
        float distSE = length(vec2(1.0 - uv.x, 1.0 - uv.y));  // distance from corner (1,1)
        if (distSE < seRadius) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }

    // SW corner (bottom-left): connects south edge (at x=0) and west edge (at y=1)
    if ((cornerMask & 0x8u) != 0u) {
        float swRadius = (sThreshold + wThreshold) * 0.5;
        float distSW = length(vec2(uv.x, 1.0 - uv.y));  // distance from corner (0,1)
        if (distSW < swRadius) {
            darkenFactor = min(darkenFactor, kEdgeDarkenFactor);
        }
    }

    return darkenFactor;
}
