-- Conifer (pine/spruce) generator -- 3/4 top-down, converged recipe.
-- A cohesive PEAKED evergreen: stacked spiky needle tiers, widest/darkest at the
-- base, narrowing to a lit peak. Dark blue-green. One dark outline (expanded tier
-- copies behind) + a dark backing fill so gaps between tiers read as shadow, not
-- sky. Canopy mass above the origin; a SHORT cel-shaded, bark-textured trunk stub
-- peeks below. NO ground shadow. Light from upper-left. Strong per-instance
-- variation (aspect, taper, lean, tier count, raggedness, colour) + growth.

local canopyR = getFloat("canopyRadius", 0.6)
local trunkW  = getFloat("trunkWidth", 0.18)
local tiers   = getInt("tiers", 6)
local growth  = getFloat("growthStage", 1.0)   -- 0.3 sapling .. 1.0 mature

-- Trunk-base collision (half-extent = trunkW/2), captured before per-tree jitter.
asset:setCollisionRect(trunkW / 2, trunkW / 2, 0, 0)

-- ---- per-instance variation: this is what makes each tree read as distinct ----
local g        = clamp(growth, 0.3, 1.0)
canopyR        = canopyR * g * (0.72 + math.random() * 0.56)        -- 0.72..1.28 size
tiers          = math.max(4, math.floor(tiers * (0.7 + math.random() * 0.7)))  -- ~4..8
local points   = 7 + math.floor(math.random() * 5)                 -- 7..11 spikes / tier
local peakRise = canopyR * (1.05 + math.random() * 0.95)            -- aspect: squat .. tall-pointed
local falloff  = 0.58 + math.random() * 0.30                        -- columnar .. sharply tapered
local lean     = (math.random() - 0.5) * canopyR * 0.45            -- slight sideways lean
local spike    = 0.44 + math.random() * 0.18                        -- needle spikiness (inner ratio)
local trunkWv  = trunkW * (0.8 + math.random() * 0.5)              -- visual trunk thickness

-- Raggedness: a FIXED per-tier width wobble (sampled once) so the dark rim copies
-- stay aligned with the tiers they outline.
local tierJit = {}
for i = 0, tiers - 1 do tierJit[i] = 1.0 + (math.random() - 0.5) * 0.20 end

-- Palette: dark blue-green spruce, varied per instance in brightness + warm/cool.
local light = 0.78 + math.random() * 0.42        -- overall brightness
local temp  = (math.random() - 0.5) * 0.13       -- + greener/warmer, - cooler/bluer
local nR = 0.17 * light
local nG = (0.29 + temp) * light
local nB = (0.21 - temp * 0.6) * light
local function tone(m) return nR * m, nG * m, nB * m end
local barkR = 0.26 * (0.85 + math.random() * 0.3)
local barkG, barkB = 0.18, 0.11

-- A spiky star ring (one needle tier seen from above): 2*points verts alternating
-- outer/inner radius. sin is flattened by 0.7 to foreshorten the tier (top-down).
-- Monotonic angle sweep + always-positive radius, so it never self-intersects.
local function star(cx, cy, outerR, innerRatio, r, g, b, a)
    a = a or 1.0
    local p = Path()
    local startA = math.random() * math.pi
    for i = 0, points * 2 - 1 do
        local ang = startA + (i / (points * 2)) * 2 * math.pi
        local rr  = ((i % 2 == 0) and outerR or outerR * innerRatio) * (1.0 + (math.random() - 0.5) * 0.10)
        p:addVertex(cx + rr * math.cos(ang), cy + rr * math.sin(ang) * 0.7)
    end
    p:setColor(r, g, b, a); p:close(); asset:addPath(p)
end

local function quad(x0, y0, x1, y1, x2, y2, x3, y3, r, g, b)
    local p = Path()
    p:addVertex(x0, y0); p:addVertex(x1, y1); p:addVertex(x2, y2); p:addVertex(x3, y3)
    p:setColor(r, g, b, 1.0); p:close(); asset:addPath(p)
end

-- Short cel-shaded trunk: a slightly tapered cylinder = mid base + lit left band +
-- shadow right edge + one dark vertical bark crack. Reads as bark, not a flat stick.
local function texturedTrunk(topY, botY, topW, botW)
    -- x at side fraction f in [-0.5, 0.5], interpolating top->bot width at height t (0=top,1=bot)
    local function edge(f, t) return f * (topW + (botW - topW) * t) end
    local function yAt(t) return topY + (botY - topY) * t end
    quad(edge(-0.5, 0), topY, edge(0.5, 0), topY, edge(0.5, 1), botY, edge(-0.5, 1), botY, barkR, barkG, barkB)
    quad(edge(-0.5, 0), topY, edge(-0.1, 0), topY, edge(-0.1, 1), botY, edge(-0.5, 1), botY,
         barkR * 1.38, barkG * 1.38, barkB * 1.32)                       -- lit left band
    quad(edge(0.24, 0), topY, edge(0.5, 0), topY, edge(0.5, 1), botY, edge(0.24, 1), botY,
         barkR * 0.6, barkG * 0.6, barkB * 0.58)                         -- shadow right edge
    local cf = -0.12 + (math.random() - 0.5) * 0.2                       -- crack offset
    quad(edge(cf - 0.1, 0.12), yAt(0.12), edge(cf + 0.1, 0.12), yAt(0.12),
         edge(cf + 0.1, 1), botY, edge(cf - 0.1, 1), botY,
         barkR * 0.48, barkG * 0.48, barkB * 0.45)                       -- vertical bark crack
end

-- trunk base sits AT the origin (= the collision point / ground contact); canopy above
local trunkLen =  canopyR * (0.5 + math.random() * 0.5)                 -- SHORT stub, proportional
local trunkBot =  0.0                                                   -- BASE at the origin
local trunkTop = -trunkLen                                              -- trunk rises from the base
local baseY    =  trunkTop - canopyR * 0.35                             -- bottom tier centered above the trunk top

-- tier i: 0 = base (widest, lowest), tiers-1 = tip (smallest, highest); leans + wobbles
local function tierAt(i)
    local t = (tiers <= 1) and 0.0 or i / (tiers - 1)
    return lean * t, baseY - t * peakRise, canopyR * (1.0 - t * falloff) * tierJit[i]
end

-- 1. trunk stub (short, textured) -- drawn first so the lowest tier overlaps its top
texturedTrunk(trunkTop, trunkBot, trunkWv * 0.82, trunkWv)

-- 2. RIM: an expanded dark copy of every tier -> their union is one dark outline
for i = 0, tiers - 1 do
    local cx, cy, rr = tierAt(i)
    star(cx, cy, rr * 1.12, spike + 0.08, tone(0.42))
end

-- 3. BACKING fallback: a solid dark wedge over the interior so any gap reads as shadow
do
    local br, bg, bb = tone(0.58)
    local p = Path()
    p:addVertex(-canopyR * 0.8, baseY + canopyR * 0.3)
    p:addVertex(canopyR * 0.8, baseY + canopyR * 0.3)
    p:addVertex(lean, baseY - peakRise - canopyR * 0.1)
    p:setColor(br, bg, bb, 1.0); p:close(); asset:addPath(p)
end

-- 4. TIERS base-to-tip: dark base star + a smaller lit cap offset up-left
for i = 0, tiers - 1 do
    local cx, cy, rr = tierAt(i)
    local t = (tiers <= 1) and 0.0 or i / (tiers - 1)
    star(cx, cy, rr, spike, tone(1.0))
    star(cx - rr * 0.12, cy - rr * 0.10, rr * 0.66, spike, tone(1.22 + 0.28 * t))  -- lit cap, brighter up
end

-- 5. PEAK: small bright tip highlight at the top
local px, py, pr = tierAt(tiers - 1)
star(px - pr * 0.1, py - pr * 0.12, pr * 0.5, spike, tone(1.6))
