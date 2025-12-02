# Entity Placement System - Technical Specification

## Overview

A declarative system for placing entities on the world map. **All placement configuration lives in the asset itself** - biome rules, relationships, everything in one place. Groups provide a way to reference collections of entity types.

**Key Design Decisions:**
- **Asset-centric**: Each asset defines its own complete placement behavior
- **Groups**: Named collections of entity types (e.g., "flowers" = 8 flower types)
- **Dependency graph**: Auto-resolves spawn order from declared requirements
- **Probability-based**: Relationships modify spawn probability, not active clustering

---

## Architecture

```
Asset Definitions (XML)
  └─ biome rules
  └─ groups (self-declared)
  └─ relationships (all inline!)
              │
              ▼
     Dependency Graph Builder
       (topological sort)
              │
              ▼
     Placement Executor
       (spawn in order)
              │
              ▼
       Spatial Index
    (neighbor queries)
```

---

## Data Model

### 1. Asset Placement (UNIFIED - everything in one place)

```xml
<!-- In assets/definitions/flora/mushroom.xml -->
<AssetDef>
  <defName>Flora_Mushroom</defName>
  <!-- ... generator stuff ... -->

  <placement>
    <!-- Biome rules (per-biome, each can have different settings) -->
    <biome name="Forest">
      <spawnChance>0.1</spawnChance>
      <distribution>uniform</distribution>
    </biome>
    <biome name="Wetland" near="Water" distance="2">
      <spawnChance>0.2</spawnChance>
      <distribution>clumped</distribution>
      <clumping>
        <clumpSize min="3" max="8"/>
        <clumpRadius min="0.5" max="1.5"/>
      </clumping>
    </biome>

    <!-- Groups I belong to -->
    <groups>
      <group>mushroom</group>
      <group>fungus</group>
      <group>small_flora</group>
    </groups>

    <!-- MY relationships to OTHER ENTITIES - not tile types -->
    <relationships>
      <!-- I REQUIRE trees nearby to spawn -->
      <requires group="trees" distance="3.0" effect="required"/>

      <!-- I'm MORE LIKELY near fallen logs -->
      <affinity group="logs" distance="2.0" strength="1.5"/>

      <!-- I AVOID other mushrooms (variety) -->
      <avoids type="same" distance="2.0" penalty="0.3"/>
    </relationships>
  </placement>
</AssetDef>
```

### Two Types of Proximity

**Tile-Type Proximity** (`near` attribute on `<biome>`):
- Controls proximity to **terrain features** (Water, Rock, Sand tile types)
- Evaluated during biome eligibility check
- Example: Reeds only spawn in Wetland biome within 2 tiles of Water

**Entity Relationships** (`<relationships>` element):
- Controls proximity to **other spawned entities** (trees, grass, flowers)
- Evaluated during spawn probability calculation
- Example: Mushrooms more likely near trees

### Distribution Patterns

Each biome can specify a distribution pattern:

| Pattern  | Description | Use Case | Status |
|----------|-------------|----------|--------|
| `uniform` | Random placement, no clustering | Grass, small debris | ✅ Implemented |
| `clumped` | Groups together in patches | Flower patches, mushroom rings | 🚧 Planned |
| `spaced`  | Maintains minimum distance | Trees, large bushes | 🚧 Planned |

> **Note:** Currently only `uniform` distribution is implemented. All assets use random per-tile placement regardless of the distribution setting. Clumped and spaced distribution patterns are planned for a future phase.

```
UNIFORM                    CLUMPED                    SPACED
(random placement)         (patches/groups)           (regular spacing)

  •    •   •               •••    ••                     •       •
    •       •              ••••  •••                  •       •
  •   •  •    •              ••     •••                  •       •
     •    •                     ••••                  •       •
```

### Clumping Parameters

When `distribution="clumped"`:

```xml
<clumping>
  <clumpSize min="3" max="12"/>      <!-- instances per clump -->
  <clumpRadius min="0.5" max="2.0"/> <!-- tiles within clump -->
  <clumpSpacing min="3" max="8"/>    <!-- tiles between clump centers -->
</clumping>
```

### Spacing Parameters

When `distribution="spaced"`:

```xml
<spacing>
  <minDistance>2.0</minDistance>     <!-- minimum tiles between instances -->
</spacing>
```

Uses Poisson disk sampling - each new instance placed at least `minDistance` from all existing instances.

### 2. Groups (self-declared membership)

Groups are simply self-declared by assets - no separate file needed. The system collects all assets that declare membership in a group.

```xml
<!-- short_grass.xml -->
<AssetDef>
  <defName>Flora_ShortGrass</defName>
  <placement>
    <biome name="Grassland"><spawnChance>0.5</spawnChance></biome>
    <groups>
      <group>grass</group>
      <group>ground_cover</group>
    </groups>
  </placement>
</AssetDef>

<!-- long_grass.xml -->
<AssetDef>
  <defName>Flora_LongGrass</defName>
  <placement>
    <biome name="Grassland"><spawnChance>0.3</spawnChance></biome>
    <groups>
      <group>grass</group>
      <group>ground_cover</group>
    </groups>
  </placement>
</AssetDef>
```

**Result**: The "grass" group automatically contains both ShortGrass and LongGrass.
Any asset can reference this group in relationships.

---

## Example Use Cases

### "Birch trees clump with other birch and pine trees"
```xml
<!-- In birch.xml -->
<placement>
  <biome name="Forest"><spawnChance>0.2</spawnChance></biome>
  <groups><group>tree</group><group>deciduous</group></groups>

  <relationships>
    <!-- More likely near other birch -->
    <affinity defName="Flora_TreeBirch" distance="5.0" strength="1.8"/>
    <!-- Also likes pine -->
    <affinity defName="Flora_TreePine" distance="5.0" strength="1.3"/>
  </relationships>
</placement>
```

### "Flowers don't clump with themselves but spawn near grass"
```xml
<!-- In daisy.xml (and similar for other flowers) -->
<placement>
  <biome name="Grassland"><spawnChance>0.15</spawnChance></biome>
  <groups><group>flower</group></groups>

  <relationships>
    <!-- Avoid same flower type (variety) -->
    <avoids type="same" distance="1.5" penalty="0.3"/>

    <!-- Boost near any grass -->
    <affinity group="grass" distance="2.0" strength="1.5"/>
  </relationships>
</placement>
```

### "8 flower types in the flowers group"
Each flower declares itself as part of the "flower" group:
```xml
<!-- daisy.xml -->
<groups><group>flower</group></groups>

<!-- rose.xml -->
<groups><group>flower</group></groups>

<!-- ... same for tulip, bluebell, sunflower, lavender, poppy, violet ... -->
```

The system automatically collects all assets declaring `<group>flower</group>`.
Any asset can then reference this group in relationships:
```xml
<relationships>
  <avoids group="flower" distance="1.0" penalty="0.5"/>
</relationships>
```

---

## Execution Flow

1. **Load Time**: Build dependency graph from relationships
   - Topological sort determines spawn order
   - Detect circular dependencies (error)

2. **Per-Chunk Spawn**:
   ```
   for each group in dependency_order:
       for each entity_type in group:
           for each tile where biome matches:
               chance = base_spawn_chance
               chance *= dependency_multipliers(nearby_entities)
               chance *= affinity_multipliers(nearby_entities)
               chance *= avoidance_multipliers(nearby_entities)
               if random() < chance:
                   spawn(entity_type, position)
                   spatial_index.insert(instance)
   ```

3. **Spatial Index**: Grid-based (4 tiles per cell) for O(1) neighbor queries

---

## C++ Data Structures

```cpp
// libs/engine/assets/AssetDefinition.h (existing, with additions)

/// Distribution pattern for asset placement
enum class Distribution { Uniform, Clumped, Spaced };

/// Clumping parameters (existing)
struct ClumpingParams {
    int32_t clumpSizeMin = 3, clumpSizeMax = 12;
    float clumpRadiusMin = 0.5f, clumpRadiusMax = 2.0f;
    float clumpSpacingMin = 3.0f, clumpSpacingMax = 8.0f;
};

/// Spacing parameters (existing)
struct SpacingParams {
    float minDistance = 2.0f;
};

/// Per-biome placement configuration (existing, extended)
struct BiomePlacement {
    std::string biomeName;
    float spawnChance = 0.3f;
    Distribution distribution = Distribution::Uniform;
    ClumpingParams clumping;
    SpacingParams spacing;

    // NEW: tile-type proximity
    std::string nearTileType;     // e.g., "Water" (empty = no restriction)
    float nearDistance = 0.0f;    // tiles from nearTileType
};
```

```cpp
// libs/engine/assets/placement/PlacementTypes.h (NEW)

/// How to reference an entity in relationships
struct EntityRef {
    enum class Type { DefName, Group, Same };
    Type type;
    std::string value;  // defName or group name (empty if Same)
};

/// A single relationship rule (parsed from asset XML)
struct PlacementRelationship {
    enum class Kind { Requires, Affinity, Avoids };
    Kind kind;
    EntityRef target;           // What we relate to
    float distance = 5.0f;      // Radius for neighbor check
    float strength = 1.0f;      // Multiplier (for affinity)
    float penalty = 0.5f;       // Multiplier (for avoids)
    bool required = false;      // For requires: must exist to spawn?
};
```

```cpp
// libs/engine/assets/AssetDefinition.h (extended PlacementParams)

struct PlacementParams {
    std::vector<BiomePlacement> biomes;              // Existing
    std::vector<std::string> groups;                 // NEW: groups I belong to
    std::vector<PlacementRelationship> relationships; // NEW: entity relationships
};

// In AssetRegistry - built at load time
// std::unordered_map<std::string, std::vector<std::string>> m_groupIndex;
// Maps group name → list of defNames that declared membership
```

---

## File Structure

```
libs/engine/assets/
├── placement/                    # NEW directory
│   ├── PlacementTypes.h          # Enums, structs (above)
│   ├── DependencyGraph.h/.cpp    # Topological sort
│   ├── SpatialIndex.h/.cpp       # Grid-based neighbor queries
│   ├── PlacementExecutor.h/.cpp  # Main placement engine
│   └── *.test.cpp                # Unit tests
├── AssetDefinition.h             # Add groups + relationships fields
└── AssetRegistry.cpp             # Parse new XML elements, build group index

assets/definitions/
└── flora/*.xml                   # Add <groups> + <relationships>
```

---

## Implementation Phases

### Phase 1: Foundation
- [ ] Extend AssetDefinition with groups and relationships fields
- [ ] Parse `<groups>` and `<relationships>` in AssetRegistry
- [ ] Build group index (group name → list of defNames) at load time
- [ ] Create DependencyGraph with topological sort
- [ ] Create SpatialIndex with grid-based queries
- [ ] Unit tests for all components

### Phase 2: Executor
- [ ] Create PlacementExecutor
- [ ] Integrate with ChunkManager (including adjacent chunk queries)
- [ ] Wire up to GameScene

### Phase 3: Content
- [ ] Add groups + relationships to existing assets (grass, trees)
- [ ] Visual validation in world-sim

### Phase 4: (Future) Lua Extension
- [ ] Optional Lua placement scripts for truly custom cases
- [ ] Only if XML patterns prove insufficient

---

## Performance Considerations

| Operation | Complexity |
|-----------|------------|
| Dependency graph build | O(E + V) one-time |
| Spatial index insert | O(1) |
| Spatial index query | O(k) entities in nearby cells |
| Per-tile spawn decision | O(R * k) for R relationships |

**Memory**: ~700 KB per active chunk placement (dominated by spatial index)

**Optimizations**:
- Pre-computed defName→relationships lookup tables
- Early exit if required dependency has no matches
- Parallel processing within same dependency group

---

## Design Decisions

1. **Unified configuration**: All placement config lives in the asset itself:
   - Biome rules (where can I spawn?)
   - Group memberships (what am I?)
   - Relationships (who do I spawn near/avoid?)

   No separate registry files - everything in one place.

2. **Self-declared groups**: Assets declare what groups they belong to. The system builds a group index at load time from all asset definitions.

3. **Cross-chunk relationships**: Query adjacent chunks for neighbor lookups. This ensures mushrooms near a tree at chunk edge can still spawn correctly.

4. **Conflicting affinities**: Multiplicative - all applicable modifiers multiply together. Simple, predictable, no priority system needed initially.
