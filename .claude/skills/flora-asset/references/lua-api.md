# Lua generator API

Verified against `libs/engine/assets/lua/LuaEngine.cpp`. If the engine changes, re-read that file (especially `registerBindings` and `executeGenerator`).

The LOOK is medium-agnostic: a Lua generator builds the SAME thing the static SVG path builds. Follow `references/style-and-view.md` for the construction (canopy composed of overlapping cel-shaded cluster shapes, one dark outline, dark backing fallback, dome shading, connected trunk + thick limbs, no ground shadow). This file is the Lua-specific *how*: the API, plus the two things only procedural gives you, per-instance variation and growth stages.

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
asset:setCollisionRect(halfX, halfY, offX, offY)  -- trunk-base collision, capture before jitter
asset:clear()
asset.paths
```

## Coordinate system

- Centered at `(0, 0)`. The asset's footprint straddles the origin.
- `+Y` is screen-DOWN (toward the viewer). So the canopy sits at negative Y (up), the trunk extends toward positive Y (down).
- Units are meters. XML params are meters (`canopyRadius` 0.8 = 0.8m). Don't pre-normalize; procedural geometry is used at world scale.
- Draw order = paint order; earlier paths are behind later ones. For a tree: **trunk -> thick limbs -> rim (expanded dark cluster copies) -> backing fill -> cluster bases + caps, back-to-front.** No ground shadow.

## Tessellation discipline

- Each path needs ≥3 vertices, and should be closed (`p:close()`).
- No self-intersection (silently mis-tessellates). Build every blob from a monotonic angle sweep (the `cluster` helper below does this).
- Avoid duplicate/collinear runs; they're dropped.
- Concave is fine (ear clipping); convex is faster.

## Construction (same recipe as SVG, emitted in Lua)

Build the canopy ONLY from overlapping cluster shapes whose union is the silhouette. Do NOT draw one smooth background blob and layer detail on it, that's the old look and it reads flat. No ground shadow. Per `style-and-view.md`.

### Helpers (copy into a generator)

```lua
-- One leaf-cluster: a lumpy CLOSED blob with `lobes` outward bumps (cloud / broccoli edge),
-- never a smooth ellipse. sy < 1 flattens it for the foreshortened dome. Monotonic angle
-- sweep, so it never self-intersects. Appends to the asset.
local function cluster(cx, cy, radius, lobes, r, g, b, a, sy)
    a = a or 1.0; sy = sy or 1.0
    local p = Path()
    local segs  = lobes * 3
    local phase = math.random() * 2 * math.pi
    for i = 0, segs - 1 do
        local ang  = (i / segs) * 2 * math.pi
        local bump = 0.80 + 0.20 * math.sin(ang * lobes + phase)    -- `lobes` outward bumps
        local rr   = radius * bump * (0.94 + math.random() * 0.12)  -- + per-point jitter
        p:addVertex(cx + rr * math.cos(ang), cy + rr * math.sin(ang) * sy)
    end
    p:setColor(r, g, b, a); p:close(); asset:addPath(p)
end

-- Short tapered trunk stub (a quad, wider at the base).
local function trunk(cx, topY, botY, topW, botW, r, g, b)
    local p = Path()
    p:addVertex(cx - topW/2, topY); p:addVertex(cx + topW/2, topY)
    p:addVertex(cx + botW/2, botY); p:addVertex(cx - botW/2, botY)
    p:setColor(r, g, b, 1.0); p:close(); asset:addPath(p)
end

-- THICK tapering limb (NOT a stick: base ~half to two-thirds the trunk width).
local function limb(x0, y0, x1, y1, baseW, tipW, r, g, b)
    local dx, dy = x1 - x0, y1 - y0
    local len = math.sqrt(dx*dx + dy*dy)
    if len < 1e-4 then return end
    local px, py = -dy/len, dx/len
    local p = Path()
    p:addVertex(x0 + px*baseW/2, y0 + py*baseW/2); p:addVertex(x1 + px*tipW/2, y1 + py*tipW/2)
    p:addVertex(x1 - px*tipW/2,  y1 - py*tipW/2);  p:addVertex(x0 - px*baseW/2, y0 - py*baseW/2)
    p:setColor(r, g, b, 1.0); p:close(); asset:addPath(p)
end
```

### Tree template (cluster-composed, the converged recipe)

```lua
-- Deciduous tree, 3/4 top-down. Canopy = union of overlapping scalloped clusters.
-- Canopy mass sits above the origin (negative Y); short trunk stub peeks below. Meters.
-- RNG pre-seeded per variant.

local canopyR = getFloat("canopyRadius", 0.8)
local trunkH  = getFloat("trunkHeight", 1.5)
local trunkW  = getFloat("trunkWidth", 0.2)
local nClust  = getInt("clusterCount", 11)     -- foliage density (species / age)
local lobes   = getInt("lobeCount", 7)         -- 6-9: cluster-edge texture (species)
local growth  = getFloat("growthStage", 1.0)   -- 0.3 seedling .. 1.0 mature

-- collision from the UNSCALED trunk so the footprint is stable across variants
asset:setCollisionRect(trunkW/2, trunkW/2, 0, 0)

-- growth scaling + per-instance variation
local g = clamp(growth, 0.25, 1.0)
canopyR = canopyR * g * (0.85 + math.random()*0.30)
trunkH  = trunkH  * g * (0.80 + math.random()*0.40)
trunkW  = trunkW  *     (0.85 + math.random()*0.30)
nClust  = math.max(5, math.floor(nClust * (0.55 + 0.45*g)))   -- sparser when young
local sy = 0.80 * (0.92 + math.random()*0.16)                 -- foreshortened: wider than tall

-- palette: one hue family; rim = base*0.40 (dark outline), cap = base*~1.3 (lit). Jitter per
-- instance so a stand isn't uniform. Swap base for the species (style-and-view cheat-sheet:
-- oak deep cool green, birch light yellow-green, maple burgundy, beech warm green).
local bR = getFloat("leafR", 0.24) + (math.random()-0.5)*0.04
local bG = getFloat("leafG", 0.37) + (math.random()-0.5)*0.05
local bB = getFloat("leafB", 0.17) + (math.random()-0.5)*0.03
local function tone(m) return bR*m, bG*m, bB*m end
local barkR, barkG, barkB = 0.27, 0.19, 0.11

local cy       = -canopyR * 0.9               -- canopy center (up)
local trunkTop =  cy + canopyR * 0.55         -- buried under the canopy's lower edge

-- 1. trunk stub (short) -- drawn first so the canopy overlaps its top
trunk(0, trunkTop, trunkH*0.5, trunkW*0.8, trunkW, barkR*1.3, barkG*1.3, barkB*1.3)

-- 2. a couple of THICK limbs rising into the canopy (peek through gaps), dark bark
for i = 1, 2 do
    local side = (i == 1) and -1 or 1
    limb(side*trunkW*0.3, trunkTop, side*canopyR*0.5, cy + canopyR*0.1,
         trunkW*0.6, trunkW*0.25, barkR*0.8, barkG*0.8, barkB*0.8)
end

-- cluster positions across a wide, shallow dome footprint
local pos = {}
for i = 1, nClust do
    local ang = math.random() * 2 * math.pi
    local d   = canopyR * 0.6 * math.sqrt(math.random())     -- even fill of the disc
    pos[i] = { x = d*math.cos(ang), y = cy + d*math.sin(ang)*sy,
               r = canopyR * (0.34 + math.random()*0.16) }
end
table.sort(pos, function(p, q) return p.y < q.y end)         -- upper/back first, lower/front last

-- 3. RIM: an expanded dark copy of every cluster -> their union is one thin dark outline
for _, c in ipairs(pos) do cluster(c.x, c.y, c.r*1.12, lobes, tone(0.40), 1.0, sy) end

-- 4. BACKING fallback: one solid dark blob over the canopy interior, so any gap between
--    clusters reads as deep foliage shadow, not sky (jitter can open gaps).
cluster(0, cy, canopyR*0.92, lobes+2, tone(0.62), 1.0, sy)

-- 5. cluster bases + caps, back-to-front, DOME-shaded by position
for _, c in ipairs(pos) do
    cluster(c.x, c.y, c.r, lobes, tone(1.0), 1.0, sy)                 -- base (mid tone)
    -- brighter cap toward the top-center-left (lit dome surface), dimmer toward lower/far rim
    local lit = clamp(0.55 - c.x/(canopyR*1.6) + (cy - c.y)/(canopyR*1.6), 0.0, 1.0)
    cluster(c.x - c.r*0.16, c.y - c.r*0.16, c.r*0.62, lobes, tone(1.15 + 0.30*lit), 0.95, sy)
end
```

Per species, change colour + footprint + texture + density, never the camera: `leafR/G/B` (palette), `canopyRadius`/`sy` (footprint), `lobeCount` (edge texture), `clusterCount` (density). A conifer is the one exception (a slightly peaked crown of spiky downward fan-tufts, dark blue-green), write a `conifer.lua` rather than forcing the round-canopy template. A shrub is the same recipe, lower and flatter (smaller `canopyR`, smaller `sy`, no trunk peeking).

### The existing shared generators are the OLD look

`deciduous.lua` / `conifer.lua` / `palm.lua` in `assets/shared/scripts/` still produce the pre-convergence look (single background blob + ground shadow + thin branches). They are slated to be rewritten to the recipe above. Until they are: follow `style-and-view.md` for the look, NOT the current generator output, and don't reuse the old construction.

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
    local d = canopyR * (0.2 + math.random() * 0.6)
    asset:stampSvg(fruit, d * math.cos(ang), cy + d * math.sin(ang) * 0.7, 0.9 + math.random() * 0.3, 0)
end
```
Implementation notes for when this is built: the C++ side already parses SVG (`libs/renderer/vector/SVGLoader.cpp`). The binding needs (1) a per-execution `loadSvg(relPath)` that resolves against the asset folder (pass the folder into `GenerationContext`) and parses via `SVGLoader`, returning a handle, and (2) `asset:stampSvg(handle, x, y, scale, rotation)` that copies the sub-asset's paths and transforms their vertices before appending. The Lua sandbox stays intact — Lua never touches the filesystem; the C++ binding does the controlled read. Until it lands, draw such detail with `cluster`/paths in the script.
