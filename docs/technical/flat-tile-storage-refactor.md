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
│  3D SPHERICAL WORLD (the ultimate source of truth)          │
│  - Sampled via IWorldSampler at chunk corners               │
│  - Deterministic from world seed                            │
│  - Adjacent chunks share corner positions → seamless edges  │
└─────────────────────────────────────────────────────────────┘
                              ↓
              IWorldSampler::sampleChunk() returns
              ChunkSampleResult (temporary input)
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  CHUNK GENERATION (one-time computation)                    │
│  - Iterates all 262,144 tiles                               │
│  - Queries ChunkSampleResult for biome weights              │
│  - Applies noise for surface variation (ponds, etc.)        │
│  - Stores results in tiles[] array                          │
│  - ChunkSampleResult discarded after generation             │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  TILES[] ARRAY (the chunk's source of truth)                │
│  What IS each tile? Surface, biome, elevation, moisture     │
│  - Single source of truth for all systems                   │
│  - Fast O(1) array access                                   │
│  - Affects walkability, buildability, flora placement       │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│  FLORA / GROUND COVER (entities placed on surface)          │
│  Grass blades, flowers, moss, shrubs, trees                 │
│  - Placed by PlacementExecutor AFTER tiles are generated    │
│  - Queries tiles[] to check surface type                    │
│  - Skips Water surfaces (fixes flora-on-water bug!)         │
└─────────────────────────────────────────────────────────────┘
```

**Key Insight**: Surface must be computed FIRST, stored definitively, THEN flora placement can reliably check "is this tile water?" and skip it.

## Proposed Solution

Replace the layered system with a **flat tile array** per chunk:

```cpp
struct Chunk {
    ChunkCoordinate coordinate;
    std::array<TileData, kChunkSize * kChunkSize> tiles;  // 512×512 = 262,144 tiles

    // Thread safety
    std::atomic<bool> generationComplete{false};

    // Sparse storage for player modifications (unchanged)
    HashMap<TileLocalCoord, ModifiedTileData> modifications;

    // NOTE: No ChunkSampleResult stored — it's temporary during generate()
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

#### 2. ChunkSampleResult is Temporary (Not Stored)

> **Design History Note:** We initially considered keeping the sector grid (from `ChunkSampleResult`) permanently in each chunk for potential future BiomeWeights queries. After analysis, we determined this is unnecessary because:
>
> 1. **Seed-based regeneration**: Any chunk can be regenerated from `worldSeed + chunkCoordinate` at any time
> 2. **No neighbor dependencies**: Adjacent chunks don't need each other's data — they share corner positions in the 3D world, so edges match automatically
> 3. **Biome info stored in tiles**: `primaryBiome`, `secondaryBiome`, and `biomeBlend` capture what we need for ecotones
> 4. **Memory savings**: ~32 KB per chunk (1,024 BiomeWeights × 32 bytes each)
>
> If we ever need full BiomeWeights (all 8 biome percentages) for a tile, we can re-sample the 3D world. This is rare and acceptable.

The generation flow:

```cpp
// In ChunkManager
void loadChunk(ChunkCoordinate coord) {
    // 1. Sample 3D world (creates temporary ChunkSampleResult)
    ChunkSampleResult sampleResult = m_worldSampler->sampleChunk(coord);

    // 2. Create chunk and generate tiles
    auto chunk = std::make_unique<Chunk>(coord);
    chunk->generate(m_worldSeed, sampleResult);

    // 3. sampleResult goes out of scope — sector grid discarded
    //    Only tiles[] array persists

    m_chunks[coord] = std::move(chunk);
}
```

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
void Chunk::generate(uint64_t worldSeed, const ChunkSampleResult& sampleResult) {
    for (uint16_t y = 0; y < kChunkSize; ++y) {
        for (uint16_t x = 0; x < kChunkSize; ++x) {
            TileData& tile = tiles[y * kChunkSize + x];

            // World position for deterministic noise
            float worldX = static_cast<float>(m_coord.x * kChunkSize + x);
            float worldY = static_cast<float>(m_coord.y * kChunkSize + y);

            // Step 1: Get biome weights from sample result (temporary)
            BiomeWeights weights = sampleResult.getTileBiome(x, y);
            tile.primaryBiome = weights.primary();
            tile.secondaryBiome = weights.secondary();  // New method needed
            tile.biomeBlend = static_cast<uint8_t>(weights.get(tile.primaryBiome) * 255.0f);

            // Step 2: Select surface (deterministic noise based on world position + seed)
            tile.surface = selectSurface(tile.primaryBiome, worldX, worldY, worldSeed);

            // Step 3: Elevation (interpolated from sample result corners)
            tile.elevation = sampleResult.getTileElevation(x, y);

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

**Why adjacent chunks match at edges:**
- Chunk A's NE corner and Chunk B's NW corner sample the **same 3D world position**
- Because 3D sampling is deterministic, they get identical biome data
- Interpolation across each chunk produces seamless ecotones
- No chunk needs to "know about" its neighbors

#### 5. Thread Safety

Chunks may be generated on background threads while the main thread renders. Solution:

```cpp
class Chunk {
public:
    // Called by background thread
    void generate(uint64_t worldSeed, const ChunkSampleResult& sampleResult);

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
- No sector grid stored (temporary during generation)
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

## Why No Neighbor Dependencies?

A key property of the architecture: **chunks are independently generatable**.

```
┌─────────────────┬─────────────────┐
│    Chunk A      │    Chunk B      │
│   (generated)   │  (not yet)      │
│                 │                 │
│      NE corner ─┼─ NW corner      │  ← Same 3D world position!
│                 │                 │
└─────────────────┴─────────────────┘
```

When Chunk B is eventually generated:
1. `IWorldSampler::sampleChunk(B)` queries the 3D world at B's corners
2. B's NW corner = A's NE corner (same coordinates)
3. Deterministic sampling → same biome weights
4. Seamless edge, no communication needed

This enables:
- Infinite streaming in any direction
- Parallel chunk generation
- No ordering constraints
- Player can teleport anywhere

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
   - Remove `ChunkSampleResult` member (becomes parameter to generate())

4. **Create `Chunk::generate(worldSeed, sampleResult)` method**
   - Move surface selection logic here
   - Populate entire array
   - Set `generationComplete` flag when done
   - Ensure all noise is deterministic from seed

5. **Add `BiomeWeights::secondary()` method**
   - Returns second-highest weighted biome
   - For ecotone blend storage

### Phase 2: System Updates

6. **Update `ChunkManager`**
   - Sample 3D world, pass result to generate(), discard after
   - Handle `isReady()` checks for in-progress chunks

7. **Update `ChunkRenderer`**
   - Check `chunk.isReady()` before accessing tiles
   - Read from `tiles[]` array directly
   - Remove pure chunk optimization (no longer needed)

8. **Update `PlacementExecutor`**
   - Query `tiles[]` for surface checks
   - **This fixes flora-on-water bug!**
   - Future: use biome blend for probabilistic spawning

9. **Update `VisionSystem`**
   - Query `tiles[]` for terrain checks
   - Check `isReady()` for thread safety

10. **Update `AIDecisionSystem`**
    - Query `tiles[]` for water detection
    - Pathfinding uses definitive tile data

### Phase 3: Cleanup & Testing

11. **Remove ChunkSampleResult from Chunk**
    - It's now a temporary parameter to generate()
    - Keep the class itself (IWorldSampler still returns it)
    - Remove `isPure` flag (no longer needed)

12. **Update tests**
    - Add generation determinism tests (same seed = same tiles)
    - Add thread safety tests
    - Add test: no flora entities on Surface::Water
    - Test biome blend values at ecotones
    - Test edge matching between adjacent chunks

## Files to Modify

### Rename (Phase 0)
- `libs/engine/world/GroundCover.h` → `libs/engine/world/Surface.h`
- All files referencing `GroundCover`

### Core Changes (Phase 1)
- `libs/engine/world/chunk/Chunk.h` — Add `tiles` array, `generate()`, `isReady()`, remove stored biomeData
- `libs/engine/world/chunk/Chunk.cpp` — Implement generation
- `libs/engine/world/chunk/TileData.h` — New/updated struct
- `libs/engine/world/BiomeWeights.h` — Add `secondary()` method

### System Updates (Phase 2)
- `libs/engine/world/chunk/ChunkManager.cpp` — Pass sampleResult to generate()
- `libs/engine/world/rendering/ChunkRenderer.cpp` — Thread safety, read from array
- `libs/engine/assets/placement/PlacementExecutor.cpp` — Query tiles, fix flora-on-water
- `libs/engine/ecs/systems/VisionSystem.cpp` — Query tiles
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` — Query tiles

### Simplified (not removed)
- `libs/engine/world/chunk/ChunkSampleResult.h` — Remove `isPure`, keep as temporary data structure

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

### Risk: Edge Mismatch Between Chunks
**Mitigation**:
- Adjacent chunks share corner coordinates in 3D world
- Deterministic sampling guarantees identical biome data at shared corners
- Add test: generate two adjacent chunks, verify edge tiles have compatible biomes

## Success Criteria

1. **No more layered misalignment bugs** — All systems read same data
2. **No flora on water** — PlacementExecutor checks definitive surface
3. **Deterministic generation** — Same seed = identical tiles
4. **Thread safe** — No crashes or data races during chunk streaming
5. **Biome blending preserved** — Ecotone tiles have blend information
6. **Seamless chunk edges** — Adjacent chunks match at boundaries
7. **Memory usage < 250 MB** for typical 25-chunk load (~52 MB expected)
8. **All existing tests pass** — No behavioral regressions

## Related Documentation

- [Chunk Management System](./chunk-management-system.md) — Original design
- [3D to 2D Sampling](./3d-to-2d-sampling.md) — Coordinate transforms (still relevant)
- [World Generation Architecture](./world-generation-architecture.md) — Generator interface
- [Biome Influence System](../design/features/game-view/biome-influence-system.md) — Biome blending design

## Revision History

- 2025-12-11: Clarified ChunkSampleResult is temporary (discarded after generation), added neighbor independence explanation
- 2025-12-11: Added thread safety, determinism requirements, biome blend storage, kept sector grid
- 2025-12-11: Added GroundCover→Surface rename, clarified conceptual model, added flora-on-water fix
- 2025-12-11: Initial proposal based on water detection debugging session
