-- Conifer Tree Generator (3/4 Top-Down View - Rimworld Style)
-- A spruce/pine seen from above: a short trunk under a stack of pointed,
-- star-shaped needle tiers, darkest and widest at the base, lighter and smaller
-- toward the tip. Narrower and darker than the deciduous generator.

local canopyRadius = getFloat("canopyRadius", 0.6)
local trunkHeight = getFloat("trunkHeight", 1.6)
local trunkWidth = getFloat("trunkWidth", 0.18)
local tiers = getInt("tiers", 4)

-- Per-tree variation
canopyRadius = canopyRadius * (0.7 + math.random() * 0.6)
trunkHeight = trunkHeight * (0.7 + math.random() * 0.6)
trunkWidth = trunkWidth * (0.7 + math.random() * 0.5)

-- Trunk: dark red-brown
local trunkR = 0.4 + (math.random() - 0.5) * 0.08
local trunkG = 0.26 + (math.random() - 0.5) * 0.06
local trunkB = 0.16 + (math.random() - 0.5) * 0.04

-- Needles: dark blue-green (spruce)
local needleR = 0.16 + (math.random() - 0.5) * 0.05
local needleG = 0.34 + (math.random() - 0.5) * 0.08
local needleB = 0.24 + (math.random() - 0.5) * 0.05

local shadowR, shadowG, shadowB, shadowA = 0.12, 0.12, 0.08, 0.3

-- A pointed (star) ring: 2*points vertices alternating between outerR and
-- outerR*innerRatio, giving the spiky needle silhouette from above.
local function createStar(cx, cy, outerR, innerRatio, points, r, g, b, a)
    a = a or 1.0
    local path = Path()
    local startAngle = math.random() * math.pi  -- rotate each tier a little
    for i = 0, points * 2 - 1 do
        local angle = startAngle + (i / (points * 2)) * 2 * math.pi
        local radius = (i % 2 == 0) and outerR or (outerR * innerRatio)
        radius = radius * (1.0 + (math.random() - 0.5) * 0.12)
        path:addVertex(cx + radius * math.cos(angle), cy + radius * math.sin(angle))
    end
    path:setColor(r, g, b, a)
    path:close()
    asset:addPath(path)
end

local function createTrunk(cx, topY, bottomY, topWidth, bottomWidth, r, g, b)
    local path = Path()
    path:addVertex(cx - topWidth / 2, topY)
    path:addVertex(cx + topWidth / 2, topY)
    path:addVertex(cx + bottomWidth / 2, bottomY)
    path:addVertex(cx - bottomWidth / 2, bottomY)
    path:setColor(r, g, b, 1.0)
    path:close()
    asset:addPath(path)
end

-- Canopy sits above center; trunk drops below it (+Y is toward the viewer).
local canopyCenterY = -canopyRadius * 1.1
local pointCount = 7 + math.floor(math.random() * 4) -- 7-10 needle points

-- 1. Shadow
createStar(canopyRadius * 0.18, canopyCenterY + canopyRadius * 0.4, canopyRadius * 0.95,
           0.6, pointCount, shadowR, shadowG, shadowB, shadowA)

-- 2. Trunk (mostly hidden, grounds the tree)
createTrunk(0, canopyRadius * 0.1, trunkHeight * 0.5, trunkWidth * 0.7, trunkWidth, trunkR, trunkG, trunkB)

-- 3. Stacked needle tiers: largest/darkest at the base, smaller/lighter upward.
tiers = math.max(3, tiers)
for tier = 0, tiers - 1 do
    local t = tier / (tiers - 1)               -- 0 at base, 1 at tip
    local scale = 1.0 - t * 0.62               -- shrink toward the tip
    local cy = canopyCenterY - t * canopyRadius * 0.95  -- stack upward
    local lighten = 0.78 + t * 0.5             -- base darker, tip lighter
    createStar(0, cy, canopyRadius * scale, 0.52, pointCount,
               needleR * lighten, needleG * lighten, needleB * lighten, 0.96)
end

-- 4. Bright tip highlight
createStar(0, canopyCenterY - canopyRadius * 0.95, canopyRadius * 0.22, 0.55, pointCount,
           needleR * 1.5, needleG * 1.35, needleB * 1.3, 0.95)
