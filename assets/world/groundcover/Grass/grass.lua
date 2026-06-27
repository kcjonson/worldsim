-- Groundcover grass tuft generator.
-- A fan of hair-fine tapered blades radiating up from a common base near the origin.
-- Each blade is two stacked tapered segments: a darker base tone and a lit tip tone, so
-- the tuft reads with depth. Seed-varied per variant (the engine generates several variants
-- and GPU-instances them at the placed positions).
--
-- Centered at (0,0); +Y is screen-down so blades fan toward -Y (up). Units are meters.
-- math.random is pre-seeded per variant; just call it for variation.
--
-- All look/feel lives here (asset data), tunable via the XML <params> below or by editing
-- this script -- no engine recompile.

local nMin, nMax     = getFloatRange("bladeCount", 8, 12)
local lenMin, lenMax = getFloatRange("bladeLength", 0.28, 0.50)
local hwMin, hwMax   = getFloatRange("bladeHalfWidth", 0.005, 0.009)
local spread         = getFloat("fanSpread", 0.5)   -- max lean from vertical (radians)
local curveAmt       = getFloat("bladeCurve", 0.12) -- lateral bow toward the tip (meters)
local jitter         = getFloat("baseJitter", 0.03) -- base scatter around the origin (meters)

-- Muted-green palette, harmonized with the grass tile (#4a7040 / Surface::Grass 0.29,0.49,0.25).
local dark = Color.rgba(getFloat("darkR", 0.21), getFloat("darkG", 0.33), getFloat("darkB", 0.16), 1.0)
local mid  = Color.rgba(getFloat("midR", 0.31), getFloat("midG", 0.47), getFloat("midB", 0.24), 1.0)
local tip  = Color.rgba(getFloat("tipR", 0.46), getFloat("tipG", 0.63), getFloat("tipB", 0.33), 1.0)

local function blade(bx, by, angle, len, hw, baseCol, tipCol)
    local dx, dy = math.sin(angle), -math.cos(angle) -- blade up direction (up = -Y)
    local px, py = math.cos(angle), math.sin(angle)  -- perpendicular (blade right)
    local mx = bx + dx * len * 0.5 + px * curveAmt * 0.25
    local my = by + dy * len * 0.5 + py * curveAmt * 0.25
    local tx = bx + dx * len + px * curveAmt
    local ty = by + dy * len + py * curveAmt
    local hwm = hw * 0.5

    -- Base segment: a tapered quad, darker.
    local p1 = Path()
    p1:addVertex(bx + px * hw, by + py * hw)
    p1:addVertex(mx + px * hwm, my + py * hwm)
    p1:addVertex(mx - px * hwm, my - py * hwm)
    p1:addVertex(bx - px * hw, by - py * hw)
    p1:setColor(baseCol.r, baseCol.g, baseCol.b, 1.0)
    p1:close()
    asset:addPath(p1)

    -- Tip segment: tapers to a point, lit.
    local p2 = Path()
    p2:addVertex(mx + px * hwm, my + py * hwm)
    p2:addVertex(tx, ty)
    p2:addVertex(mx - px * hwm, my - py * hwm)
    p2:setColor(tipCol.r, tipCol.g, tipCol.b, 1.0)
    p2:close()
    asset:addPath(p2)
end

local n = math.floor(nMin + math.random() * (nMax - nMin) + 0.5)
for i = 1, n do
    local bx = (math.random() - 0.5) * 2 * jitter
    local by = (math.random() - 0.5) * 2 * jitter * 0.4
    local angle = (math.random() - 0.5) * 2 * spread
    local len = lenMin + math.random() * (lenMax - lenMin)
    local hw = hwMin + math.random() * (hwMax - hwMin)

    -- Per-blade tone: base interpolates dark->mid, tip interpolates mid->tip.
    local tb = math.random()
    local baseCol = Color.rgba(lerp(dark.r, mid.r, tb), lerp(dark.g, mid.g, tb), lerp(dark.b, mid.b, tb), 1.0)
    local tt = 0.4 + 0.6 * math.random()
    local tipCol = Color.rgba(lerp(mid.r, tip.r, tt), lerp(mid.g, tip.g, tt), lerp(mid.b, tip.b, tt), 1.0)

    blade(bx, by, angle, len, hw, baseCol, tipCol)
end
