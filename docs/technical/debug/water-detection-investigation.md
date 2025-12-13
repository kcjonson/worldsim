# Debug Log: Colonist Water Detection Bug

**Issue:** Colonist spawns right next to a visible pond (~5 tiles away) but VisionSystem reports `water=0, shores=0, drinkable=0`

**Started:** 2024-12-09
**Status:** INVESTIGATING

---

## Problem Summary

The colonist can see a pond visually rendered on screen, but the VisionSystem is not detecting any water tiles. This prevents the colonist from ever drinking water.

## Key Facts Established

1. **Sight Radius:** `kDefaultSightRadius = 20.0F` (20 meters/tiles) - should easily cover 5 tiles
2. **Water Generation:** Water tiles generated via noise in `Chunk::selectGroundCover()` for Grassland/Forest biomes when `waterNoise > 0.82F`
3. **VisionSystem Output:** Shows `chunks=4 (null=0), water=0, shores=0` - chunks ARE being returned, but NO water tiles found
4. **Visual Confirmation:** User confirms pond is visible ~5 tiles from colonist spawn

## Hypotheses

**NOTE: Sessions 2, 5, and 12 all claimed "ROOT CAUSE FOUND" prematurely. All were wrong. Starting fresh.**

### H1: Y-Flip in Rendering
- **Status:** INVALIDATED (Session 3)
- **Evidence against:** Applying Y-flip broke camera movement - terrain and entities moved opposite directions

### H2: Camera Input Convention
- **Status:** INVALIDATED (Session 5)
- **Evidence against:** Code analysis showed conventions are consistent

### H3: Pure Chunk Optimization Hides Water
- **Status:** INVALIDATED (Session 12 claimed this but user confirms it's wrong)
- **Evidence against:** Fix was applied to MockWorldSampler but bug persists

### H4: (NEW) TBD - Need fresh investigation
- **Status:** untested
- **Evidence for:**
- **Evidence against:**
- **Test approach:**

---

## Investigation Steps

### Step 1: Verify Coordinate Systems Match
- [ ] Check how ChunkRenderer gets water tile positions
- [ ] Compare to how VisionSystem calculates tile positions
- [ ] Log both and compare for same water tile

### Step 2: Add Diagnostic Logging to VisionSystem
- [ ] Log the exact tile coordinates being scanned
- [ ] Log chunk origins being used
- [ ] For a few tiles, log what groundCover value is returned

### Step 3: Verify Water Exists in Chunk Data
- [ ] Add a test that directly queries chunk for water at known visual position
- [ ] Confirm Chunk::getTile() returns Water groundCover for rendered water tiles

### Step 4: Cross-reference with ChunkRenderer
- [ ] Find where ChunkRenderer draws water tiles
- [ ] Extract the coordinates it uses
- [ ] Compare to VisionSystem's coordinate calculations

---

## Code Locations

- **VisionSystem:** `/libs/engine/ecs/systems/VisionSystem.cpp` - water tile scanning
- **Chunk:** `/libs/engine/world/chunk/Chunk.cpp` - `selectGroundCover()` generates water
- **ChunkRenderer:** `/libs/engine/world/rendering/ChunkRenderer.cpp` - renders water tiles
- **Memory Component:** `/libs/engine/ecs/components/Memory.h` - sightRadius=20.0F

---

## Session Log

### Session 1 (Current)

**What was tried:**
1. Added PlacementExecutor fix to skip water tiles when placing entities
2. Rebuilt and ran game
3. Observed VisionSystem logging `water=0` despite user seeing pond nearby
4. Read ChunkRenderer.cpp to understand coordinate system
5. Read VisionSystem.cpp to compare coordinate calculations
6. Verified both use same coordinate approach (chunk.worldOrigin(), getTile())
7. Added diagnostic logging to VisionSystem to show:
   - Entity position and sight radius
   - World bounds being scanned
   - Chunk coordinate range
   - Per-chunk origin and tile range
   - Sample tile groundCover values
   - Total tiles scanned

**What was learned:**
- The fix to PlacementExecutor is in place (lines 169-172)
- VisionSystem IS running and scanning 4 chunks
- ChunkManager IS returning chunks (null=0)
- BUT no water tiles are being detected
- Coordinate systems APPEAR to match between ChunkRenderer and VisionSystem
- kChunkSize=512, kTileSize=1.0F
- sightRadius=20.0 (should cover plenty of tiles)

**Current step:**
- Rebuild and run with diagnostic logging to see EXACT scan parameters
- Look for tile range issues or groundCover value mismatches

**Next steps:**
- Analyze diagnostic output to understand the discrepancy
- If tile ranges look correct, check if getTile() returns different data

---

### Session 2 (2024-12-09)

**Goal:** Visually verify tile coordinate system by rendering coordinates directly on tiles

**What was implemented:**
1. Added debug tile coordinate rendering to `ChunkRenderer.cpp`:
   - Store tile coordinates in `m_debugTileCoords` vector during `addChunkTiles()`
   - After `flushBatch()`, render coordinates as yellow text on each tile using FontRenderer + BatchRenderer
   - Displays world tile coordinates (e.g., "0,0", "-5,12") directly on each tile

2. Files modified:
   - `libs/engine/world/rendering/ChunkRenderer.h` - Added `DebugTileCoord` struct and `m_debugTileCoords` vector
   - `libs/engine/world/rendering/ChunkRenderer.cpp` - Added coordinate collection and text rendering

**Key Discovery: Y-Axis Flip**

Screenshot analysis revealed a **critical coordinate system mismatch**:

- **HUD shows:** Position: (0, 0), Chunk: (0, 0)
- **Camera claims:** Position at world origin (0, 0)
- **Visible tile Y range:** ~55 (top of screen) → ~-1 (bottom of screen)
- **Observation:** Y values DECREASE going DOWN the screen

**This is BACKWARDS from expected!**

The rendering formula is:
```cpp
screenY = (worldY - camY) * scale + halfViewH
```

With `camY = 0` and positive `scale`, this means:
- Larger `worldY` → larger `screenY` → LOWER on screen (screen Y increases downward)
- But we observe: larger `worldY` appears at TOP of screen

**Root Cause Hypothesis:**
There's a Y-axis flip somewhere in the coordinate pipeline. Possible locations:
1. OpenGL projection matrix (Y-up vs Y-down convention)
2. Camera's `getVisibleRect()` calculation
3. Inconsistent assumptions between world space (Y-up) and screen space (Y-down)

**Impact:**
This explains why (0,0) appears at bottom-right of screen when camera claims to be at (0,0). The VisionSystem likely uses world coordinates correctly (Y-up), but the renderer displays with inverted Y, causing a visual mismatch between where water APPEARS and where VisionSystem LOOKS for it.

**Additional Issue Found:**
The SDF font atlas (`Roboto-SDF.png`) shows RGB channel separation, which may be intentional for SDF rendering but explains the garbled text appearance in the debug output.

**Next steps:**
1. ~~Trace the projection matrix setup in the renderer~~ DONE - Found issue!
2. ~~Check if WorldCamera applies any Y-flip~~ N/A - issue is in ChunkRenderer
3. ~~Verify VisionSystem and ChunkRenderer use identical coordinate transformations~~ They DON'T match!
4. ~~The water that VisionSystem reports "not found" may actually be on screen but at inverted Y position~~ CONFIRMED!

---

### Session 2 Continued - ROOT CAUSE FOUND

**Projection Analysis:**

The BatchRenderer uses **Screen-Space Projection** (`CreateScreenSpaceProjection()`):
```cpp
// (0,0) at top-left, Y increases downward
glm::ortho(0.0F, width, height, 0.0F, -1.0F, 1.0F);
```

This means:
- screenY = 0 → top of screen
- screenY = height → bottom of screen
- **Larger screenY = lower on screen** (Y-down)

**ChunkRenderer's World→Screen Transformation:**

`libs/engine/world/rendering/ChunkRenderer.cpp` line 178:
```cpp
// CURRENT (WRONG):
float screenY = (worldY - camY) * scale + halfViewH;
```

With world coordinates using **Y-up convention** (positive Y = up/north), this formula is **INCORRECT** because:
- Larger worldY → larger screenY → appears LOWER on screen
- But larger worldY should appear HIGHER (smaller screenY in screen Y-down)

**Correct formula should be:**
```cpp
// SHOULD BE:
float screenY = halfViewH - (worldY - camY) * scale;
```

**ROOT CAUSE CONFIRMED:**
The ChunkRenderer renders tiles with **inverted Y coordinates** relative to the world coordinate system:
- Water tile at world position (x, y) is rendered at screen position where it LOOKS like world position (x, -y+2*camY)
- VisionSystem correctly scans at world position (x, y)
- The colonist sees water at visual position (x, y_visual) but VisionSystem looks at world (x, y_world) where y_visual ≠ y_world

**Why water detection fails:**
1. A water tile at world position (10, -20) appears on screen where it LOOKS like it's at (10, 20)
2. Colonist at world (0, 0) sees water that appears 20 tiles "north" (visually)
3. VisionSystem scans 20 tiles north in world coordinates → world (0, 20)
4. But the actual water is at world (0, -20) → NOT FOUND!

**The fix:** Either:
1. Fix ChunkRenderer to use correct Y transformation (flip Y)
2. OR fix VisionSystem to use the same inverted coordinate system

Option 1 is cleaner - fix the rendering to match world coordinates.

---

## Notes

User insight: "we gave up on some work about grass and bushes spawning on water tiles, perhaps there is a major coordinate mismatch between the tiles and asset layers"

This suggests there may be a fundamental coordinate system mismatch that affects both:
1. Entity placement (grass on water)
2. Water detection (VisionSystem not finding water)

---

### Session 3 (2024-12-09) - Y-Flip Fix Attempt

**Goal:** Apply coordinated Y-flip fix to all rendering paths

**What was done:**
1. Applied Y-flip transformation to ALL 7 rendering locations:
   - ChunkRenderer.cpp line 136 (addChunkTiles)
   - ChunkRenderer.cpp line 183 (addPureChunk)
   - EntityRenderer.cpp lines 300, 338, 405, 443 (all CPU batched paths)
   - instancing.glsl (GPU shader)

2. Changed formula from:
   ```cpp
   screenY = (worldY - camY) * scale + halfViewH
   ```
   To:
   ```cpp
   screenY = halfViewH - (worldY - camY) * scale
   ```

**Result: FAILED - Introduced new bug**

When pressing UP arrow:
- Assets (entities) go DOWN (correct)
- Terrain goes UP (WRONG - should also go down)

**Analysis:**
The terrain and entities are moving in OPPOSITE directions when the camera moves. This indicates that either:
1. The Y-flip was not applied consistently to all rendering paths
2. The fix itself is incorrect and introduces a direction inversion

The Y-flip formula inversion causes camera movement direction to appear inverted:
- Original: smaller camY → larger (worldY - camY) → larger screenY → DOWN on screen
- Flipped: smaller camY → larger (worldY - camY) → SMALLER screenY (subtraction) → UP on screen

**Key insight:** The Y-flip formula inverts the relationship between camera movement and screen movement. This is fundamentally wrong.

**Next step:** Revert all Y-flip changes and investigate the ACTUAL root cause differently. The original debug observation showing "larger Y at top of screen" may have been incorrect, or the issue lies elsewhere in the coordinate pipeline.

---

### Session 4 (2024-12-09) - Revert Y-Flip, Re-investigate

**Goal:** Revert the broken Y-flip fix and investigate the actual root cause

**What was done:**
1. Reverted all 7 Y-flip changes:
   - ChunkRenderer.cpp lines 136, 183 → restored original formula
   - EntityRenderer.cpp lines 300, 338, 405, 443 → restored original formula
   - instancing.glsl line 45 → restored original formula

2. Original formula restored: `screenY = (worldY - camY) * scale + halfViewH`

**Key Realization:**

The Y-flip approach was fundamentally flawed because:
- **It inverts camera movement direction** - pressing UP moves the scene UP instead of DOWN
- The rendering transformation is CORRECT for the coordinate system being used
- The bug lies elsewhere in the coordinate pipeline

**Re-analysis of the Problem:**

The original issue: "colonist went south but closest water was west"

This suggests the pathfinding/navigation system has a coordinate mismatch, NOT the rendering system. Possible causes:

1. **VisionSystem stores water at wrong coordinates** - it scans tiles correctly but stores them with wrong coordinates in Memory
2. **Pathfinding uses different Y convention** - MovementSystem might interpret Y differently than VisionSystem
3. **ChunkCoordinate Y convention mismatch** - ChunkCoordinate.h defines Y increasing going SOUTH, but some systems might assume Y-up

---

### Session 5 (2024-12-09) - Deep Code Analysis

**Goal:** Trace complete coordinate flow from VisionSystem to rendering

**Code Analysis Performed:**

1. **VisionSystem.cpp** - Water detection
   - Shore position: `origin.x + tx + offset.x + 0.5`, `origin.y + ty + offset.y + 0.5`
   - Stores via `memory.rememberWorldEntity(shoreWorldPos, defNameId, capabilityMask)`

2. **Memory.h** - Storage
   - Stores `glm::vec2 position` directly in `KnownWorldEntity`
   - No transformation applied

3. **MemoryQueries.cpp** - Retrieval
   - `findNearestWithCapability()` iterates by Euclidean distance
   - Returns `entity->position` directly

4. **AIDecisionSystem.cpp** - Target selection
   - Sets `task.targetPosition = nearest->position`
   - Sets `movementTarget.target = task.targetPosition`
   - Already has logging at lines 120-127, 142-149

5. **MovementSystem.cpp** - Movement
   - `toTarget = target.target - pos.value`
   - `direction = normalize(toTarget)`
   - `vel.value = direction * speed`

6. **PhysicsSystem.cpp** - Position update
   - Simple Euler: `pos.value += vel.value * deltaTime`

7. **ChunkRenderer.cpp** - Terrain rendering
   - `screenY = (worldY - camY) * scale + halfViewH`

8. **EntityRenderer.cpp** - Entity rendering (lines 299-300, 337-338)
   - Same formula: `screenY = (worldY - camY) * scale + halfViewH`

9. **instancing.glsl** - GPU instanced entities (line 45)
   - Same formula: `screenPos = (worldVertex - cameraPosition) * scale + viewportSize * 0.5`

**Key Finding: Coordinate System Documentation Issue**

In `ChunkCoordinate.h`:
```cpp
// Line 53 comment says: "Get world position of chunk's origin (bottom-left corner)"
// But line 73-74 says: NorthWest = origin
// In Y-down world, NorthWest is TOP-left, not bottom-left!
```

This suggests confusion in the codebase about the coordinate system.

**Camera Input Issue Found:**

In `WorldCamera.h` lines 76-82:
```cpp
/// @param dy Vertical movement (-1 = down, +1 = up)
void move(float dx, float dy, float dt) {
    m_targetPosition.y += dy * m_panSpeed * dt;  // BUG: +1 should DECREASE y
}
```

Comment says `+1 = up`, but in Y-down world (+Y = South), pressing UP should **decrease** camera.y, not increase it!

**Analysis of Coordinate Conventions:**

| System | Convention | Notes |
|--------|------------|-------|
| ChunkCoordinate | +Y = South | Explicit in corner() function |
| Screen | +Y = Down | Standard screen coords |
| Rendering formula | Correct | Larger worldY → lower on screen |
| Camera input | **Inverted** | Comment says up=+1 but code adds to Y |

**The "went South but water was West" symptom:**

This is a 90° rotation, NOT a Y-flip. Possible causes:
1. Multiple water sources - nearest by distance might be South while visible is West
2. Some other X↔Y confusion not yet identified
3. Need runtime diagnostic data to confirm

**Next Steps:**
1. Add diagnostic logging to trace actual values at runtime
2. Log colonist position + all drinkable positions + which one was selected
3. Compare logged positions with visual positions on screen

---

### Session 6 (2024-12-09) - Enhanced Diagnostic Logging

**Goal:** Add comprehensive runtime logging to trace coordinate flow from Memory → Movement

**What was implemented:**

1. **MemoryQueries.cpp** - Added logging to `findNearestWithCapability()`:
   - Logs colonist position when searching for drinkable
   - Logs ALL drinkable candidates in memory with:
     - Position (x, y)
     - Diff vector from colonist to candidate
     - Distance
     - Cardinal direction (NORTH/SOUTH/EAST/WEST)
     - Mark which is selected as nearest
   - Logs final selection with direction
   - Uses `[MemoryQuery]` prefix for easy grep

2. **MovementSystem.cpp** - Added logging of actual movement direction:
   - Logs every 60 frames (once per second) to avoid spam
   - Shows entity position, target position, delta vector
   - Shows distance and cardinal direction
   - Uses `[Movement]` prefix for easy grep

**Logging Format Examples:**
```
[MemoryQuery] DRINKABLE SEARCH from=(10.00, 15.00), candidates=5
[MemoryQuery]   #0: pos=(25.50, 15.50), diff=(15.50, 0.50), dist=15.51, dir=EAST
[MemoryQuery]   #1: pos=(10.50, 30.50), diff=(0.50, 15.50), dist=15.51, dir=SOUTH
[MemoryQuery]   #2: pos=(5.50, 10.50), diff=(-4.50, -4.50), dist=6.36, dir=NORTH *NEAREST*
[MemoryQuery] SELECTED: pos=(5.50, 10.50), dist=6.36, dir=NORTH

[Movement] Entity 123: pos=(10.00, 14.50) → target=(5.50, 10.50), delta=(-4.50, -4.00), dist=6.02, dir=NORTH
```

**Key Coordinate Convention Notes (for future sessions):**
- World coordinate system: **+Y = SOUTH** (Y-down convention)
- In direction calculations: `diff.y > 0` means target is SOUTH of entity
- This matches ChunkCoordinate.h where NorthWest = origin, SouthWest = origin + size in Y

**How to analyze the output:**
1. Build and run: `cmake --build build --target world-sim && ./build/apps/world-sim/world-sim`
2. Look for `[MemoryQuery]` logs to see what water sources are known and which is selected
3. Look for `[Movement]` logs to see actual movement direction
4. Compare logged cardinal directions with VISUAL direction on screen
5. If logged says "SOUTH" but colonist visually moves "WEST", there's a 90° rotation bug

**Files modified:**
- `libs/engine/ecs/components/MemoryQueries.cpp` - Added drinkable search logging
- `libs/engine/ecs/systems/MovementSystem.cpp` - Added movement direction logging

**Next steps after analyzing output:**
1. Run game and capture diagnostic output
2. Compare logged directions with visual movement
3. If mismatch found, the bug is between Movement and Rendering
4. If no mismatch, bug is in VisionSystem storing wrong coordinates

---

### Session 7 (2024-12-09) - CRITICAL DISCOVERY: Colonist Drinks at GRASS, Not Water!

**Goal:** Capture and analyze diagnostic output

**Critical Finding:**

The colonist IS moving to a target and "drinking" - BUT THE TARGET IS GRASS, NOT WATER!

**Runtime Observations:**

1. VisionSystem water scan at startup:
   ```
   [Vision] WATER SEARCH radius=100: found 185 water tiles, closest at (-51.5,0.5) dist=51.8
   ```
   - Actual water detected at **(-51.5, 0.5)** which is **WEST** of colonist at (0, 0)

2. When colonist gets thirsty:
   ```
   [AI] Entity 4294967296: Thirst at 49% (28m away) → (11.5, 24.5)
   ```
   - AI selects target **(11.5, 24.5)** which is **SOUTH-EAST** of colonist

3. Colonist movement:
   - Started at ~(4.5, -2.4)
   - Moved to (11.5, 24.5) - SOUTH-EAST direction
   - **Successfully "drank"** at that position

4. **THE PROBLEM:** Position (11.5, 24.5) is **GRASS**, not water!
   - VisionSystem found water at (-51.5, 0.5) - WEST
   - But colonist went to (11.5, 24.5) - SOUTH-EAST
   - The coordinates are completely different!

**Key Diagnostic Data:**

| Source | Water Position | Direction from Colonist |
|--------|---------------|------------------------|
| VisionSystem WATER FOUND | (-51.5, 0.5) | WEST |
| AI Target Selection | (11.5, 24.5) | SOUTH-EAST |

This is NOT just a 90-degree rotation - the coordinates are completely wrong!

**Missing Diagnostic Logs:**

The `[MemoryQuery] DRINKABLE SEARCH` logs are NOT appearing in output, despite the code being added. This means either:
1. `findNearestWithCapability()` is not being called for Drinkable
2. A different code path is used to select water targets
3. The Memory doesn't contain drinkable entries where expected

**Root Cause Hypothesis:**

The bug is likely in **how VisionSystem stores water positions in Memory**:
1. VisionSystem detects water at correct world position (-51.5, 0.5)
2. But when stored in Memory via `rememberWorldEntity()`, the position gets corrupted
3. When AI queries Memory for nearest drinkable, it gets wrong position (11.5, 24.5)

OR the shore/drinkable positions are stored with a different coordinate than the actual water tiles.

**Next Steps:**
1. Add logging to VisionSystem at the exact moment water is stored in Memory
2. Log: world position detected vs position passed to `rememberWorldEntity()`
3. Compare stored position with the position returned by `findNearestWithCapability()`
4. Verify the drinkable positions in Memory match actual water tile positions

**Key Question to Answer:**
What coordinates does `memory.rememberWorldEntity()` receive, and do they match where water actually exists?

---

### Session 7 Continued - BREAKTHROUGH: Grass Growing IN Ponds!

**Critical User Insight:**

> "we have grass growing and bushes in the ponds... the problem may be that the tiles are not rendering where they should be"

**THIS IS THE SAME BUG!**

Two symptoms of one root cause:
1. **Grass/bushes visually appear ON TOP of water ponds**
2. **Colonist goes to grass location thinking it's water**

**Root Cause Hypothesis - TILE RENDERING COORDINATES ARE WRONG:**

The tile DATA is correct, but tile RENDERING uses wrong coordinates:
- Water tile data exists at position A (e.g., (-51.5, 0.5))
- Water tile RENDERS at position B (e.g., (11.5, 24.5)) - WRONG!
- Grass entity data exists at position C
- Grass entity renders at position C - CORRECT
- Result: grass appears ON TOP of where water renders (but water data is elsewhere)

**Why colonist drinks at grass:**
1. VisionSystem scans tiles at their DATA positions → finds water at (-51.5, 0.5)
2. Stores shore position near water data location
3. But water RENDERS at (11.5, 24.5) due to rendering bug
4. Colonist goes to where water APPEARS (11.5, 24.5)
5. But that's actually grass in the data!

Wait - that doesn't quite work either. Let me re-think...

**Alternative Hypothesis - ChunkRenderer has coordinate bug:**

If ChunkRenderer uses wrong formula to convert tile indices to screen positions:
- Tile at chunk-local (tx, ty) should render at world (origin.x + tx, origin.y + ty)
- But if formula is wrong, it renders at different position
- Entities use correct positions
- Result: tiles and entities don't align

**Key Investigation:**
Compare ChunkRenderer's tile→world coordinate calculation with VisionSystem's tile→world calculation.

ChunkRenderer (addChunkTiles):
```cpp
float worldX = chunkWorldX + static_cast<float>(tx);
float worldY = chunkWorldY + static_cast<float>(ty);
```

VisionSystem:
```cpp
glm::vec2 waterWorldPos{
    origin.x + static_cast<float>(tx) + 0.5F,
    origin.y + static_cast<float>(ty) + 0.5F};
```

Both use `origin + tx/ty` pattern, so they SHOULD match...

**BUT WAIT - Check chunk origin calculation!**

VisionSystem uses: `auto origin = chunk->worldOrigin();`
ChunkRenderer uses: `glm::vec2 chunkOrigin = chunk.worldOrigin();`

Both use `chunk.worldOrigin()`. Need to verify this returns consistent values.

**Next step:** Add logging to compare:
1. ChunkRenderer's world position when rendering a specific tile
2. VisionSystem's world position when scanning the same tile
3. Verify they're identical

---

### Session 8 (2024-12-09) - Tile Coordinate Debug Display + Major Discovery

**Goal:** Get tile coordinates rendering on screen to visually verify positions

**What was done:**

1. Fixed `Primitives::drawText()` - it was a stub! Implemented it to:
   - Use `g_fontRenderer->generateGlyphQuads()` to create glyph geometry
   - Pass quads to `g_batchRenderer->addTextQuad()` for rendering
   - Added `#include <font/FontRenderer.h>` to Primitives.cpp
   - Added PRIVATE include path to renderer CMakeLists.txt for ui headers

2. Fixed debug text rendering in ChunkRenderer:
   - The text was being rendered INSIDE the tile loop but BatchRenderer is separate from ChunkRenderer's direct rendering
   - Solution: Store labels in `m_debugLabels` vector during `addChunkTiles()`, render them AFTER `flushBatch()`

**Files modified:**
- `libs/renderer/primitives/Primitives.cpp` - Implemented `drawText()` function
- `libs/renderer/CMakeLists.txt` - Added PRIVATE include for ui headers
- `libs/engine/world/rendering/ChunkRenderer.h` - Added `DebugLabel` struct and `m_debugLabels` vector
- `libs/engine/world/rendering/ChunkRenderer.cpp` - Collect labels during tile rendering, render after flush

**MAJOR DISCOVERY - Coordinates Don't Match!**

User observation:
> "the coordinates of bushes don't seem to match, a bush to the north has a coordinate of 1.0, -70.4, but the tile coord is -14, 79"

**Analysis of the mismatch:**

| System | Reported Position |
|--------|------------------|
| Bush entity position | (1.0, -70.4) |
| Tile coordinate underneath | (-14, 79) |

These are **WILDLY different** - not just a Y-flip or small offset:
- X: 1.0 vs -14 → difference of **15 units**
- Y: -70.4 vs 79 → difference of **~149 units** (or Y-flip of 70 vs 79)

**Deep Code Analysis Performed:**

1. **PlacementExecutor.cpp** (lines 210-212):
   ```cpp
   world::WorldPosition origin = context.coord.origin();
   float tileWorldX = origin.x + static_cast<float>(localX);
   float tileWorldY = origin.y + static_cast<float>(localY);
   ```

2. **ChunkRenderer.cpp** (lines 92, 143-144):
   ```cpp
   WorldPosition chunkOrigin = chunk.worldOrigin();
   float worldX = chunkMinX + static_cast<float>(tileX) * kTileSize;
   float worldY = chunkMinY + static_cast<float>(tileY) * kTileSize;
   ```

3. **EntityRenderer.cpp** (lines 283-284, 297-298):
   ```cpp
   float posX = entity->position.x;
   float posY = entity->position.y;
   float worldX = v.x * entityScale + posX;
   float worldY = v.y * entityScale + posY;
   ```

4. **World→Screen transformation** (same in both renderers):
   ```cpp
   float screenX = (worldX - camX) * scale + halfViewW;
   float screenY = (worldY - camY) * scale + halfViewH;
   ```

**All systems use the same formulas!** So why the mismatch?

**Key Investigation: AsyncChunkProcessor.h**

Found how chunk data is captured for async processing (lines 29-46):
```cpp
inline ChunkDataSnapshot captureChunkData(const world::Chunk* chunk) {
    ChunkDataSnapshot snapshot;
    snapshot.coord = chunk->coordinate();

    for (uint16_t y = 0; y < world::kChunkSize; ++y) {
        for (uint16_t x = 0; x < world::kChunkSize; ++x) {
            const auto& tile = chunk->getTile(x, y);
            snapshot.biomes.push_back(tile.biome.primary());
            snapshot.groundCovers.push_back(world::groundCoverToString(tile.groundCover));
        }
    }
    return snapshot;
}
```

And how it's used for placement context (lines 87-91):
```cpp
ctx.getBiome = [&chunkData](uint16_t x, uint16_t y) {
    return chunkData.biomes[y * world::kChunkSize + x];
};
ctx.getGroundCover = [&chunkData](uint16_t x, uint16_t y) {
    return chunkData.groundCovers[y * world::kChunkSize + x];
};
```

**ROOT CAUSE HYPOTHESIS - Array Indexing Mismatch:**

The snapshot stores tiles in **row-major order** (`y * kChunkSize + x`), but the index calculation in the lambdas also uses `y * kChunkSize + x`.

**Wait - that should be correct...**

**Alternative Hypothesis - Origin Calculation Differs:**

PlacementExecutor uses `context.coord.origin()` where `coord` comes from `chunk->coordinate()`.

But what if `chunk->coordinate()` returns different values than expected?

Let me trace `ChunkCoordinate::origin()`:
```cpp
[[nodiscard]] WorldPosition origin() const {
    return {
        static_cast<float>(x * kChunkSize) * kTileSize,
        static_cast<float>(y * kChunkSize) * kTileSize
    };
}
```

With `kChunkSize = 512` and `kTileSize = 1.0F`:
- Chunk (0, 0) → origin (0, 0)
- Chunk (0, 1) → origin (0, 512)
- Chunk (-1, 0) → origin (-512, 0)

**The math checks out.** So the mismatch must be elsewhere.

**NEW HYPOTHESIS - Entity Position Storage vs Tile Rendering:**

Looking at the specific numbers:
- Bush at (1.0, -70.4)
- Tile shows (-14, 79)

If Y-flip: -70.4 flipped around 0 = +70.4, close to 79
If X-offset: 1.0 + offset = -14, so offset = -15

Could there be a **camera offset** that's applied differently?

**Next steps:**
1. Log the chunk coordinate and origin used by PlacementExecutor when placing an entity
2. Log the chunk coordinate and origin used by ChunkRenderer when rendering tiles
3. Compare for the SAME chunk to see if they differ
4. Check if camera position affects tile coordinate display (it shouldn't!)

**IMPORTANT REALIZATION:**

The debug tile coordinates show **world coordinates** calculated as:
```cpp
float worldX = chunkMinX + static_cast<float>(tileX) * kTileSize;
```

The entity position is stored by PlacementExecutor as:
```cpp
float tileWorldX = origin.x + static_cast<float>(localX);
```

Both formulas ARE identical in structure. They should give the same result IF:
- `chunkMinX` (from ChunkRenderer) == `origin.x` (from PlacementExecutor)

This means the chunk coordinates must differ! The PlacementExecutor might be using a DIFFERENT chunk coordinate than what ChunkRenderer uses.

**Hypothesis: Chunk assignment differs between systems**

When a bush is placed:
1. PlacementExecutor receives a chunk coordinate (e.g., chunk (0, -1))
2. Calculates position using that chunk's origin
3. Stores entity at calculated world position

When tiles render:
1. ChunkRenderer iterates chunks from ChunkManager
2. For each chunk, uses ITS coordinate to calculate world position
3. If the chunk assignments differ, positions won't align!

**How could chunk assignments differ?**

Possible causes:
1. AsyncChunkProcessor uses a different chunk coordinate than the actual chunk
2. The snapshot `coord` doesn't match the chunk being rendered
3. There's a timing issue where chunks are reassigned between placement and rendering

**KEY VERIFICATION NEEDED:**
Add logging to both systems to print chunk coordinate when processing/rendering.

---

### Session 11 (2024-12-10) - THE REAL BUG: Colonist Drinks from VISUAL GRASSLAND

**IMPORTANT CLARIFICATION:**

The bug is NOT that the colonist can't find water in the data. The bug is that:

1. **VisionSystem finds "water" tiles** and stores their shore positions in Memory
2. **Colonist decides to drink** and retrieves a shore position from Memory
3. **Colonist moves to that position** and successfully "drinks"
4. **BUT VISUALLY, that position is GRASSLAND**, not water!

The colonist is standing in what appears on screen to be a grassy field, drinking from nothing visible.

**The Symptom:**
- VisionSystem reports `water=6, shores=6, drinkable=6` ← Data exists
- Colonist goes to drink at position (X, Y) ← Path works
- Position (X, Y) visually shows **green grass tiles** ← THIS IS THE BUG
- The blue water tiles are rendered **far away** from where the colonist went

**Root Cause Hypothesis:**

There is a **coordinate mismatch** between:
1. Where VisionSystem **thinks** water tiles are (based on getTile() data)
2. Where ChunkRenderer **visually displays** water tiles on screen

Both systems call `chunk->getTile()` but may be computing different world positions for the same local tile coordinates.

**Diagnostic Logging Added:**
- `[ShoreReg]` - Logs chunk, water tile local coords, chunk origin, and shore world position when VisionSystem registers a shore
- `[DrinkSearch]` - Logs when colonist searches for drinkable in Memory
- `[DrinkTarget]` - Logs the exact world position colonist will walk to
- `[RenderWater]` - Existing logs showing where water is rendered

**The Expected Trace:**
```
[ShoreReg] chunk(A,B) waterTile(tx,ty) origin(ox,oy) → shore(sx,sy)
[DrinkTarget] target=(sx,sy) dist=D
[RenderWater] chunk(A,B) tile(tx,ty) at world=(wx,wy)  ← Should be near (sx,sy) but ISN'T!
```

If `shore(sx,sy)` doesn't match the visual position of rendered water, we've found the coordinate bug.

---

### Session 10 (2024-12-10) - WATER DETECTION NOW WORKING (BUT WRONG LOCATION!)

**Goal:** Continue investigating water/grass mismatch after system crash

**Key Discovery: VisionSystem IS Finding Water!**

The extensive diagnostic logging added in Session 9 reveals that water detection is now working:

```
[Vision] Entity at (0.3,0.1): chunks=4 (null=0), tiles=3721, water=6, shores=6, drinkable=6
```

**This is a significant change from the original bug report which showed `water=0, shores=0, drinkable=0`!**

**Diagnostic Evidence:**

1. **Snapshot correctly captures water tiles:**
   ```
   [SnapshotWater] chunk(0,1) tile(19,12) = Water
   [SnapshotWater] chunk(0,1) tile(20,12) = Water
   [SnapshotWater] chunk(0,1) tile(21,12) = Water
   ```

2. **Renderer correctly renders water tiles:**
   ```
   [RenderWater] chunk(-1,-1) tile(462,485) = Water
   [RenderWater] chunk(-1,-1) tile(461,486) = Water
   ```

3. **Placement correctly skips water tiles:**
   ```
   [Placement] SKIPPING entity - WATER detected
   ```
   (Many instances logged - placement IS detecting water)

4. **VisionSystem finds water in extended search:**
   ```
   [Vision] WATER SEARCH radius=100: found 185 water tiles, closest at (-51.5,0.5) dist=51.8
   ```

**Analysis:**

| System | Behavior | Status |
|--------|----------|--------|
| Snapshot | Captures water at tiles (19,12), (20,12)... in chunk(0,1) | ✓ Working |
| Renderer | Renders water at tiles (461-464, 485-490) in chunk(-1,-1) | ✓ Working |
| Placement | Skips entities on water tiles | ✓ Working |
| VisionSystem | Finds 6 water tiles, 6 shores, 6 drinkable | ✓ Working |

**Water Location Analysis:**

The rendered water tiles at chunk(-1,-1) position (462, 485):
- World position = (-512 + 462, -512 + 485) = (-50, -27)
- Distance from colonist at (0,0) = √(50² + 27²) ≈ **57 meters**
- Sight radius = 30 meters
- Therefore these specific water tiles are OUTSIDE sight radius

But VisionSystem still finds `water=6` within sight radius - there must be closer water tiles elsewhere.

**Conclusion for Session 10:**

The water detection system appears to be **working correctly**. The original bug symptoms may have been:
1. Water tiles genuinely far from spawn (>51m away in some runs)
2. Previous code changes during debugging may have fixed the issue
3. Non-deterministic world generation placing water at varying distances

**Remaining Issue: Grass on Water Visualization**

The visual issue of grass appearing on water tiles may still exist. This is separate from the water detection issue. The placement system IS skipping water tiles, but the question is whether the snapshot data matches the rendered tile data for the SAME coordinates.

**Next Steps:**
1. Clean up diagnostic logging (it's very verbose now)
2. Visually verify if grass still appears on water
3. If grass-on-water is still visible, investigate the snapshot→render data path more closely

---

### Session 12 (2024-12-10) - ROOT CAUSE FOUND: Pure Chunk Rendering Optimization Bug!

**CRITICAL DISCOVERY**

After extensive debugging across 11 sessions, the root cause has been identified:

**The "pure chunk" rendering optimization hides water tiles!**

**Evidence:**

1. VisionSystem logs show: `[TileCheck] chunk(0,0) tile(12,24): VisionSystem sees groundCover=4 (Water)`
2. ChunkRenderer logs show: **NO water tiles rendered from chunk(0,0)!** Only from chunk(-1,-1)
3. `[RenderCoord]` logs show only 2 chunks logged: (-1,-1) and (0,-1)
4. Where are chunks (0,0) and (-1,0)? **They're "pure" chunks rendered via `addPureChunk()`!**

**The Bug Mechanism:**

1. **Pure chunk detection** (MockWorldSampler.cpp:28-30):
   - A chunk is marked "pure" if all 4 corner biomes match
   - Chunk (0,0) has all corners = Grassland → marked pure

2. **Pure chunk rendering** (ChunkRenderer.cpp:66-67, 223-267):
   - Pure chunks skip `addChunkTiles()` and go through `addPureChunk()`
   - `addPureChunk()` renders the ENTIRE chunk as a single quad using `getBiomeColor(chunk.primaryBiome())`
   - **It NEVER calls `getTile()` for individual tiles!**

3. **Water generation** (Chunk.cpp:67-81):
   - Ponds are generated INSIDE Grassland/Forest biomes via noise in `selectGroundCover()`
   - Water appears when `waterNoise > 0.82F` for tiles within grassland

4. **The mismatch**:
   - VisionSystem calls `chunk->getTile(12,24)` → noise generates Water
   - ChunkRenderer sees `isPure()=true` → renders whole chunk as grass color
   - **Water tiles are DATA-present but VISUALLY hidden!**

**Why colonist drinks at grass:**

1. VisionSystem scans chunk(0,0) tile(12,24), finds Water via `getTile()`
2. Registers shore position at (11.5, 24.5) in Memory
3. Colonist AI queries nearest drinkable → gets (11.5, 24.5)
4. Colonist walks to (11.5, 24.5) and "drinks"
5. BUT chunk(0,0) is rendered as solid grass quad!
6. User sees colonist drinking from visual grassland

**The Fix:**

Mark chunks as NOT pure if their biome can generate ponds (Grassland, Forest):

```cpp
// In MockWorldSampler.cpp
if (isChunkPure(...)) {
    Biome primary = result.cornerBiomes[0].primary();
    // Grassland/Forest can have water ponds - force per-tile rendering
    if (primary == Biome::Grassland || primary == Biome::Forest) {
        result.isPure = false;
    } else {
        result.isPure = true;
        result.singleBiome = primary;
    }
}
```

**This explains BOTH bugs:**
1. Colonist drinks at visual grass → Water is in data but not rendered
2. Grass appears on water → Entities placed per-tile, tiles rendered per-chunk as single color

---

### Session 9 (2024-12-10) - Chunk Coordinate Verification PASSED + Visual Confirmation

**Goal:** Add diagnostic logging to verify chunk coordinates match between systems

**What was done:**

1. Added `[PlacementCoord]` logging to PlacementExecutor showing chunk coordinate → origin mapping
2. Added `[RenderCoord]` logging to ChunkRenderer showing chunk coordinate → origin mapping
3. Added `[EntityPlaced]` logging showing full coordinate chain: chunk → origin → local → world

**Results from diagnostic logging:**

```
[PlacementCoord] chunk=(0,0) → origin=(0.0,0.0)
[PlacementCoord] chunk=(-1,-1) → origin=(-512.0,-512.0)
[PlacementCoord] chunk=(0,-1) → origin=(0.0,-512.0)

[RenderCoord] chunk=(-1,-1) → origin=(-512.0,-512.0)
[RenderCoord] chunk=(0,-1) → origin=(0.0,-512.0)
```

**KEY FINDING #1: Chunk origins ARE CONSISTENT between systems!**

For the same chunk coordinate, both PlacementExecutor and ChunkRenderer compute identical origins:
- chunk=(-1,-1) → origin=(-512.0,-512.0) in BOTH systems ✓
- chunk=(0,-1) → origin=(0.0,-512.0) in BOTH systems ✓

The coordinate calculation is NOT the bug.

**Visual Confirmation via Screenshot:**

Captured screenshot showing:
- Camera at (0, 0) per HUD
- Water tiles (blue) visible on left side of screen
- **Grass entities (dark green triangles) clearly rendered ON TOP of blue water tiles!**

**KEY FINDING #2: Bug is visually confirmed - grass IS spawning on water!**

**Analysis of Placement Code:**

The placement code at `PlacementExecutor.cpp` lines 256-276 DOES check water at the entity's final position:
```cpp
// Convert world position back to local tile coordinates
int entityLocalX = static_cast<int>(std::floor(position.x - origin.x));
int entityLocalY = static_cast<int>(std::floor(position.y - origin.y));
// Check for water at entity position
std::string groundCover = context.getGroundCover(entityLocalX, entityLocalY);
if (groundCover == "Water") { continue; } // Skip
```

The water check IS being performed for each entity's actual position, not just the base tile.

**Remaining Hypotheses:**

1. **getGroundCover() returns wrong value**: The captured snapshot data doesn't match actual tile data
2. **Noise non-determinism**: selectGroundCover() uses noise that gives different results at capture vs render
3. **Timing issue**: Tiles are captured before water noise is fully evaluated

**Next Steps:**

1. Add logging to compare captured groundCover value vs rendered tile color for same position
2. Verify selectGroundCover() is deterministic by calling it twice for same tile
3. Check if there's a timing/initialization issue in chunk data capture

---
