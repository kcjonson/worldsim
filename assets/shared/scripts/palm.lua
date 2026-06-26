-- Palm generator -- 3/4 top-down, converged recipe adapted to the palm form.
-- A radiating crown of long, arching, FEATHERED fronds springing from one crown
-- point atop a slender, ringed, cel-shaded trunk. Each frond is cel-shaded (dark
-- expanded copy behind = one dark rim, mid body, bright midrib sheen) and droops
-- toward the viewer under gravity. NO ground shadow. Light from upper-left. Strong
-- per-instance variation (frond count, length, droop, lean, palette) + growth.

local trunkW     = getFloat("trunkWidth", 0.12)
local frondLen   = getFloat("frondLength", 0.7)
local frondCount = getInt("frondCount", 9)
local growth     = getFloat("growthStage", 1.0)

-- Trunk-base collision (half-extent = trunkW/2), captured before per-tree jitter.
asset:setCollisionRect(trunkW / 2, trunkW / 2, 0, 0)

-- ---- per-instance variation ----
local g       = clamp(growth, 0.3, 1.0)
frondLen      = frondLen * g * (0.8 + math.random() * 0.5)             -- crown reach
local nFronds = math.max(6, frondCount - 2 + math.floor(math.random() * 6))  -- ~7..13
local trunkLen = frondLen * (1.2 + math.random() * 0.8)                -- slender, taller than crown
local trunkWv  = trunkW * (0.85 + math.random() * 0.5)
local lean     = (math.random() - 0.5) * trunkLen * 0.28               -- crown sits off the base
local baseDroop = 0.30 + math.random() * 0.30                          -- how hard fronds arch down

-- Palette: tropical green crown, sun-bleached sandy trunk; varied per instance.
local light = 0.82 + math.random() * 0.34
local temp  = (math.random() - 0.5) * 0.12                             -- + yellow-green, - deep green
local fR = (0.27 + temp) * light
local fG = (0.55 + temp * 0.5) * light
local fB = (0.20 - temp * 0.4) * light
local function frondTone(m) return fR * m, fG * m, fB * m end
local barkR = (0.58 + (math.random() - 0.5) * 0.1)
local barkG, barkB = 0.46, 0.30

-- Trunk base sits AT the origin (= the collision point / ground contact); the crown
-- and fronds build upward from there, so the origin-centered collision lands on the base.
local crownX, crownY = lean, -trunkLen
local trunkBotY = 0.0   -- trunk base at the origin

-- A feathered frond: a sawtooth blade along a downward-arching midrib. Deterministic
-- given fr (no internal RNG) so the dark/mid/highlight passes line up into a clean
-- rim. Built like the conifer star (alternating outer/inner offsets along an advancing
-- midrib) so it never self-intersects.
local TEETH = 6
local function frond(fr, lenScale, widScale, r, g, b, a)
    a = a or 1.0
    local ang = fr.angle
    local len = fr.length * lenScale
    local hw  = fr.width * widScale
    local ca, sa = math.cos(ang), math.sin(ang)
    local function midrib(t)
        local along = len * t
        -- foreshorten screen-y by 0.7; droop bends the tip toward the viewer (+y)
        return crownX + ca * along, crownY + sa * along * 0.7 + fr.droop * len * (t * t)
    end
    local p = Path()
    p:addVertex(crownX, crownY)                              -- base at the crown
    for k = 1, TEETH do                                     -- side A: out/in sawtooth to the tip
        local t  = (k - 0.5) / TEETH
        local lw = hw * (1.0 - t * 0.82)
        local mx, my = midrib(t)
        p:addVertex(mx - sa * lw, my + ca * lw * 0.7)        -- leaflet tip (+perp)
        local nt = k / TEETH
        local nx, ny = midrib(nt)
        p:addVertex(nx - sa * lw * 0.18, ny + ca * lw * 0.18 * 0.7)  -- notch toward midrib
    end
    local tx, ty = midrib(1.0)
    p:addVertex(tx, ty)                                     -- tip
    for k = TEETH, 1, -1 do                                 -- side B: back to base
        local t  = (k - 0.5) / TEETH
        local lw = hw * (1.0 - t * 0.82)
        local nt = (k - 1) / TEETH
        local nx, ny = midrib(nt > 0 and nt or 0.0)
        p:addVertex(nx + sa * lw * 0.18, ny - ca * lw * 0.18 * 0.7)  -- notch (-perp)
        local mx, my = midrib(t)
        p:addVertex(mx + sa * lw, my - ca * lw * 0.7)        -- leaflet tip (-perp)
    end
    p:setColor(r, g, b, a); p:close(); asset:addPath(p)
end

-- Slender ringed, cel-shaded trunk: a leaning tapered cylinder = mid base + lit left
-- band + shadow right edge + a few darker horizontal segment rings (palm leaf scars).
local function palmTrunk(topX, topY, botX, botY, topW, botW)
    local function pt(side, t)
        local w = topW + (botW - topW) * t
        local cx = topX + (botX - topX) * t
        local cy = topY + (botY - topY) * t
        local dx, dy = botX - topX, botY - topY
        local len = math.sqrt(dx * dx + dy * dy)
        local pxn, pyn = (len > 1e-4) and (-dy / len) or 1.0, (len > 1e-4) and (dx / len) or 0.0
        return cx + pxn * side * w, cy + pyn * side * w
    end
    local function band(s0, s1, t0, t1, r, g, b)
        local p = Path()
        local x0, y0 = pt(s0, t0); local x1, y1 = pt(s1, t0)
        local x2, y2 = pt(s1, t1); local x3, y3 = pt(s0, t1)
        p:addVertex(x0, y0); p:addVertex(x1, y1); p:addVertex(x2, y2); p:addVertex(x3, y3)
        p:setColor(r, g, b, 1.0); p:close(); asset:addPath(p)
    end
    band(-0.5, 0.5, 0, 1, barkR, barkG, barkB)                          -- base
    band(-0.5, -0.12, 0, 1, barkR * 1.3, barkG * 1.3, barkB * 1.25)     -- lit left
    band(0.22, 0.5, 0, 1, barkR * 0.62, barkG * 0.62, barkB * 0.6)      -- shadow right
    local rings = 3 + math.floor(math.random() * 3)
    for k = 1, rings do
        local t = k / (rings + 1)
        band(-0.5, 0.5, t - 0.045, t + 0.025, barkR * 0.55, barkG * 0.55, barkB * 0.5)  -- leaf-scar ring
    end
end

-- precompute fronds (deterministic geometry so the passes align)
local fronds = {}
for i = 0, nFronds - 1 do
    local base = (i / nFronds) * 2 * math.pi
    fronds[i] = {
        angle  = base + (math.random() - 0.5) * (math.pi / nFronds) * 0.7,
        length = frondLen * (0.82 + math.random() * 0.36),
        width  = frondLen * (0.18 + math.random() * 0.09),
        -- lower fronds (pointing down/+y) droop more; a per-frond wobble on top
        droop  = baseDroop * (0.6 + math.random() * 0.5),
    }
end

-- 1. trunk (behind the crown)
palmTrunk(crownX, crownY + frondLen * 0.12, 0, trunkBotY, trunkWv * 0.7, trunkWv)

-- 2. dark frond pass (expanded) -> one cohesive dark rim + frond undersides
for i = 0, nFronds - 1 do
    frond(fronds[i], 1.05, 1.3, frondTone(0.42))
end
-- 3. mid frond pass (body)
for i = 0, nFronds - 1 do
    frond(fronds[i], 1.0, 1.0, frondTone(1.0))
end
-- 4. bright midrib sheen (thin, near the base->mid; lit upper surface)
for i = 0, nFronds - 1 do
    frond(fronds[i], 0.72, 0.34, frondTone(1.35))
end

-- 5. crown boss: a small lit nub where the fronds meet the trunk top
do
    local p = Path()
    local r = trunkWv * 0.62
    for s = 0, 9 do
        local a = (s / 10) * 2 * math.pi
        p:addVertex(crownX + math.cos(a) * r, crownY + math.sin(a) * r * 0.8)
    end
    p:setColor(barkR * 0.68, barkG * 0.6, barkB * 0.48, 1.0); p:close(); asset:addPath(p)
end
