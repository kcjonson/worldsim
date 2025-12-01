# Asset Definition Schema

**Status**: Design Phase
**Created**: 2025-11-30

## Overview

Asset definitions are XML files that describe what assets exist in the game. They separate **data** (properties, parameters) from **logic** (generation scripts, engine code), enabling modders to add content without touching C++.

This approach is inspired by RimWorld's Def system, which has proven highly moddable and extensible.

## Design Principles

1. **Declarative**: Definitions describe *what*, not *how*
2. **Inheritance**: Use `ParentDef` to reduce duplication
3. **Overridable**: Later-loaded definitions can override earlier ones (mod support)
4. **Validatable**: Schema can be validated at load time

## Definition Structure

### Basic Asset Definition

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AssetDefinitions>

  <!-- Simple asset: designer-authored SVG -->
  <AssetDef>
    <defName>Flower_Daisy</defName>
    <label>Daisy</label>
    <description>A common wildflower with white petals</description>

    <assetType>simple</assetType>
    <svgPath>svg/flora/flowers/daisy.svg</svgPath>

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
      <tier>instanced</tier>
      <maxInstances>50000</maxInstances>
    </rendering>

    <placement>
      <biomes>
        <li>Grassland</li>
        <li>Temperate_Forest</li>
        <li>Meadow</li>
      </biomes>
      <density>0.3</density>
      <clustering>0.7</clustering>
      <minSpacing>2.0</minSpacing>
    </placement>
  </AssetDef>

</AssetDefinitions>
```

### Procedural Asset Definition

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AssetDefinitions>

  <!-- Procedural asset: Lua-generated tree -->
  <AssetDef>
    <defName>Tree_Oak</defName>
    <label>Oak Tree</label>
    <description>A large deciduous tree with spreading branches</description>

    <assetType>procedural</assetType>

    <generator>
      <script>generators/trees/deciduous.lua</script>
      <params>
        <trunkHeightRange>35,50</trunkHeightRange>
        <trunkWidthRange>4,6</trunkWidthRange>
        <branchLevels>3,5</branchLevels>
        <branchAngleRange>25,45</branchAngleRange>
        <leafDensity>0.7</leafDensity>
        <leafSvg>svg/flora/leaves/oak_leaf.svg</leafSvg>
        <barkColor>#5D4037</barkColor>
        <leafColorRange>
          <base>#228B22</base>
          <hueShift>-15,15</hueShift>
        </leafColorRange>
      </params>
    </generator>

    <pregenerate>
      <variantCount>200</variantCount>
      <cacheToDisk>true</cacheToDisk>
      <cacheFile>cache/flora/tree_oak_variants.bin</cacheFile>
    </pregenerate>

    <rendering>
      <tier>cpu_tessellated</tier>
      <lodLevels>
        <li distance="100" tier="instanced" variantIndex="0" />
        <li distance="50" tier="cpu_tessellated" />
      </lodLevels>
    </rendering>

    <placement>
      <biomes>
        <li>Temperate_Forest</li>
        <li>Deciduous_Forest</li>
      </biomes>
      <density>0.1</density>
      <clustering>0.4</clustering>
      <minSpacing>15.0</minSpacing>
    </placement>

    <animation>
      <enabled>true</enabled>
      <windResponse>0.3</windResponse>
      <swayFrequency>0.5,1.0</swayFrequency>
    </animation>
  </AssetDef>

</AssetDefinitions>
```

### Inheritance Example

```xml
<?xml version="1.0" encoding="UTF-8"?>
<AssetDefinitions>

  <!-- Abstract base for all flowers -->
  <AssetDef Abstract="true" Name="FlowerBase">
    <assetType>simple</assetType>
    <rendering>
      <tier>instanced</tier>
      <maxInstances>50000</maxInstances>
    </rendering>
    <variation>
      <scaleRange>0.8,1.2</scaleRange>
      <rotationRange>0,360</rotationRange>
    </variation>
  </AssetDef>

  <!-- Concrete flower inheriting from base -->
  <AssetDef ParentDef="FlowerBase">
    <defName>Flower_Poppy</defName>
    <label>Poppy</label>
    <svgPath>svg/flora/flowers/poppy.svg</svgPath>
    <variation>
      <colorRange>
        <hueShift>-5,5</hueShift>
      </colorRange>
    </variation>
    <placement>
      <biomes>
        <li>Grassland</li>
        <li>Meadow</li>
      </biomes>
      <density>0.2</density>
    </placement>
  </AssetDef>

  <!-- Another flower with different properties -->
  <AssetDef ParentDef="FlowerBase">
    <defName>Flower_Tulip</defName>
    <label>Tulip</label>
    <svgPath>svg/flora/flowers/tulip.svg</svgPath>
    <variation>
      <colorRange>
        <hueShift>-60,60</hueShift>  <!-- Wide color variation -->
      </colorRange>
    </variation>
    <placement>
      <biomes>
        <li>Temperate_Forest</li>
        <li>Garden</li>
      </biomes>
      <density>0.15</density>
      <clustering>0.9</clustering>  <!-- Tulips cluster heavily -->
    </placement>
  </AssetDef>

</AssetDefinitions>
```

## Schema Reference

### Root Elements

| Element | Required | Description |
|---------|----------|-------------|
| `defName` | Yes | Unique identifier (e.g., `Tree_Oak`) |
| `label` | Yes | Human-readable name |
| `description` | No | Tooltip/documentation text |
| `assetType` | Yes | `simple` or `procedural` |

### Simple Asset Elements

| Element | Required | Description |
|---------|----------|-------------|
| `svgPath` | Yes | Path to SVG file (relative to assets/) |

### Procedural Asset Elements

| Element | Required | Description |
|---------|----------|-------------|
| `generator/script` | Yes | Path to Lua script |
| `generator/params` | No | Key-value parameters passed to script |
| `pregenerate/variantCount` | No | Number of variants to pre-generate |
| `pregenerate/cacheToDisk` | No | Whether to cache generated variants |

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
| `tier` | `instanced` or `cpu_tessellated` |
| `maxInstances` | Max GPU instances (instanced tier) |
| `lodLevels/li` | LOD settings by distance |

### Placement Elements

| Element | Description |
|---------|-------------|
| `biomes/li` | List of biomes where this asset spawns |
| `density` | Spawn density (0-1) |
| `clustering` | How much assets cluster together (0-1) |
| `minSpacing` | Minimum distance between instances |

### Animation Elements

| Element | Description |
|---------|-------------|
| `enabled` | Whether animation is active |
| `windResponse` | How much wind affects this asset (0-1) |
| `swayFrequency` | Range of sway frequency in Hz |

## Loading Process

```
1. Scan assets/definitions/ for *.xml files
2. Parse all definitions, build inheritance tree
3. Resolve ParentDef references
4. Validate required fields
5. Build AssetRegistry lookup table
6. For procedural assets:
   a. Check if cache exists and is valid
   b. If not, load Lua script and generate variants
   c. Tessellate and cache all variants
7. For simple assets:
   a. Load SVG file
   b. Tessellate once
   c. Store in GPU buffer for instancing
```

## Modding Support

### Load Order

```
1. Core game definitions (assets/definitions/)
2. Mod definitions (mods/*/definitions/)
3. Later definitions override earlier ones with same defName
```

### Adding New Assets (Modder)

1. Create new XML file in `mods/MyMod/definitions/flora/`
2. Define new AssetDef with unique defName
3. Place SVG files in `mods/MyMod/svg/` (or reference core assets)
4. For procedural: place Lua scripts in `mods/MyMod/generators/`

### Overriding Existing Assets (Modder)

```xml
<!-- In mods/MyMod/definitions/overrides.xml -->
<AssetDefinitions>
  <!-- Override core Tree_Oak to be bigger -->
  <AssetDef>
    <defName>Tree_Oak</defName>
    <generator>
      <params>
        <trunkHeightRange>60,80</trunkHeightRange>  <!-- Taller! -->
      </params>
    </generator>
  </AssetDef>
</AssetDefinitions>
```

## Related Documents

- [Asset System Architecture](./README.md) - System overview
- [Lua Scripting API](./lua-scripting-api.md) - Generator script API
- [Variant Cache Format](./variant-cache.md) - Binary cache format
