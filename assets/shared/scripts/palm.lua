-- Palm Tree Generator (3/4 Top-Down View - Rimworld Style)
-- Distinctive tropical silhouette: thin trunk, radiating frond clusters
-- from a single crown point rather than a broad deciduous canopy.

local trunkHeight = getFloat("trunkHeight", 35.0)
local trunkWidth  = getFloat("trunkWidth",  5.0)
local frondCount  = getInt("frondCount",    7)
local frondLength = getFloat("frondLength", 22.0)

-- Trunk-base collision: a square footprint (half-extent = trunkWidth/2 each
-- axis), captured before per-tree randomization so it is stable across variants.
asset:setCollisionRect(trunkWidth / 2, trunkWidth / 2, 0, 0)

-- Per-tree variation
trunkHeight = trunkHeight * (0.7 + math.random() * 0.6)
trunkWidth  = trunkWidth  * (0.7 + math.random() * 0.6)
frondLength = frondLength * (0.75 + math.random() * 0.5)
local actualFronds = frondCount - 1 + math.floor(math.random() * 3)  -- frondCount-1 to frondCount+1

-- Colors
-- Trunk: warm sandy-brown (lighter than deciduous, bleached by sun)
local trunkR = 0.65 + (math.random() - 0.5) * 0.08
local trunkG = 0.50 + (math.random() - 0.5) * 0.06
local trunkB = 0.32 + (math.random() - 0.5) * 0.05

-- Fronds: bright tropical green, more yellow-green than oak
local frondR = 0.28 + (math.random() - 0.5) * 0.08
local frondG = 0.60 + (math.random() - 0.5) * 0.10
local frondB = 0.20 + (math.random() - 0.5) * 0.06

local frondDarkR = frondR * 0.65
local frondDarkG = frondG * 0.65
local frondDarkB = frondB * 0.65

local shadowR = 0.12
local shadowG = 0.12
local shadowB = 0.08
local shadowA = 0.25

-- Helper: tapered quad (trunk or frond stem)
local function drawQuad(x0, y0, x1, y1, w0, w1, r, g, b)
    local dx = x1 - x0
    local dy = y1 - y0
    local len = math.sqrt(dx * dx + dy * dy)
    if len < 0.001 then return end
    local px = -dy / len
    local py =  dx / len
    local path = Path()
    path:addVertex(x0 + px * w0 * 0.5, y0 + py * w0 * 0.5)
    path:addVertex(x0 - px * w0 * 0.5, y0 - py * w0 * 0.5)
    path:addVertex(x1 - px * w1 * 0.5, y1 - py * w1 * 0.5)
    path:addVertex(x1 + px * w1 * 0.5, y1 + py * w1 * 0.5)
    path:setColor(r, g, b, 1.0)
    path:close()
    asset:addPath(path)
end

-- Helper: leaf frond (elongated diamond/lozenge tapering to tip)
local function drawFrond(cx, cy, angle, length, baseWidth, r, g, b)
    local cos_a = math.cos(angle)
    local sin_a = math.sin(angle)
    -- Frond mid-rib endpoint
    local tipX = cx + cos_a * length
    local tipY = cy + sin_a * length
    -- Wide point at ~35% along
    local midT = 0.35
    local midX = cx + cos_a * length * midT
    local midY = cy + sin_a * length * midT
    -- Perpendicular
    local px = -sin_a
    local py =  cos_a
    local path = Path()
    path:addVertex(cx, cy)
    path:addVertex(midX + px * baseWidth * 0.5, midY + py * baseWidth * 0.5)
    path:addVertex(tipX, tipY)
    path:addVertex(midX - px * baseWidth * 0.5, midY - py * baseWidth * 0.5)
    path:setColor(r, g, b, 0.92)
    path:close()
    asset:addPath(path)
end

-- Layout: palm is tall and narrow. In 3/4 view the crown sits above center.
local crownY = -trunkHeight * 0.45   -- crown elevated
local trunkTopY    =  trunkHeight * 0.02
local trunkBottomY =  trunkHeight * 0.48

-- 1. Shadow (offset to lower-right, soft)
local function drawEllipse(cx, cy, rx, ry, r, g, b, a, segs)
    local path = Path()
    for i = 0, segs - 1 do
        local ang = (i / segs) * 2 * math.pi
        path:addVertex(cx + math.cos(ang) * rx, cy + math.sin(ang) * ry)
    end
    path:setColor(r, g, b, a)
    path:close()
    asset:addPath(path)
end

local shadowRx = frondLength * 0.55
local shadowRy = frondLength * 0.30
drawEllipse(frondLength * 0.18, crownY + frondLength * 0.28,
            shadowRx, shadowRy,
            shadowR, shadowG, shadowB, shadowA, 18)

-- 2. Trunk - thin, slightly curved (represented by a tapered quad)
drawQuad(0, trunkTopY, 0, trunkBottomY, trunkWidth * 0.6, trunkWidth,
         trunkR, trunkG, trunkB)

-- Dark edge stripe for trunk depth
drawQuad(trunkWidth * 0.15, trunkTopY, trunkWidth * 0.15, trunkBottomY,
         trunkWidth * 0.12, trunkWidth * 0.18,
         trunkR * 0.7, trunkG * 0.7, trunkB * 0.7)

-- 3. Fronds - radiating from crown, spread ~270 degrees (skip a gap at bottom/trunk)
-- Angle spread: from about 200 degrees to 520 degrees (clockwise from 3 o'clock)
-- In screen coords: 0 = right, pi/2 = down
local startAngle = math.pi * 0.15   -- slight lower-left frond starts here
local spreadAngle = math.pi * 1.70  -- covers about 3/4 of full circle

for i = 0, actualFronds - 1 do
    local t = i / (actualFronds - 1)
    local baseAngle = startAngle + t * spreadAngle

    -- Add jitter so fronds don't fan perfectly
    local jitter = (math.random() - 0.5) * (spreadAngle / actualFronds) * 0.4
    local angle = baseAngle + jitter

    -- Fronds further from viewer (upper half) drawn first, closer ones (lower) on top
    local fl = frondLength * (0.80 + math.random() * 0.35)
    local fw = fl * (0.18 + math.random() * 0.10)

    -- Alternate slight color variation for natural look
    local fr = frondR + (math.random() - 0.5) * 0.06
    local fg = frondG + (math.random() - 0.5) * 0.08
    local fb = frondB + (math.random() - 0.5) * 0.04

    -- Back (darker) frond silhouette first
    drawFrond(0, crownY, angle, fl * 1.05, fw * 1.1, frondDarkR, frondDarkG, frondDarkB)
    -- Bright top frond
    drawFrond(0, crownY, angle, fl, fw, fr, fg, fb)
end

-- 4. Crown nub (small circle where fronds meet trunk top)
local crownR = trunkWidth * 1.0
drawEllipse(0, crownY, crownR, crownR * 0.9,
            trunkR * 1.1, trunkG * 1.05, trunkB * 0.9, 1.0, 10)
