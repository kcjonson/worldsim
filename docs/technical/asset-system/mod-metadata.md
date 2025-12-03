# Mod Metadata Specification

**Status**: Design Phase
**Created**: 2025-12-03
**Last Updated**: 2025-12-03

## Overview

Every mod requires a `Mod.xml` file in its root directory. This file identifies the mod to the engine and controls load order.

## File Location

```
mods/
└── MyMod/
    ├── Mod.xml               # Required metadata file
    ├── assets/               # Mod's assets (optional)
    └── patches/              # Mod's patches (optional)
```

## Minimal Mod.xml

For a simple mod, only `id` and `name` are required:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Mod>
  <id>myname.mymod</id>
  <name>My Mod</name>
</Mod>
```

## Full Schema

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Mod>
  <id>myname.mymod</id>
  <name>My Awesome Mod</name>
  <version>1.0.0</version>
  <author>My Name</author>
  <description>A short description of what this mod does.</description>

  <gameVersion>>=1.0.0</gameVersion>

  <loadAfter>
    <li>core</li>
    <li>otherauthor.dependency</li>
  </loadAfter>

  <loadBefore>
    <li>someauthor.shouldloadlater</li>
  </loadBefore>

  <preview>preview.png</preview>
  <icon>icon.png</icon>
</Mod>
```

## Field Reference

### Required Fields

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Unique identifier. Format: `author.modname` (lowercase, no spaces) |
| `name` | string | Human-readable display name |

### Optional Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `version` | string | `"1.0.0"` | Semantic version (major.minor.patch) |
| `author` | string | `""` | Author name for display |
| `description` | string | `""` | Short description |
| `gameVersion` | string | `"*"` | Required game version (semver range) |
| `loadAfter` | string[] | `["core"]` | Mods that must load before this one |
| `loadBefore` | string[] | `[]` | Mods that must load after this one |
| `preview` | string | `null` | Path to preview image (relative to mod folder) |
| `icon` | string | `null` | Path to icon image (relative to mod folder) |

## ID Format

The `id` field must follow this format:
- Lowercase letters, numbers, dots, and underscores only
- Must contain exactly one dot separating author from mod name
- No spaces or special characters

**Valid IDs:**
- `johnsmith.bigtrees`
- `studio123.enhanced_flora`
- `myname.mod_v2`

**Invalid IDs:**
- `BigTrees` (no author prefix)
- `john smith.big trees` (spaces not allowed)
- `john.smith.bigtrees` (too many dots)

## Load Order

### The `loadAfter` Element

Lists mod IDs that must be loaded before this mod. The engine ensures these mods load first.

```xml
<Mod>
  <id>myname.treepatch</id>
  <loadAfter>
    <li>core</li>
    <li>otherauthor.bigtrees</li>
  </loadAfter>
</Mod>
```

**Special value: `core`**
- Refers to the base game
- Most mods should include `core` in loadAfter
- Default if loadAfter is omitted

### The `loadBefore` Element

Lists mod IDs that should load after this mod.

```xml
<Mod>
  <id>myname.framework</id>
  <loadBefore>
    <li>*</li>
  </loadBefore>
</Mod>
```

**Special value: `*`**
- Means "before all other mods"
- Use sparingly (framework mods only)

### Load Order Resolution

1. Parse all Mod.xml files
2. Build dependency graph from loadAfter/loadBefore
3. Topological sort for final order
4. Alphabetical order for mods with no ordering constraints
5. Error if circular dependency detected

## Game Version Compatibility

The `gameVersion` element specifies which game versions the mod supports:

```xml
<Mod>
  <gameVersion>>=1.0.0</gameVersion>
</Mod>
```

### Version Range Syntax

| Pattern | Meaning |
|---------|---------|
| `1.0.0` | Exactly version 1.0.0 |
| `>=1.0.0` | Version 1.0.0 or higher |
| `>=1.0.0 <2.0.0` | At least 1.0.0, below 2.0.0 |
| `1.x` | Any 1.x version |
| `*` | Any version (default) |

### Behavior

- If game version doesn't match, mod is disabled with warning
- Can be overridden with `--force-mods` launch flag

## Preview and Icon Images

For mod browsers and UI:

| Field | Purpose | Recommended Size |
|-------|---------|------------------|
| `preview` | Large preview image | 640x360 PNG |
| `icon` | Small icon | 64x64 PNG |

```xml
<Mod>
  <preview>images/preview.png</preview>
  <icon>images/icon.png</icon>
</Mod>
```

## Examples

### Simple Content Mod

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Mod>
  <id>naturelover.exoticflora</id>
  <name>Exotic Flora</name>
  <version>1.0.0</version>
  <author>NatureLover</author>
  <description>Adds 20 new exotic plants to the world.</description>
</Mod>
```

### Patch Mod

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Mod>
  <id>tweaker.biggertrees</id>
  <name>Bigger Trees</name>
  <version>1.2.0</version>
  <author>Tweaker</author>
  <description>Makes all trees 50% taller.</description>
  <loadAfter>
    <li>core</li>
  </loadAfter>
</Mod>
```

### Compatibility Patch

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Mod>
  <id>helper.seasoncompat</id>
  <name>Seasons Compatibility</name>
  <version>1.0.0</version>
  <description>Makes Exotic Flora work with Seasons mod.</description>
  <loadAfter>
    <li>core</li>
    <li>naturelover.exoticflora</li>
    <li>otherdev.seasons</li>
  </loadAfter>
</Mod>
```

### Framework Mod

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Mod>
  <id>modder.framework</id>
  <name>Modder's Framework</name>
  <version>2.0.0</version>
  <description>Common utilities for other mods.</description>
  <loadAfter>
    <li>core</li>
  </loadAfter>
  <loadBefore>
    <li>*</li>
  </loadBefore>
</Mod>
```

## Mod Folder Structure

Complete mod folder structure:

```
mods/
└── myname.mymod/
    ├── Mod.xml                     # Required
    │
    ├── assets/                     # New assets (optional)
    │   ├── shared/
    │   │   └── scripts/
    │   │       └── my_utils.lua
    │   └── world/
    │       └── flora/
    │           └── ExoticPalm/
    │               ├── ExoticPalm.xml
    │               └── generate.lua
    │
    ├── patches/                    # Patches (optional)
    │   └── tree_tweaks.xml
    │
    └── images/                     # Presentation (optional)
        ├── preview.png
        └── icon.png
```

## Error Handling

### Missing Mod.xml

Folder without Mod.xml is ignored with warning:
```
[Mod] Warning: mods/SomeFolder has no Mod.xml, skipping
```

### Invalid XML

Mod disabled with error:
```
[Mod] Error: mods/broken.mod/Mod.xml - Parse error at line 5
```

### Missing Dependency

Mod disabled with error listing missing dependencies:
```
[Mod] Error: myname.mymod requires otherauthor.framework which is not installed
```

### Circular Dependency

All mods in cycle disabled:
```
[Mod] Error: Circular dependency detected: mod.a -> mod.b -> mod.a
```

## Related Documents

- [Asset System Architecture](./README.md) - System overview
- [Folder-Based Assets](./folder-based-assets.md) - Asset folder structure
- [Patching System](./patching-system.md) - XPath patch operations
