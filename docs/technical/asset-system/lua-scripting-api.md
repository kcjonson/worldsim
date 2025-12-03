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

## Module Loading

Scripts can load modules using `require()`:

```lua
-- Asset-local modules (checked first)
local helper = require("helper")           -- ./helper.lua

-- Shared modules (registered by name, no paths)
local branch = require("branch_utils")     -- From shared/scripts/
local noise = require("noise")             -- From shared/scripts/

-- Load local SVG components
local leaf = loadSvg("maple_leaf.svg")     -- ./maple_leaf.svg

-- Load shared SVG components
local template = loadComponent("leaf_shapes")  -- From shared/components/
```

**Resolution order for `require("foo")`:**
1. Check asset folder for `foo.lua`
2. Check shared module registry for "foo"
3. Error if not found

## Script Structure

Every generator script must define a `generate` function:

```lua
-- assets/world/flora/MapleTree/generate.lua

-- Load shared utilities (by name, not path)
local branch = require("branch_utils")

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
```

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

Seeded RNG for deterministic generation. The seed comes from the variant index.

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

## Example: Deciduous Tree Generator

```lua
-- assets/world/flora/OakTree/generate.lua
-- Generates deciduous trees using Weber & Penn-style branching

-- Load shared utilities (by name)
local branch_utils = require("branch_utils")

function generate(params, rng)
    local asset = VectorAsset.new()

    -- Parse parameters with defaults
    local trunkHeight = rng:range(params.trunkHeightRange[1], params.trunkHeightRange[2])
    local trunkWidth = rng:range(params.trunkWidthRange[1], params.trunkWidthRange[2])
    local branchLevels = rng:rangeInt(params.branchLevels[1], params.branchLevels[2])
    local leafDensity = params.leafDensity or 0.7

    -- Colors
    local barkColor = Color.hex(params.barkColor or "#5D4037")
    local leafBaseColor = Color.hex(params.leafColorRange.base or "#228B22")

    -- Generate trunk
    local trunk = generateTrunk(trunkHeight, trunkWidth, barkColor, rng)
    asset:addPath(trunk)

    -- Store branch endpoints for leaves
    local endpoints = {}

    -- Generate branches
    local trunkTop = Vec2.new(0, -trunkHeight)
    generateBranches(asset, trunkTop, Vec2.new(0, -1), trunkWidth * 0.7,
                     branchLevels, params, barkColor, endpoints, rng)

    -- Add leaves at endpoints (load local SVG)
    if params.leafSvg then
        local leafTemplate = loadSvg(params.leafSvg)  -- Local: oak_leaf.svg
        for _, endpoint in ipairs(endpoints) do
            addLeavesAtPoint(asset, leafTemplate, endpoint, leafBaseColor,
                            leafDensity, params.leafColorRange, rng)
        end
    end

    return asset
end

function generateTrunk(height, width, color, rng)
    local path = VectorPath.new()

    -- Slight taper and curve for organic look
    local wobble = rng:range(0.05, 0.15)
    local midWidth = width * (1 + wobble)

    path:moveTo(-width/2, 0)  -- bottom left
    path:cubicTo(-midWidth/2, -height*0.3,
                 -width*0.4, -height*0.7,
                 -width*0.3, -height)  -- top left
    path:lineTo(width*0.3, -height)    -- top right
    path:cubicTo(width*0.4, -height*0.7,
                 midWidth/2, -height*0.3,
                 width/2, 0)            -- bottom right
    path:close()

    path:setFill(color)
    return path
end

function generateBranches(asset, startPos, direction, width, depth, params, color, endpoints, rng)
    if depth <= 0 or width < 0.5 then
        -- Terminal branch - store for leaves
        table.insert(endpoints, { pos = startPos, dir = direction })
        return
    end

    -- Branch parameters
    local length = width * rng:range(4, 7)
    local angle = math.atan2(direction.y, direction.x)

    -- Create branch path
    local branch = createBranchPath(startPos, angle, length, width, color, rng)
    asset:addPath(branch)

    -- Calculate branch endpoint
    local endPos = startPos + direction * length

    -- Spawn child branches
    local numChildren = rng:rangeInt(2, 4)
    local baseAngle = rng:range(params.branchAngleRange[1], params.branchAngleRange[2])

    for i = 1, numChildren do
        local childAngle = angle + math.rad(baseAngle * (i - (numChildren+1)/2))
        childAngle = childAngle + rng:range(-0.2, 0.2)  -- randomize

        local childDir = Vec2.new(math.cos(childAngle), math.sin(childAngle))
        local childWidth = width * rng:range(0.5, 0.7)

        generateBranches(asset, endPos, childDir, childWidth, depth - 1,
                        params, color, endpoints, rng)
    end
end

function createBranchPath(start, angle, length, width, color, rng)
    local path = VectorPath.new()

    -- Create tapered branch shape
    local endWidth = width * 0.6
    local perpAngle = angle + math.pi/2

    local dx = math.cos(angle) * length
    local dy = math.sin(angle) * length
    local px = math.cos(perpAngle)
    local py = math.sin(perpAngle)

    path:moveTo(start.x - px * width/2, start.y - py * width/2)
    path:lineTo(start.x + dx - px * endWidth/2, start.y + dy - py * endWidth/2)
    path:lineTo(start.x + dx + px * endWidth/2, start.y + dy + py * endWidth/2)
    path:lineTo(start.x + px * width/2, start.y + py * width/2)
    path:close()

    path:setFill(color)
    return path
end

function addLeavesAtPoint(asset, template, endpoint, baseColor, density, colorRange, rng)
    local numLeaves = math.floor(10 * density * rng:range(0.7, 1.3))

    for i = 1, numLeaves do
        local leaf = template:clone()

        -- Random position around endpoint
        local offsetDist = rng:range(2, 8)
        local offsetAngle = rng:angle()
        local x = endpoint.pos.x + math.cos(offsetAngle) * offsetDist
        local y = endpoint.pos.y + math.sin(offsetAngle) * offsetDist

        leaf:setPosition(x, y)
        leaf:setRotation(rng:angle())
        leaf:setScale(rng:range(0.7, 1.3))

        -- Color variation
        local hueShift = rng:range(colorRange.hueShift[1], colorRange.hueShift[2])
        local leafColor = baseColor:hueShift(hueShift)

        -- Apply color to all paths in leaf
        for _, path in ipairs(leaf:getPaths()) do
            path:setFill(leafColor)
        end

        asset:addChild(leaf)
    end
end
```

## Sandbox Restrictions

For mod safety, the scripting environment is sandboxed:

### Allowed
- Math operations
- String manipulation
- Table operations
- Custom asset API (VectorAsset, VectorPath, etc.)
- Loading SVGs via `loadSvg()` (local) and `loadComponent()` (shared)
- Loading modules via `require()` (local first, then shared registry)

### NOT Allowed
- File I/O (`io` module disabled)
- OS access (`os` module disabled)
- Loading arbitrary scripts (`loadfile`, `dofile` disabled)
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
