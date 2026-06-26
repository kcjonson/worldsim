# Lua generator API

Verified against `libs/engine/assets/lua/LuaEngine.cpp`. If the engine changes, re-read that file (especially `registerBindings` and `executeGenerator`).

## Execution model

Each variant runs the script once with a fresh seed. The script builds geometry into the global `asset`. No return value is needed.

Pre-set globals:
- `seed` — integer world/asset seed.
- `variantIndex` — integer, 0..variantCount-1.
- `math.random` is already seeded with `seed + variantIndex` before the script runs. Do NOT call `math.randomseed` yourself. Just use `math.random()` for variation; same variant index always reproduces the same asset.

Available stdlib: `base`, `math`, `string`, `table`. Removed (sandbox): `os`, `io`, `loadfile`, `dofile`, `debug`, `package`, `require`.

## Param accessors (read XML `<params>`)

```lua
local r   = getFloat("canopyRadius", 0.8)        -- (key, default) -> number
local n   = getInt("branchCount", 4)             -- (key, default) -> int
local kind= getString("leafShape", "round")      -- (key, default) -> string
local lo, hi = getFloatRange("height", 1.0, 2.0) -- (key, defMin, defMax) -> (min, max)
```

## Global utility math

```lua
lerp(a, b, t)              -- linear interpolate
clamp(v, min, max)
smoothstep(edge0, edge1, x)
```
Plus standard Lua `math.*` (`random`, `floor`, `sin`, `cos`, `pi`, `sqrt`, `min`, `max`, ...).

## Types

### Vec2
```lua
local v = Vec2(x, y)        -- or Vec2()
v.x, v.y                    -- read/write
v + v2, v - v2, v * 2.0     -- operators (scalar mult both sides)
v:length(); v:normalize(); v:dot(v2)
```

### Color (components are 0..1 floats, NOT 0..255)
```lua
local c = Color(r, g, b, a) -- or Color()
c.r, c.g, c.b, c.a
Color.rgb(r, g, b); Color.rgba(r, g, b, a); Color.lerp(c1, c2, t)
```

### Path
```lua
local p = Path()            -- factory; preferred over Path.new()
p:addVertex(x, y)           -- append a point
p:setColor(r, g, b, a)      -- 0..1 floats
p:close()                   -- mark closed (do this; tessellator wants closed)
p:clear()
p.vertices                  -- vector<Vec2>
p.fillColor                 -- Color
p.isClosed                  -- bool
```

### Asset (the global `asset` is the output)
```lua
asset:addPath(p)            -- add a finished path
asset:createPath()          -- returns a new Path& already attached to the asset
asset:clear()
asset.paths
asset:setCollisionRect(halfW, halfH, cx, cy)  -- declare a rect collider in local meters (this
                            -- script's (0,0)-centered frame). The navmesh + Tier-3 collision use it.
                            -- Trees: setCollisionRect(trunkWidth/2, trunkWidth/2, 0, 0). Non-positive ignored.
```

## Coordinate system

- Centered at `(0, 0)`. The asset's footprint straddles the origin.
- `+Y` is screen-DOWN (toward the viewer). So the canopy sits at negative Y (up), the trunk extends toward positive Y (down). See `deciduous.lua`.
- Units are meters. XML params are meters (`canopyRadius` 0.8 = 0.8m). Don't pre-normalize; procedural geometry is used at world scale.
- Draw order = paint order. Earlier paths are behind later ones. Order: shadow -> trunk -> back canopy -> branches -> front clusters -> highlight.

## Tessellation discipline

- Each path needs ≥3 vertices, and should be closed (`p:close()`).
- No self-intersection (silently mis-tessellates). Build blobs from a monotonic angle sweep.
- Avoid duplicate/collinear runs; they're dropped.
- Concave is fine (ear clipping); convex is faster.

## Helper functions (copy these into a generator)

```lua
-- Irregular organic blob from an angle sweep (never self-intersects).
-- stretchY < 1 flattens (mound seen from above); > 1 elongates vertically.
local function blob(cx, cy, radius, irregularity, segments, r, g, b, a, stretchY)
    a = a or 1.0
    stretchY = stretchY or 1.0
    local p = Path()
    local irr = irregularity * (0.8 + math.random() * 0.4)
    for i = 0, segments - 1 do
        local ang = (i / segments) * 2 * math.pi
        local rr = radius * (1.0 + (math.random() - 0.5) * irr)
        p:addVertex(cx + rr * math.cos(ang), cy + rr * math.sin(ang) * stretchY)
    end
    p:setColor(r, g, b, a)
    p:close()
    asset:addPath(p)
end

-- Tapered trunk / stem (a quad, wider at the base).
local function trunk(cx, topY, bottomY, topW, bottomW, r, g, b)
    local p = Path()
    p:addVertex(cx - topW / 2, topY)
    p:addVertex(cx + topW / 2, topY)
    p:addVertex(cx + bottomW / 2, bottomY)
    p:addVertex(cx - bottomW / 2, bottomY)
    p:setColor(r, g, b, 1.0)
    p:close()
    asset:addPath(p)
end
```

## Complete template — a new shared generator (rounded shrub)

Save as `assets/shared/scripts/<form>.lua`, reference via `<scriptPath>@shared/<form>.lua</scriptPath>`.

```lua
-- Rounded shrub generator -- mound seen slightly from above.
-- Centered at (0,0), +Y screen-down, meters. RNG pre-seeded per variant.

local radius     = getFloat("radius", 0.5)
local heightBias = getFloat("heightBias", 0.8)   -- <1 flatter mound, >1 taller
local berryCount = getInt("berryCount", 0)

-- Per-instance variation
radius = radius * (0.75 + math.random() * 0.5)
local stretchY = heightBias * (0.85 + math.random() * 0.3)

-- Palette (0..1). One hue, layered tones. Edge = base * 0.7 for the dark rim.
local baseR = 0.27 + (math.random() - 0.5) * 0.06
local baseG = 0.42 + (math.random() - 0.5) * 0.08
local baseB = 0.20 + (math.random() - 0.5) * 0.05

local function blob(cx, cy, r, irregularity, segments, cr, cg, cb, ca, sy)
    ca = ca or 1.0; sy = sy or 1.0
    local p = Path()
    local irr = irregularity * (0.8 + math.random() * 0.4)
    for i = 0, segments - 1 do
        local ang = (i / segments) * 2 * math.pi
        local rr = r * (1.0 + (math.random() - 0.5) * irr)
        p:addVertex(cx + rr * math.cos(ang), cy + rr * math.sin(ang) * sy)
    end
    p:setColor(cr, cg, cb, ca)
    p:close()
    asset:addPath(p)
end

-- 1. ground shadow (offset toward viewer)
blob(radius * 0.15, radius * 0.35, radius * 0.95, 0.2, 20, 0.12, 0.12, 0.08, 0.3, 0.5)
-- 2. dark rim
blob(0, 0, radius, 0.35, 24, baseR * 0.7, baseG * 0.7, baseB * 0.7, 0.95, stretchY)
-- 3. base body
blob(0, 0, radius * 0.9, 0.30, 20, baseR, baseG, baseB, 1.0, stretchY)
-- 4. mid tone, biased upper-left toward the light
blob(-radius * 0.15, -radius * 0.15, radius * 0.6, 0.30, 16, baseR * 1.12, baseG * 1.12, baseB * 1.1, 0.95, stretchY)
-- 5. highlight
blob(-radius * 0.20, -radius * 0.20, radius * 0.32, 0.20, 12, baseR * 1.30, baseG * 1.30, baseB * 1.25, 0.95, stretchY)

-- 6. optional fruit
for i = 1, berryCount do
    local ang = math.random() * 2 * math.pi
    local d = radius * (0.2 + math.random() * 0.5)
    blob(d * math.cos(ang), d * math.sin(ang) * stretchY * 0.8, radius * 0.10, 0.1, 8, 0.78, 0.16, 0.16, 1.0, 1.0)
end
```

For trees, study `assets/shared/scripts/deciduous.lua` (canopy elevated at negative Y, trunk + branches below, back layers -> branches -> front clusters -> highlight). Reuse it via params before writing a new tree generator.

## Vector sub-assets (package model)

Author repeated hand-drawn detail (fruit, blossoms, distinctive leaves) as small SVGs in the asset folder and have the generator place them, rather than approximating them with blobs.

Reference them from XML params, resolved relative to the asset folder:
```xml
<params>
  <fruitSvg>apple.svg</fruitSvg>
  <fruitCount>8</fruitCount>
</params>
```

Proposed binding (STATUS: not implemented in `LuaEngine.cpp` yet — add it before a script relies on this):
```lua
local fruit = loadSvg(getString("fruitSvg", ""))   -- parse sub-SVG -> handle (path relative to asset folder)
for i = 1, getInt("fruitCount", 0) do
    local ang = math.random() * 2 * math.pi
    local d = canopyRadius * (0.2 + math.random() * 0.6)
    asset:stampSvg(fruit, d * math.cos(ang), canopyCenterY + d * math.sin(ang) * 0.7, 0.9 + math.random() * 0.3, 0)
end
```
Implementation notes for when this is built: the C++ side already parses SVG (`libs/renderer/vector/SVGLoader.cpp`). The binding needs (1) a per-execution `loadSvg(relPath)` that resolves against the asset folder (pass the folder into `GenerationContext`) and parses via `SVGLoader`, returning a handle, and (2) `asset:stampSvg(handle, x, y, scale, rotation)` that copies the sub-asset's paths and transforms their vertices before appending. The Lua sandbox stays intact — Lua never touches the filesystem; the C++ binding does the controlled read. Until it lands, draw such detail with `blob`/paths in the script.
