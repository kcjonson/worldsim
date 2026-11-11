# Asset Definition Schema

**Status**: Implemented (Core Features)
**Created**: 2025-11-30
**Last Updated**: 2025-12-22

> **Implementation Note**: Core schema is fully implemented and in production use. Optional features (variation, inheritance, LOD) are spec'd but not yet implemented—they will be added when needed.

## Overview

Asset definitions are XML files that describe what assets exist in the game. They separate **data** (properties, parameters) from **logic** (generation scripts, engine code), enabling modders to add content without touching C++.

This approach is inspired by RimWorld's Def system, which has proven highly moddable and extensible.

**Key change**: Each asset lives in its own folder with all resources co-located. Paths in definitions are **relative to the asset folder**.

## Design Principles

1. **Declarative**: Definitions describe *what*, not *how*
2. **Self-Contained**: Each asset folder contains definition + all resources
3. **Inheritance**: Use `parent` attribute to reduce duplication
4. **Overridable**: Later-loaded definitions can override earlier ones (mod support)
5. **Validatable**: Schema can be validated at load time

## Definition Structure

### Simple Asset Definition

A simple asset lives in its own folder with an XML definition and SVG file:

```
assets/world/flora/Daisy/
├── Daisy.xml           # Definition
└── flower.svg          # SVG source
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/world/flora/Daisy/Daisy.xml -->
<AssetDef>
  <defName>Daisy</defName>
  <label>Daisy</label>
  <description>A common wildflower with white petals</description>

  <assetType>simple</assetType>

  <!-- Path relative to this asset folder -->
  <svgPath>flower.svg</svgPath>
  <worldHeight>0.3</worldHeight>

  <variation>
    <colorRange>
      <hueShift>-10,10</hueShift>
      <saturationMult>0.9,1.1</saturationMult>
      <valueMult>0.95,1.05</valueMult>
    </colorRange>
    <scaleRange>0.8,1.2</scaleRange>
    <rotationRange>0,360</rotationRange>
  </variation>

  <rendering>
    <complexity>simple</complexity>
    <tier>instanced</tier>
    <maxInstances>50000</maxInstances>
  </rendering>

  <placement>
    <biome name="Grassland">
      <spawnChance>0.3</spawnChance>
      <distribution>clumped</distribution>
    </biome>
    <biome name="Forest">
      <spawnChance>0.1</spawnChance>
    </biome>
    <groups>
      <group>flowers</group>
      <group>small_flora</group>
    </groups>
  </placement>
</AssetDef>
```

### Procedural Asset Definition

A procedural asset includes a generator script:

```
assets/world/flora/OakTree/
├── OakTree.xml         # Definition
├── generate.lua        # Generator script
└── oak_leaf.svg        # Component SVG used by script
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/world/flora/OakTree/OakTree.xml -->
<AssetDef>
  <defName>OakTree</defName>
  <label>Oak Tree</label>
  <description>A large deciduous tree with spreading branches</description>

  <assetType>procedural</assetType>

  <generator>
    <!-- Path relative to asset folder -->
    <script>generate.lua</script>
    <params>
      <trunkHeightRange>1.2,1.5</trunkHeightRange>
      <trunkWidthRange>0.15,0.2</trunkWidthRange>
      <branchLevels>3,5</branchLevels>
      <branchAngleRange>25,45</branchAngleRange>
      <leafDensity>0.7</leafDensity>
      <!-- Local SVG component -->
      <leafSvg>oak_leaf.svg</leafSvg>
      <barkColor>#5D4037</barkColor>
      <leafColorRange>
        <base>#228B22</base>
        <hueShift>-15,15</hueShift>
      </leafColorRange>
    </params>
  </generator>

  <variantCount>200</variantCount>

  <rendering>
    <complexity>complex</complexity>
    <tier>batched</tier>
    <lodLevels>
      <li distance="100" tier="instanced" variantIndex="0" />
      <li distance="50" tier="batched" />
    </lodLevels>
  </rendering>

  <placement>
    <biome name="Forest">
      <spawnChance>0.05</spawnChance>
      <distribution>spaced</distribution>
      <spacing>
        <minDistance>6.0</minDistance>
      </spacing>
    </biome>
    <groups>
      <group>trees</group>
      <group>deciduous_trees</group>
      <group>large_flora</group>
    </groups>
    <relationships>
      <avoids type="same" distance="6.0" penalty="0.1"/>
      <avoids group="trees" distance="4.0" penalty="0.5"/>
    </relationships>
  </placement>

  <animation>
    <enabled>true</enabled>
    <windResponse>0.3</windResponse>
    <swayFrequency>0.5,1.0</swayFrequency>
  </animation>
</AssetDef>
```

### Inheritance Example

For related assets, use inheritance with the `parent` attribute:

```
assets/world/flora/Flowers/
├── Flowers.xml         # Contains abstract base + concrete definitions
├── poppy.svg
└── tulip.svg
```

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- assets/world/flora/Flowers/Flowers.xml -->
<AssetDefinitions>

  <!-- Abstract base for all flowers (not instantiated) -->
  <AssetDef abstract="true" name="FlowerBase">
    <assetType>simple</assetType>
    <rendering>
      <complexity>simple</complexity>
      <tier>instanced</tier>
      <maxInstances>50000</maxInstances>
    </rendering>
    <variation>
      <scaleRange>0.8,1.2</scaleRange>
      <rotationRange>0,360</rotationRange>
    </variation>
  </AssetDef>

  <!-- Concrete flower inheriting from base -->
  <AssetDef parent="FlowerBase">
    <defName>Poppy</defName>
    <label>Poppy</label>
    <svgPath>poppy.svg</svgPath>
    <worldHeight>0.25</worldHeight>
    <variation>
      <colorRange>
        <hueShift>-5,5</hueShift>
      </colorRange>
    </variation>
    <placement>
      <biome name="Grassland">
        <spawnChance>0.2</spawnChance>
      </biome>
      <groups>
        <group>flowers</group>
      </groups>
    </placement>
  </AssetDef>

  <!-- Another flower with different properties -->
  <AssetDef parent="FlowerBase">
    <defName>Tulip</defName>
    <label>Tulip</label>
    <svgPath>tulip.svg</svgPath>
    <worldHeight>0.3</worldHeight>
    <variation>
      <colorRange>
        <hueShift>-60,60</hueShift>  <!-- Wide color variation -->
      </colorRange>
    </variation>
    <placement>
      <biome name="Forest">
        <spawnChance>0.15</spawnChance>
        <distribution>clumped</distribution>
        <clumping>
          <clumpSize>5,15</clumpSize>
        </clumping>
      </biome>
      <groups>
        <group>flowers</group>
      </groups>
    </placement>
  </AssetDef>

</AssetDefinitions>
```

### Cross-Asset Inheritance

To inherit from an asset in a different folder, use `@defName`:

```xml
<!-- assets/world/flora/RedMaple/RedMaple.xml -->
<AssetDef parent="@MapleTree">
  <defName>RedMaple</defName>
  <label>Red Maple</label>
  <!-- Override leaf color -->
  <generator>
    <params>
      <leafColorRange>
        <base>#8B0000</base>
      </leafColorRange>
    </params>
  </generator>
</AssetDef>
```

## Schema Reference

### Root Elements

| Element | Required | Description |
|---------|----------|-------------|
| `defName` | Yes | Unique identifier (e.g., `MapleTree`) |
| `label` | Yes | Human-readable name |
| `description` | No | Tooltip/documentation text |
| `assetType` | Yes | `simple` or `procedural` |

### Simple Asset Elements

| Element | Required | Description |
|---------|----------|-------------|
| `svgPath` | Yes | Path to SVG file (relative to asset folder) |
| `worldHeight` | No | Normalize SVG to this height in meters |

### Procedural Asset Elements

| Element | Required | Description |
|---------|----------|-------------|
| `generator/script` | Yes | Path to script (relative to asset folder) |
| `generator/params` | No | Key-value parameters passed to script |
| `variantCount` | No | Number of variants to pre-generate |
| `ageClasses` | No | A tree's age classes, in place of `variantCount` (below) |
| `detailLevels` | No | The screen densities a mesh is cut for, one mesh a level (below) |

### Age classes

A tree def can split into age classes. Each placed tree gets one age class and one mesh variant from its def, world position, and the world seed (`assets::computeTreeTraits`, [tree-variety.md](./tree-variety.md) R1), so old stands and young regrowth form patches about 30 m across and thickets lean young.

```xml
<ageClasses>
  <ageClass name="sapling" weight="0.25" variants="4" yieldScale="0.25" workScale="0.35"/>
  <ageClass name="mature"  weight="0.55" variants="8" yieldScale="1.0"  workScale="1.0"/>
  <ageClass name="veteran" weight="0.20" variants="4" yieldScale="1.8"  workScale="1.6"/>
</ageClasses>
```

| Attribute | Required | Description |
|-----------|----------|-------------|
| `name` | Yes | Unique within the def, not empty |
| `weight` | Yes | Share of the def's trees in this class, above 0; a def's weights sum to 1 (within 0.001) |
| `variants` | Yes | Mesh variants in this class, 1 to 65535 |
| `yieldScale` | No | Multiplier on the `<harvestable>` yield, above 0; default 1 |
| `workScale` | No | Multiplier on the `<harvestable>` durability, above 0; default 1 |

A def with both `<ageClasses>` and `<variantCount>` is a validation error, as is any broken attribute above; the loader then ignores the whole block. A def without `<ageClasses>` has one age class of `variantCount` variants. Nothing reads the yield and work scales or draws per variant yet.

Collision is per age class. A generator's `asset:setCollisionRect` is captured from each class's variant 0 while the variants are prewarmed at load, and `AssetDefinition::collisionFor(ageClass)` answers with it (a def without `<ageClasses>` answers for class 0). An XML `<collision>` or SVG metadata collider applies to every class and wins over the generator. The rect may not depend on the variant: `asset-cli validate --render-smoke` generates variants 0 and 1 of each class with two or more and fails the def, naming the class, if they differ.

### Detail levels

A procedural plant can be cut for several screen densities, in pixels a meter (the camera's 8 px/m times its zoom, 2 to 160):

```xml
<detailLevels>2, 4, 8, 24, 80</detailLevels>
```

Up to 8 positive densities, strictly ascending; anything else is a validation error and the def keeps one mesh. Each template (def, age class, variant) has a mesh at every level, and the generator sees the level's density as the Lua global `detailPxPerMeter`. The game draws a template at the highest level at or below the camera's density, else the lowest (`assets::detailLevelFor`, `DetailLevels.h`). The template id has no level in it, so what entities store doesn't change with the zoom.

The reference level is the one the default view (24 px/m) picks. The load prewarms it, and everything that isn't drawing reads it: the silhouette (picking, selection, see-through), `maxReachMeters` and so the cull margin, tall routing, and the collision rect. The renderer builds the other levels on request, off the main thread, when the camera first needs them, and draws the nearest ready level meanwhile (`InstancedEntityRenderer`). A level's collision rect must equal the reference level's: `asset-cli validate --render-smoke` builds every level of every aged def and fails one that differs or whose generator fails. `asset-cli inspect` counts each level, and `render --grid` or `--age` takes `--level <px/m>`.

### Variation Elements

| Element | Description |
|---------|-------------|
| `colorRange/hueShift` | Range of hue adjustment in degrees |
| `colorRange/saturationMult` | Range of saturation multiplier |
| `colorRange/valueMult` | Range of value/brightness multiplier |
| `scaleRange` | Min,max scale factor |
| `rotationRange` | Min,max rotation in degrees |

### Rendering Elements

| Element | Description |
|---------|-------------|
| `complexity` | `simple` (instanced) or `complex` (tessellated) |
| `tier` | `instanced` or `batched` |
| `maxInstances` | Max GPU instances (instanced tier) |
| `lodLevels/li` | LOD settings by distance |

### Placement Elements

| Element | Description |
|---------|-------------|
| `biome/@name` | Biome this rule applies to |
| `spawnChance` | Spawn probability (0-1) |
| `distribution` | `uniform`, `clumped`, or `spaced` |
| `scale/@min`, `scale/@max` | This def's placement scale range, with 0 < min <= max; default 0.8 to 1.2 for every def. A def that sets it moves only its own scales. |
| `groups/group` | Groups this asset belongs to |
| `relationships/*` | Entity relationship rules |

### Animation Elements

| Element | Description |
|---------|-------------|
| `enabled` | Whether animation is active |
| `windResponse` | How much wind affects this asset (0-1) |
| `swayFrequency` | Range of sway frequency in Hz |

## Path Resolution

All paths in asset definitions are **relative to the asset folder** by default:

| Path | Resolves To |
|------|-------------|
| `flower.svg` | `<asset_folder>/flower.svg` |
| `generate.lua` | `<asset_folder>/generate.lua` |
| `@components/leaf` | `assets/shared/components/leaf.svg` |
| `@OakTree` | Reference to OakTree asset by defName |

See [Folder-Based Assets](./folder-based-assets.md) for complete path resolution rules.

## Loading Process

```
1. Scan assets/ recursively for folders containing <FolderName>.xml
2. For each asset folder:
   a. Parse XML definition(s)
   b. Resolve relative paths to absolute
   c. Register in AssetRegistry by defName
3. Resolve parent references (inheritance)
4. Validate required fields
5. For procedural assets:
   a. Check if cache exists and is valid
   b. If not, execute script and generate variants
   c. Tessellate and cache all variants
6. For simple assets:
   a. Load SVG file
   b. Tessellate once
   c. Store in GPU buffer for instancing
```

## Modding Support

### Load Order

```
1. Core game assets (assets/)
2. Mod assets (mods/*/assets/)
3. Later definitions override earlier ones with same defName
```

### Adding New Assets (Modder)

1. Create asset folder: `mods/MyMod/assets/world/flora/ExoticFlower/`
2. Add XML definition: `ExoticFlower.xml`
3. Add resources: SVG files, scripts
4. All paths in XML are relative to the asset folder

### Overriding Existing Assets (Modder)

```xml
<!-- mods/MyMod/assets/world/flora/OakTree/OakTree.xml -->
<AssetDef override="true">
  <defName>OakTree</defName>
  <generator>
    <params>
      <trunkHeightRange>2.0,2.5</trunkHeightRange>  <!-- Taller! -->
    </params>
  </generator>
</AssetDef>
```

## Related Documents

- [Asset System Architecture](./README.md) - System overview
- [Folder-Based Assets](./folder-based-assets.md) - Folder structure and path resolution
- [Scripting API](./lua-scripting-api.md) - Generator script API
- [Variant Cache Format](./variant-cache.md) - Binary cache format
