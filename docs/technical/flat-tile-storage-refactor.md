# Flat Tile Storage Refactor

Created: 2025-12-11
Status: Proposed

## Problem Statement

The current chunk system uses a **layered/lazy approach** where tile properties are computed on-demand from multiple sources:

```
getTile(x, y) → {
    biome:       from ChunkSampleResult (sampled at chunk load)
    elevation:   from ChunkSampleResult (interpolated)
    groundCover: from selectGroundCover() (computed on-demand via noise)
    moisture:    from hash (computed on-demand)
}
```

This creates **no single source of truth** for tile state. Multiple systems must independently query and interpret this layered data, leading to:

1. **Misalignment bugs** — Systems can disagree about what a tile "is"
2. **Hidden state** — Ponds exist in data but not in rendering (pure chunk optimization bug)
3. **Difficult debugging** — Can't simply inspect "what is this tile?"
4. **Scattered logic** — Surface selection duplicated across systems
5. **Flora on water** — PlacementExecutor can't reliably check surface type because it's computed lazily

### Concrete Bug Examples

**Pure Chunk Rendering Bug:**
The "pure chunk" rendering optimization renders entire chunks as single-color quads when all corners have the same biome. But ponds are generated at the TILE level via noise in `selectGroundCover()`, so:

- **Data**: Pond tiles exist (noise > threshold)
- **Rendering**: Single green quad (optimization hides per-tile variation)
- **Entity Placement**: Must independently check the same noise

**Flora on Water Bug:**
Grass and other flora entities spawn on water tiles because:
- PlacementExecutor runs AFTER chunk creation
- Surface type is computed lazily, not stored
- PlacementExecutor would need to recompute the same noise to check surface
- Any mismatch in noise parameters = flora on water

Both bug classes are **structurally impossible** with flat tile storage.

## Terminology Clarification

The current codebase uses "GroundCover" incorrectly. In ecology and game design:

- **Surface** = The underlying material (soil, dirt, sand, rock, water, snow)
- **Ground Cover** = Plants that cover the ground (grass, shrubs, moss, flowers)

You plant ground cover **on** a surface. They're different layers.

### Correct Conceptual Model

```
┌─────────────────────────────────────────────────────────────┐
│  BIOME (large scale, from 3D world sampling)                │
│  "This region is Grassland" or "70% Grassland, 30% Forest"  │
│  - Sampled at chunk corners from spherical world            │
│  - Interpolated via sector grid for boundary chunks         │
└─────────────────────────────────────────────────────────────┘
                              ↓
                    influences surface selection
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  SURFACE (per tile, stored in flat array) ← THIS REFACTOR  │
│  What IS this tile? Soil, Dirt, Sand, Rock, Water, Snow     │
│  - Computed ONCE at chunk generation                        │
│  - Single source of truth for all systems                   │
│  - Affects walkability, buildability, flora placement       │
└─────────────────────────────────────────────────────────────┘
                              ↓
                    constrains what can grow
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  FLORA / GROUND COVER (entities placed on surface)          │
│  Grass blades, flowers, moss, shrubs, trees                 │
│  - Placed by PlacementExecutor AFTER surface is known       │
│  - Requires compatible surface (grass needs Soil, not Water)│
│  - Visual variety via Lua procedural generation             │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight**: Surface must be computed FIRST, stored definitively, THEN flora placement can reliably check "is this tile water?" and skip it.

## Proposed Solution

Replace the layered system with a **flat tile array** per chunk:

```cpp
struct Chunk {
    ChunkCoordinate coordinate;
    std::array<TileData, kChunkSize * kChunkSize> tiles;  // 512×512 = 262,144 tiles

    // Sparse storage for player modifications (unchanged)
    HashMap<TileLocalCoord, ModifiedTileData> modifications;
};

struct TileData {
    Surface surface;      // THE definitive surface type (1 byte) — renamed from GroundCover
    Biome biome;          // Primary biome (1 byte)
    uint16_t elevation;   // Fixed-point elevation in cm (2 bytes)
    uint8_t moisture;     // 0-255 normalized (1 byte)
    uint8_t flags;        // Reserved for future use (1 byte)
    // Total: 6 bytes/tile (can pack to 4 if needed)
};

enum class Surface : uint8_t {
    Soil,       // Vegetated ground — plants can grow here (renamed from "Grass")
    Dirt,       // Bare earth — exposed, no vegetation
    Sand,
    Rock,
    Water,
    Snow
};
```

### Key Changes

1. **Rename `GroundCover` → `Surface`**
   - Reflects actual meaning (terrain material, not plants)
   - Clears up conceptual confusion

2. **Tile generation moves to chunk load time**
   - `Chunk::generate()` runs once, populates entire `tiles` array
   - All noise sampling (ponds, dirt patches, variation) happens here
   - Result is stored, not recomputed

3. **Tile queries become simple array access**
   ```cpp
   TileData Chunk::getTile(uint16_t localX, uint16_t localY) const {
       return tiles[localY * kChunkSize + localX];
   }
   ```

4. **Single source of truth**
   - Renderer reads `tiles[]` — sees ponds
   - PlacementExecutor reads `tiles[]` — avoids water (fixes flora-on-water bug!)
   - VisionSystem reads `tiles[]` — correct LOS
   - AIDecisionSystem reads `tiles[]` — accurate pathfinding

5. **Correct ordering: Surface THEN Flora**
   - Chunk generates surface array
   - PlacementExecutor queries definitive surface
   - Flora only placed on compatible surfaces

## How Biome Blending Fits In

Biome blending and surface selection are related but distinct:

### Biome Blending (Unchanged)
- **Source**: 3D spherical world sampling
- **Stored in**: `ChunkSampleResult` (corner biomes + sector grid)
- **Purpose**: Smooth transitions at biome boundaries
- **Data**: `BiomeWeights` — percentages like "70% Grassland, 30% Forest"

### Surface Selection (This Refactor)
- **Source**: Biome + procedural noise
- **Stored in**: `tiles[]` array (new)
- **Purpose**: Definitive terrain material per tile
- **Data**: `Surface` enum — single value, not percentages

### Single-Pass Generation

Surface selection happens in ONE pass at chunk generation, using biome as input:

```cpp
void Chunk::generate() {
    for (uint16_t y = 0; y < kChunkSize; ++y) {
        for (uint16_t x = 0; x < kChunkSize; ++x) {
            TileData& tile = tiles[y * kChunkSize + x];

            // Step 1: Get biome from 3D sampling (already computed)
            BiomeWeights biomeWeights = m_biomeData.getTileBiome(x, y);
            tile.biome = biomeWeights.primary();

            // Step 2: Select surface based on biome + noise
            tile.surface = selectSurface(tile.biome, x, y);

            // Step 3: Interpolate elevation
            tile.elevation = computeElevation(x, y);

            // Step 4: Generate moisture
            tile.moisture = computeMoisture(x, y);
        }
    }
}

Surface Chunk::selectSurface(Biome biome, uint16_t x, uint16_t y) {
    // Ocean biome is always water
    if (biome == Biome::Ocean) {
        return Surface::Water;
    }

    float worldX = static_cast<float>(m_coord.x * kChunkSize + x);
    float worldY = static_cast<float>(m_coord.y * kChunkSize + y);

    // Ponds in Grassland/Forest
    if (biome == Biome::Grassland || biome == Biome::Forest) {
        float waterNoise = fractalNoise(worldX * 0.08F, worldY * 0.08F,
                                         m_worldSeed + 100000, 2, 0.5F);
        if (waterNoise > 0.82F) {
            return Surface::Water;
        }
    }

    // Biome-specific surface with variation noise
    // ... rest of selection logic ...
}
```

## Memory Analysis

### Current System (Layered)

From `chunk-management-system.md`:
```
Per Chunk:
- Pure chunk (pristine): ~200 bytes
- Boundary chunk (pristine): ~4 KB
- Typical Load (25 chunks): 5 KB to 178 KB
```

### Proposed System (Flat Array)

```
Per Chunk:
- TileData: 6 bytes/tile × 262,144 tiles = 1.57 MB
- With modifications: +30 bytes per modified tile

Typical Load (25 chunks): 39 MB
Maximum Load (100 chunks): 157 MB
```

### Memory Comparison

| Scenario | Current | Proposed | Increase |
|----------|---------|----------|----------|
| 25 chunks (pristine) | ~100 KB | ~39 MB | 390× |
| 100 chunks (pristine) | ~400 KB | ~157 MB | 392× |

### Is This Acceptable?

**Yes.** Modern games routinely use gigabytes of RAM. 157 MB for the worst-case chunk load is:
- Less than a single 4K texture
- ~1% of a typical gaming PC's RAM (16 GB)
- Negligible compared to entity data, audio, etc.

The original design optimized for memory that doesn't need optimization.

## Performance Analysis

### Generation Cost (One-Time at Chunk Load)

**Current**: Deferred — tiles generated on first access
**Proposed**: Upfront — all 262,144 tiles generated at load

```
Per tile:
- Biome lookup: ~5 cycles (array access)
- Noise sampling: ~200 cycles (fractalNoise with 2 octaves)
- Surface selection: ~50 cycles
- Total: ~255 cycles/tile

Per chunk:
- 262,144 tiles × 255 cycles = ~67M cycles
- At 3 GHz: ~22ms per chunk
```

This is acceptable because:
1. Chunk loading already happens on background threads
2. 22ms is faster than disk I/O for saved chunks
3. Only ~5 chunks load when moving (not 25 at once)

### Query Cost

**Current**: O(1) but with noise computation
**Proposed**: O(1) pure array access

```cpp
// Current: ~50-200 cycles (noise + conditionals)
TileData Chunk::getTile(x, y) const {
    TileData tile;
    tile.biome = m_biomeData.getTileBiome(x, y);  // sector lookup
    tile.groundCover = selectGroundCover(...);    // NOISE COMPUTATION
    // ...
    return tile;
}

// Proposed: ~5 cycles (array access)
TileData Chunk::getTile(x, y) const {
    return tiles[y * kChunkSize + x];
}
```

**Query speedup: 10-40×**

## Implementation Plan

### Phase 0: Rename (Prerequisite)

1. **Rename `GroundCover` → `Surface`**
   - Update enum and all references
   - Rename `selectGroundCover()` → `selectSurface()`
   - Update comments and documentation

### Phase 1: Data Structure Changes

2. **Add `tiles` array to Chunk**
   - `std::array<TileData, kChunkSize * kChunkSize> tiles`
   - Keep existing lazy system working in parallel initially

3. **Create `Chunk::generate()` method**
   - Move `selectSurface()` logic here
   - Move `generateTile()` logic here
   - Populate entire array at construction

4. **Update `TileData` struct**
   - Use `Surface` enum (renamed)
   - Add packed representation (6 bytes)
   - Ensure deterministic generation for multiplayer

### Phase 2: System Updates

5. **Update `ChunkRenderer`**
   - Read from `tiles[]` array directly
   - Remove pure chunk optimization (no longer needed)
   - Simplify rendering path

6. **Update `PlacementExecutor`**
   - Query `tiles[]` for surface checks
   - Remove independent noise sampling
   - **This fixes flora-on-water bug!**

7. **Update `VisionSystem`**
   - Query `tiles[]` for terrain visibility
   - Simplify line-of-sight checks

8. **Update `AIDecisionSystem`**
   - Query `tiles[]` for water detection
   - Pathfinding uses definitive tile data

### Phase 3: Cleanup

9. **Remove layered infrastructure**
   - Delete `selectSurface()` from query path (keep in generate())
   - Simplify `ChunkSampleResult` (corners still useful for biome sampling)
   - Remove sector grid (no longer needed for per-tile queries)

10. **Update tests**
    - Add generation tests (verify determinism)
    - Update existing tile query tests
    - Add memory usage benchmarks
    - Add test: no flora entities on Water surface

## Files to Modify

### Rename (Phase 0)
- `libs/engine/world/GroundCover.h` → `libs/engine/world/Surface.h`
- All files referencing `GroundCover`

### Core Changes (Phase 1)
- `libs/engine/world/chunk/Chunk.h` — Add `tiles` array, `generate()` method
- `libs/engine/world/chunk/Chunk.cpp` — Implement generation, update queries
- `libs/engine/world/chunk/TileData.h` — Packed struct with Surface

### System Updates (Phase 2)
- `libs/engine/world/rendering/ChunkRenderer.cpp` — Read from array
- `libs/engine/assets/placement/PlacementExecutor.cpp` — Query tiles, fix flora-on-water
- `libs/engine/ecs/systems/VisionSystem.cpp` — Query tiles
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` — Query tiles

### Cleanup (Phase 3)
- `libs/engine/world/chunk/ChunkSampleResult.h` — Simplify (keep corner data)
- `libs/engine/world/chunk/MockWorldSampler.cpp` — Remove pure chunk logic

## Risks and Mitigations

### Risk: Increased Chunk Load Time
**Mitigation**: Generation is parallelizable and faster than disk I/O. Profile before/after.

### Risk: Memory Pressure on Low-End Systems
**Mitigation**:
- Reduce load radius (3×3 instead of 5×5)
- Stream chunks more aggressively
- Consider compression for distant chunks

### Risk: Determinism Bugs in Multiplayer
**Mitigation**:
- All noise uses integer-based hash (already true)
- Add determinism tests comparing generation across platforms

## Success Criteria

1. **No more layered misalignment bugs** — All systems read same data
2. **No flora on water** — PlacementExecutor checks definitive surface
3. **Simpler debugging** — Can inspect `tiles[]` array directly
4. **Equivalent or better performance** — Query time reduced
5. **Memory usage < 200 MB** for typical 25-chunk load
6. **All existing tests pass** — No behavioral regressions

## Alternatives Considered

### Keep Layered System, Fix Bugs Individually
**Rejected**: Each fix is a band-aid. The architecture enables the bug class.

### Hybrid: Flat Array for Surface Only
**Considered**: Could store only `Surface` in array (262 KB/chunk).
**Rejected**: Partial solution, still has some layering complexity.

### Compression (RLE, etc.)
**Deferred**: Add only if memory becomes a real problem. Premature optimization.

## Related Documentation

- [Chunk Management System](./chunk-management-system.md) — Original design
- [3D to 2D Sampling](./3d-to-2d-sampling.md) — Coordinate transforms (still relevant)
- [World Generation Architecture](./world-generation-architecture.md) — Generator interface
- [Biome Influence System](../design/features/game-view/biome-influence-system.md) — Biome blending design

## Revision History

- 2025-12-11: Added GroundCover→Surface rename, clarified conceptual model, added flora-on-water fix
- 2025-12-11: Initial proposal based on water detection debugging session
