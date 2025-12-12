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
4. **Scattered logic** — Ground cover selection duplicated across systems

### Concrete Bug Example

The "pure chunk" rendering optimization renders entire chunks as single-color quads when all corners have the same biome. But ponds are generated at the TILE level via noise in `selectGroundCover()`, so:

- **Data**: Pond tiles exist (noise > threshold)
- **Rendering**: Single green quad (optimization hides per-tile variation)
- **Entity Placement**: Must independently check the same noise

This class of bug is **structurally impossible** with flat tile storage.

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
    GroundCover groundCover;  // THE definitive surface type (1 byte)
    Biome biome;              // Primary biome (1 byte)
    uint16_t elevation;       // Fixed-point elevation in cm (2 bytes)
    uint8_t moisture;         // 0-255 normalized (1 byte)
    uint8_t flags;            // Reserved for future use (1 byte)
    // Total: 6 bytes/tile (can pack to 4 if needed)
};
```

### Key Changes

1. **Tile generation moves to chunk load time**
   - `Chunk::generate()` runs once, populates entire `tiles` array
   - All noise sampling (ponds, dirt patches, variation) happens here
   - Result is stored, not recomputed

2. **Tile queries become simple array access**
   ```cpp
   TileData Chunk::getTile(uint16_t localX, uint16_t localY) const {
       return tiles[localY * kChunkSize + localX];
   }
   ```

3. **Single source of truth**
   - Renderer reads `tiles[]` — sees ponds
   - PlacementExecutor reads `tiles[]` — avoids water
   - VisionSystem reads `tiles[]` — correct LOS
   - AIDecisionSystem reads `tiles[]` — accurate pathfinding

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
- Ground cover selection: ~50 cycles
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

### Phase 1: Data Structure Changes

1. **Add `tiles` array to Chunk**
   - `std::array<TileData, kChunkSize * kChunkSize> tiles`
   - Keep existing lazy system working in parallel initially

2. **Create `Chunk::generate()` method**
   - Move `selectGroundCover()` logic here
   - Move `generateTile()` logic here
   - Populate entire array at construction

3. **Update `TileData` struct**
   - Add packed representation (6 bytes)
   - Ensure deterministic generation for multiplayer

### Phase 2: System Updates

4. **Update `ChunkRenderer`**
   - Read from `tiles[]` array directly
   - Remove pure chunk optimization (no longer needed)
   - Simplify rendering path

5. **Update `PlacementExecutor`**
   - Query `tiles[]` for ground cover checks
   - Remove independent noise sampling

6. **Update `VisionSystem`**
   - Query `tiles[]` for terrain visibility
   - Simplify line-of-sight checks

7. **Update `AIDecisionSystem`**
   - Query `tiles[]` for water detection
   - Pathfinding uses definitive tile data

### Phase 3: Cleanup

8. **Remove layered infrastructure**
   - Delete `selectGroundCover()` from query path
   - Simplify `ChunkSampleResult` (corners still useful for biome sampling)
   - Remove sector grid (no longer needed for per-tile queries)

9. **Update tests**
   - Add generation tests (verify determinism)
   - Update existing tile query tests
   - Add memory usage benchmarks

## Files to Modify

### Core Changes
- `libs/engine/world/chunk/Chunk.h` — Add `tiles` array, `generate()` method
- `libs/engine/world/chunk/Chunk.cpp` — Implement generation, update queries
- `libs/engine/world/chunk/TileData.h` — Packed struct definition

### System Updates
- `libs/engine/world/rendering/ChunkRenderer.cpp` — Read from array
- `libs/engine/assets/placement/PlacementExecutor.cpp` — Query tiles
- `libs/engine/ecs/systems/VisionSystem.cpp` — Query tiles
- `libs/engine/ecs/systems/AIDecisionSystem.cpp` — Query tiles

### Cleanup
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
2. **Simpler debugging** — Can inspect `tiles[]` array directly
3. **Equivalent or better performance** — Query time reduced
4. **Memory usage < 200 MB** for typical 25-chunk load
5. **All existing tests pass** — No behavioral regressions

## Alternatives Considered

### Keep Layered System, Fix Bugs Individually
**Rejected**: Each fix is a band-aid. The architecture enables the bug class.

### Hybrid: Flat Array for Ground Cover Only
**Considered**: Could store only `GroundCover` in array (262 KB/chunk).
**Rejected**: Partial solution, still has some layering complexity.

### Compression (RLE, etc.)
**Deferred**: Add only if memory becomes a real problem. Premature optimization.

## Related Documentation

- [Chunk Management System](./chunk-management-system.md) — Original design
- [3D to 2D Sampling](./3d-to-2d-sampling.md) — Coordinate transforms (still relevant)
- [World Generation Architecture](./world-generation-architecture.md) — Generator interface

## Revision History

- 2025-12-11: Initial proposal based on water detection debugging session
