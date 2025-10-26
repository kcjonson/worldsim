# 3D to 2D Sampling

Created: 2025-10-26
Status: Technical Design

## Overview

The 3D to 2D sampling system converts data from the spherical world model into 2D chunks suitable for gameplay. This document specifies the algorithms and data structures for efficiently transforming spherical coordinates, querying biome data, and generating deterministic terrain while maintaining performance.

**Core Principle**: Sample the 3D world at **chunk granularity** (not per-tile), minimizing expensive spherical mathematics and 3D world queries.

## Coordinate Systems

### Three Coordinate Spaces

**1. Chunk Coordinates** (2D integer grid):
```cpp
struct ChunkCoordinate {
    int32_t x;  // Chunk index in X direction
    int32_t y;  // Chunk index in Y direction
};
// Example: Chunk (5, -3) means 5 chunks east, 3 chunks south of origin
```

**2. World Coordinates** (2D continuous, meters):
```cpp
struct WorldPosition {
    float x;  // Meters east from origin
    float y;  // Meters north from origin
};
// Example: (2560.0, -1536.0) means 2.56km east, 1.536km south
```

**3. Spherical Coordinates** (3D surface, lat/lon):
```cpp
struct SphericalCoordinate {
    float latitude;   // Degrees: -90 (South Pole) to +90 (North Pole)
    float longitude;  // Degrees: -180 to +180, wraps at ±180
};
// Example: (35.5, -118.2) is 35.5°N, 118.2°W
```

### Coordinate Transformations

#### Chunk ↔ World

```cpp
constexpr int kChunkSize = 512;        // Tiles per chunk
constexpr float kTileSize = 1.0f;      // Meters per tile
constexpr float kChunkWorldSize = kChunkSize * kTileSize;  // 512m

WorldPosition ChunkToWorld(ChunkCoordinate chunk) {
    return {
        chunk.x * kChunkWorldSize,
        chunk.y * kChunkWorldSize
    };
}

ChunkCoordinate WorldToChunk(WorldPosition world) {
    return {
        int32_t(floor(world.x / kChunkWorldSize)),
        int32_t(floor(world.y / kChunkWorldSize))
    };
}
```

#### World ↔ Spherical

This is the **critical transformation** that bridges 2D gameplay and 3D world model.

```cpp
class SphericalProjection {
public:
    SphericalProjection(float planetRadius, WorldPosition origin)
        : m_planetRadius(planetRadius)
        , m_originLatLon(origin)
    {}

    // Convert 2D world position to spherical lat/lon
    SphericalCoordinate WorldToSpherical(WorldPosition world) const {
        // Offset from origin
        float dx = world.x - m_originLatLon.x;
        float dy = world.y - m_originLatLon.y;

        // Convert meters to angular distance on sphere
        float latDelta = dy / m_planetRadius * kRadToDeg;
        float lonDelta = dx / (m_planetRadius * cos(m_originLat)) * kRadToDeg;

        SphericalCoordinate result{
            m_originLat + latDelta,
            m_originLon + lonDelta
        };

        // Normalize longitude to [-180, +180]
        while (result.longitude > 180.0f) result.longitude -= 360.0f;
        while (result.longitude < -180.0f) result.longitude += 360.0f;

        // Clamp latitude to [-90, +90]
        result.latitude = Clamp(result.latitude, -90.0f, 90.0f);

        return result;
    }

    // Convert spherical lat/lon back to 2D world position
    WorldPosition SphericalToWorld(SphericalCoordinate spherical) const {
        float latDelta = spherical.latitude - m_originLat;
        float lonDelta = spherical.longitude - m_originLon;

        // Handle longitude wrapping
        if (lonDelta > 180.0f) lonDelta -= 360.0f;
        if (lonDelta < -180.0f) lonDelta += 360.0f;

        float dx = lonDelta * kDegToRad * m_planetRadius * cos(m_originLat * kDegToRad);
        float dy = latDelta * kDegToRad * m_planetRadius;

        return {
            m_originLatLon.x + dx,
            m_originLatLon.y + dy
        };
    }

private:
    float m_planetRadius;     // Meters (e.g., 6,371,000 for Earth)
    float m_originLat;        // Landing site latitude
    float m_originLon;        // Landing site longitude
    WorldPosition m_originLatLon;

    static constexpr float kDegToRad = M_PI / 180.0f;
    static constexpr float kRadToDeg = 180.0f / M_PI;
};
```

**Cost**: ~10-20 floating-point operations per transformation

## Spherical World Queries

### 3D World Interface

The spherical world provides these queries:

```cpp
class SphericalWorld {
public:
    // Get tile at spherical coordinates
    const SphericalTile* GetTileAtPosition(SphericalCoordinate coord) const;

    // Get distance to nearest spherical tile boundary
    float CalculateDistanceToNearestBoundary(SphericalCoordinate coord) const;

    // Get neighboring tiles (for boundary blending)
    vector<const SphericalTile*> GetNeighborTiles(SphericalTileID tileID) const;

    // Sample biome with blending at boundaries
    BiomeInfluence SampleBiomeInfluence(SphericalCoordinate coord,
                                        float blendDistance = 500.0f) const;

    // Sample elevation (interpolated between tiles)
    float SampleElevation(SphericalCoordinate coord) const;

    // Sample climate data
    ClimateData SampleClimate(SphericalCoordinate coord) const;
};
```

### Spatial Indexing

The spherical world uses spatial indexing for fast queries:

```cpp
class SphericalTileIndex {
public:
    // Build index from generated world
    void Build(const vector<SphericalTile>& tiles);

    // Fast tile lookup by lat/lon
    const SphericalTile* FindTile(SphericalCoordinate coord) const;

private:
    // Quadtree or hierarchical grid for fast spatial queries
    unique_ptr<Quadtree<SphericalTile>> m_index;

    // Or simpler: uniform grid of tile IDs
    // Grid[lat_bucket][lon_bucket] = TileID
};
```

**Query Cost**: O(log N) for quadtree, O(1) for grid

**Target**: <0.01ms per query on modern CPU

## Chunk Biome Sampling

### Pure vs Boundary Classification

The **first and most important** step when sampling a chunk:

```cpp
struct ChunkSampleResult {
    bool isPure;                            // Can use fast path?
    BiomeType singleBiome;                  // If pure
    BiomeInfluence cornerBiomes[4];         // If boundary: NW, NE, SW, SE
    float cornerElevations[4];              // Always sampled
    ClimateData climate;                    // Average for chunk
    SphericalTileID primaryTile;            // Primary 3D tile
    Set<SphericalTileID> neighborTiles;     // For boundary chunks
};

ChunkSampleResult SampleChunkFromSphere(ChunkCoordinate chunkCoord,
                                        const SphericalWorld& world,
                                        const SphericalProjection& projection) {
    ChunkSampleResult result;

    // 1. Get chunk bounds in spherical coordinates
    WorldPosition chunkWorldPos = ChunkToWorld(chunkCoord);
    SphericalCoordinate center = projection.WorldToSpherical(
        chunkWorldPos + Vec2{kChunkWorldSize * 0.5f, kChunkWorldSize * 0.5f}
    );

    // 2. Query primary spherical tile
    const SphericalTile* primaryTile = world.GetTileAtPosition(center);
    result.primaryTile = primaryTile->id;
    result.climate = primaryTile->climate;

    // 3. Calculate distance to nearest boundary
    float distToBoundary = world.CalculateDistanceToNearestBoundary(center);

    // 4. Chunk diagonal radius (half diagonal of square)
    constexpr float kChunkRadius = sqrt(2.0f) * kChunkWorldSize * 0.5f;  // ~362m

    // 5. Classify as pure or boundary
    constexpr float kPureThreshold = 500.0f;  // 500m safety margin

    if (distToBoundary > kChunkRadius + kPureThreshold) {
        // PURE CHUNK - entire chunk is far from boundaries
        result.isPure = true;
        result.singleBiome = primaryTile->biome;

        // Still sample corners for elevation interpolation
        result.cornerElevations[0] = SampleCornerElevation(chunkCoord, Corner::NW);
        result.cornerElevations[1] = SampleCornerElevation(chunkCoord, Corner::NE);
        result.cornerElevations[2] = SampleCornerElevation(chunkCoord, Corner::SW);
        result.cornerElevations[3] = SampleCornerElevation(chunkCoord, Corner::SE);

        return result;  // Done! Only ~5 3D queries total
    }

    // BOUNDARY CHUNK - need to sample corners for biome blending
    result.isPure = false;

    SphericalCoordinate corners[4] = {
        projection.WorldToSpherical(chunkWorldPos),  // NW
        projection.WorldToSpherical(chunkWorldPos + Vec2{kChunkWorldSize, 0}),  // NE
        projection.WorldToSpherical(chunkWorldPos + Vec2{0, kChunkWorldSize}),  // SW
        projection.WorldToSpherical(chunkWorldPos + Vec2{kChunkWorldSize, kChunkWorldSize})  // SE
    };

    for (int i = 0; i < 4; i++) {
        result.cornerBiomes[i] = world.SampleBiomeInfluence(corners[i]);
        result.cornerElevations[i] = world.SampleElevation(corners[i]);
    }

    // Get neighbor tiles for reference
    result.neighborTiles = world.GetNeighborTiles(primaryTile->id);

    return result;  // ~8-10 3D queries for boundary chunk
}
```

### Biome Influence Sampling

When querying biome at a spherical coordinate:

```cpp
BiomeInfluence SphericalWorld::SampleBiomeInfluence(SphericalCoordinate coord,
                                                     float blendDistance) const {
    // 1. Find which spherical tile we're in
    const SphericalTile* tile = GetTileAtPosition(coord);

    // 2. Calculate distance to nearest boundary
    float distToBoundary = CalculateDistanceToNearestBoundary(coord);

    // 3. Fast path: far from boundary
    if (distToBoundary > blendDistance) {
        return BiomeInfluence{tile->biome, 1.0f};  // 100% single biome
    }

    // 4. Slow path: near boundary, need to blend
    vector<const SphericalTile*> neighbors = GetNeighborTiles(tile->id);

    // Find closest boundary and neighbor across it
    const SphericalTile* neighborTile = FindClosestNeighbor(coord, neighbors);
    float distToEdge = CalculateDistanceToEdge(coord, tile, neighborTile);

    // Blend based on distance
    float t = Clamp(distToEdge / blendDistance, 0.0f, 1.0f);  // 0 = edge, 1 = center

    BiomeInfluence result;
    result.Add(tile->biome, t);
    result.Add(neighborTile->biome, 1.0f - t);
    result.Normalize();

    return result;
}
```

**Fast path** (>95% of queries): 2 queries (tile + distance check)
**Slow path** (<5% of queries): 5-8 queries (neighbors + blending)

## Sector Grid Interpolation

For boundary chunks, we pre-compute a 32×32 sector grid:

```cpp
constexpr int kSectorGridSize = 32;
constexpr int kTilesPerSector = kChunkSize / kSectorGridSize;  // 512 / 32 = 16

void BuildSectorGrid(ChunkBiomeData& data,
                     const BiomeInfluence cornerBiomes[4]) {
    // Bilinear interpolation across chunk
    for (int sy = 0; sy < kSectorGridSize; sy++) {
        for (int sx = 0; sx < kSectorGridSize; sx++) {
            // Normalized coordinates [0, 1]
            float u = float(sx) / (kSectorGridSize - 1);
            float v = float(sy) / (kSectorGridSize - 1);

            // Bilinear interpolation
            BiomeInfluence top = Lerp(cornerBiomes[0], cornerBiomes[1], u);  // NW → NE
            BiomeInfluence bottom = Lerp(cornerBiomes[2], cornerBiomes[3], u);  // SW → SE
            data.sectorGrid[sy][sx] = Lerp(top, bottom, v);
        }
    }
}

BiomeInfluence Lerp(const BiomeInfluence& a, const BiomeInfluence& b, float t) {
    if (a.IsPure() && b.IsPure() && a.biome == b.biome) {
        return a;  // Both same pure biome - no blending needed
    }

    // Blend influences
    BiomeInfluence result;
    for (const auto& [biome, weight] : a.influences) {
        result.Add(biome, weight * (1.0f - t));
    }
    for (const auto& [biome, weight] : b.influences) {
        result.Add(biome, weight * t);
    }
    result.Normalize();

    return result;
}
```

**Cost**: 1024 interpolations (32×32), done once at chunk creation

**Result**: Tile biome lookups become O(1) sector array access

## Tile Biome Lookup

Using the sector grid:

```cpp
BiomeInfluence GetTileBiome(const ChunkBiomeData& data, int localX, int localY) {
    if (data.isPure) {
        return BiomeInfluence{data.singleBiome, 1.0f};  // Instant!
    }

    // Lookup in pre-computed sector grid
    int sectorX = localX / kTilesPerSector;  // 0-31
    int sectorY = localY / kTilesPerSector;  // 0-31

    return data.sectorGrid[sectorY][sectorX];
}
```

**Cost**:
- Pure chunk: O(1), ~1 CPU cycle
- Boundary chunk: O(1), array lookup, ~5 CPU cycles

**No 3D world queries per tile!**

## Elevation Sampling

Elevation is interpolated from chunk corners:

```cpp
float GetTileElevation(const ChunkBiomeData& data,
                       int localX, int localY,
                       RNG& rng) {
    // Bilinear interpolation from corners
    float u = float(localX) / (kChunkSize - 1);
    float v = float(localY) / (kChunkSize - 1);

    float top = Lerp(data.cornerElevations[0], data.cornerElevations[1], u);
    float bottom = Lerp(data.cornerElevations[2], data.cornerElevations[3], u);
    float baseElevation = Lerp(top, bottom, v);

    // Add micro-variation (deterministic noise)
    float microNoise = rng.Range(-0.5f, 0.5f);  // ±50cm

    return baseElevation + microNoise;
}
```

**Result**: Smooth elevation changes across chunk boundaries

## Procedural Detail Generation

Add fine detail using deterministic noise:

```cpp
TileData GenerateTileDetail(int localX, int localY,
                            const ChunkBiomeData& chunkData,
                            ChunkCoordinate chunkCoord,
                            uint64_t worldSeed) {
    TileData tile;

    // 1. Get biome (from sector grid or pure chunk)
    tile.biome = GetTileBiome(chunkData, localX, localY);

    // 2. Create deterministic RNG for this tile
    uint64_t tileSeed = HashCombine(worldSeed,
                                     chunkCoord.x,
                                     chunkCoord.y,
                                     localX,
                                     localY);
    RNG rng(tileSeed);

    // 3. Select ground cover based on biome
    tile.groundCover = SelectGroundCover(tile.biome, rng);

    // 4. Get elevation
    tile.elevation = GetTileElevation(chunkData, localX, localY, rng);

    // 5. Generate moisture (based on climate + noise)
    tile.moisture = GenerateMoisture(chunkData.climate.precipitation,
                                     tile.biome,
                                     rng);

    // 6. Add micro-features (rocks, flowers, grass tufts)
    tile.features = GenerateFeatures(tile.biome, tile.groundCover, rng);

    return tile;
}

GroundCoverType SelectGroundCover(const BiomeInfluence& biome, RNG& rng) {
    if (biome.IsPure()) {
        // Single biome - select from its ground cover types
        return SelectFromBiomeGroundCovers(biome.biome, rng);
    }

    // Blended biome - probabilistically select based on influences
    float roll = rng.Float();
    float cumulative = 0.0f;

    for (const auto& [biomeType, weight] : biome.influences) {
        cumulative += weight;
        if (roll < cumulative) {
            return SelectFromBiomeGroundCovers(biomeType, rng);
        }
    }

    // Fallback (should never reach here)
    return GroundCoverType::Grass;
}
```

**Key Property**: Same seed + same coordinates = same tile, always

## Two-Scale Ecotone System

From the game design docs, ecotones have two scales:

### Primary: 3D Spherical Boundaries (~500m)

Handled by `SampleBiomeInfluence`:

```cpp
constexpr float kPrimaryBlendDistance = 500.0f;  // From 3D world spec

BiomeInfluence biome = world.SampleBiomeInfluence(coord, kPrimaryBlendDistance);
// Returns blended percentages if within 500m of spherical boundary
```

### Secondary: 2D Micro-Variation (2-10km total)

Applied via procedural noise **after** primary sampling:

```cpp
BiomeInfluence ApplyMicroVariation(BiomeInfluence primaryBiome,
                                   WorldPosition worldPos,
                                   uint64_t worldSeed) {
    if (primaryBiome.IsPure()) {
        // Don't add variation to pure regions
        return primaryBiome;
    }

    // Only add variation near primary ecotones
    float distFromPrimary = CalculateDistanceFromPrimaryEcotone(worldPos);

    if (distFromPrimary > 10000.0f) {
        return primaryBiome;  // Too far from ecotone
    }

    // Generate noise-based variation
    float noise = PerlinNoise(worldPos.x / 1000.0f,
                              worldPos.y / 1000.0f,
                              worldSeed);

    // Shift blend percentages slightly
    BiomeInfluence varied = primaryBiome;
    for (auto& [biome, weight] : varied.influences) {
        weight += noise * 0.1f;  // ±10% variation
    }
    varied.Normalize();

    return varied;
}
```

**Result**:
- 500m core blend from 3D boundaries
- Extended to 2-10km via procedural noise
- Creates natural, organic-looking ecotones

## Performance Optimization

### Query Batching

When loading a chunk, batch 3D world queries:

```cpp
struct BatchedChunkSample {
    ChunkCoordinate coord;
    SphericalCoordinate center;
    SphericalCoordinate corners[4];
};

vector<ChunkSampleResult> SampleChunkBatch(
    const vector<ChunkCoordinate>& chunks,
    const SphericalWorld& world,
    const SphericalProjection& projection) {

    // 1. Transform all coordinates (cheap)
    vector<BatchedChunkSample> samples;
    for (const auto& chunk : chunks) {
        BatchedChunkSample sample;
        sample.coord = chunk;
        sample.center = ChunkCenterToSpherical(chunk, projection);
        for (int i = 0; i < 4; i++) {
            sample.corners[i] = ChunkCornerToSpherical(chunk, i, projection);
        }
        samples.push_back(sample);
    }

    // 2. Batch spatial queries (better cache coherency)
    vector<const SphericalTile*> tiles;
    for (const auto& sample : samples) {
        tiles.push_back(world.GetTileAtPosition(sample.center));
    }

    // 3. Build results
    vector<ChunkSampleResult> results;
    for (size_t i = 0; i < samples.size(); i++) {
        results.push_back(BuildChunkResult(samples[i], tiles[i], world));
    }

    return results;
}
```

### Caching Strategy

```cpp
class ChunkSampleCache {
public:
    ChunkSampleResult GetChunkSample(ChunkCoordinate coord) {
        if (auto cached = m_cache.Get(coord)) {
            return *cached;
        }

        ChunkSampleResult result = SampleChunkFromSphere(coord, m_world, m_projection);
        m_cache.Insert(coord, result);
        return result;
    }

private:
    LRUCache<ChunkCoordinate, ChunkSampleResult> m_cache;
    const SphericalWorld& m_world;
    const SphericalProjection& m_projection;

    static constexpr size_t kCacheSize = 256;  // Cache 256 chunk samples
};
```

**Benefit**: Regenerating evicted chunk is instant (cache hit)

### Parallel Chunk Generation

```cpp
void LoadChunksParallel(const vector<ChunkCoordinate>& coords) {
    ParallelFor(coords.begin(), coords.end(), [&](ChunkCoordinate coord) {
        ChunkSampleResult sample = SampleChunkFromSphere(coord, world, projection);

        // Build chunk from sample (CPU-bound, can parallelize)
        unique_ptr<Chunk> chunk = BuildChunkFromSample(coord, sample);

        // Insert into cache (requires lock)
        {
            ScopedLock lock(m_chunkCacheMutex);
            m_chunkCache.Insert(coord, std::move(chunk));
        }
    });
}
```

## Performance Targets

### 3D Sampling Cost

**Per Chunk**:
- Pure chunk: 5 queries × 0.01ms = **0.05ms**
- Boundary chunk: 8 queries × 0.01ms + 1024 interpolations × 0.0001ms = **0.18ms**

**Batch of 25 chunks** (5×5 grid):
- All pure: 25 × 0.05ms = **1.25ms**
- All boundary: 25 × 0.18ms = **4.5ms**
- Typical mix (20 pure, 5 boundary): 20 × 0.05ms + 5 × 0.18ms = **1.9ms**

**Acceptable**: 2ms for 25 chunks is fast enough for dynamic loading

### Tile Generation Cost

**Per Tile**:
- Biome lookup: 0.001ms (sector grid access)
- RNG + procedural: 0.002ms
- Total: **0.003ms per tile**

**Per Chunk** (262,144 tiles):
- All tiles: 262,144 × 0.003ms = **786ms**
- But we don't generate all at once!
- Typical viewport: 10,000 tiles × 0.003ms = **30ms** (acceptable)

### Memory Access Patterns

**Cache-Friendly**:
- Sector grid: 32×32 array, fits in L1 cache (4 KB)
- Corner elevations: 4 floats, fits in cache line
- Sequential tile access: good spatial locality

**Cache Misses**:
- Sparse modification lookups (HashMap): scattered access
- Mitigation: Keep modifications sorted by tile coordinate

## Determinism Guarantees

### Reproducibility

```cpp
// Same inputs → same output, ALWAYS
TileData tile1 = GenerateTileDetail(10, 20, chunkData, chunkCoord, seed);
TileData tile2 = GenerateTileDetail(10, 20, chunkData, chunkCoord, seed);

assert(tile1 == tile2);  // Must be true!
```

### Floating-Point Consistency

**Problem**: Float operations can differ across platforms/compilers

**Solution**: Use deterministic RNG and integer math where possible

```cpp
// Bad: platform-dependent
float noise = sin(x * 12.9898 + y * 78.233) * 43758.5453;

// Good: deterministic integer hash
uint32_t hash = Hash(seed, x, y);
float noise = float(hash) / float(UINT32_MAX);  // Exact on all platforms
```

### Multiplayer Sync

```cpp
// Server and client must generate identical tiles
TileData server = GenerateTile(x, y, chunkData, worldSeed);
TileData client = GenerateTile(x, y, chunkData, worldSeed);

assert(server == client);  // Critical for multiplayer!
```

## Edge Cases

### Polar Regions

Near poles, longitude lines converge:

```cpp
SphericalCoordinate WorldToSphericalNearPole(WorldPosition world, float latitude) {
    // Scale longitude by cos(latitude) to account for convergence
    float lonScale = cos(latitude * kDegToRad);

    SphericalCoordinate result;
    result.latitude = latitude + (world.y / m_planetRadius) * kRadToDeg;
    result.longitude = m_originLon + (world.x / (m_planetRadius * lonScale)) * kRadToDeg;

    return result;
}
```

### Date Line Crossing

Longitude wraps at ±180°:

```cpp
float NormalizeLongitude(float lon) {
    while (lon > 180.0f) lon -= 360.0f;
    while (lon < -180.0f) lon += 360.0f;
    return lon;
}

float LongitudeDelta(float lon1, float lon2) {
    float delta = lon2 - lon1;

    // Handle wrapping: shortest path
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;

    return delta;
}
```

### Chunk Boundaries Across Spherical Boundaries

**Scenario**: Chunk corners sample different spherical tiles

**Solution**: Bilinear interpolation handles this naturally

```cpp
// Chunk corners might sample different 3D tiles
cornerBiomes[0] = SampleBiomeInfluence(NW);  // Forest tile
cornerBiomes[1] = SampleBiomeInfluence(NE);  // Forest tile
cornerBiomes[2] = SampleBiomeInfluence(SW);  // Grassland tile
cornerBiomes[3] = SampleBiomeInfluence(SE);  // Grassland tile

// Interpolation creates smooth transition through chunk
// NW corner: 100% Forest
// Center: 50% Forest, 50% Grassland
// SE corner: 100% Grassland
```

**Result**: Seamless transitions across spherical boundaries

## Testing and Validation

### Unit Tests

```cpp
TEST(Sampling, DeterministicGeneration) {
    ChunkBiomeData data = /* ... */;
    ChunkCoordinate coord{5, 10};
    uint64_t seed = 12345;

    TileData tile1 = GenerateTileDetail(100, 200, data, coord, seed);
    TileData tile2 = GenerateTileDetail(100, 200, data, coord, seed);

    EXPECT_EQ(tile1, tile2);
}

TEST(Sampling, BiomeInterpolation) {
    BiomeInfluence corners[4] = {
        {BiomeType::Forest, 1.0f},
        {BiomeType::Forest, 1.0f},
        {BiomeType::Grassland, 1.0f},
        {BiomeType::Grassland, 1.0f}
    };

    ChunkBiomeData data;
    BuildSectorGrid(data, corners);

    // Check corners
    EXPECT_EQ(data.sectorGrid[0][0].biome, BiomeType::Forest);
    EXPECT_EQ(data.sectorGrid[31][31].biome, BiomeType::Grassland);

    // Check center (should be ~50/50)
    BiomeInfluence center = data.sectorGrid[15][15];
    EXPECT_NEAR(center.influences[BiomeType::Forest], 0.5f, 0.1f);
    EXPECT_NEAR(center.influences[BiomeType::Grassland], 0.5f, 0.1f);
}

TEST(Sampling, CoordinateRoundTrip) {
    WorldPosition world{25600.0f, -15360.0f};
    SphericalCoordinate spherical = projection.WorldToSpherical(world);
    WorldPosition roundTrip = projection.SphericalToWorld(spherical);

    EXPECT_NEAR(roundTrip.x, world.x, 0.1f);  // Within 10cm
    EXPECT_NEAR(roundTrip.y, world.y, 0.1f);
}
```

### Profiling

```cpp
void ProfileChunkSampling() {
    Timer timer;

    // Sample 100 chunks
    for (int i = 0; i < 100; i++) {
        ChunkCoordinate coord{i % 10, i / 10};
        ChunkSampleResult result = SampleChunkFromSphere(coord, world, projection);
    }

    float avgTime = timer.ElapsedMs() / 100.0f;
    LOG_INFO("Average chunk sample time: %.2f ms", avgTime);

    // Target: <0.2ms per chunk
    EXPECT_LT(avgTime, 0.2f);
}
```

## Related Documentation

**Game Design**:
- [World Generation README](../design/features/world-generation/README.md) - 3D world overview
- [World Data Model](../design/features/world-generation/data-model.md) - Spherical tile structure
- [Biomes](../design/features/world-generation/biomes.md) - Biome classification
- [Game View README](../design/features/game-view/README.md) - 2D rendering
- [Biome Influence System](../design/features/game-view/biome-influence-system.md) - Blending

**Technical**:
- [Chunk Management System](./chunk-management-system.md) - Chunk loading and caching

## Revision History

- 2025-10-26: Initial 3D to 2D sampling technical specification
