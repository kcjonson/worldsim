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
