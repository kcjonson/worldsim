-- Ground grass: one seamless period x period meter tile variant (role ground), painted dark to
-- light, low to high. Everything is laid out first, then a coarse tone model of the near stack is
-- built from the layout, then two stacks are drawn, one over the other:
--
-- The far stack (mips 4 to 6, untagged) is the tone model averaged into one rect a mip 4 texel, so
-- the coarse mips keep the near stack's mean and its clump and patch variation, in its places.
-- A full-tile gap cover tagged setLod(kCover) hides it from mip 3 up.
--
-- The near stack (mips 0 to 3) is the grass itself: soil peeks, pebbles, dark undergrowth between
-- the clumps, clover and rosettes, then clumps of two-tone tuft fans around a lit crown, lighter at
-- the core and darker at the rim, then top blades. Only fine elements carry tags above kCover. Each
-- blade takes the model's tone where it lies over a proxy strip of that tone (kept to mip 3): the
-- light blade and its dark underlay average to the proxy, so the mips that drop the blade keep mip
-- 0's mean and its blanket over the tufts.
--
-- Meters, +Y down, sRGB colors 0..1. Shapes may cross the tile edge; the registry wraps them.
-- Every density field is periodicNoise2 with a whole number of cycles across the tile, every count
-- is fixed per square meter and the random draws that move color are normalized per variant, so
-- all variants share the same statistics. No light direction is baked in (the shader rotates
-- tiles freely): underlays and halos are centered.

local P = getFloat("period", 4)
local AREA = P * P
local TAU = math.pi * 2

local rand, floor, sqrt, sin, cos, atan, exp, log, min, max =
	math.random, math.floor, math.sqrt, math.sin, math.cos, math.atan, math.exp, math.log, math.min, math.max

local function clamp(v, lo, hi)
	return v < lo and lo or (v > hi and hi or v)
end
local function uniform(a, b)
	return a + (b - a) * rand()
end
local function signed()
	return rand() * 2 - 1
end
local function gauss()
	return sqrt(-2 * log(1 - rand())) * cos(TAU * rand())
end
-- Lognormal around the range's geometric middle, clamped to the range.
local function lognormal(lo, hi, sigma)
	return clamp(sqrt(lo * hi) * exp(sigma * gauss()), lo, hi)
end
local function count(perSquareMeter)
	return floor(perSquareMeter * AREA + 0.5)
end

-- Colors -------------------------------------------------------------------------------------

local function hex(key, default)
	local s = getString(key, default):gsub("#", "")
	return { tonumber(s:sub(1, 2), 16) / 255, tonumber(s:sub(3, 4), 16) / 255, tonumber(s:sub(5, 6), 16) / 255 }
end

local function toHsv(c)
	local r, g, b = c[1], c[2], c[3]
	local hi, lo = max(r, g, b), min(r, g, b)
	local d = hi - lo
	local h = 0
	if d > 0 then
		if hi == r then
			h = ((g - b) / d) % 6
		elseif hi == g then
			h = (b - r) / d + 2
		else
			h = (r - g) / d + 4
		end
		h = h / 6
	end
	return h, hi > 0 and d / hi or 0, hi
end

local function fromHsv(h, s, v)
	local i = floor(h * 6) % 6
	local f = h * 6 - floor(h * 6)
	local p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
	if i == 0 then
		return { v, t, p }
	elseif i == 1 then
		return { q, v, p }
	elseif i == 2 then
		return { p, v, t }
	elseif i == 3 then
		return { p, q, v }
	elseif i == 4 then
		return { t, p, v }
	end
	return { v, p, q }
end

-- c with its value scaled by vMul, hue turned by hueDeg (positive = warmer) and saturation scaled by sMul.
local function tone(c, vMul, hueDeg, sMul)
	local h, s, v = toHsv(c)
	return fromHsv((h - hueDeg / 360) % 1, clamp(s * sMul, 0, 1), clamp(v * vMul, 0, 1))
end

local function mix(a, b, t)
	return { a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t }
end

-- Params -------------------------------------------------------------------------------------

local function triple(key, default)
	local out = {}
	for v in getString(key, default):gmatch("[^,]+") do
		out[#out + 1] = tonumber(v)
	end
	return out
end
local farGain = triple("farGain", "1,1,1")
local farContrast = getFloat("farContrast", 1)
local farCoarseContrast = getFloat("farCoarseContrast", 1)
local farCellsPerMeter = getFloat("farCellsPerMeter", 8)
local modelUndergrowth = getFloat("modelUndergrowth", 0.45)
local modelClumpFill = getFloat("modelClumpFill", 0.9)
local gapColor = hex("gapColor", "#2A361A")

local soilPerM2 = getFloat("soilDensity", 9)
local soilLo, soilHi = getFloatRange("soilSize", 0.03, 0.12)
local soilColor = hex("soilColor", "#63513A")
local soilDark = hex("soilSpeckColor", "#4C3E2D")
local soilDry = hex("soilDryColor", "#7A6648")

local pebblePerM2 = getFloat("pebbleDensity", 24)
local pebbleLo, pebbleHi = getFloatRange("pebbleSize", 0.005, 0.02)
local pebbleColors = { hex("pebbleColorA", "#8A8274"), hex("pebbleColorB", "#A39A88"), hex("pebbleColorC", "#6F695E") }
local haloColor = hex("pebbleHaloColor", "#2A2A22")
local haloAlpha = getFloat("pebbleHaloAlpha", 0.5)

local underPerM2 = getFloat("undergrowthDensity", 280)
local underLenLo, underLenHi = getFloatRange("undergrowthLength", 0.04, 0.08)
local underHubLo, underHubHi = getFloatRange("undergrowthHubWidth", 0.009, 0.014)
local underSpikesLo, underSpikesHi = getFloatRange("undergrowthSpikes", 4, 7)
local underDark = hex("undergrowthDark", "#35491F")
local underLight = hex("undergrowthLight", "#435A27")

local rosettePerM2 = getFloat("rosetteDensity", 0.6)
local rosetteLenLo, rosetteLenHi = getFloatRange("rosetteLeafLength", 0.06, 0.14)
local rosetteColor = hex("rosetteColor", "#46703A")
local rosetteInner = hex("rosetteInnerColor", "#5C8A48")
local rosetteRib = hex("rosetteMidribColor", "#8AAE6E")

local cloverPatchPerM2 = getFloat("cloverPatches", 1.4)
local cloverPerLo, cloverPerHi = getFloatRange("cloverPerPatch", 6, 16)
local cloverSigmaLo, cloverSigmaHi = getFloatRange("cloverSpread", 0.05, 0.09)
local leafletLo, leafletHi = getFloatRange("cloverLeaflet", 0.022, 0.032)
local cloverColor = hex("cloverColor", "#5E9C4A")
local chevronColor = hex("cloverChevronColor", "#A8CC90")
local cloverTopShare = getFloat("cloverTopShare", 0.6)

local clumpSpacing = getFloat("clumpSpacing", 0.17)
local clumpRLo, clumpRHi = getFloatRange("clumpRadius", 0.045, 0.16)
local tuftsPerClumpLo, tuftsPerClumpHi = getFloatRange("tuftsPerClump", 5, 10)
local tuftSpread = getFloat("tuftSpread", 0.38)
local tuftReachLo, tuftReachHi = getFloatRange("tuftReach", 0.55, 0.95)
local crownReachLo, crownReachHi = getFloatRange("crownReach", 0.35, 0.6)
local crownSpikesLo, crownSpikesHi = getFloatRange("crownSpikes", 7, 11)
local tuftSpikesLo, tuftSpikesHi = getFloatRange("tuftSpikes", 4, 7)
local tuftHubLo, tuftHubHi = getFloatRange("tuftHubWidth", 0.012, 0.02)
local tuftDark = hex("tuftDark", "#4A672B")
local tuftLight = hex("tuftLight", "#587833")
local tuftHiDark = hex("tuftHighlightDark", "#648C38")
local tuftHiLight = hex("tuftHighlightLight", "#739C40")
local tuftLean = getFloat("tuftLean", 0.85)

local bladePerM2 = getFloat("bladeDensity", 240)
local bladeLenLo, bladeLenHi = getFloatRange("bladeLength", 0.07, 0.2)
local bladeWLo, bladeWHi = getFloatRange("bladeWidth", 0.012, 0.02)
local broadShare = getFloat("broadBladeShare", 0.25)
local broadWLo, broadWHi = getFloatRange("broadBladeWidth", 0.028, 0.038)
local bladeBow = getFloat("bladeBow", 0.15)
local bladeInTufts = getFloat("bladeTuftShare", 0.85)
local bladeFanSpread = getFloat("bladeFanSpread", 0.55)
local bladeRadialShare = getFloat("bladeRadialShare", 0.3)
local bladeGain = getFloat("bladeGain", 1.18)
local bladeTipGain = getFloat("bladeTipGain", 1.05)
local bladeHue = getFloat("bladeHue", 3)
local bladeTipHue = getFloat("bladeTipHue", 5)
local underlayColor = hex("underlayColor", "#161E0E")
local underlayAlpha = getFloat("underlayAlpha", 0.5)
local underlayWidth = getFloat("underlayWidth", 2.0)
local strawShare = getFloat("strawShare", 0.04)
local strawDark = hex("strawDark", "#9C9260")
local strawLight = hex("strawLight", "#B0A46E")
local dryTipShare = getFloat("dryTipShare", 0.12)
local tipHighlightShare = getFloat("tipHighlightShare", 0.2)
local flowSpread = getFloat("flowSpread", 0.5)

local densityContrast = getFloat("densityContrast", 1.6)
local clumpValue = getFloat("clumpValue", 0.12)
local clumpDome = getFloat("clumpDome", 0.14)
local clumpHue = getFloat("clumpHue", 12)
local clumpSaturation = getFloat("clumpSaturation", 0.1)
local patchHue = getFloat("patchHue", 5)
local patchValue = getFloat("patchValue", 0.03)
local elementValue = getFloat("elementValue", 0.05)
local elementHue = getFloat("elementHue", 4)
local lushDarken = getFloat("lushDarken", 0.03)
local lushSaturate = getFloat("lushSaturate", 0.05)

-- LOD tags: a path is baked into mip k only when its tag is at most that mip's px/m (128 / 2^k).
-- The cover sits between mip 4 (8 px/m) and mip 3 (16), so the near stack shows from mip 3 up and
-- the far stack alone below it. Fine elements keep the one-texel rule: tag 1 / width.
local kCover = 12
local function lodFor(width)
	return clamp(1 / width, kCover, 128)
end

-- Fields -------------------------------------------------------------------------------------

local noiseSeed = floor(rand() * 1000000)

-- Periodic noise with a whole number of cycles across the tile, so it wraps exactly.
local function field(x, y, cycles, salt)
	cycles = max(1, floor(cycles + 0.5))
	local f = cycles / P
	return periodicNoise2(x * f, y * f, cycles, noiseSeed + salt)
end

-- A field's mean over this tile, sampled on a grid. A few cycles of noise don't average to zero
-- over one period, so the fields that move color subtract it: otherwise each variant's mean drifts
-- with its seed and the hex cells paint a patchwork at far zoom.
local function tileMean(f)
	local n, sum = 16, 0
	for i = 0, n - 1 do
		for j = 0, n - 1 do
			sum = sum + f((i + 0.5) * P / n, (j + 0.5) * P / n)
		end
	end
	return sum / (n * n)
end

local function rawDensity(x, y)
	return 0.6 * field(x, y, P / 0.8, 11) + 0.4 * field(x, y, P / 0.4, 23)
end
local densityOffset = tileMean(rawDensity)

-- Grass density, 0 (thin, soil shows) to 1 (lush). 0.8 m and 0.4 m only: nothing in the tile
-- varies slower than that, the macro tint does.
local function density(x, y)
	return clamp(0.5 + (rawDensity(x, y) - densityOffset) * densityContrast, 0, 1)
end

-- Local blade direction: an isotropic vector field, so blades comb in patches with no global grain.
local function flowAngle(x, y)
	return atan(field(x, y, P, 37), field(x, y, P, 41))
end

-- A soft olive-to-green drift over patches of a meter or so, small next to the clump tones.
local patchValueOffset = tileMean(function(x, y)
	return field(x, y, 6, 59)
end)
local patchHueOffset = tileMean(function(x, y)
	return field(x, y, 3, 53)
end)
local function patchTone(x, y)
	return 1 + patchValue * (field(x, y, 6, 59) - patchValueOffset), patchHue * (field(x, y, 3, 53) - patchHueOffset)
end

-- Lush ground is a little darker and more saturated.
local function lush(c, d)
	return tone(c, 1 - lushDarken * (d - 0.5) * 2, 0, 1 + lushSaturate * (d - 0.5) * 2)
end

local function jitter(c, vAmp, hueAmp)
	return tone(c, 1 + vAmp * signed(), hueAmp * signed(), 1)
end

-- Emitting -----------------------------------------------------------------------------------

-- The binding methods, looked up once: a script this size makes hundreds of thousands of calls.
local pathProto = Path()
local addVertex, setColor, setLod, closePath = pathProto.addVertex, pathProto.setColor, pathProto.setLod, pathProto.close
local addPath = asset.addPath

local function finish(p, c, alpha, lod)
	setColor(p, c[1], c[2], c[3], alpha)
	if lod > 0 then
		setLod(p, lod)
	end
	closePath(p)
	addPath(asset, p)
end

local function rect(x0, y0, x1, y1, c, lod)
	local p = Path()
	addVertex(p, x0, y0)
	addVertex(p, x1, y0)
	addVertex(p, x1, y1)
	addVertex(p, x0, y1)
	finish(p, c, 1, lod)
end

-- An organic blob: radius r modulated by two low harmonics, stretched by `aspect`, turned by `rot`.
local function blob(x, y, r, n, wobble, aspect, rot, c, alpha, lod)
	local p = Path()
	local a2, a3 = rand() * TAU, rand() * TAU
	local w2, w3 = wobble * rand(), wobble * 0.6 * rand()
	local cr, sr = cos(rot), sin(rot)
	for i = 0, n - 1 do
		local a = TAU * i / n
		local rr = r * (1 + w2 * cos(2 * a + a2) + w3 * cos(3 * a + a3))
		local lx, ly = cos(a) * rr * aspect, sin(a) * rr
		addVertex(p, x + lx * cr - ly * sr, y + lx * sr + ly * cr)
	end
	finish(p, c, alpha, lod)
end

-- Spike angles for a fan of k spikes, evenly spread with jitter, then pulled toward lean angle
-- phi by strength s (< 1 keeps them in order, so the fan stays a simple star).
local function fanAngles(k, s, phi)
	local angles = {}
	local a0 = rand() * TAU
	local lx, ly = s * cos(phi), s * sin(phi)
	for i = 1, k do
		local a = a0 + (i - 1 + (rand() - 0.5) * 0.8) * TAU / k
		angles[i] = atan(sin(a) + ly, cos(a) + lx)
	end
	-- Unwrap into one ascending turn.
	for i = 2, k do
		while angles[i] < angles[i - 1] do
			angles[i] = angles[i] + TAU
		end
	end
	return angles
end

-- A fan: spikes from a hub, as one star polygon. hubHalf is a spike's half width at the hub;
-- bends are per-spike sideways curl as a fraction of length; withSides adds a mid point on each
-- side so spikes taper with a curve.
local function fan(x, y, angles, lens, bends, hubHalf, withSides, c, alpha, lod)
	local k = #angles
	local p = Path()
	for i = 1, k do
		local ai = angles[i]
		local nextI = i % k + 1
		local an = i < k and angles[i + 1] or angles[1] + TAU
		local dx, dy = cos(ai), sin(ai)
		local px, py = -dy, dx
		local L = lens[i]
		local b = bends[i] * L
		if withSides then
			local mx, my = x + dx * L * 0.5 + px * b * 0.3, y + dy * L * 0.5 + py * b * 0.3
			local hw = hubHalf * 0.5
			addVertex(p, mx - px * hw, my - py * hw)
			addVertex(p, x + dx * L + px * b, y + dy * L + py * b)
			addVertex(p, mx + px * hw, my + py * hw)
		else
			addVertex(p, x + dx * L + px * b, y + dy * L + py * b)
		end
		local r = clamp(2 * hubHalf / max(an - ai, 0.05), 0.002, 0.45 * min(L, lens[nextI]))
		local am = (ai + an) * 0.5
		addVertex(p, x + cos(am) * r, y + sin(am) * r)
	end
	finish(p, c, alpha, lod)
end

-- A blade from (x, y) along angle a: length L, base width w, bowed sideways by bow * L. Paints a
-- proxy strip in the ground tone under it (kept down to mip 3, so the mips that drop the blade
-- still cover what's beneath the way the blade does, at its mean), then a centered dark underlay
-- underlayWidth times as wide, a base segment and a lit tip segment, plus an optional tip highlight.
local bladeT = { 0, 0.28, 0.55, 0.78 }
local function blade(x, y, a, L, w, bow, proxyCol, baseCol, tipCol, highlight, lod)
	local dx, dy = cos(a), sin(a)
	local px, py = -dy, dx
	local cx, cy, hw = {}, {}, {}
	for i = 1, 4 do
		local t = bladeT[i]
		local off = bow * L * t * t
		cx[i] = x + dx * L * t + px * off
		cy[i] = y + dy * L * t + py * off
		hw[i] = w * 0.5 * (1 - t) ^ 0.7
	end
	local tx, ty = x + dx * L + px * bow * L, y + dy * L + py * bow * L

	local s = underlayWidth
	local ex, ey = tx + dx * L * 0.05, ty + dy * L * 0.05
	local ax, ay = px * hw[1] * s, py * hw[1] * s
	local bx, by = px * hw[3] * s, py * hw[3] * s
	local p = Path()
	addVertex(p, cx[1] + ax, cy[1] + ay)
	addVertex(p, cx[3] + bx, cy[3] + by)
	addVertex(p, ex, ey)
	addVertex(p, cx[3] - bx, cy[3] - by)
	addVertex(p, cx[1] - ax, cy[1] - ay)
	finish(p, proxyCol, 1, kCover)

	p = Path()
	addVertex(p, cx[1] + ax, cy[1] + ay)
	addVertex(p, cx[3] + bx, cy[3] + by)
	addVertex(p, ex, ey)
	addVertex(p, cx[3] - bx, cy[3] - by)
	addVertex(p, cx[1] - ax, cy[1] - ay)
	finish(p, underlayColor, underlayAlpha, lod)

	p = Path()
	for i = 1, 3 do
		addVertex(p, cx[i] + px * hw[i], cy[i] + py * hw[i])
	end
	for i = 3, 1, -1 do
		addVertex(p, cx[i] - px * hw[i], cy[i] - py * hw[i])
	end
	finish(p, baseCol, 1, lod)

	p = Path()
	addVertex(p, cx[3] + px * hw[3], cy[3] + py * hw[3])
	addVertex(p, cx[4] + px * hw[4], cy[4] + py * hw[4])
	addVertex(p, tx, ty)
	addVertex(p, cx[4] - px * hw[4], cy[4] - py * hw[4])
	addVertex(p, cx[3] - px * hw[3], cy[3] - py * hw[3])
	finish(p, tipCol, 1, lod)

	if highlight then
		local h = 0.6
		p = Path()
		addVertex(p, cx[4] + px * hw[4] * h, cy[4] + py * hw[4] * h)
		addVertex(p, tx, ty)
		addVertex(p, cx[4] - px * hw[4] * h, cy[4] - py * hw[4] * h)
		finish(p, tone(tipCol, 1.06, 0, 1), 1, 96)
	end
end

-- An obcordate clover leaflet from the center along angle a, length l.
local leafletShape = { 0, 0, 0.3, -0.38, 0.62, -0.5, 0.92, -0.36, 1.0, -0.12, 0.84, 0, 1.0, 0.12, 0.92, 0.36, 0.62, 0.5, 0.3, 0.38 }
local function leaflet(x, y, a, l, c, lod)
	local ca, sa = cos(a), sin(a)
	local p = Path()
	for i = 1, #leafletShape, 2 do
		local lx, ly = leafletShape[i] * l, leafletShape[i + 1] * l
		addVertex(p, x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	finish(p, c, 1, lod)
end

local chevronShape = { 0.4, -0.24, 0.62, 0, 0.4, 0.24, 0.5, 0 }
local function chevron(x, y, a, l, c, alpha)
	local ca, sa = cos(a), sin(a)
	local p = Path()
	for i = 1, #chevronShape, 2 do
		local lx, ly = chevronShape[i] * l, chevronShape[i + 1] * l
		addVertex(p, x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	finish(p, c, alpha, 96)
end

-- A trifoliate stays down to mip 3, where a patch of them averages to the patch's tone.
local function trifoliate(x, y, l, c)
	local a0 = rand() * TAU
	for i = 0, 2 do
		local a = a0 + i * TAU / 3 + (rand() - 0.5) * 0.5
		local ll = l * uniform(0.9, 1.1)
		leaflet(x, y, a, ll, jitter(c, 0.05, 2), kCover)
		chevron(x, y, a, ll, chevronColor, 0.55)
	end
end

-- A pointed broadleaf leaf from the center along angle a: length l, width w.
local function leaf(x, y, a, l, w, c, lod)
	local ca, sa = cos(a), sin(a)
	local p = Path()
	local n = 6
	for i = 0, n do
		local t = i / n
		local lx, ly = t * l, -sin(math.pi * t ^ 0.8) * w * 0.5
		addVertex(p, x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	for i = n - 1, 1, -1 do
		local t = i / n
		local lx, ly = t * l, sin(math.pi * t ^ 0.8) * w * 0.5
		addVertex(p, x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	finish(p, c, 1, lod)
end

local function midrib(x, y, a, l, w, c, lod)
	local ca, sa = cos(a), sin(a)
	local px, py = -sa, ca
	local p = Path()
	addVertex(p, x + px * w, y + py * w)
	addVertex(p, x + ca * l * 0.8, y + sa * l * 0.8)
	addVertex(p, x - px * w, y - py * w)
	finish(p, c, 1, lod)
end

-- Placement ----------------------------------------------------------------------------------

local function wrapDelta(d)
	if d > P * 0.5 then
		return d - P
	elseif d < -P * 0.5 then
		return d + P
	end
	return d
end

-- Poisson-disk points on the P x P torus (Bridson): no two closer than rMin across the wrap.
local function poissonTorus(rMin)
	local gn = floor(P / (rMin / sqrt(2)))
	local cell = P / gn
	local grid, pts, active = {}, {}, {}
	local r2 = rMin * rMin
	local function fits(x, y)
		local gx, gy = floor(x / cell), floor(y / cell)
		for ox = -2, 2 do
			for oy = -2, 2 do
				local q = grid[((gx + ox) % gn) * gn + (gy + oy) % gn]
				if q then
					local dx, dy = wrapDelta(q[1] - x), wrapDelta(q[2] - y)
					if dx * dx + dy * dy < r2 then
						return false
					end
				end
			end
		end
		return true
	end
	local function add(x, y)
		local pt = { x, y }
		pts[#pts + 1] = pt
		active[#active + 1] = pt
		grid[floor(x / cell) * gn + floor(y / cell)] = pt
	end
	add(rand() * P, rand() * P)
	while #active > 0 do
		local i = floor(rand() * #active) + 1
		local base = active[i]
		local placed = false
		for _ = 1, 24 do
			local a, r = rand() * TAU, rMin * (1 + rand())
			local x, y = (base[1] + cos(a) * r) % P, (base[2] + sin(a) * r) % P
			if fits(x, y) then
				add(x, y)
				placed = true
				break
			end
		end
		if not placed then
			active[i] = active[#active]
			active[#active] = nil
		end
	end
	return pts
end

-- n points by dart throwing with a minimum spacing across the wrap (falls back to the closest fit).
local function blueNoise(n, spacing, accept)
	local pts = {}
	local s2 = spacing * spacing
	for _ = 1, n do
		local best, bestD = nil, -1
		for _ = 1, 30 do
			local x, y = rand() * P, rand() * P
			if accept == nil or accept(x, y) then
				local d = math.huge
				for j = 1, #pts do
					local dx, dy = wrapDelta(pts[j][1] - x), wrapDelta(pts[j][2] - y)
					d = min(d, dx * dx + dy * dy)
				end
				if d >= s2 then
					best = { x, y }
					break
				elseif d > bestD then
					best, bestD = { x, y }, d
				end
			end
		end
		if best then
			pts[#pts + 1] = best
		end
	end
	return pts
end

-- n points accepted with probability weight(x, y) (0..1).
local function weighted(n, weight)
	local pts = {}
	local guard = n * 200
	while #pts < n and guard > 0 do
		guard = guard - 1
		local x, y = rand() * P, rand() * P
		if rand() < weight(x, y) then
			pts[#pts + 1] = { x, y }
		end
	end
	return pts
end

local function shuffle(t)
	for i = #t, 2, -1 do
		local j = floor(rand() * i) + 1
		t[i], t[j] = t[j], t[i]
	end
end

-- Layout (everything placed before anything is drawn, so the far stack can mirror it) ---------

-- Soil peeks where the grass is thin.
local soils = weighted(count(soilPerM2), function(x, y)
	local d = density(x, y)
	return (1 - d) ^ 3
end)
for _, s in ipairs(soils) do
	s.r = lognormal(soilLo, soilHi, 0.5) * 0.5
end

-- Clumps: Poisson-disk centers, each a disc of radius R holding a Thomas cluster of tufts that share
-- a tone and a lean, around a lit crown. Tufts at the rim flop outward and sit lower, so a clump
-- shades like a dome seen from straight above: light core, darker rim, no light direction. Thin
-- ground grows smaller clumps, so more of the dark ground between them shows.
--
-- The random parts of size and tone are normalized over the variant (the summed area to what the
-- spacing and size range give, tones to zero mean), so coverage and mean don't drift from seed to
-- seed with the clump count or the draws.
local clumps = {}
do
	local sizeSigma = 0.4
	local sumR2, sumV, sumH, sumS = 0, 0, 0, 0
	for _, cl in ipairs(poissonTorus(clumpSpacing)) do
		local c = {
			x = cl[1],
			y = cl[2],
			base = lognormal(clumpRLo, clumpRHi, sizeSigma),
			dv = signed(),
			dh = signed(),
			ds = signed(),
			lean = tuftLean * uniform(0.4, 1),
		}
		sumR2, sumV, sumH, sumS = sumR2 + c.base * c.base, sumV + c.dv, sumH + c.dh, sumS + c.ds
		clumps[#clumps + 1] = c
	end
	local n = max(1, #clumps)
	-- Bridson's sampling packs about 0.67 / spacing^2 points a square meter.
	local targetR2 = AREA * 0.67 / (clumpSpacing * clumpSpacing) * clumpRLo * clumpRHi * exp(2 * sizeSigma * sizeSigma)
	local sizeScale = sqrt(targetR2 / max(sumR2, 1e-9))
	local mv, mh, ms = sumV / n, sumH / n, sumS / n
	for _, c in ipairs(clumps) do
		c.d = density(c.x, c.y)
		local pv, ph = patchTone(c.x, c.y)
		c.R = c.base * sizeScale * lerp(0.65, 1.1, c.d)
		c.v = (1 + clumpValue * (c.dv - mv)) * pv
		c.h = clumpHue * (c.dh - mh) + ph
		c.s = 1 + clumpSaturation * (c.ds - ms)
		c.phi = flowAngle(c.x, c.y) + gauss() * 0.8
		local body = lush(tone(mix(tuftDark, tuftLight, 0.5), c.v, c.h, c.s), c.d)
		local hi = lush(tone(mix(tuftHiDark, tuftHiLight, 0.5), c.v, c.h, c.s), c.d)
		c.tone = mix(body, hi, 0.4)
		c.crown = lush(tone(mix(tuftHiDark, tuftHiLight, 0.5), c.v * (1 + clumpDome * 0.8), c.h, c.s), c.d)
	end
end

local tufts = {}
for _, c in ipairs(clumps) do
	local sigma = c.R * tuftSpread
	for _ = 1, floor(lerp(tuftsPerClumpLo, tuftsPerClumpHi, c.d) + rand()) do
		local ox, oy = gauss() * sigma, gauss() * sigma
		local out = min(1, sqrt(ox * ox + oy * oy) / c.R)
		local ux, uy = cos(c.phi) * c.lean + ox / sigma * 0.3 * out, sin(c.phi) * c.lean + oy / sigma * 0.3 * out
		tufts[#tufts + 1] = {
			(c.x + ox) % P,
			(c.y + oy) % P,
			clump = c,
			v = c.v * (1 + clumpDome * (1 - 2 * out)) * (1 + 0.03 * signed()),
			lean = min(0.9, sqrt(ux * ux + uy * uy)),
			phi = atan(uy, ux) + gauss() * 0.35,
			len = clamp(c.R * uniform(tuftReachLo, tuftReachHi), 0.025, 0.15),
		}
	end
end
shuffle(tufts)
for _, t in ipairs(tufts) do
	local c = t.clump
	t.k = floor(uniform(tuftSpikesLo, tuftSpikesHi + 1))
	t.hub = uniform(tuftHubLo, tuftHubHi) * 0.5
	t.body = lush(tone(mix(tuftDark, tuftLight, rand()), t.v, c.h, c.s), c.d)
	t.hi = lush(tone(mix(tuftHiDark, tuftHiLight, rand()), t.v, c.h, c.s), c.d)
end

-- Clover patches: Thomas clusters around blue-noise parents.
local patches = blueNoise(count(cloverPatchPerM2), 0.6 / sqrt(max(cloverPatchPerM2, 0.01)))
local clovers = {}
for _, pa in ipairs(patches) do
	pa.sigma = uniform(cloverSigmaLo, cloverSigmaHi)
	pa.c = tone(cloverColor, 1 + 0.05 * signed(), 3 * signed(), 1)
	pa.n = floor(uniform(cloverPerLo, cloverPerHi + 1))
	for _ = 1, pa.n do
		clovers[#clovers + 1] = {
			(pa[1] + gauss() * pa.sigma) % P,
			(pa[2] + gauss() * pa.sigma) % P,
			l = uniform(leafletLo, leafletHi),
			c = pa.c,
			top = rand() < cloverTopShare,
		}
	end
end
shuffle(clovers)

-- Broadleaf rosettes (blue noise).
local rosettes = blueNoise(count(rosettePerM2), 0.8 / sqrt(max(rosettePerM2, 0.01)))
for _, ro in ipairs(rosettes) do
	ro.k = floor(uniform(5, 9))
	ro.a0 = rand() * TAU
	ro.c = jitter(rosetteColor, 0.05, 3)
	ro.inner = jitter(rosetteInner, 0.04, 3)
	ro.leaves = {}
	local sum = 0
	for i = 1, ro.k do
		local l = uniform(rosetteLenLo, rosetteLenHi)
		ro.leaves[i] = { a = ro.a0 + (i - 1 + (rand() - 0.5) * 0.5) * TAU / ro.k, l = l, w = l * uniform(0.3, 0.4), v = uniform(0.95, 1.05) }
		sum = sum + l
	end
	ro.meanLen = sum / ro.k
end

-- Tone model ---------------------------------------------------------------------------------

-- A coarse model of the near stack before its blades: linear light on a ~3 cm grid, each feature
-- painted over what's below by the share of its area it covers. Blades take their tone from it, so
-- each blade averages to the ground it lies on, and the far stack is it, averaged.

-- Built in a function of its own: the main chunk is near Lua's 200-local limit. Returns the
-- model's sRGB color at (x, y) and the share of (x, y) its clumps cover, after drawing the far
-- stack from it.
local groundAt, clumpCover = (function()
local function toLinear(c)
	local out = {}
	for i = 1, 3 do
		local v = c[i]
		out[i] = v <= 0.04045 and v / 12.92 or ((v + 0.055) / 1.055) ^ 2.4
	end
	return out
end
local function toSrgb(l)
	local out = {}
	for i = 1, 3 do
		local v = clamp(l[i], 0, 1)
		out[i] = v <= 0.0031308 and v * 12.92 or 1.055 * v ^ (1 / 2.4) - 0.055
	end
	return out
end

local gridN = floor(P / 0.03125 + 0.5)
local cellSize = P / gridN
local gridR, gridG, gridB, cover = {}, {}, {}, {}
do
	local g = toLinear(mix(gapColor, mix(underDark, underLight, 0.5), modelUndergrowth))
	for k = 1, gridN * gridN do
		gridR[k], gridG[k], gridB[k], cover[k] = g[1], g[2], g[3], 0
	end
end

-- Paints a disc of radius r at (cx, cy), wrapped: fn(out) gives the linear color and the covered
-- share at out = distance / r (0..1).
local function paint(cx, cy, r, fn)
	local i0, i1 = floor((cx - r) / cellSize), floor((cx + r) / cellSize)
	local j0, j1 = floor((cy - r) / cellSize), floor((cy + r) / cellSize)
	for j = j0, j1 do
		local py = (j + 0.5) * cellSize - cy
		local row = (j % gridN) * gridN + 1
		for i = i0, i1 do
			local px = (i + 0.5) * cellSize - cx
			local out = sqrt(px * px + py * py) / r
			if out < 1 then
				local c, w = fn(out)
				if w > 0 then
					local k = row + i % gridN
					gridR[k] = gridR[k] + (c[1] - gridR[k]) * w
					gridG[k] = gridG[k] + (c[2] - gridG[k]) * w
					gridB[k] = gridB[k] + (c[3] - gridB[k]) * w
				end
			end
		end
	end
end

for _, s in ipairs(soils) do
	local c = toLinear(soilColor)
	paint(s[1], s[2], s.r * 1.1, function(out)
		return c, 0.6 * (1 - out * out)
	end)
end
-- Clumps, inlined: they are most of the painting. A clump's tone runs from its crown at the
-- center through the dome to its rim, over 1.2 radii, fading out past 0.75.
do
	local reach = 1.2
	local tr, tg, tb = {}, {}, {}
	for _, c in ipairs(clumps) do
		local core = toLinear(c.crown)
		for i = 0, 12 do
			local out = i / 10
			local col = toLinear(tone(c.tone, 1 + clumpDome * (1 - 2 * min(out, 1)), 0, 1))
			if out < 0.5 then
				col = mix(core, col, out / 0.5)
			end
			tr[i], tg[i], tb[i] = col[1], col[2], col[3]
		end
		local r = c.R * reach
		local inv = 1 / c.R
		local i0, i1 = floor((c.x - r) / cellSize), floor((c.x + r) / cellSize)
		local j0, j1 = floor((c.y - r) / cellSize), floor((c.y + r) / cellSize)
		for j = j0, j1 do
			local py = ((j + 0.5) * cellSize - c.y) * inv
			local row = (j % gridN) * gridN + 1
			for i = i0, i1 do
				local px = ((i + 0.5) * cellSize - c.x) * inv
				local out = sqrt(px * px + py * py)
				if out < reach then
					local t = clamp((out - 0.75) / (reach - 0.75), 0, 1)
					local w = modelClumpFill * (1 - t * t * (3 - 2 * t))
					local q = floor(out * 10 + 0.5)
					local k = row + i % gridN
					cover[k] = cover[k] + (1 - cover[k]) * w
					gridR[k] = gridR[k] + (tr[q] - gridR[k]) * w
					gridG[k] = gridG[k] + (tg[q] - gridG[k]) * w
					gridB[k] = gridB[k] + (tb[q] - gridB[k]) * w
				end
			end
		end
	end
end
for _, ro in ipairs(rosettes) do
	local c = toLinear(mix(ro.c, ro.inner, 0.35))
	paint(ro[1], ro[2], ro.meanLen, function(out)
		return c, 0.7 * (1 - out * out)
	end)
end
for _, pa in ipairs(patches) do
	local c = toLinear(pa.c)
	-- Trifoliates cover about 0.35 l^2 a leaflet; the patch is Gaussian with sigma pa.sigma.
	local peak = pa.n * 3 * 0.35 * (0.5 * (leafletLo + leafletHi)) ^ 2 / (TAU * pa.sigma * pa.sigma)
	paint(pa[1], pa[2], pa.sigma * 2.5, function(out)
		local r = out * 2.5
		return c, 1 - exp(-peak * exp(-0.5 * r * r))
	end)
end


-- Far stack ----------------------------------------------------------------------------------

-- The model averaged into one rect a mip 4 texel (farCellsPerMeter), its color scaled by farGain:
-- what mips 4 to 6 show, in the near stack's places. Contrast is set in two bands, so mip 4 and mips
-- 5 and 6 can each match mip 0: detail finer than a few cells (each cell against a blur of its
-- neighbors) by farContrast, and the coarser drift by farCoarseContrast.
do
	local farN = max(1, floor(P * farCellsPerMeter + 0.5))
	local sumR, sumG, sumB, cnt = {}, {}, {}, {}
	for k = 1, farN * farN do
		sumR[k], sumG[k], sumB[k], cnt[k] = 0, 0, 0, 0
	end
	local mR, mG, mB = 0, 0, 0
	for j = 0, gridN - 1 do
		local fj = floor(j * farN / gridN) * farN + 1
		for i = 0, gridN - 1 do
			local k = j * gridN + i + 1
			local f = fj + floor(i * farN / gridN)
			sumR[f], sumG[f], sumB[f], cnt[f] = sumR[f] + gridR[k], sumG[f] + gridG[k], sumB[f] + gridB[k], cnt[f] + 1
			mR, mG, mB = mR + gridR[k], mG + gridG[k], mB + gridB[k]
		end
	end
	local nn = gridN * gridN
	mR, mG, mB = mR / nn, mG / nn, mB / nn
	local avg = { {}, {}, {} }
	for f = 1, farN * farN do
		local n = max(1, cnt[f])
		avg[1][f], avg[2][f], avg[3][f] = sumR[f] / n, sumG[f] / n, sumB[f] / n
	end
	-- A wrapped 5-tap box blur, twice along each axis.
	local function blur(src)
		local a, b = src, {}
		for _ = 1, 2 do
			for j = 0, farN - 1 do
				for i = 0, farN - 1 do
					local sum = 0
					for o = -2, 2 do
						sum = sum + a[j * farN + (i + o) % farN + 1]
					end
					b[j * farN + i + 1] = sum / 5
				end
			end
			a, b = b, {}
			for j = 0, farN - 1 do
				for i = 0, farN - 1 do
					local sum = 0
					for o = -2, 2 do
						sum = sum + a[((j + o) % farN) * farN + i + 1]
					end
					b[j * farN + i + 1] = sum / 5
				end
			end
			a, b = b, {}
		end
		return a
	end
	local means = { mR, mG, mB }
	local fs = P / farN
	local coarse = { blur(avg[1]), blur(avg[2]), blur(avg[3]) }
	for j = 0, farN - 1 do
		for i = 0, farN - 1 do
			local f = j * farN + i + 1
			local lin = {}
			for ch = 1, 3 do
				local m, a, cc = means[ch], avg[ch][f], coarse[ch][f]
				lin[ch] = (m + (a - cc) * farContrast + (cc - m) * farCoarseContrast) * farGain[ch]
			end
			rect(i * fs, j * fs, (i + 1) * fs, (j + 1) * fs, toSrgb(lin), 0)
		end
	end
end

local function cell(x, y)
	return (floor(y / cellSize) % gridN) * gridN + floor(x / cellSize) % gridN + 1
end
return function(x, y)
	local k = cell(x, y)
	return toSrgb({ gridR[k], gridG[k], gridB[k] })
end, function(x, y)
	return cover[cell(x, y)]
end
end)()


-- Near stack ---------------------------------------------------------------------------------

rect(0, 0, P, P, gapColor, kCover)

-- Soil peeks: two or three overlapping blobs, a few darker specks, sometimes a dry edge.
for _, s in ipairs(soils) do
	if rand() < 0.15 then
		blob(s[1], s[2], s.r * 1.12, 12, 0.25, 1, 0, soilDry, 1, kCover)
	end
	local parts = rand() < 0.5 and 2 or 3
	for _ = 1, parts do
		local ox, oy = gauss() * s.r * 0.35, gauss() * s.r * 0.35
		blob(s[1] + ox, s[2] + oy, s.r * uniform(0.6, 0.9), 12, 0.3, uniform(0.75, 1.3), rand() * TAU, jitter(soilColor, 0.04, 2), 1, kCover)
	end
	for _ = 1, floor(uniform(1, 4)) do
		local ox, oy = gauss() * s.r * 0.4, gauss() * s.r * 0.4
		blob(s[1] + ox, s[2] + oy, s.r * uniform(0.08, 0.16), 6, 0.2, 1, 0, soilDark, 1, 64)
	end
end

-- Pebbles, mostly on the soil, each over a soft centered halo.
for _ = 1, count(pebblePerM2) do
	local x, y
	if #soils > 0 and rand() < 0.7 then
		local s = soils[floor(rand() * #soils) + 1]
		x, y = s[1] + gauss() * s.r * 0.6, s[2] + gauss() * s.r * 0.6
	else
		x, y = rand() * P, rand() * P
	end
	local r = lognormal(pebbleLo, pebbleHi, 0.5) * 0.5
	local lod = lodFor(r * 2)
	local sides = floor(uniform(6, 10))
	local aspect, rot = uniform(0.7, 1.0), rand() * TAU
	blob(x, y, r * 1.25, sides, 0.15, aspect, rot, haloColor, haloAlpha, lod)
	blob(x, y, r, sides, 0.15, aspect, rot, jitter(pebbleColors[floor(rand() * 3) + 1], 0.04, 0), 1, lod)
end

-- Dark undergrowth fans in the ground between clumps, undergrowthDensity a square meter of it (the
-- ones a clump would hide aren't drawn), thinner where the grass is thin.
local gapShare = 1 - tileMean(clumpCover)
for _, u in ipairs(weighted(count(underPerM2 * gapShare), function(x, y)
	return (0.6 + 0.4 * density(x, y)) * (1 - clumpCover(x, y))
end)) do
	local k = floor(uniform(underSpikesLo, underSpikesHi + 1))
	local angles = fanAngles(k, uniform(0.4, 0.8), rand() * TAU)
	local lens, bends = {}, {}
	for i = 1, k do
		lens[i] = uniform(underLenLo, underLenHi) * uniform(0.7, 1)
		bends[i] = gauss() * 0.2
	end
	local pv, ph = patchTone(u[1], u[2])
	local c = lush(tone(jitter(mix(underDark, underLight, rand()), elementValue, elementHue), pv, ph, 1), density(u[1], u[2]))
	fan(u[1], u[2], angles, lens, bends, uniform(underHubLo, underHubHi) * 0.5, false, c, 1, kCover)
end

-- Clover under the tufts.
for _, cl in ipairs(clovers) do
	if not cl.top then
		trifoliate(cl[1], cl[2], cl.l, cl.c)
	end
end

-- Tufts: every body fan in its clump's tone, then each clump's lit crown, then the lighter fans on
-- top, turned off the body's spikes.
for _, t in ipairs(tufts) do
	local angles = fanAngles(t.k, t.lean, t.phi)
	local lens, bends = {}, {}
	for i = 1, t.k do
		local along = cos(angles[i] - t.phi)
		lens[i] = t.len * uniform(0.6, 1.15) * (1 + 0.5 * t.lean * along)
		bends[i] = gauss() * 0.12
	end
	fan(t[1], t[2], angles, lens, bends, t.hub, true, t.body, 1, kCover)
end

for _, c in ipairs(clumps) do
	local k = floor(uniform(crownSpikesLo, crownSpikesHi + 1))
	local angles = fanAngles(k, c.lean * 0.6, c.phi)
	local lens, bends = {}, {}
	for i = 1, k do
		lens[i] = c.R * uniform(crownReachLo, crownReachHi) * (1 + 0.4 * c.lean * cos(angles[i] - c.phi))
		bends[i] = gauss() * 0.15
	end
	local shift = c.R * 0.15 * c.lean
	fan(c.x + cos(c.phi) * shift, c.y + sin(c.phi) * shift, angles, lens, bends, 0.007, true, c.crown, 1, kCover)
end

for _, t in ipairs(tufts) do
	local hk = floor(uniform(3, 7))
	local hAngles = fanAngles(hk, t.lean, t.phi)
	local hLens, hBends = {}, {}
	for i = 1, hk do
		hLens[i] = t.len * uniform(0.6, 0.75) * (1 + 0.5 * t.lean * cos(hAngles[i] - t.phi))
		hBends[i] = gauss() * 0.12
	end
	-- The lit fan sits toward the lean: the part of a flopped tuft lying on top.
	local shift = t.len * 0.2 * t.lean
	fan(t[1] + cos(t.phi) * shift, t[2] + sin(t.phi) * shift, hAngles, hLens, hBends, t.hub * 0.7, false, t.hi, 1, kCover)
end

-- Rosettes, over the tuft fans and under the top blades that cross them.
for _, ro in ipairs(rosettes) do
	for _, lf in ipairs(ro.leaves) do
		leaf(ro[1], ro[2], lf.a, lf.l, lf.w, tone(ro.c, lf.v, 0, 1), kCover)
		-- The lit inner leaf, cel-shaded on the same axis.
		leaf(ro[1] + cos(lf.a) * lf.l * 0.12, ro[2] + sin(lf.a) * lf.l * 0.12, lf.a, lf.l * 0.72, lf.w * 0.6, tone(ro.inner, lf.v, 0, 1), kCover)
		midrib(ro[1], ro[2], lf.a, lf.l, lf.w * 0.08, rosetteRib, 48)
	end
end

-- Clover over the tufts.
for _, cl in ipairs(clovers) do
	if cl.top then
		trifoliate(cl[1], cl[2], cl.l, cl.c)
	end
end

-- Top blades: most grow from tuft crowns and fall with the tuft's lean (a few radiate); the rest
-- follow the local flow anywhere. Each segment takes the ground tone where it lies, brightened by
-- bladeGain, so a blade over a clump's core is light and one reaching into a gap is dark: the
-- blades carry texture, not tone, and the mips that drop them keep mip 0's mean and patches.
do
	local n = count(bladePerM2)
	local nTuft = #tufts > 0 and floor(n * bladeInTufts + 0.5) or 0
	for i = 1, n do
		local x, y, a
		if i <= nTuft then
			local t = tufts[floor(rand() * #tufts) + 1]
			local ox, oy = gauss() * 0.012, gauss() * 0.012
			x, y = t[1] + ox, t[2] + oy
			if rand() < bladeRadialShare then
				local radial = atan(oy, ox) + gauss() * 0.45
				a = atan(sin(radial) + t.lean * sin(t.phi), cos(radial) + t.lean * cos(t.phi))
			else
				a = t.phi + gauss() * bladeFanSpread
			end
		else
			x, y = rand() * P, rand() * P
			a = flowAngle(x, y) + gauss() * flowSpread
		end
		local L = lognormal(bladeLenLo, bladeLenHi, 0.3)
		-- A share of broad blades: they stay in the coarser near mips, where the fine ones drop out.
		local w = rand() < broadShare and uniform(broadWLo, broadWHi) or uniform(bladeWLo, bladeWHi) * uniform(0.6, 1.0)
		local bow = bladeBow * gauss()
		local dx, dy = cos(a), sin(a)
		local baseGround = groundAt(x + dx * L * 0.28, y + dy * L * 0.28)
		local tipGround = groundAt(x + dx * L * 0.75, y + dy * L * 0.75)
		local proxy = mix(baseGround, tipGround, 0.45)
		local baseCol, tipCol
		if rand() < strawShare then
			baseCol = jitter(mix(strawDark, strawLight, rand()), elementValue, elementHue)
			tipCol = tone(baseCol, 1.05, 0, 0.95)
			w = w * 0.7
		else
			local e = bladeGain * (1 + elementValue * signed())
			local eh = elementHue * signed()
			baseCol = tone(baseGround, e, bladeHue + eh, 1)
			tipCol = tone(tipGround, e * bladeTipGain, bladeTipHue + eh, 1)
			if rand() < dryTipShare then
				tipCol = mix(tipCol, strawDark, 0.45)
			end
		end
		blade(x, y, a, L, w, bow, proxy, baseCol, tipCol, rand() < tipHighlightShare, lodFor(w))
	end
end
