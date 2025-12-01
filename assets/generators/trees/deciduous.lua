-- Deciduous Tree Generator (3/4 Top-Down View - Rimworld Style)
-- The canopy is viewed from above, but the trunk is visible extending
-- downward from beneath the canopy, giving a sense of depth.

-- Get parameters from XML definition
local canopyRadius = getFloat("canopyRadius", 25.0)
local trunkHeight = getFloat("trunkHeight", 40.0)
local trunkWidth = getFloat("trunkWidth", 8.0)

-- MUCH more random variation for distinct trees
canopyRadius = canopyRadius * (0.6 + math.random() * 0.8)  -- 60% to 140% of base
trunkHeight = trunkHeight * (0.5 + math.random() * 1.0)    -- 50% to 150% of base
trunkWidth = trunkWidth * (0.6 + math.random() * 0.8)      -- 60% to 140% of base

-- Random canopy shape - some tall, some wide
local canopyStretch = 0.7 + math.random() * 0.6  -- Vertical stretch factor

-- Colors with slight variation
-- Trunk: orange-brown like Rimworld
local trunkR = 0.55 + (math.random() - 0.5) * 0.1
local trunkG = 0.35 + (math.random() - 0.5) * 0.08
local trunkB = 0.2 + (math.random() - 0.5) * 0.05

-- Canopy: olive/forest green
local leafR = 0.35 + (math.random() - 0.5) * 0.1
local leafG = 0.5 + (math.random() - 0.5) * 0.12
local leafB = 0.25 + (math.random() - 0.5) * 0.08

-- Darker canopy edge color (for outline effect)
local edgeR = leafR * 0.7
local edgeG = leafG * 0.7
local edgeB = leafB * 0.7

-- Shadow color
local shadowR = 0.15
local shadowG = 0.15
local shadowB = 0.1
local shadowA = 0.3

-- Helper to create an irregular blob shape (for organic canopy look)
-- stretchY controls vertical elongation (>1 = tall, <1 = wide)
local function createBlob(cx, cy, baseRadius, irregularity, segments, r, g, b, a, stretchY)
    a = a or 1.0
    stretchY = stretchY or 1.0
    local path = Path()
    -- Add extra irregularity variance per-blob
    local blobIrreg = irregularity * (0.8 + math.random() * 0.4)
    for i = 0, segments - 1 do
        local angle = (i / segments) * 2 * math.pi
        local radiusNoise = 1.0 + (math.random() - 0.5) * blobIrreg
        local radius = baseRadius * radiusNoise
        local x = cx + radius * math.cos(angle)
        local y = cy + radius * math.sin(angle) * stretchY
        path:addVertex(x, y)
    end
    path:setColor(r, g, b, a)
    path:close()
    asset:addPath(path)
end

-- Helper to create a tapered trunk/branch
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

-- Helper to draw a branch
local function drawBranch(startX, startY, endX, endY, baseWidth, tipWidth, r, g, b)
    local path = Path()
    local dx = endX - startX
    local dy = endY - startY
    local len = math.sqrt(dx * dx + dy * dy)
    if len > 0.001 then
        local perpX = -dy / len
        local perpY = dx / len
        path:addVertex(startX + perpX * baseWidth / 2, startY + perpY * baseWidth / 2)
        path:addVertex(endX + perpX * tipWidth / 2, endY + perpY * tipWidth / 2)
        path:addVertex(endX - perpX * tipWidth / 2, endY - perpY * tipWidth / 2)
        path:addVertex(startX - perpX * baseWidth / 2, startY - perpY * baseWidth / 2)
        path:setColor(r, g, b, 1.0)
        path:close()
        asset:addPath(path)
    end
end

-- Tree is centered at (0,0)
-- In 3/4 view: +Y is "down" on screen (toward viewer)

-- Calculate positions
-- In 3/4 view, canopy is elevated so we can see branches connecting to it
local canopyCenterY = -canopyRadius * 1.3  -- Canopy elevated but not too high
local trunkTopY = canopyRadius * 0.15      -- Trunk top just below center
local trunkBottomY = trunkHeight * 0.5     -- Trunk extends down

-- 1. Draw shadow
local shadowOffsetX = canopyRadius * 0.2
local shadowOffsetY = canopyRadius * 0.35
createBlob(shadowOffsetX, canopyCenterY + shadowOffsetY, canopyRadius * 0.9, 0.2, 20, shadowR, shadowG, shadowB, shadowA)

-- 2. Draw trunk
local trunkTopWidth = trunkWidth * 0.7
local trunkBottomWidth = trunkWidth
createTrunk(0, trunkTopY, trunkBottomY, trunkTopWidth, trunkBottomWidth, trunkR, trunkG, trunkB)

-- Darker edge on trunk for depth
local trunkEdgeWidth = trunkWidth * 0.15
createTrunk(-trunkWidth/2 + trunkEdgeWidth/2, trunkTopY, trunkBottomY, trunkEdgeWidth * 0.7, trunkEdgeWidth, trunkR * 0.7, trunkG * 0.7, trunkB * 0.7)

-- 3. Draw the BACK canopy layers (branches will appear on top of these)
-- VARY the number of back layers (1-3)
local backLayers = 1 + math.floor(math.random() * 3)

-- Always draw outer edge (darker outline)
createBlob(0, canopyCenterY, canopyRadius, 0.35, 24, edgeR, edgeG, edgeB, 0.95, canopyStretch)

-- Optionally add more back layers with different sizes/colors
for layer = 1, backLayers do
    local layerScale = 0.95 - layer * 0.08  -- Each layer slightly smaller
    local layerR = leafR + (math.random() - 0.5) * 0.1
    local layerG = leafG + (math.random() - 0.5) * 0.1
    local layerB = leafB + (math.random() - 0.5) * 0.05
    local layerStretch = canopyStretch * (0.9 + math.random() * 0.2)
    createBlob(0, canopyCenterY, canopyRadius * layerScale, 0.3, 18, layerR, layerG, layerB, 0.9, layerStretch)
end

-- 4. Draw branches ON TOP of main canopy (visible!)
-- These branches extend from VARIOUS PLACES on trunk and END WITHIN the canopy
local branchCount = 1 + math.floor(math.random() * 4)  -- 1-4 branches (more variation)
local trunkLength = trunkBottomY - trunkTopY

-- Calculate canopy bounds for constraining branch endpoints
local canopyMinX = -canopyRadius * 0.7  -- Well within canopy
local canopyMaxX = canopyRadius * 0.7
local canopyMinY = canopyCenterY - canopyRadius * canopyStretch * 0.7  -- Top of canopy
local canopyMaxY = canopyCenterY + canopyRadius * canopyStretch * 0.5  -- Bottom of canopy

for i = 1, branchCount do
    -- Branch starts from RANDOM POSITION along the trunk (not just top)
    local trunkProgress = math.random() * 0.7  -- 0% to 70% down the trunk
    local branchStartY = trunkTopY + trunkLength * trunkProgress

    -- X offset based on which side of trunk, with variation
    local side = (math.random() > 0.5) and 1 or -1
    local branchStartX = side * trunkWidth * (0.3 + math.random() * 0.2)

    -- Calculate a target endpoint WITHIN the canopy
    -- Pick a random point inside the canopy bounds
    local targetX = (math.random() - 0.5) * canopyRadius * 1.0  -- Within canopy X
    local targetY = canopyCenterY + (math.random() - 0.6) * canopyRadius * canopyStretch * 0.8  -- Within canopy Y, biased upward

    -- Clamp to canopy bounds
    targetX = math.max(canopyMinX, math.min(canopyMaxX, targetX))
    targetY = math.max(canopyMinY, math.min(canopyMaxY, targetY))

    -- Varied branch thickness
    local baseW = trunkWidth * (0.25 + math.random() * 0.25)
    local tipW = trunkWidth * (0.05 + math.random() * 0.12)
    drawBranch(branchStartX, branchStartY, targetX, targetY, baseW, tipW, trunkR * 0.85, trunkG * 0.85, trunkB * 0.85)
end

-- 5. Draw FRONT canopy clusters (partially cover branches for depth)
local numClusters = 1 + math.floor(math.random() * 3)  -- 1-3 clusters (more variation)
for i = 1, numClusters do
    local angle = math.random() * 2 * math.pi
    local dist = canopyRadius * (0.1 + math.random() * 0.3)  -- More varied position
    local ox = dist * math.cos(angle)
    local oy = dist * math.sin(angle) * 0.7 * canopyStretch
    local clusterRadius = canopyRadius * (0.3 + math.random() * 0.25)  -- More size variation

    -- More color variation
    local cr = leafR + (math.random() - 0.5) * 0.12
    local cg = leafG + (math.random() - 0.5) * 0.15
    local cb = leafB + (math.random() - 0.5) * 0.08

    -- Each cluster gets its own random stretch
    local clusterStretch = canopyStretch * (0.8 + math.random() * 0.4)
    createBlob(ox, canopyCenterY + oy, clusterRadius, 0.35, 14, cr, cg, cb, 0.85, clusterStretch)
end

-- 6. Highlight area (lighter, on top)
local highlightX = -canopyRadius * 0.15
local highlightY = canopyCenterY - canopyRadius * 0.15
createBlob(highlightX, highlightY, canopyRadius * 0.35, 0.15, 12, leafR * 1.15, leafG * 1.15, leafB * 1.1, 0.9)
