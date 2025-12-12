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

    // Biome blending data — KEEP for ecotone support
    ChunkSampleResult biomeData;  // Sector grid for BiomeWeights queries

    // Thread safety
    std::atomic<bool> generationComplete{false};

    // Sparse storage for player modifications (unchanged)
    HashMap<TileLocalCoord, ModifiedTileData> modifications;
};

struct TileData {
    Surface surface;          // THE definitive surface type (1 byte)
    Biome primaryBiome;       // Dominant biome (1 byte)
    Biome secondaryBiome;     // For ecotones, or same as primary (1 byte)
    uint8_t biomeBlend;       // 0-255: weight of primary (255 = 100% primary) (1 byte)
    uint16_t elevation;       // Fixed-point elevation in cm (2 bytes)
    uint8_t moisture;         // 0-255 normalized (1 byte)
    uint8_t flags;            // Reserved for future use (1 byte)
    // Total: 8 bytes/tile
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

### Key Design Decisions

#### 1. Rename `GroundCover` → `Surface`
- Reflects actual meaning (terrain material, not plants)
- Clears up conceptual confusion

#### 2. Keep Sector Grid for Biome Blending
The `ChunkSampleResult` with its sector grid is **retained** for:
- Ecotone entity spawning (future: probabilistic spawning based on biome blend)
- Visual ground color blending at biome boundaries
- The sector grid efficiently provides BiomeWeights for any tile

The flat array stores the **result** of biome blending (primary/secondary + weight), while the sector grid remains available for more detailed queries if needed.

#### 3. Store Biome Blend Information
Each tile stores:
- `primaryBiome`: The dominant biome
- `secondaryBiome`: The secondary biome (for ecotones), or same as primary if pure
- `biomeBlend`: Weight of primary (255 = 100% primary, 128 = 50/50 blend)

This preserves ecotone information for future probabilistic entity spawning:
```cpp
// Future ecotone spawning logic
if (tile.biomeBlend < 255) {
    // This is an ecotone tile
    float primaryChance = tile.biomeBlend / 255.0f;
    if (rng.Float() < primaryChance) {
        // Spawn primary biome entity
    } else {
        // Spawn secondary biome entity
    }
}
```

#### 4. Deterministic Generation from Seed
All tile generation MUST be deterministic for multiplayer sync:

```cpp
void Chunk::generate(uint64_t worldSeed) {
    for (uint16_t y = 0; y < kChunkSize; ++y) {
        for (uint16_t x = 0; x < kChunkSize; ++x) {
            TileData& tile = tiles[y * kChunkSize + x];

            // World position for deterministic noise
            float worldX = static_cast<float>(m_coord.x * kChunkSize + x);
            float worldY = static_cast<float>(m_coord.y * kChunkSize + y);

            // Step 1: Get biome weights from sector grid
            BiomeWeights weights = m_biomeData.getTileBiome(x, y);
            tile.primaryBiome = weights.primary();
            tile.secondaryBiome = weights.secondary();  // New method needed
            tile.biomeBlend = static_cast<uint8_t>(weights.get(tile.primaryBiome) * 255.0f);

            // Step 2: Select surface (deterministic noise based on world position + seed)
            tile.surface = selectSurface(tile.primaryBiome, worldX, worldY, worldSeed);

            // Step 3: Elevation (interpolated from corners)
            tile.elevation = computeElevation(x, y);

            // Step 4: Moisture (deterministic hash)
            tile.moisture = computeMoisture(worldX, worldY, worldSeed);
        }
    }

    // Mark generation complete for thread safety
    generationComplete.store(true, std::memory_order_release);
}
```

**Determinism requirements:**
- All noise functions use `worldSeed + offset` (already true)
- Integer-based hashing, no floating-point platform variance
- Same seed + same coordinates = identical tiles everywhere

#### 5. Thread Safety

Chunks may be generated on background threads while the main thread renders. Solution:

```cpp
class Chunk {
public:
    // Called by background thread
    void generate(uint64_t worldSeed);

    // Called by main thread — safe to call during/after generation
    [[nodiscard]] bool isReady() const {
        return generationComplete.load(std::memory_order_acquire);
    }

    // Only call after isReady() returns true
    [[nodiscard]] const TileData& getTile(uint16_t x, uint16_t y) const {
        assert(isReady());
        return tiles[y * kChunkSize + x];
    }

private:
    std::atomic<bool> generationComplete{false};
    std::array<TileData, kChunkSize * kChunkSize> tiles;
};
```

**Usage in renderer:**
```cpp
void ChunkRenderer::render(const Chunk& chunk) {
    if (!chunk.isReady()) {
        renderPlaceholder(chunk.coordinate());  // Or skip
        return;
    }
    // Safe to access tiles
    for (/* visible tiles */) {
        const TileData& tile = chunk.getTile(x, y);
        // render...
    }
}
```

## Memory Analysis

### Proposed System (Flat Array)

```
Per Chunk:
- TileData: 8 bytes/tile × 262,144 tiles = 2.1 MB
- ChunkSampleResult (sector grid): ~4 KB
- Total: ~2.1 MB per chunk

Typical Load (25 chunks): ~52 MB
Maximum Load (100 chunks): ~210 MB
```

### Is This Acceptable?

**Yes, absolutely.** Modern games routinely use gigabytes of RAM:
- AAA games: 8-16 GB typical
- A single 4K texture: 32-64 MB
- Our entire loaded world: ~52 MB

52 MB for the entire visible world is **tiny** by modern standards. This is not worth optimizing.

## How Biome Blending Works

### The Sector Grid (Kept)

The sector grid in `ChunkSampleResult` provides efficient biome weight queries:

```cpp
// 32×32 grid covering 512×512 tiles
// Each sector covers 16×16 tiles
std::array<BiomeWeights, 32 * 32> sectorGrid;

BiomeWeights getTileBiome(uint16_t x, uint16_t y) {
    int sectorX = x / 16;
    int sectorY = y / 16;
    return sectorGrid[sectorY * 32 + sectorX];
}
```

At chunk generation, we query this grid and store the results in TileData. The grid remains available for any future systems that need full BiomeWeights.

### Ecotone Support

With `primaryBiome`, `secondaryBiome`, and `biomeBlend` stored per tile:

1. **Entity Spawning**: PlacementExecutor can use blend weight for probabilistic spawning
2. **Visual Blending**: Renderer can interpolate ground colors based on blend
3. **Gameplay**: Movement speed, resource availability can factor in blend

## Implementation Plan

### Phase 0: Rename (Prerequisite)

1. **Rename `GroundCover` → `Surface`**
   - Update enum and all references
   - Rename `Grass` → `Soil`
   - Rename `selectGroundCover()` → `selectSurface()`
   - Update comments and documentation

### Phase 1: Data Structure Changes

2. **Update `TileData` struct**
   - Add `Surface surface`
   - Add `Biome primaryBiome`, `Biome secondaryBiome`, `uint8_t biomeBlend`
   - Total 8 bytes per tile

3. **Add `tiles` array to Chunk**
   - `std::array<TileData, kChunkSize * kChunkSize> tiles`
   - Add `std::atomic<bool> generationComplete`
   - Keep `ChunkSampleResult biomeData` (don't remove!)

4. **Create `Chunk::generate()` method**
   - Move surface selection logic here
   - Populate entire array at construction
   - Set `generationComplete` flag when done
   - Ensure all noise is deterministic from seed

5. **Add `BiomeWeights::secondary()` method**
   - Returns second-highest weighted biome
   - For ecotone blend storage

### Phase 2: System Updates

6. **Update `ChunkRenderer`**
   - Check `chunk.isReady()` before accessing tiles
   - Read from `tiles[]` array directly
   - Remove pure chunk optimization (no longer needed)

7. **Update `PlacementExecutor`**
   - Query `tiles[]` for surface checks
   - **This fixes flora-on-water bug!**
   - Future: use biome blend for probabilistic spawning

8. **Update `VisionSystem`**
   - Query `tiles[]` for terrain checks
   - Check `isReady()` for thread safety

9. **Update `AIDecisionSystem`**
   - Query `tiles[]` for water detection
   - Pathfinding uses definitive tile data

### Phase 3: Cleanup

10. **Simplify but don't remove ChunkSampleResult**
    - Keep sector grid (for future BiomeWeights queries)
    - Remove pure chunk optimization flag (no longer needed)
    - Keep corner elevations

11. **Update tests**
    - Add generation determinism tests (same seed = same tiles)
    - Add thread safety tests
    - Add test: no flora entities on Surface::Water
    - Test biome blend values at ecotones

## Files to Modify

### Rename (Phase 0)
- `libs/engine/world/GroundCover.h` → `libs/engine/world/Surface.h`
- All files referencing `GroundCover`

### Core Changes (Phase 1)
- `libs/engine/world/chunk/Chunk.h` — Add `tiles` array, `generate()`, `isReady()`
- `libs/engine/world/chunk/Chunk.cpp` — Implement generation
- `libs/engine/world/chunk/TileData.h` — New/updated struct
- `libs/engine/world/BiomeWeights.h` — Add `secondary()` method

### System Updates (Phase 2)
- `libs/engine/world/rendering/ChunkRenderer.cpp` — Thread safety, read from array
- `libs/engine/assets/placement/PlacementExecutor.cpp` — Query tiles, fix flora-on-water
- `libs/engine/ecs/systems/VisionSystem.cpp` — Query tiles
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` — Query tiles

### Kept (not removed)
- `libs/engine/world/chunk/ChunkSampleResult.h` — Keep sector grid for biome queries

## Risks and Mitigations

### Risk: Increased Chunk Load Time
**Mitigation**:
- Generation is parallelizable on background threads
- 22ms per chunk is faster than typical disk I/O
- Profile before/after to verify

### Risk: Thread Safety Bugs
**Mitigation**:
- Simple atomic flag pattern (`generationComplete`)
- All systems check `isReady()` before tile access
- No partial reads possible — entire array or nothing

### Risk: Determinism Bugs in Multiplayer
**Mitigation**:
- All noise uses integer-based hash from `worldSeed`
- Add determinism tests: generate same chunk twice, verify identical
- Test across platforms if targeting cross-play

### Risk: Breaking Biome Blending
**Mitigation**:
- Keep sector grid (ChunkSampleResult)
- Store blend info in TileData for common queries
- Full BiomeWeights available via sector grid if needed

## Success Criteria

1. **No more layered misalignment bugs** — All systems read same data
2. **No flora on water** — PlacementExecutor checks definitive surface
3. **Deterministic generation** — Same seed = identical tiles
4. **Thread safe** — No crashes or data races during chunk streaming
5. **Biome blending preserved** — Ecotone tiles have blend information
6. **Memory usage < 250 MB** for typical 25-chunk load (with headroom)
7. **All existing tests pass** — No behavioral regressions

## Related Documentation

- [Chunk Management System](./chunk-management-system.md) — Original design
- [3D to 2D Sampling](./3d-to-2d-sampling.md) — Coordinate transforms (still relevant)
- [World Generation Architecture](./world-generation-architecture.md) — Generator interface
- [Biome Influence System](../design/features/game-view/biome-influence-system.md) — Biome blending design

## Revision History

- 2025-12-11: Added thread safety, determinism requirements, biome blend storage, kept sector grid
- 2025-12-11: Added GroundCover→Surface rename, clarified conceptual model, added flora-on-water fix
- 2025-12-11: Initial proposal based on water detection debugging session
