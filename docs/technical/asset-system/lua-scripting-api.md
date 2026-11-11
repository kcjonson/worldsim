# Scripting API for Procedural Asset Generation

**Status**: Design Phase
**Created**: 2025-11-30
**Last Updated**: 2025-12-03

## Overview

Procedural assets use scripts to generate vector graphics at load time. This document defines the API available to generator scripts.

**Note**: The current implementation uses Lua, but the scripting API is designed to be language-agnostic. References to "scripts" rather than "Lua" allow for potential future scripting language changes.

## Why Lua (Current Implementation)?

1. **Embeddable**: ~200KB footprint, easy C++ integration via sol2
2. **Sandboxed**: Can restrict file/network access for mod safety
3. **Fast**: LuaJIT achieves near-C performance for numerical code
4. **Moddable**: Modders can write generators without compiling C++
5. **Industry Standard**: Used by Factorio, Roblox, World of Warcraft

## Script Location

Scripts live inside asset folders:

```
assets/world/flora/MapleTree/
├── MapleTree.xml       # Definition references generate.lua
├── generate.lua        # Primary generator script
├── helper.lua          # Asset-local helper (optional)
└── maple_leaf.svg      # SVG component used by script
```

## Prelude

Each generator runs in its own sandboxed Lua state, and `require`, `dofile`, `loadfile` and `package` are nil. Shared helpers come from the prelude instead: every `.lua` file directly in `assets/shared/scripts/lib/` runs in the generator's state before the generator script, sorted by file name. The engine loads it, not Lua, so the sandbox is unchanged. A file can use what the files sorted before it defined at load time; at call time it can use all of them.

A prelude folder that does not exist is no prelude. A prelude file that cannot be read or fails to run, or a folder that cannot be listed (or is a file), fails the generator with the path named in the error; the bake cache declines to cache it. It must not draw random numbers when it loads, since the generator's stream is already seeded.

Everything lives in one global table, `lib`, with one sub-table per file:

| File | Table | Holds |
|------|-------|-------|
| `num.lua` | `lib.num` | `clamp` (doubles; the global `clamp` the engine binds is a float one), `smoothstep`, `curve` (a monotone cubic through points, straight on past the ends), `curveParam` (a curve param written `x:y, x:y`) |
| `random.lua` | `lib.random` | `uniform`, `signed`, `gauss`, `lognormal`, `stream(id)` (reseeds `math.random` from the variant's seed and an id, one stream per stage) |
| `color.lua` | `lib.color` | `parseHex`, `hex`, `toLinear`, `toSrgb`, `luminance`, `shade`, `mixLinear`, `mixSrgb`, `toHsv`, `fromHsv`, `tone` |
| `shape.lua` | `lib.shape` | `polygon`, `stripSides`, `stripPart`, `strip` (a tapered strip along a polyline), `upFan` (a grass tuft), `up`, `lobed` (a lobed blob), `emit` (adds a polygon to the asset) |
| `foot.lua` | `lib.foot` | `contactShadow` (where a plant meets the ground) |
| `bark.lua` | `lib.bark` | `texturedTrunk` |

A script that calls a helper often can alias it once at the top (`local clamp = lib.num.clamp`). Moving a helper into the prelude must not change what a generator draws, so keep its arithmetic and its random draw order as they were.

The prelude is part of the ground tile bake cache key: the names and bytes of every file in `lib/` are hashed with the generator script, so editing a helper re-bakes the defs that run. A generator is re-run from the files on disk each time, so the asset manager's Reload picks up an edited helper wherever it runs a generator again.

## Globals and the seed

The engine sets these globals before the prelude runs:

| Global | What it holds |
|--------|---------------|
| `seed` | The generation seed. `math.random` is seeded from it alone, so it is already different for every variant. |
| `variantIndex` | Which variant of its age class is being drawn, from 0. Set for every template the game draws (`AssetRegistry::templateId`). Generators drawn by seed alone (ground tiles, groundcover, `asset-cli render --seed`) see 0. |
| `ageClass` | The age class name (`"sapling"`, `"mature"`, `"veteran"`) for a def with `<ageClasses>`, and nil for any other def. |
| `period` | A ground def's `<period>` in metres; nil for other roles. |
| `detailPxPerMeter` | The screen density, pixels a metre, of the `<detailLevels>` level being built ([asset-definitions.md](./asset-definitions.md#detail-levels)); nil for a def without levels. A seed-only build (`asset-cli render --seed`, the asset-manager previews) gets the reference level's. |

A template's seed comes from `AssetRegistry::generationSeed`: 0 for a def with a single variant, otherwise the hash ground tiles use (`assets::variantSeed`, `VariantSeed.h`) of the def name and age class name (`Flora_TreeOak:mature`), at the variant index. A def without age classes hashes its name alone. The seed is a pure function of (def, age class, variant), so a variant draws the same mesh every load.

## Script Structure

Every generator script must define a `generate` function:

```lua
-- assets/world/flora/MapleTree/generate.lua

-- Shared helpers come from the prelude (lib.*); require is not available

-- Called once per variant during pre-generation
-- @param params  Table of parameters from asset definition
-- @param rng     Seeded random number generator
-- @return        VectorAsset or nil on failure
function generate(params, rng)
    local asset = VectorAsset.new()

    -- Generate trunk
    local trunk = generateTrunk(params, rng)
    asset:addPath(trunk)

    -- Generate branches recursively
    generateBranches(asset, trunk, params, rng, 0)

    -- Add leaves at branch endpoints
    addLeaves(asset, params, rng)

    return asset
end

-- Helper functions (not called by engine)
function generateTrunk(params, rng)
    -- ...
end

function generateBranches(asset, parent, params, rng, depth)
    -- ...
end

function addLeaves(asset, params, rng)
    -- ...
end
```

## Core API Reference

### VectorAsset

The main container for generated vector graphics.

```lua
-- Create new empty asset
local asset = VectorAsset.new()

-- Add a path (filled shape)
asset:addPath(path)

-- Add a child asset (e.g., leaves on a tree)
-- Child inherits parent transforms
asset:addChild(childAsset)

-- Get all path objects in asset
local paths = asset:getPaths()

-- Get bounding box (calculated from all paths)
local bounds = asset:getBounds()  -- { min={x,y}, max={x,y} }

-- Clone asset for modification
local copy = asset:clone()

-- Set asset-level transform
asset:setPosition(x, y)
asset:setRotation(radians)
asset:setScale(s)      -- uniform
asset:setScale(sx, sy) -- non-uniform

-- Declare the rect collider (local meters): half width, half height, center x, center y.
-- Non-positive half-extents are ignored.
asset:setCollisionRect(halfW, halfH, cx, cy)
```

The collider is per age class: the loader keeps the rect from each class's variant 0, so a script may read `ageClass` to size it (a sapling's trunk is thinner than a veteran's). It must not depend on `variantIndex` or the seed; `asset-cli validate --render-smoke` fails a def whose rect differs between two variants of one class.

### VectorPath

A filled polygon defined by vertices and optionally Bezier curves.

```lua
-- Create path from vertices
local path = VectorPath.new()
path:moveTo(0, 0)
path:lineTo(10, 0)
path:lineTo(10, 20)
path:close()

-- Bezier curves
path:moveTo(0, 0)
path:cubicTo(cp1x, cp1y, cp2x, cp2y, endX, endY)
path:quadTo(cpX, cpY, endX, endY)

-- Get/set fill color
path:setFill(r, g, b, a)  -- 0-1 range
path:setFill("#FF5733")   -- hex string
local r, g, b, a = path:getFill()

-- Get/set stroke (outline)
path:setStroke(r, g, b, a)
path:setStrokeWidth(width)

-- Transform the path
path:translate(dx, dy)
path:rotate(radians)
path:rotateAround(radians, cx, cy)
path:scale(s)
path:scale(sx, sy)

-- Get path info
local verts = path:getVertices()  -- array of {x, y}
local bounds = path:getBounds()   -- { min={x,y}, max={x,y} }
local closed = path:isClosed()

-- Clone for modification
local copy = path:clone()
```

### Random Number Generator (RNG)

Seeded RNG for deterministic generation. The seed is the `seed` global (see "Globals and the seed").

```lua
-- Random float in range [min, max]
local val = rng:range(0.5, 1.5)

-- Random integer in range [min, max] (inclusive)
local val = rng:rangeInt(1, 10)

-- Random boolean with probability
local val = rng:chance(0.7)  -- 70% chance of true

-- Random element from array
local item = rng:pick(items)

-- Random unit vector
local dx, dy = rng:unitVector()

-- Random angle in radians
local angle = rng:angle()

-- Gaussian/normal distribution
local val = rng:gaussian(mean, stddev)

-- Perlin noise (coherent noise)
local val = rng:perlin(x, y)          -- 2D
local val = rng:perlin(x, y, z)       -- 3D
```

### Color Utilities

```lua
-- Create color
local c = Color.rgb(0.3, 0.6, 0.2)
local c = Color.rgba(0.3, 0.6, 0.2, 1.0)
local c = Color.hex("#4CAF50")
local c = Color.hsl(120, 0.5, 0.4)
local c = Color.hsla(120, 0.5, 0.4, 1.0)

-- Modify color
local darker = c:darken(0.2)
local lighter = c:lighten(0.2)
local saturated = c:saturate(0.3)
local shifted = c:hueShift(30)  -- degrees
local faded = c:withAlpha(0.5)

-- Blend colors
local mixed = Color.mix(c1, c2, 0.5)  -- 50/50 blend
local lerped = Color.lerp(c1, c2, t)  -- interpolate

-- Get components
local r, g, b, a = c:rgba()
local h, s, l, a = c:hsla()
local hex = c:toHex()
```

### Math Utilities

```lua
-- Vector operations
local v = Vec2.new(x, y)
local len = v:length()
local norm = v:normalized()
local dot = v:dot(other)
local angle = v:angle()  -- radians from +x axis
local rotated = v:rotate(radians)
local perp = v:perpendicular()

-- Bezier evaluation
local point = Bezier.cubic(t, p0, p1, p2, p3)  -- t in [0,1]
local point = Bezier.quadratic(t, p0, p1, p2)
local tangent = Bezier.cubicTangent(t, p0, p1, p2, p3)
local length = Bezier.cubicLength(p0, p1, p2, p3)

-- Interpolation
local val = Math.lerp(a, b, t)
local val = Math.smoothstep(edge0, edge1, x)
local val = Math.clamp(x, min, max)
local val = Math.remap(x, inMin, inMax, outMin, outMax)

-- Easing functions
local val = Ease.inQuad(t)
local val = Ease.outQuad(t)
local val = Ease.inOutQuad(t)
-- Also: Cubic, Quart, Quint, Sine, Expo, Circ, Back, Elastic, Bounce
```

### Asset Loading

Load SVG components from within a generator:

```lua
-- Load local SVG (from asset folder)
local leaf = loadSvg("oak_leaf.svg")

-- Load shared SVG component (by name)
local template = loadComponent("leaf_shapes")

-- Clone and modify
local myLeaf = leaf:clone()
myLeaf:setScale(rng:range(0.8, 1.2))
myLeaf:setRotation(rng:angle())

-- Add to parent asset
asset:addChild(myLeaf)
```

## Example: deciduous tree generator

`assets/shared/scripts/deciduous.lua` is the working example; oak and maple run it with different params. It reads one `age` from 0 to 1 (the `age` param when set, else the `ageClass` global mapped to an age at the top of the script), grows a branch skeleton for that long, hangs leaf clusters on the tips, and draws the crown, trunk, and ground contact through the prelude. Its shape, in brief:

```lua
local age = lib.num.clamp(getFloat("age", ({ sapling = 0.15, mature = 0.55, veteran = 0.9 })[ageClass] or 0.55), 0, 1)
local trunkWidth = lib.num.curve(lib.num.curveParam("trunkWidth", "0.15:0.18, 0.55:0.45, 0.9:0.72"), age)
asset:setCollisionRect(trunkWidth / 2, trunkWidth / 2, 0, 0) -- from the age only, never the variant

lib.random.stream(3)          -- each stage draws from its own stream
-- ... grow the skeleton, one stream per shoot, then group its tips into clusters ...

lib.random.stream(6)
lib.foot.contactShadow(trunkWidth, lib.color.hex("contactShadow", "#1d2616"), 0.18)
-- ... the wood as one shape: tapered strips along the skeleton's axes, all in one bark colour ...
lib.shape.emit(lib.shape.lobed(x, y, r, 10, 0.88, 0.1, rot, 0.88), tone) -- one leaf mass
```

## Sandbox Restrictions

For mod safety, the scripting environment is sandboxed:

### Allowed
- Math operations
- String manipulation
- Table operations
- Custom asset API (VectorAsset, VectorPath, etc.)
- Loading SVGs via `loadSvg()` (local) and `loadComponent()` (shared)
- The prelude's helpers in `lib` (see Prelude)

### NOT Allowed
- File I/O (`io` module disabled)
- OS access (`os` module disabled)
- Loading arbitrary scripts (`require`, `loadfile`, `dofile` and `package` are nil)
- Network access
- `debug` module

## Performance Considerations

1. **Pre-generation**: Scripts run at load time, not runtime
2. **Variant caching**: Generated assets are cached to disk
3. **Batch generation**: All variants generated in parallel where possible
4. **Memory limits**: Scripts have a memory cap (default 64MB)
5. **Time limits**: Scripts timeout after 10 seconds per variant

## Related Documents

- [Asset System Architecture](./README.md) - System overview
- [Folder-Based Assets](./folder-based-assets.md) - Folder structure and module loading
- [Asset Definition Schema](./asset-definitions.md) - XML format
- [Variant Cache Format](./variant-cache.md) - Binary cache format
