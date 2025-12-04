# Folder-Based Asset Definition System

**Status**: Implemented (Phase 1)
**Created**: 2025-12-03
**Updated**: 2025-12-03
**Author**: Technical Design

## Overview

This document proposes restructuring the asset system from a **flat file structure** (definitions, SVGs, and scripts in separate directories) to a **folder-per-asset structure** where each asset is a self-contained folder containing all related files.

### Current vs Proposed

**Current Structure:**
```
assets/
├── definitions/flora/grass.xml      # Definition references...
├── svg/grass_blade.svg              # ...this SVG in a different location
└── generators/trees/deciduous.lua   # Scripts also separate
```

**Proposed Structure:**
```
assets/
└── world/flora/GrassBlade/
    ├── GrassBlade.xml               # Definition + SVG + scripts all together
    └── blade.svg
```

## Design Goals

1. **Self-Contained**: Each asset folder contains everything needed for that asset
2. **Portable**: Easy to copy/share/distribute individual assets
3. **Moddable**: Clear structure for modders to understand and extend
4. **Discoverable**: Asset folder name matches defName - easy to find
5. **Versionable**: Each asset can be independently versioned/tracked
6. **Clear Ownership**: One defName = one folder (no ambiguity)

## Proposed File Structure

### Core Structure

The asset system uses a **3-level hierarchy**: `category/subcategory/Asset/`

```
assets/
├── shared/                          # Shared resources
│   ├── scripts/                     # Shared script modules (via @shared/ prefix)
│   │   ├── deciduous.lua           # → @shared/deciduous.lua in XML
│   │   ├── branch_utils.lua        # → @shared/branch_utils.lua
│   │   └── noise.lua               # → @shared/noise.lua
│   │
│   └── components/                  # Shared SVG components (planned)
│       ├── leaf_shapes.svg         # Reusable leaf templates
│       └── bark_patterns.svg       # Bark texture patterns
│
├── world/                           # World generation assets
│   ├── flora/                       # Plants
│   │   ├── GrassBlade/
│   │   │   ├── GrassBlade.xml      # Primary definition (REQUIRED)
│   │   │   └── blade.svg
│   │   ├── MapleTree/
│   │   │   ├── MapleTree.xml
│   │   │   ├── generate.lua        # Primary generator script
│   │   │   └── maple_leaf.svg      # Component SVG
│   │   ├── OakTree/
│   │   │   ├── OakTree.xml
│   │   │   ├── generate.lua
│   │   │   └── oak_leaf.svg
│   │   └── Mushroom/
│   │       ├── Mushroom.xml
│   │       ├── cap_red.svg
│   │       └── stem.svg
│   │
│   ├── terrain/                     # Ground features
│   │   ├── Boulder/
│   │   │   ├── Boulder.xml
│   │   │   └── boulder.svg
│   │   └── WaterPool/
│   │       ├── WaterPool.xml
│   │       └── generate.lua
│   │
│   └── structures/                  # Natural/ancient structures
│       └── Ruin/
│           ├── Ruin.xml
│           └── generate.lua
│
├── creatures/                       # Living things (animals, monsters)
│   ├── Deer/
│   │   ├── Deer.xml
│   │   ├── body.svg
│   │   └── animations/
│   │       └── walk.lua
│   ├── Rabbit/
│   │   └── ...
│   └── Wolf/
│       └── ...
│
├── objects/                         # World-placed objects
│   ├── crafting/
│   │   ├── Workbench/
│   │   │   ├── Workbench.xml
│   │   │   └── workbench.svg
│   │   └── Forge/
│   │       └── ...
│   ├── furniture/
│   │   ├── Bed/
│   │   └── Chair/
│   └── storage/
│       ├── Chest/
│       └── Barrel/
│
├── items/                           # Inventory items
│   ├── tools/
│   │   ├── Axe/
│   │   │   ├── Axe.xml
│   │   │   └── axe.svg
│   │   └── Pickaxe/
│   ├── weapons/
│   │   ├── Sword/
│   │   └── Bow/
│   └── resources/
│       ├── Wood/
│       └── Stone/
│
└── ui/                              # UI assets
    ├── icons/
    │   └── HealthIcon/
    └── cursors/
        └── SelectCursor/
```

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Asset folder | PascalCase, matches defName | `MapleTree/` |
| Primary XML | `<FolderName>.xml` | `MapleTree.xml` |
| Primary generator | `generate.lua` (standardized) | `generate.lua` |
| SVG files | snake_case, descriptive | `maple_leaf.svg` |
| Helper scripts | snake_case, descriptive | `branch_helper.lua` |
| Categories | lowercase | `flora/`, `terrain/` |

### The Primary XML File

Each asset folder MUST contain exactly one XML file matching the folder name. This is the **entry point** for the asset and contains the complete definition.

```xml
<!-- /assets/flora/MapleTree/MapleTree.xml -->
<?xml version="1.0" encoding="UTF-8"?>
<AssetDef>
  <defName>Flora_MapleTree</defName>
  <label>Maple Tree</label>
  <description>A deciduous tree with distinctive lobed leaves</description>

  <assetType>procedural</assetType>

  <!-- Paths are RELATIVE to this asset folder -->
  <generator>
    <script>generate.lua</script>           <!-- ./generate.lua -->
    <params>
      <trunkHeight>1.2</trunkHeight>
      <canopyRadius>0.6</canopyRadius>
      <leafSvg>maple_leaf.svg</leafSvg>     <!-- ./maple_leaf.svg -->
    </params>
  </generator>

  <!-- Rendering, placement, animation as before... -->
</AssetDef>
```

**Key Change**: All paths in the XML are **relative to the asset folder**, not the project root.

## Asset Types

### Type 1: Simple Assets (SVG-Based)

Simple assets are hand-authored SVG files tessellated once at load time.

```
flora/Daisy/
├── Daisy.xml
├── flower.svg          # Primary SVG
└── stem.svg           # Optional additional components
```

**Definition:**
```xml
<AssetDef>
  <defName>Flora_Daisy</defName>
  <label>Daisy</label>
  <assetType>simple</assetType>

  <!-- SVG path relative to asset folder -->
  <svgPath>flower.svg</svgPath>
  <worldHeight>0.3</worldHeight>

  <rendering>
    <complexity>simple</complexity>
    <tier>instanced</tier>
  </rendering>

  <!-- ... placement, animation ... -->
</AssetDef>
```

### Type 2: Procedural Assets (Lua-Based)

Procedural assets use Lua scripts to generate geometry at load time.

```
flora/MapleTree/
├── MapleTree.xml
├── generate.lua        # Main generator script
├── maple_leaf.svg      # SVG components used by generator
└── branch_helper.lua   # Optional asset-specific utilities
```

**Definition:**
```xml
<AssetDef>
  <defName>Flora_MapleTree</defName>
  <label>Maple Tree</label>
  <assetType>procedural</assetType>

  <generator>
    <script>generate.lua</script>
    <params>
      <trunkHeight>1.2</trunkHeight>
      <canopyRadius>0.6</canopyRadius>
      <leafSvg>maple_leaf.svg</leafSvg>
    </params>
  </generator>

  <variantCount>20</variantCount>

  <!-- ... rendering, placement ... -->
</AssetDef>
```

**Generator Script:**
```lua
-- deciduous.lua (in assets/shared/scripts/)
-- This script is referenced from XML via: <scriptPath>@shared/deciduous.lua</scriptPath>
-- The C++ AssetRegistry resolves the @shared/ prefix before executing

-- Get parameters from XML
local trunkHeight = getFloat("trunkHeight", 1.2)
local canopyRadius = getFloat("canopyRadius", 0.6)

-- Generate the tree...
local asset = Asset.new()

-- ... generation logic using params ...

return asset
```

> **Note:** The original proposal showed Lua using `require("branch_utils")` to load shared modules. In the implemented system, script selection happens at the XML level with `@shared/` prefix—the Lua script itself doesn't need to know about shared vs local paths.

### Type 3: Hybrid Assets (SVG + Lua Variation)

Some assets combine an SVG base with Lua-driven variation.

```
flora/BerryBush/
├── BerryBush.xml
├── bush_base.svg       # Base SVG shape
├── berry.svg           # Berry component
└── vary.lua            # Adds berries procedurally
```

**Definition:**
```xml
<AssetDef>
  <defName>Flora_BerryBush</defName>
  <assetType>hybrid</assetType>

  <baseSvg>bush_base.svg</baseSvg>

  <generator>
    <script>vary.lua</script>
    <params>
      <berryCount>5,15</berryCount>
      <berrySvg>berry.svg</berrySvg>
    </params>
  </generator>
</AssetDef>
```

### Type 4: Multi-Variant Simple Assets

A single asset definition with multiple SVG variants (selected randomly or by parameter).

```
flora/Mushroom/
├── Mushroom.xml
├── variants/
│   ├── red_cap.svg
│   ├── brown_cap.svg
│   └── spotted_cap.svg
└── stem.svg
```

**Definition:**
```xml
<AssetDef>
  <defName>Flora_Mushroom</defName>
  <assetType>simple</assetType>

  <components>
    <component name="stem">stem.svg</component>
    <component name="cap" variant="random">
      <variant>variants/red_cap.svg</variant>
      <variant>variants/brown_cap.svg</variant>
      <variant>variants/spotted_cap.svg</variant>
    </component>
  </components>
</AssetDef>
```

## Shared Resources

### The `shared/` Directory

Shared utilities and components live in `assets/shared/`.

```
assets/shared/
├── scripts/                 # Shared script modules
│   ├── deciduous.lua       # Shared tree generator
│   ├── branch_utils.lua    # Branch generation utilities
│   └── noise.lua           # Noise functions
│
└── components/              # Shared SVG components (future)
    ├── leaf_shapes.svg     # Common leaf templates
    └── bark_patterns.svg   # Bark pattern library
```

### The `@shared/` Prefix

Shared scripts are referenced using the **`@shared/` prefix** in XML definitions. The C++ AssetRegistry resolves these paths at load time.

> **Implementation Note:** The original proposal called for a "Module Registry" where Lua's `require()` would be extended to resolve shared modules by name (e.g., `require("branch_utils")` finding `shared/scripts/branch_utils.lua`). We implemented a simpler approach instead:
>
> - **Proposed**: Lua-level module registry with custom `require()` resolver
> - **Implemented**: XML-level `@shared/` prefix resolved by C++ before Lua execution
>
> The `@shared/` prefix approach is simpler because:
> 1. No Lua environment customization needed
> 2. Path resolution happens in one place (AssetRegistry)
> 3. XML definitions are explicit about where scripts come from
> 4. Easier to debug—paths are visible in the definition files

**How it works:**
1. XML uses `<scriptPath>@shared/deciduous.lua</scriptPath>`
2. AssetRegistry recognizes the `@shared/` prefix
3. Path is resolved to `assets/shared/scripts/deciduous.lua`

**In XML definitions:**
```xml
<!-- MapleTree.xml -->
<AssetDef>
  <defName>Flora_TreeMaple</defName>
  <assetType>procedural</assetType>

  <!-- @shared/ prefix resolves to assets/shared/scripts/ -->
  <scriptPath>@shared/deciduous.lua</scriptPath>

  <!-- Local paths (relative to asset folder) -->
  <svgPath>maple_leaf.svg</svgPath>
</AssetDef>
```

**Path resolution in C++:**
```cpp
// In AssetRegistry::generateAsset()
const std::string kSharedPrefix = "@shared/";
if (def->scriptPath.compare(0, kSharedPrefix.size(), kSharedPrefix) == 0) {
    // Strip prefix, resolve from shared scripts path
    std::string relativePath = def->scriptPath.substr(kSharedPrefix.size());
    resolvedScriptPath = (m_sharedScriptsPath / relativePath).string();
} else {
    // Local path - resolve from asset's baseFolder
    resolvedScriptPath = def->resolvePath(def->scriptPath).string();
}
```

**Resolution rules:**
- **`@shared/foo.lua`**: Resolves to `assets/shared/scripts/foo.lua`
- **`foo.lua`** (no prefix): Resolves relative to the asset folder

### Referencing Shared SVG Components (Future)

For shared SVG components, the `@components/` prefix is planned but not yet implemented:

**Planned XML syntax:**
```xml
<!-- Local SVG (relative to asset folder) - IMPLEMENTED -->
<svgPath>blade.svg</svgPath>

<!-- Shared component - PLANNED -->
<leafTemplate>@components/leaf_shapes</leafTemplate>
```

### Asset-Local vs Shared

| Use Case | Location | XML Reference | Status |
|----------|----------|---------------|--------|
| Asset's own SVG | Asset folder | `<svgPath>blade.svg</svgPath>` | ✅ Implemented |
| Shared script | `shared/scripts/` | `<scriptPath>@shared/deciduous.lua</scriptPath>` | ✅ Implemented |
| Shared SVG component | `shared/components/` | `<template>@components/leaf</template>` | 🔮 Planned |

## Path Resolution

### Resolution Rules (Phase 1 - Implemented)

**XML `<scriptPath>`:**
1. `@shared/foo.lua` → `assets/shared/scripts/foo.lua`
2. `foo.lua` (no prefix) → `<asset_folder>/foo.lua`

**XML `<svgPath>`:**
1. Relative path → `<asset_folder>/<path>`

### In XML Definitions

```xml
<!-- Local paths (relative to asset folder) - IMPLEMENTED -->
<svgPath>blade.svg</svgPath>
<scriptPath>generate.lua</scriptPath>

<!-- Shared scripts - IMPLEMENTED -->
<scriptPath>@shared/deciduous.lua</scriptPath>

<!-- Shared components - PLANNED -->
<leafTemplate>@components/leaf_shapes</leafTemplate>

<!-- Reference another asset by defName - PLANNED -->
<basedOn>@MapleTree</basedOn>
```

### Future: Lua-Level Resolution (Not Implemented)

The original proposal included Lua-level path resolution. This is **not implemented** in Phase 1:

```lua
-- PLANNED (not yet implemented):
local branch = require("branch_utils")       -- Would find shared module
local template = loadComponent("leaf_shapes") -- Would find shared SVG
local oakMesh = getAssetMesh("OakTree")      -- Would get another asset's mesh
```

Currently, all path resolution happens at the XML/C++ level before scripts execute.

## Definition Inheritance

### Within-Folder Inheritance (Abstract Bases)

For complex assets with variants, use abstract bases within the same folder:

```
flora/Tree/
├── Tree.xml              # Contains abstract TreeBase + concrete variants
├── deciduous.lua
└── conifer.lua
```

```xml
<!-- Tree.xml -->
<AssetDefinitions>
  <!-- Abstract base (not instantiated) -->
  <AssetDef abstract="true" name="TreeBase">
    <assetType>procedural</assetType>
    <rendering>
      <complexity>complex</complexity>
      <tier>batched</tier>
    </rendering>
  </AssetDef>

  <!-- Concrete variants inheriting from base -->
  <AssetDef parent="TreeBase">
    <defName>Flora_TreeMaple</defName>
    <generator>
      <script>deciduous.lua</script>
      <params>...</params>
    </generator>
  </AssetDef>

  <AssetDef parent="TreeBase">
    <defName>Flora_TreePine</defName>
    <generator>
      <script>conifer.lua</script>
      <params>...</params>
    </generator>
  </AssetDef>
</AssetDefinitions>
```

### Cross-Asset Inheritance

For inheriting from other assets, reference by **defName** (not path):

```xml
<!-- world/flora/RedMaple/RedMaple.xml -->
<AssetDef parent="@MapleTree">
  <defName>RedMaple</defName>
  <!-- Override just the color params -->
  <generator>
    <params>
      <leafColor>#8B0000</leafColor>
    </params>
  </generator>
</AssetDef>
```

The `@` prefix indicates a reference to another asset by defName. The engine resolves the location.

## Asset Discovery & Loading

### Loading Process

```
1. Scan assets/ recursively for directories
2. For each directory containing <FolderName>.xml:
   a. Mark as asset folder
   b. Parse XML definition(s)
   c. Resolve relative paths to absolute
   d. Register in AssetRegistry
3. Skip directories starting with underscore (_shared, _cache, etc.)
4. Process inheritance (parent references)
5. Validate all definitions
6. Pre-generate procedural assets
```

### Asset Registry API

```cpp
class AssetRegistry {
public:
  // Load all assets from folder structure
  size_t loadAssetsFromFolder(const std::string& assetsRoot);

  // Get asset by defName
  const AssetDefinition* getDefinition(const std::string& defName) const;

  // Get asset folder path (for resolving relative paths)
  std::filesystem::path getAssetFolder(const std::string& defName) const;

  // Resolve path relative to asset folder
  std::filesystem::path resolvePath(const std::string& defName,
                                     const std::string& relativePath) const;
};
```

### Loading Context

When loading an asset, the engine creates a **loading context** that knows the asset's folder:

```cpp
struct AssetLoadContext {
  std::filesystem::path assetFolder;  // e.g., "assets/flora/MapleTree/"
  std::filesystem::path sharedFolder; // e.g., "assets/_shared/"
  std::filesystem::path assetsRoot;   // e.g., "assets/"

  std::filesystem::path resolve(const std::string& path) const {
    if (path.starts_with("_shared/")) {
      return sharedFolder / path.substr(8);
    }
    if (path.find('/') != std::string::npos && !path.starts_with("./")) {
      // Category path like "flora/OakTree/leaf.svg"
      return assetsRoot / path;
    }
    // Relative to asset folder
    return assetFolder / path;
  }
};
```

## Modding Support

### Mod Structure

Mods follow the same folder structure:

```
mods/
└── MyTreeMod/
    ├── Mod.xml                     # Mod metadata (required)
    ├── assets/
    │   ├── shared/
    │   │   └── scripts/
    │   │       └── my_branch_algo.lua
    │   │
    │   └── world/flora/
    │       └── ExoticPalm/
    │           ├── ExoticPalm.xml
    │           ├── generate.lua
    │           └── palm_frond.svg
    │
    └── patches/                    # Patches to existing assets
        └── tree_tweaks.xml
```

**For mod metadata and patching specifications**, see:
- [Mod Metadata](./mod-metadata.md) - Mod.xml specification
- [Patching System](./patching-system.md) - XPath-based modification

### Load Order

```
1. Core: assets/                    # Base game assets
2. Mods: mods/*/assets/             # Mod assets (alphabetical by mod name)

Later-loaded definitions with same defName override earlier ones.
```

### Overriding Core Assets

To override a core asset, create the same folder structure in your mod:

```
mods/BiggerTrees/assets/flora/MapleTree/
└── MapleTree.xml    # Overrides core MapleTree with same defName
```

```xml
<!-- Just override what you need -->
<AssetDef override="true">
  <defName>Flora_MapleTree</defName>
  <generator>
    <params>
      <trunkHeight>2.5</trunkHeight>  <!-- Bigger! -->
    </params>
  </generator>
</AssetDef>
```

## Migration Path

### From Current to Folder-Based

**Phase 1: Structure Migration** ✅ COMPLETE
1. ✅ Create 3-level folder hierarchy (`world/flora/GrassBlade/`)
2. ✅ Create folder for each existing defName
3. ✅ Move XML definitions into matching folders
4. ✅ Move SVG and script files into asset folders
5. ✅ Update paths in XML to be folder-relative
6. ✅ Move shared scripts to `shared/scripts/`

**Phase 2: Code Updates** ✅ COMPLETE (partial)
1. ✅ Update AssetRegistry to scan 3-level folder structure
2. ✅ Implement `@shared/` prefix resolution for shared scripts
3. ⏳ Implement `@` reference resolution for cross-asset references (planned)
4. ⏳ Add `loadComponent()` API for shared SVG components (planned)

> **Note:** We implemented `@shared/` prefix resolution in C++/XML instead of the originally planned Lua-level `require()` module registry. See the "Implementation Note" in the Shared Resources section for rationale.

**Phase 3: Validation** ✅ COMPLETE
1. ✅ All existing assets load correctly
2. ✅ All tests pass
3. ✅ Performance unchanged

### Example Migration

**Before:**
```
assets/
├── definitions/flora/grass.xml     # <svgPath>assets/svg/grass_blade.svg</svgPath>
├── definitions/flora/trees.xml     # <scriptPath>assets/generators/trees/deciduous.lua</scriptPath>
├── svg/grass_blade.svg
└── generators/trees/deciduous.lua
```

**After:**
```
assets/
├── shared/
│   └── scripts/
│       └── deciduous.lua           # Referenced via @shared/deciduous.lua
│
└── world/flora/
    ├── GrassBlade/
    │   ├── GrassBlade.xml          # <svgPath>blade.svg</svgPath>
    │   └── blade.svg
    │
    ├── MapleTree/
    │   └── MapleTree.xml           # <scriptPath>@shared/deciduous.lua</scriptPath>
    │
    └── OakTree/
        └── OakTree.xml             # <scriptPath>@shared/deciduous.lua</scriptPath>
```

## Open Questions

1. **Multiple defs per folder?** Should we allow multiple AssetDefs in one XML file for related variants, or enforce strict one-def-per-folder?
   - **Proposal**: Allow multiple for closely related variants (e.g., MapleTree, OakTree in Trees folder), but defName must be unique globally.

2. **Folder naming**: Should folder name match defName exactly?
   - **Proposal**: Folder name IS the defName. No prefixes. `MapleTree` folder = `MapleTree` defName.

3. **Cache location**: Where do pre-generated variant caches live?
   - **Proposal**: `assets/.cache/<defName>/` (dotfile to hide from normal browsing)

4. **Hot-reload scope**: When an asset folder changes, what gets reloaded?
   - **Proposal**: Reload the entire asset folder (definition + all resources).

5. **Subcategory depth**: Is `world/flora/trees/MapleTree/` (4 levels) ever needed, or stick to 3?
   - **Proposal**: Stick to 3 levels max. Use folder name for specificity (`MapleTree` not `trees/Maple`).

## Related Documents

- [Asset System Architecture](./README.md) - System overview
- [Asset Definition Schema](./asset-definitions.md) - XML schema details
- [Scripting API](./lua-scripting-api.md) - Generator script API
- [Patching System](./patching-system.md) - XPath-based modification
- [Mod Metadata](./mod-metadata.md) - Mod.xml specification
- [Entity Placement System](../entity-placement-system.md) - Placement rules
