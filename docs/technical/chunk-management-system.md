# Chunk Management System

Created: 2025-10-26
Status: Technical Design

## Overview

The chunk management system provides an infinite 2D world by dividing the game world into fixed-size chunks that are loaded, cached, and evicted on demand. Chunks sample data from the 3D spherical world and support sparse storage of player modifications while maintaining deterministic procedural generation.

**Key Design Goals**:
- **Infinite world**: Load/unload chunks as player explores
- **Deterministic**: Same seed produces identical unmodified chunks
- **Multiplayer compatible**: Fixed chunk size, synchronized state
- **Terraform support**: Sparse storage for player modifications
- **Performance**: Minimize 3D sampling, cache aggressively, fast tile access

## Core Concepts

### What is a Chunk?

A **chunk** is a fixed-size square region of the 2D game world that serves as:
- **Rendering unit**: Visual tiles within viewport
- **Simulation unit**: AI, economy, and game logic granularity
- **Storage unit**: Save game modifications at chunk level
- **Loading unit**: Stream world data on demand

**Chunk Size**: **512 × 512 tiles** (512m × 512m)

**Rationale**:
- Large enough to contain meaningful gameplay (settlements, resources)
- Covers 2-4 screens at 5K resolution (comfortable for player)
- Small enough to load/unload efficiently
- Takes ~1.7 minutes to cross at walking speed (5 m/s)
- Power of 2 for efficient addressing and bitwise operations
- Tile size: 1 meter (precise enough for building and character movement)

### Chunk vs Spherical Tiles

**Spherical Tile** (3D world generation):
- ~5km diameter (at medium resolution)
- Stores single definitive biome
- Contains climate, elevation, geological data
- Source of truth for world properties

**Chunk** (2D gameplay):
- 512m square
- Samples data from spherical tiles
- One spherical tile ≈ 100 chunks (10×10 grid)
- Chunk is a *sampling window* into 3D world, not 1:1 mapping

**Critical Insight**: Chunks don't store world data - they *sample and cache* data from the 3D spherical world.

## Chunk Architecture

### Chunk Structure

```cpp
struct ChunkCoordinate {
    int32_t x;  // Chunk grid coordinates (not world coordinates)
    int32_t y;

    // Convert to world position
    WorldPosition ToWorldPos() const {
        return {x * kChunkSize * kTileSize, y * kChunkSize * kTileSize};
    }

    // Convert to spherical coordinates for 3D sampling
    SphericalCoordinate ToSpherical() const;
};

struct Chunk {
    ChunkCoordinate coordinate;

    // Biome data sampled from 3D world (cheap, always present)
    ChunkBiomeData biomeData;

    // Sparse storage: only modified tiles
    HashMap<TileLocalCoord, ModifiedTileData> modifications;

    // Cache state
    bool isDirty = false;           // Has unsaved modifications?
    Timestamp lastAccessed;         // For LRU eviction

    // Simulation state (optional, not always loaded)
    unique_ptr<ChunkSimulationData> simulationData;
};

// Local tile coordinates within chunk (0-511, 0-511)
struct TileLocalCoord {
    uint16_t x;  // 0-511
    uint16_t y;  // 0-511

    bool operator==(const TileLocalCoord& other) const {
        return x == other.x && y == other.y;
    }
};

// Hash function for HashMap
namespace std {
    template<>
    struct hash<TileLocalCoord> {
        size_t operator()(const TileLocalCoord& coord) const {
            return (size_t(coord.x) << 16) | coord.y;
        }
    };
}
```

### Chunk Biome Data

This is the **sampled data from the 3D world**, stored per-chunk (not per-tile).

```cpp
struct ChunkBiomeData {
    // Pure chunk optimization
    bool isPure;                    // Entire chunk >1km from spherical boundaries?
    BiomeType singleBiome;          // If pure, all tiles use this biome

    // Boundary chunk interpolation grid (only if !isPure)
    static constexpr int kSectorGridSize = 32;
    BiomeInfluence sectorGrid[kSectorGridSize][kSectorGridSize];  // 32×32 = 1024 sectors

    // Base elevation from 3D world (interpolated within chunk)
    float cornerElevations[4];      // NW, NE, SW, SE corners

    // Climate data (sampled from 3D world)
    float temperature;              // Annual mean
    float precipitation;            // Annual total
    WindData wind;                  // Prevailing wind

    // Metadata
    SphericalTileID primaryTile;    // Which 3D tile we're primarily in
    Set<SphericalTileID> neighbors; // For boundary chunks, neighboring 3D tiles
};

struct BiomeInfluence {
    BiomeType biome;                // If pure chunk, single biome
    HashMap<BiomeType, float> influences;  // If boundary, blended percentages

    bool IsPure() const { return influences.empty(); }
};
```

### Modified Tile Data

**Sparse storage**: Only store deltas from procedural generation.

```cpp
struct ModifiedTileData {
    // Only store what player changed
    optional<GroundCoverType> groundCover;      // Player paved/terraformed
    optional<int16_t> elevationDelta;           // +/- cm from procedural base
    optional<BiomeInfluence> forcedBiome;       // Player changed biome (rare)
    optional<uint8_t> moisture;                 // Player irrigation/drainage

    // Flags
    bool isFoundation = false;                  // Building foundation placed
    bool isRoad = false;                        // Road constructed

    // Timestamp
    uint64_t modifiedTick;                      // Game tick when modified
};
```

**Memory**: ~20-40 bytes per modified tile

## Chunk Classification

### Pure Chunks vs Boundary Chunks

When sampling a chunk from the 3D world, we classify it based on proximity to spherical tile boundaries:

**Pure Chunk** (~64% of chunks):
- Entire chunk is >500m from any spherical tile boundary
- All 262,144 tiles have identical biome
- Massive optimization: single biome for entire chunk
- Memory: ~200 bytes (just ChunkBiomeData)
- Fast generation, fast rendering

**Boundary Chunk** (~36% of chunks):
- Chunk crosses or is near spherical tile boundary
- Tiles have varying biome percentages
- Requires interpolation grid
- Memory: ~200 bytes + (32×32 × sizeof(BiomeInfluence)) ≈ 2-4 KB
- More expensive generation

**Classification Algorithm**:

```cpp
ChunkBiomeData Sample3DWorld(ChunkCoordinate coord, const SphericalWorld& world) {
    ChunkBiomeData data;

    // Sample center of chunk
    vec2 centerLatLon = ChunkToSpherical(coord.Center());
    SphericalTile* primaryTile = world.GetTileAtPosition(centerLatLon);

    // Calculate distance to nearest spherical tile boundary
    float distToBoundary = world.CalculateDistanceToNearestBoundary(centerLatLon);

    // Chunk radius: half diagonal = sqrt(512^2 + 512^2) / 2 ≈ 362m
    constexpr float kChunkRadius = 362.0f;
    constexpr float kBlendDistance = 500.0f;  // From 3D world spec

    // Is entire chunk >500m from any boundary?
    if (distToBoundary > kChunkRadius + 500.0f) {
        // PURE CHUNK
        data.isPure = true;
        data.singleBiome = primaryTile->biome;
        data.primaryTile = primaryTile->id;
        data.temperature = primaryTile->temperature;
        data.precipitation = primaryTile->precipitation;

        // Sample corner elevations for interpolation
        data.cornerElevations[0] = SampleElevation(coord.NorthWest());
        data.cornerElevations[1] = SampleElevation(coord.NorthEast());
        data.cornerElevations[2] = SampleElevation(coord.SouthWest());
        data.cornerElevations[3] = SampleElevation(coord.SouthEast());

        return data;  // Done! Very cheap.
    }

    // BOUNDARY CHUNK - need to sample multiple points
    data.isPure = false;
    data.primaryTile = primaryTile->id;

    // Sample 4 corners for biome interpolation
    BiomeInfluence cornerBiomes[4] = {
        SampleBiomeInfluence(coord.NorthWest(), world),
        SampleBiomeInfluence(coord.NorthEast(), world),
        SampleBiomeInfluence(coord.SouthWest(), world),
        SampleBiomeInfluence(coord.SouthEast(), world)
    };

    // Build 32×32 sector grid via bilinear interpolation
    for (int sy = 0; sy < kSectorGridSize; sy++) {
        for (int sx = 0; sx < kSectorGridSize; sx++) {
            float u = float(sx) / (kSectorGridSize - 1);
            float v = float(sy) / (kSectorGridSize - 1);
            data.sectorGrid[sy][sx] = BilinearInterpolate(cornerBiomes, u, v);
        }
    }

    // Sample elevations and climate
    data.cornerElevations[0] = SampleElevation(coord.NorthWest());
    data.cornerElevations[1] = SampleElevation(coord.NorthEast());
    data.cornerElevations[2] = SampleElevation(coord.SouthWest());
    data.cornerElevations[3] = SampleElevation(coord.SouthEast());

    data.temperature = primaryTile->temperature;
    data.precipitation = primaryTile->precipitation;

    // Store neighboring tiles for reference
    data.neighbors = world.GetNeighborTiles(primaryTile->id);

    return data;
}
```

**Cost Analysis**:
- Pure chunk: 1 spherical lookup + 4 elevation samples ≈ 5 3D queries
- Boundary chunk: 4 biome samples + 4 elevation samples ≈ 8 3D queries
- **Never** 262,144 queries per chunk!

## Tile Access

### Generating Tile Data

Tiles are generated procedurally on-demand, blending:
1. Chunk-level biome data (from 3D world)
2. Deterministic noise (from seed + coordinates)
3. Player modifications (if any)

```cpp
TileData Chunk::GetTile(int localX, int localY) const {
    // Check for player modifications first
    TileLocalCoord coord{localX, localY};
    if (auto mod = modifications.find(coord); mod != modifications.end()) {
        return ApplyModifications(GenerateBaseTile(localX, localY), mod->second);
    }

    // Generate procedurally (deterministic)
    return GenerateBaseTile(localX, localY);
}

TileData Chunk::GenerateBaseTile(int localX, int localY) const {
    TileData tile;

    // 1. Get biome for this tile
    if (biomeData.isPure) {
        tile.biome = {biomeData.singleBiome, 1.0f};  // 100% single biome
    } else {
        // Lookup in pre-computed sector grid
        int sectorX = localX / 16;  // 512 tiles / 32 sectors = 16 tiles/sector
        int sectorY = localY / 16;
        tile.biome = biomeData.sectorGrid[sectorY][sectorX];
    }

    // 2. Generate deterministic properties from seed
    uint64_t tileSeed = HashCombine(
        worldSeed,
        coordinate.x,
        coordinate.y,
        localX,
        localY
    );
    RNG rng(tileSeed);

    // 3. Select ground cover based on biome
    tile.groundCover = SelectGroundCover(tile.biome, rng);

    // 4. Calculate base elevation (bilinear from corners)
    float u = float(localX) / (kChunkSize - 1);
    float v = float(localY) / (kChunkSize - 1);
    tile.baseElevation = BilinearInterpolate(biomeData.cornerElevations, u, v);

    // 5. Add micro-elevation noise (deterministic)
    tile.microElevation = rng.Range(-0.5f, 0.5f);  // ±50cm variation

    // 6. Generate moisture based on biome + precipitation
    tile.moisture = GenerateMoisture(tile.biome, biomeData.precipitation, rng);

    return tile;
}

TileData Chunk::ApplyModifications(TileData base, const ModifiedTileData& mod) const {
    if (mod.groundCover) base.groundCover = *mod.groundCover;
    if (mod.elevationDelta) base.baseElevation += *mod.elevationDelta * 0.01f;  // cm to m
    if (mod.forcedBiome) base.biome = *mod.forcedBiome;
    if (mod.moisture) base.moisture = *mod.moisture;

    base.isFoundation = mod.isFoundation;
    base.isRoad = mod.isRoad;

    return base;
}
```

### Modifying Tiles

When player terraforms, we store only the delta:

```cpp
void Chunk::ModifyTile(int localX, int localY, const TileModification& change) {
    TileLocalCoord coord{localX, localY};

    // Get or create modification entry
    ModifiedTileData& mod = modifications[coord];

    // Apply changes
    if (change.newGroundCover) mod.groundCover = *change.newGroundCover;
    if (change.elevationChange) {
        int16_t current = mod.elevationDelta.value_or(0);
        mod.elevationDelta = current + change.elevationChange;
    }

    mod.modifiedTick = currentTick;
    isDirty = true;  // Mark chunk for serialization
}
```

## Chunk Loading and Caching

### Chunk Cache Manager

```cpp
class ChunkCache {
public:
    Chunk* GetChunk(ChunkCoordinate coord);
    void UnloadChunk(ChunkCoordinate coord);
    void SaveDirtyChunks();

private:
    HashMap<ChunkCoordinate, unique_ptr<Chunk>> loadedChunks;
    LRUCache<ChunkCoordinate> evictionQueue;

    static constexpr int kMaxLoadedChunks = 100;  // ~20-200 MB depending on mods
};

Chunk* ChunkCache::GetChunk(ChunkCoordinate coord) {
    // Check if already loaded
    if (auto it = loadedChunks.find(coord); it != loadedChunks.end()) {
        it->second->lastAccessed = CurrentTime();
        return it->second.get();
    }

    // Need to load chunk
    auto chunk = LoadChunk(coord);

    // Evict if over capacity
    if (loadedChunks.size() >= kMaxLoadedChunks) {
        EvictLRU();
    }

    loadedChunks[coord] = std::move(chunk);
    return loadedChunks[coord].get();
}

unique_ptr<Chunk> ChunkCache::LoadChunk(ChunkCoordinate coord) {
    auto chunk = make_unique<Chunk>();
    chunk->coordinate = coord;

    // 1. Sample 3D world for biome data (always needed)
    chunk->biomeData = Sample3DWorld(coord, sphericalWorld);

    // 2. Load modifications from save game (if any)
    chunk->modifications = saveGame.LoadChunkModifications(coord);
    chunk->isDirty = false;

    // 3. Don't load simulation data yet (lazy)
    chunk->simulationData = nullptr;

    return chunk;
}

void ChunkCache::EvictLRU() {
    ChunkCoordinate oldest = evictionQueue.GetLRU();

    auto it = loadedChunks.find(oldest);
    if (it == loadedChunks.end()) return;

    // Save if dirty
    if (it->second->isDirty) {
        saveGame.SaveChunkModifications(oldest, it->second->modifications);
        it->second->isDirty = false;
    }

    // Evict
    loadedChunks.erase(it);
}
```

### Loading Strategy

**Player-Centered Loading**:

```cpp
void LoadChunksAroundPlayer(WorldPosition playerPos) {
    ChunkCoordinate playerChunk = WorldPosToChunkCoord(playerPos);

    // Load 5×5 grid (2 chunks in each direction)
    constexpr int kLoadRadius = 2;

    for (int dy = -kLoadRadius; dy <= kLoadRadius; dy++) {
        for (int dx = -kLoadRadius; dx <= kLoadRadius; dx++) {
            ChunkCoordinate coord{playerChunk.x + dx, playerChunk.y + dy};
            chunkCache.GetChunk(coord);  // Load or retrieve from cache
        }
    }

    // Mark distant chunks for eviction (>3-4 chunks away)
    constexpr int kUnloadRadius = 4;
    for (auto& [coord, chunk] : chunkCache.loadedChunks) {
        int dx = abs(coord.x - playerChunk.x);
        int dy = abs(coord.y - playerChunk.y);
        if (dx > kUnloadRadius || dy > kUnloadRadius) {
            chunkCache.UnloadChunk(coord);
        }
    }
}
```

**Predictive Loading**:

```cpp
void PredictiveLoad(WorldPosition playerPos, Vec2 velocity) {
    if (velocity.Length() < 1.0f) return;  // Not moving

    ChunkCoordinate playerChunk = WorldPosToChunkCoord(playerPos);
    Vec2 direction = velocity.Normalized();

    // Preload chunks in movement direction
    for (int distance = 3; distance <= 5; distance++) {
        ChunkCoordinate ahead{
            playerChunk.x + int(direction.x * distance),
            playerChunk.y + int(direction.y * distance)
        };
        chunkCache.PreloadChunk(ahead);  // Low priority background load
    }
}
```

## Serialization

### Save Game Format

Only modified chunks are saved. Pristine chunks regenerate from seed.

```cpp
struct ChunkSaveData {
    ChunkCoordinate coordinate;
    vector<TileModification> modifications;
};

struct SaveGame {
    uint64_t worldSeed;
    vector<ChunkSaveData> modifiedChunks;
};

// Binary serialization
void SerializeChunk(BinaryWriter& writer, const Chunk& chunk) {
    if (chunk.modifications.empty()) return;  // Don't save pristine chunks

    writer.Write(chunk.coordinate.x);
    writer.Write(chunk.coordinate.y);
    writer.Write(uint32_t(chunk.modifications.size()));

    for (const auto& [coord, mod] : chunk.modifications) {
        writer.Write(coord.x);
        writer.Write(coord.y);
        SerializeModification(writer, mod);
    }
}

void SerializeModification(BinaryWriter& writer, const ModifiedTileData& mod) {
    uint8_t flags = 0;
    if (mod.groundCover) flags |= 0x01;
    if (mod.elevationDelta) flags |= 0x02;
    if (mod.forcedBiome) flags |= 0x04;
    if (mod.moisture) flags |= 0x08;
    if (mod.isFoundation) flags |= 0x10;
    if (mod.isRoad) flags |= 0x20;

    writer.Write(flags);

    if (mod.groundCover) writer.Write(uint8_t(*mod.groundCover));
    if (mod.elevationDelta) writer.Write(*mod.elevationDelta);
    if (mod.forcedBiome) SerializeBiomeInfluence(writer, *mod.forcedBiome);
    if (mod.moisture) writer.Write(*mod.moisture);

    writer.Write(mod.modifiedTick);
}
```

**Typical Save File Size**:
- Pristine world: ~1 KB (just seed)
- Lightly modified (1000 terraformed tiles): ~30 KB
- Heavily terraformed (100,000 tiles): ~3 MB
- Multiplayer world (millions of tiles): ~100-500 MB

## Multiplayer Synchronization

### Chunk Ownership

In multiplayer, chunks have authority:

```cpp
struct Chunk {
    // ... existing fields ...

    // Multiplayer
    PlayerID authorityPlayer;       // Which player has write authority
    uint64_t stateVersion;          // Increment on each modification
};
```

**Authority Rules**:
- **Unmodified chunks**: No authority (anyone can sample from 3D world)
- **Modified chunks**: Owned by first player to modify
- **Contested chunks**: Server arbitrates conflicts

### Synchronization Strategy

```cpp
void SynchronizeChunk(ChunkCoordinate coord) {
    Chunk* localChunk = chunkCache.GetChunk(coord);

    if (localChunk->isDirty) {
        // Send modifications to server
        ChunkModificationPacket packet{
            coord,
            localChunk->stateVersion + 1,
            localChunk->modifications
        };
        network.Send(packet);
    }

    // Receive updates from server
    if (auto update = network.ReceiveChunkUpdate(coord)) {
        if (update.stateVersion > localChunk->stateVersion) {
            ApplyUpdate(localChunk, update);
        }
    }
}
```

## Performance Considerations

### Memory Budget

**Per Chunk**:
- Pure chunk (pristine): ~200 bytes
- Pure chunk (modified): ~200 bytes + (num_modified × 30 bytes)
- Boundary chunk (pristine): ~4 KB
- Boundary chunk (modified): ~4 KB + (num_modified × 30 bytes)

**Typical Load** (25 chunks in 5×5 grid):
- All pure, pristine: 25 × 200 bytes = **5 KB**
- All pure, 10% modified (2600 tiles): 5 KB + 2600 × 30 = **83 KB**
- All boundary, pristine: 25 × 4 KB = **100 KB**
- All boundary, 10% modified: 100 KB + 78 KB = **178 KB**

**Worst Case** (100 loaded chunks, all heavily modified):
- 100 × 4 KB + 100,000 tiles × 30 bytes = **3.4 MB**

**Very reasonable** for modern systems.

### CPU Cost

**Chunk Loading**:
- Pure chunk: ~0.1 ms (5 3D queries + setup)
- Boundary chunk: ~0.5 ms (8 3D queries + 1024 interpolations)

**Tile Generation** (per tile):
- Pure chunk tile: ~0.001 ms (hash + RNG + selection)
- Boundary chunk tile: ~0.002 ms (sector lookup + hash + RNG)

**Rendering** (262k tiles per chunk):
- Don't render all at once! Only visible tiles.
- Typical viewport: 10k-20k tiles visible

### Optimization Opportunities

**1. Chunk-Level Batching**:
- Pure chunks: single render batch (all same biome)
- Boundary chunks: batch by sector (32×32 sectors)

**2. Tile Entity Pooling**:
- Reuse tile data structures (don't allocate per tile)
- Memory arenas for temporary generation

**3. Background Loading**:
- Load chunks on worker threads
- Serialize/deserialize on background threads
- Only final commit on main thread

**4. Compression**:
- Compress chunk modifications in save files
- Delta encoding for elevation changes
- RLE for repeated ground cover changes

## Edge Cases

### World Wrapping

Since the world is spherical:

```cpp
ChunkCoordinate NormalizeChunkCoord(ChunkCoordinate coord, int planetCircumference) {
    // East-West wrapping
    while (coord.x < 0) coord.x += planetCircumference;
    while (coord.x >= planetCircumference) coord.x -= planetCircumference;

    // North-South clamping (poles)
    coord.y = Clamp(coord.y, -polarLimit, +polarLimit);

    return coord;
}
```

### Poles

Near poles, chunks may be smaller in world space (latitude lines converge). Handle in coordinate transformation:

```cpp
SphericalCoordinate ChunkToSpherical(ChunkCoordinate coord) {
    // Apply polar scaling
    float latitude = ChunkYToLatitude(coord.y);
    float longitudeScale = cos(latitude);  // Smaller chunks near poles

    return {latitude, coord.x * longitudeScale};
}
```

### Small Islands

Player lands on 5km island:

```cpp
// Chunk (0, 0) is land
// Chunks (±1, ±1) might be ocean
// Each chunk samples independently - seamless boundaries
```

## Related Documentation

**Game Design**:
- [World Generation README](../design/features/world-generation/README.md) - 3D world overview
- [World Data Model](../design/features/world-generation/data-model.md) - Spherical tile structure
- [Game View README](../design/features/game-view/README.md) - 2D rendering system
- [Biome Influence System](../design/features/game-view/biome-influence-system.md) - Biome blending

**Technical**:
- [3D to 2D Sampling](./3d-to-2d-sampling.md) - Spherical world sampling algorithms
- [Multiplayer Architecture](./multiplayer-architecture.md) - Network synchronization

## Revision History

- 2025-10-26: Initial chunk management system design
