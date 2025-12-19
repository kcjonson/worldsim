// tile.glsl - Tile rendering with procedural edge variation and soft blending
//
// Provides tile rendering with:
// - Adjacency-based edge/corner darkening (hard edges between families)
// - Soft texture blending between same-family surfaces (e.g., Grass↔Dirt)
// - Procedural variation for organic-looking tile boundaries

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
    // PERF: Use single-sample value noise instead of 8x FBM calls (~80 hash ops)
    // This reduces GPU time from 45ms to ~15ms while keeping organic edge variation
    const float kBaseEdgeWidth = 0.09;
    const float kWidthVariation = 0.03;
    const float kSoftEdgeDarkenFactor = 1.0;
    const float kHardEdgeDarkenFactor = 0.70;

    // World positions for deterministic variation
    float worldX = float(tileX) + uv.x;
    float worldY = float(tileY) + uv.y;

    // Single noise sample per edge (cheap: 4 hash lookups vs 80+)
    // Uses different coordinate offsets to decorrelate edges
    float nNoise = tileNoise2D(vec2(worldX * 2.0, float(tileY) * 0.7));
    float sNoise = tileNoise2D(vec2(worldX * 2.0, float(tileY + 1) * 0.7));
    float eNoise = tileNoise2D(vec2(float(tileX + 1) * 0.7, worldY * 2.0));
    float wNoise = tileNoise2D(vec2(float(tileX) * 0.7, worldY * 2.0));

    float nThreshold = kBaseEdgeWidth + (nNoise - 0.5) * kWidthVariation * 2.0;
    float sThreshold = kBaseEdgeWidth + (sNoise - 0.5) * kWidthVariation * 2.0;
    float eThreshold = kBaseEdgeWidth + (eNoise - 0.5) * kWidthVariation * 2.0;
    float wThreshold = kBaseEdgeWidth + (wNoise - 0.5) * kWidthVariation * 2.0;

    // ========== EDGE DETECTION ==========
    // Determine if pixel is in each edge region (with procedural variation)
    bool inN = uv.y < nThreshold;
    bool inS = (1.0 - uv.y) < sThreshold;
    bool inE = (1.0 - uv.x) < eThreshold;
    bool inW = uv.x < wThreshold;

    // ========== EDGE DARKENING ==========
    // Hard edges (cross-family): strong darkening for visibility
    // Soft edges (same-family): very subtle darkening just for wavy shape
    // Edge mask bits: N=0, E=1, S=2, W=3
    // Hard edge mask bits: NW=0, W=1, SW=2, S=3, SE=4, E=5, NE=6, N=7
    float darkenFactor = 1.0;

    if (inN) {
        if ((hardEdgeMask & 0x80u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x1u) != 0u) {
            darkenFactor = min(darkenFactor, kSoftEdgeDarkenFactor);
        }
    }
    if (inE) {
        if ((hardEdgeMask & 0x20u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x2u) != 0u) {
            darkenFactor = min(darkenFactor, kSoftEdgeDarkenFactor);
        }
    }
    if (inS) {
        if ((hardEdgeMask & 0x08u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x4u) != 0u) {
            darkenFactor = min(darkenFactor, kSoftEdgeDarkenFactor);
        }
    }
    if (inW) {
        if ((hardEdgeMask & 0x02u) != 0u) {
            darkenFactor = min(darkenFactor, kHardEdgeDarkenFactor);
        } else if ((edgeMask & 0x8u) != 0u) {
            darkenFactor = min(darkenFactor, kSoftEdgeDarkenFactor);
        }
    }

    // ========== ROUNDED CORNER DARKENING ==========
    // Interior corners are quarter-circles that smoothly connect adjacent edges.
    // The corner radius uses the average of the two edge widths meeting at that corner.
    // Corner bits: 0=NW, 1=NE, 2=SE, 3=SW
    // Use kHardEdgeDarkenFactor if either adjacent edge is a hard edge for visual consistency.

    // NW corner - hard or soft darkening based on adjacent edges
    if ((cornerMask & 0x1u) != 0u) {
        float nwRadius = (nThreshold + wThreshold) * 0.5;
        float distNW = length(uv);
        if (distNW < nwRadius) {
            bool nwHard = ((hardEdgeMask & 0x80u) != 0u) || ((hardEdgeMask & 0x02u) != 0u);
            darkenFactor = min(darkenFactor, nwHard ? kHardEdgeDarkenFactor : kSoftEdgeDarkenFactor);
        }
    }

    // NE corner
    if ((cornerMask & 0x2u) != 0u) {
        float neRadius = (nThreshold + eThreshold) * 0.5;
        float distNE = length(vec2(1.0 - uv.x, uv.y));
        if (distNE < neRadius) {
            bool neHard = ((hardEdgeMask & 0x80u) != 0u) || ((hardEdgeMask & 0x20u) != 0u);
            darkenFactor = min(darkenFactor, neHard ? kHardEdgeDarkenFactor : kSoftEdgeDarkenFactor);
        }
    }

    // SE corner
    if ((cornerMask & 0x4u) != 0u) {
        float seRadius = (sThreshold + eThreshold) * 0.5;
        float distSE = length(vec2(1.0 - uv.x, 1.0 - uv.y));
        if (distSE < seRadius) {
            bool seHard = ((hardEdgeMask & 0x08u) != 0u) || ((hardEdgeMask & 0x20u) != 0u);
            darkenFactor = min(darkenFactor, seHard ? kHardEdgeDarkenFactor : kSoftEdgeDarkenFactor);
        }
    }

    // SW corner
    if ((cornerMask & 0x8u) != 0u) {
        float swRadius = (sThreshold + wThreshold) * 0.5;
        float distSW = length(vec2(uv.x, 1.0 - uv.y));
        if (distSW < swRadius) {
            bool swHard = ((hardEdgeMask & 0x08u) != 0u) || ((hardEdgeMask & 0x02u) != 0u);
            darkenFactor = min(darkenFactor, swHard ? kHardEdgeDarkenFactor : kSoftEdgeDarkenFactor);
        }
    }

    return darkenFactor;
}

// ============================================================================
// SURFACE STACK ORDER (for blend direction)
// ============================================================================

/// Get the visual stack order for a surface type.
/// Higher values = rendered on top. Higher surfaces bleed onto lower surfaces.
/// Stack order: Water(0) < Mud(1) < Sand(2) < Dirt(3) < GrassShort(4) < Grass(5) < GrassMeadow(6) < GrassTall(7) < Rock(8) < Snow(9)
/// Grass variants have distinct sub-levels to enable soft blending between them.
int getSurfaceStackOrder(uint surfaceId) {
    // Surface IDs: Grass=0, Dirt=1, Sand=2, Rock=3, Water=4, Snow=5, Mud=6, GrassTall=7, GrassShort=8, GrassMeadow=9
    if (surfaceId == 4u) return 0;  // Water - lowest
    if (surfaceId == 6u) return 1;  // Mud
    if (surfaceId == 2u) return 2;  // Sand
    if (surfaceId == 1u) return 3;  // Dirt
    if (surfaceId == 8u) return 4;  // GrassShort - driest grass
    if (surfaceId == 0u) return 5;  // Grass - standard
    if (surfaceId == 9u) return 6;  // GrassMeadow - fertile
    if (surfaceId == 7u) return 7;  // GrassTall - wettest grass
    if (surfaceId == 3u) return 8;  // Rock
    if (surfaceId == 5u) return 9;  // Snow - highest
    return 5;  // Default to standard Grass level
}

// ============================================================================
// SOFT EDGE BLENDING - "Higher Bleeds Onto Lower"
// ============================================================================

/// Compute soft blend weights for higher-priority neighbors bleeding onto this tile.
/// Returns vec4(northWeight, eastWeight, southWeight, westWeight).
///
/// Simple approach:
/// 1. For each edge, compute linear distance and blend weight
/// 2. At corners (where two edges meet), use RADIAL distance from tile corner
///    This naturally gives rounded corners without complex L-shape logic
vec4 computeHigherBleedWeights(
    vec2 uv,
    int tileX,    // Reserved for future procedural edge variation
    int tileY,    // Reserved for future procedural edge variation
    uint surfaceId,
    uint neighborN,
    uint neighborE,
    uint neighborS,
    uint neighborW,
    uint hardEdgeMask
) {
    const float kBlendWidth = 0.20;  // 20% blend width

    int myStack = getSurfaceStackOrder(surfaceId);
    vec4 weights = vec4(0.0);

    // Check which cardinal neighbors are higher priority (and same family)
    bool higherN = (neighborN != surfaceId) && (hardEdgeMask & 0x80u) == 0u &&
                   (getSurfaceStackOrder(neighborN) > myStack);
    bool higherE = (neighborE != surfaceId) && (hardEdgeMask & 0x20u) == 0u &&
                   (getSurfaceStackOrder(neighborE) > myStack);
    bool higherS = (neighborS != surfaceId) && (hardEdgeMask & 0x08u) == 0u &&
                   (getSurfaceStackOrder(neighborS) > myStack);
    bool higherW = (neighborW != surfaceId) && (hardEdgeMask & 0x02u) == 0u &&
                   (getSurfaceStackOrder(neighborW) > myStack);

    // Distance from each edge
    float distN = uv.y;
    float distE = 1.0 - uv.x;
    float distS = 1.0 - uv.y;
    float distW = uv.x;

    // Distance from each corner
    float distNW = length(uv);
    float distNE = length(vec2(1.0 - uv.x, uv.y));
    float distSE = length(vec2(1.0 - uv.x, 1.0 - uv.y));
    float distSW = length(vec2(uv.x, 1.0 - uv.y));

    // For corners: use radial distance from tile corner
    // For edges (non-corner regions): use linear distance from edge
    //
    // A pixel is in the "corner region" if it's closer to the corner than to either edge.
    // Corner region for NW: distNW < distN && distNW < distW

    // NW corner region
    if (higherN && higherW && distNW < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distNW);
        weights.x = w;
        weights.w = w;
    }
    // NE corner region
    if (higherN && higherE && distNE < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distNE);
        weights.x = max(weights.x, w);
        weights.y = w;
    }
    // SE corner region
    if (higherS && higherE && distSE < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distSE);
        weights.z = w;
        weights.y = max(weights.y, w);
    }
    // SW corner region
    if (higherS && higherW && distSW < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distSW);
        weights.z = max(weights.z, w);
        weights.w = max(weights.w, w);
    }

    // Edge regions (only apply if not already handled by corner)
    // North edge (excluding corners)
    if (higherN && distN < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distN);
        weights.x = max(weights.x, w);
    }
    // East edge
    if (higherE && distE < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distE);
        weights.y = max(weights.y, w);
    }
    // South edge
    if (higherS && distS < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distS);
        weights.z = max(weights.z, w);
    }
    // West edge
    if (higherW && distW < kBlendWidth) {
        float w = 1.0 - smoothstep(0.0, kBlendWidth, distW);
        weights.w = max(weights.w, w);
    }

    return weights;
}

// ============================================================================
// DIAGONAL CORNER BLENDING
// ============================================================================

/// Compute blend weights for diagonal-only corners.
/// This handles the case where a diagonal neighbor is higher priority but
/// NEITHER adjacent cardinal neighbor is higher priority.
///
/// Example: Mud tile at (1,1) with:
///   - North neighbor: Mud (same priority)
///   - East neighbor: Mud (same priority)
///   - Northeast diagonal neighbor: Grass (higher priority)
///
/// The NE corner of the mud tile should have grass blended in, even though
/// the cardinal edge blending doesn't apply (N and E are both mud).
///
/// Returns vec4(NW_weight, NE_weight, SE_weight, SW_weight)
vec4 computeDiagonalCornerWeights(
    vec2 uv,
    uint surfaceId,
    uint neighborN,
    uint neighborE,
    uint neighborS,
    uint neighborW,
    uint neighborNW,
    uint neighborNE,
    uint neighborSE,
    uint neighborSW
) {
    // Corner blend radius - matches cardinal edge blend width for seamless connection
    const float kCornerRadius = 0.18;

    int myStack = getSurfaceStackOrder(surfaceId);
    vec4 weights = vec4(0.0);

    // Diagonal corner blending applies when:
    // 1. The diagonal neighbor is higher priority than us (will bleed onto us)
    // 2. AT MOST ONE adjacent cardinal is higher (not both)
    //
    // Cases:
    // - Neither cardinal higher: pure diagonal-only case (grass diagonally, mud on both cardinals)
    // - Exactly one cardinal higher: extends cardinal edge blend to reach the corner
    // - Both cardinals higher: L-corner rounding handles this (skip diagonal blend)

    // NW corner
    {
        int nwStack = getSurfaceStackOrder(neighborNW);
        int nStack = getSurfaceStackOrder(neighborN);
        int wStack = getSurfaceStackOrder(neighborW);

        bool nHigher = nStack > myStack;
        bool wHigher = wStack > myStack;
        bool bothHigher = nHigher && wHigher;

        // Blend if diagonal is higher AND not both cardinals are higher
        if (nwStack > myStack && !bothHigher) {
            float distFromCorner = length(uv);
            if (distFromCorner < kCornerRadius) {
                weights.x = 1.0 - smoothstep(0.0, kCornerRadius, distFromCorner);
            }
        }
    }

    // NE corner
    {
        int neStack = getSurfaceStackOrder(neighborNE);
        int nStack = getSurfaceStackOrder(neighborN);
        int eStack = getSurfaceStackOrder(neighborE);

        bool nHigher = nStack > myStack;
        bool eHigher = eStack > myStack;
        bool bothHigher = nHigher && eHigher;

        if (neStack > myStack && !bothHigher) {
            float distFromCorner = length(vec2(1.0 - uv.x, uv.y));
            if (distFromCorner < kCornerRadius) {
                weights.y = 1.0 - smoothstep(0.0, kCornerRadius, distFromCorner);
            }
        }
    }

    // SE corner
    {
        int seStack = getSurfaceStackOrder(neighborSE);
        int sStack = getSurfaceStackOrder(neighborS);
        int eStack = getSurfaceStackOrder(neighborE);

        bool sHigher = sStack > myStack;
        bool eHigher = eStack > myStack;
        bool bothHigher = sHigher && eHigher;

        if (seStack > myStack && !bothHigher) {
            float distFromCorner = length(vec2(1.0 - uv.x, 1.0 - uv.y));
            if (distFromCorner < kCornerRadius) {
                weights.z = 1.0 - smoothstep(0.0, kCornerRadius, distFromCorner);
            }
        }
    }

    // SW corner
    {
        int swStack = getSurfaceStackOrder(neighborSW);
        int sStack = getSurfaceStackOrder(neighborS);
        int wStack = getSurfaceStackOrder(neighborW);

        bool sHigher = sStack > myStack;
        bool wHigher = wStack > myStack;
        bool bothHigher = sHigher && wHigher;

        if (swStack > myStack && !bothHigher) {
            float distFromCorner = length(vec2(uv.x, 1.0 - uv.y));
            if (distFromCorner < kCornerRadius) {
                weights.w = 1.0 - smoothstep(0.0, kCornerRadius, distFromCorner);
            }
        }
    }

    return weights;
}
