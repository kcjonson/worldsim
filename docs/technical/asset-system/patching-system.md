# Asset Patching System

**Status**: Design Phase
**Created**: 2025-12-03
**Last Updated**: 2025-12-03

## Overview

The patching system allows mods to surgically modify existing asset definitions without full replacement. This is critical for mod compatibility - multiple mods can modify different parts of the same asset without conflict.

**Key Insight**: Without patching, if two mods both want to modify OakTree, whichever loads last wins and the other mod's changes are lost. With patching, both mods can coexist.

## Design Goals

1. **Non-Destructive**: Patches modify in-memory definitions, not source files
2. **Composable**: Multiple patches can modify the same asset
3. **Ordered**: Patches apply in a deterministic order based on mod load order
4. **Conditional**: Patches can require other mods to be present
5. **Debuggable**: Easy to see which patches affected an asset

## Patch File Location

Patches live in a `patches/` folder, separate from asset definitions:

```
assets/
├── world/flora/OakTree/
│   └── OakTree.xml           # Base definition
│
└── patches/                   # Core game patches (if any)
    └── balance_tweaks.xml

mods/MyMod/
├── mod.json                   # Mod metadata
├── assets/
│   └── world/flora/ExoticPalm/
│       └── ExoticPalm.xml    # New asset
│
└── patches/                   # Mod's patches
    └── oak_changes.xml        # Modifies core OakTree
```

## Patch File Format

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Patches>
  <!-- Each <Patch> is one operation -->
  <Patch>
    <operation>replace</operation>
    <xpath>AssetDef[defName="OakTree"]/generator/params/trunkHeight</xpath>
    <value>
      <trunkHeight>2.5</trunkHeight>
    </value>
  </Patch>
</Patches>
```

### Required Elements

| Element | Description |
|---------|-------------|
| `<operation>` | The type of modification (see Operations below) |
| `<xpath>` | XPath expression selecting the target node(s) |
| `<value>` | The new content (not required for `remove`) |

### Optional Elements

| Element | Description |
|---------|-------------|
| `<requiresMod>` | Only apply if specified mod is loaded |
| `<requiresNotMod>` | Only apply if specified mod is NOT loaded |

## Operations

### `replace`

Replaces the matched node with new content.

```xml
<Patch>
  <operation>replace</operation>
  <xpath>AssetDef[defName="OakTree"]/generator/params/trunkHeight</xpath>
  <value>
    <trunkHeight>2.5</trunkHeight>
  </value>
</Patch>
```

**Before:**
```xml
<trunkHeight>1.2</trunkHeight>
```

**After:**
```xml
<trunkHeight>2.5</trunkHeight>
```

### `add`

Adds new child element(s) to the matched node.

```xml
<Patch>
  <operation>add</operation>
  <xpath>AssetDef[defName="OakTree"]/placement/groups</xpath>
  <value>
    <group>shade_trees</group>
  </value>
</Patch>
```

**Before:**
```xml
<groups>
  <group>trees</group>
</groups>
```

**After:**
```xml
<groups>
  <group>trees</group>
  <group>shade_trees</group>
</groups>
```

### `remove`

Deletes the matched node. No `<value>` required.

```xml
<Patch>
  <operation>remove</operation>
  <xpath>AssetDef[defName="OakTree"]/animation</xpath>
</Patch>
```

### `addOrReplace`

Adds the element if it doesn't exist, replaces it if it does. Useful when you don't know if another mod already added the element.

```xml
<Patch>
  <operation>addOrReplace</operation>
  <xpath>AssetDef[defName="OakTree"]/generator/params</xpath>
  <value>
    <customParam>myValue</customParam>
  </value>
</Patch>
```

### `insertBefore`

Inserts new content before the matched node (as a sibling).

```xml
<Patch>
  <operation>insertBefore</operation>
  <xpath>AssetDef[defName="OakTree"]/placement/groups/group[.="trees"]</xpath>
  <value>
    <group>priority_trees</group>
  </value>
</Patch>
```

### `insertAfter`

Inserts new content after the matched node (as a sibling).

```xml
<Patch>
  <operation>insertAfter</operation>
  <xpath>AssetDef[defName="OakTree"]/placement/groups/group[.="trees"]</xpath>
  <value>
    <group>deciduous</group>
  </value>
</Patch>
```

## XPath Syntax

Patches use XPath 1.0 expressions to select nodes. Common patterns:

| Pattern | Selects |
|---------|---------|
| `AssetDef[defName="OakTree"]` | The OakTree asset definition |
| `AssetDef[defName="OakTree"]/generator/params/trunkHeight` | The trunkHeight param |
| `AssetDef/placement/groups/group[.="trees"]` | Any group element with text "trees" |
| `AssetDef[@abstract="true"]` | All abstract asset definitions |
| `AssetDef[generator/script]` | All procedural assets (have a script) |
| `//group[.="flowers"]` | Any group element with text "flowers" anywhere |

### Attribute Selection

```xml
<!-- Select by attribute -->
<xpath>AssetDef[@parent="TreeBase"]</xpath>

<!-- Modify an attribute -->
<Patch>
  <operation>replace</operation>
  <xpath>AssetDef[defName="OakTree"]/@abstract</xpath>
  <value abstract="false"/>
</Patch>
```

## Conditional Patches

### Require Another Mod

Only apply the patch if a specific mod is loaded:

```xml
<Patch>
  <requiresMod>otherauthor.seasonmod</requiresMod>
  <operation>add</operation>
  <xpath>AssetDef[defName="OakTree"]/generator/params</xpath>
  <value>
    <seasonalLeaves>true</seasonalLeaves>
  </value>
</Patch>
```

### Require Mod NOT Present

Only apply if a mod is NOT loaded (for compatibility patches):

```xml
<Patch>
  <requiresNotMod>conflicting.mod</requiresNotMod>
  <operation>replace</operation>
  <xpath>AssetDef[defName="OakTree"]/generator/script</xpath>
  <value>
    <script>generate.lua</script>
  </value>
</Patch>
```

## Load Order

### Phase 1: Load Definitions

```
1. Core assets (assets/**/*)
2. Mod assets (mods/*/assets/**/*) in mod load order
```

At this point, all asset definitions exist but patches haven't been applied.

### Phase 2: Apply Patches

```
1. Core patches (assets/patches/*.xml)
2. Mod patches (mods/*/patches/*.xml) in mod load order
```

Within each source, patch files are processed alphabetically.

### Phase 3: Finalization

```
1. Resolve inheritance (parent references)
2. Validate all definitions
3. Pre-generate procedural assets
```

### Mod Load Order

Mod load order is determined by:
1. `loadAfter` declarations in mod.json
2. Alphabetical order (fallback)

```json
{
  "id": "myname.treepatch",
  "loadAfter": ["core", "othermod.bigtrees"]
}
```

## Error Handling

### XPath Matches Nothing

If an xpath doesn't match any nodes:
- **Default**: Warning logged, patch skipped
- Patches can specify `required="true"` to make this an error

```xml
<Patch required="true">
  <operation>replace</operation>
  <xpath>AssetDef[defName="NonExistent"]/something</xpath>
  <value>...</value>
</Patch>
```

### XPath Matches Multiple Nodes

For `replace` and `remove`, if xpath matches multiple nodes:
- All matched nodes are affected
- This is intentional for bulk operations

```xml
<!-- Remove animation from ALL trees -->
<Patch>
  <operation>remove</operation>
  <xpath>AssetDef[placement/groups/group="trees"]/animation</xpath>
</Patch>
```

### Duplicate Additions

If `add` would create a duplicate entry:
- Warning logged
- Addition proceeds (may cause validation error later)

Use `addOrReplace` to avoid duplicates.

## Debugging Patches

### Patch Log

When loading with debug mode, the engine logs:
```
[Patch] Applying patches/oak_changes.xml
[Patch]   replace AssetDef[defName="OakTree"]/generator/params/trunkHeight
[Patch]   add AssetDef[defName="OakTree"]/placement/groups
[Patch] Skipping patch (requiresMod not loaded: seasons.mod)
```

### Inspecting Final Definitions

Debug command to dump the final (patched) definition:
```
/asset dump OakTree
```

Shows the merged result of base definition + all patches.

## Examples

### Make All Trees Taller

```xml
<Patches>
  <Patch>
    <operation>replace</operation>
    <xpath>AssetDef[placement/groups/group="trees"]/generator/params/trunkHeightRange</xpath>
    <value>
      <trunkHeightRange>2.0,3.0</trunkHeightRange>
    </value>
  </Patch>
</Patches>
```

### Add Custom Behavior to Specific Asset

```xml
<Patches>
  <Patch>
    <operation>add</operation>
    <xpath>AssetDef[defName="OakTree"]/generator/params</xpath>
    <value>
      <myModFeature>enabled</myModFeature>
      <myModIntensity>0.5</myModIntensity>
    </value>
  </Patch>
</Patches>
```

### Compatibility Patch for Another Mod

```xml
<Patches>
  <!-- Only apply if BigTrees mod is present -->
  <Patch>
    <requiresMod>someone.bigtrees</requiresMod>
    <operation>replace</operation>
    <xpath>AssetDef[defName="OakTree"]/rendering/lodLevels/li[@distance="100"]/@distance</xpath>
    <value distance="200"/>
  </Patch>
</Patches>
```

### Remove Feature From Multiple Assets

```xml
<Patches>
  <!-- Disable wind animation on all flora -->
  <Patch>
    <operation>remove</operation>
    <xpath>AssetDef[placement/biome]/animation[windResponse]</xpath>
  </Patch>
</Patches>
```

## Implementation Notes

### C++ Considerations

- Use a proper XPath library (pugixml has XPath support built-in)
- Cache compiled xpath expressions for performance
- Patches modify in-memory DOM, not source files
- Consider storing patch provenance for debugging

### Performance

- Patches only run once at load time
- XPath compilation is the expensive part; cache expressions
- For large modpacks, consider parallel patch application per-asset

## Related Documents

- [Asset System Architecture](./README.md) - System overview
- [Folder-Based Assets](./folder-based-assets.md) - Folder structure
- [Asset Definition Schema](./asset-definitions.md) - XML format
- [Mod Metadata](./mod-metadata.md) - mod.json specification
