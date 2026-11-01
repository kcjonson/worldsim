-- Ground grass: one seamless period x period meter tile variant (role ground). The game's camera
-- looks down from a hair off vertical, so whatever grows stands up the screen (toward -Y) and what
-- roots lower on screen is nearer. The tile is painted that way: the flat ground first (earthy soil,
-- dark litter, soil peeks, pebbles, rosettes), then everything standing, back to front by root.
--
-- Everything is laid out first (the undergrowth into the holes the clumps leave, found on a cover
-- grid, until a set share of ground is left open), then a coarse tone model of the near stack is
-- built from the layout, then two stacks are drawn, one over the other:
--
-- The far stack (mips 4 to 6, untagged) is the tone model averaged into one rect a mip 4 texel and
-- scaled to the near stack's measured mean, so the coarse mips keep mip 0's mean and its clump and
-- patch variation, in its places. A full-tile ground cover tagged setLod(kCover) hides it from mip 3
-- up.
--
-- The near stack (mips 0 to 3) is the grass itself: the ground, then in depth order undergrowth fans,
-- clover, loose blades, and clumps of upright two-tone tuft fans, each with the blades that grow from
-- it. Only fine elements carry tags above kCover. Each blade sits on a proxy strip (kept to mip 3)
-- colored the blade and its dark underlay averaged, so the mips that drop the blade keep mip 0's
-- mean.
--
-- Meters, +Y down, sRGB colors 0..1. Every density field is periodicNoise2 with a whole number of
-- cycles across the tile, every count is fixed per square meter or per share of open ground and the
-- random draws that move color are normalized per variant, so all variants share the same
-- statistics. Nothing is lit from a side (the shader may mirror cells left to right, never turn
-- them): leans are symmetric about vertical and underlays are centered. Only up is special.

local P = getFloat("period", 4)
local AREA = P * P
local TAU = math.pi * 2
local UP = -math.pi / 2

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

-- Params -------------------------------------------------------------------------------------

local function triple(key, default)
	local out = {}
	for v in getString(key, default):gmatch("[^,]+") do
		out[#out + 1] = tonumber(v)
	end
	return out
end
local farMean = triple("farMean", "83,106,51")
farMean = toLinear({ farMean[1] / 255, farMean[2] / 255, farMean[3] / 255 })
local farContrast = getFloat("farContrast", 1)
local farCoarseContrast = getFloat("farCoarseContrast", 1)
local farCellsPerMeter = getFloat("farCellsPerMeter", 8)
local modelUndergrowth = getFloat("modelUndergrowth", 0.3)
local modelClumpFill = getFloat("modelClumpFill", 0.8)
local groundColor = hex("groundColor", "#3A3320")

-- The flat ground's and the accents' knobs live in tables: the main chunk is near Lua's 200-local
-- limit.
local litter = {
	perM2 = getFloat("litterDensity", 160),
	colors = { hex("litterDark", "#2A2416"), hex("litterMid", "#4A4428"), hex("litterDry", "#6A5E3C") },
	shares = triple("litterShares", "0.6,0.32"),
}
litter.lo, litter.hi = getFloatRange("litterSize", 0.012, 0.03)

local soil = {
	perM2 = getFloat("soilDensity", 9),
	color = hex("soilColor", "#5E4C34"),
	speck = hex("soilSpeckColor", "#45382A"),
	dry = hex("soilDryColor", "#75644A"),
}
soil.lo, soil.hi = getFloatRange("soilSize", 0.03, 0.12)

local pebble = {
	perM2 = getFloat("pebbleDensity", 10),
	colors = { hex("pebbleColorA", "#8A8274"), hex("pebbleColorB", "#A39A88"), hex("pebbleColorC", "#6F695E") },
	halo = hex("pebbleHaloColor", "#2A2A22"),
	haloAlpha = getFloat("pebbleHaloAlpha", 0.5),
}
pebble.lo, pebble.hi = getFloatRange("pebbleSize", 0.006, 0.02)

local underPerM2 = getFloat("undergrowthDensity", 220)
local underLenLo, underLenHi = getFloatRange("undergrowthLength", 0.035, 0.06)
local underHubLo, underHubHi = getFloatRange("undergrowthHubWidth", 0.008, 0.012)
local underSpikesLo, underSpikesHi = getFloatRange("undergrowthSpikes", 5, 7)
local underFanLo, underFanHi = getFloatRange("undergrowthFan", 0.9, 1.25)
local underFill = getFloat("undergrowthFill", 0.5)
local groundShow = getFloat("groundShow", 0.1)
local underDark = hex("undergrowthDark", "#3A5224")
local underLight = hex("undergrowthLight", "#48652E")

local rosette = {
	perM2 = getFloat("rosetteDensity", 0.6),
	color = hex("rosetteColor", "#46703A"),
	inner = hex("rosetteInnerColor", "#5C8A48"),
	rib = hex("rosetteMidribColor", "#8AAE6E"),
}
rosette.lenLo, rosette.lenHi = getFloatRange("rosetteLeafLength", 0.06, 0.14)

local clover = {
	patchesPerM2 = getFloat("cloverPatches", 1.4),
	color = hex("cloverColor", "#5E9C4A"),
	chevron = hex("cloverChevronColor", "#A8CC90"),
}
clover.perLo, clover.perHi = getFloatRange("cloverPerPatch", 6, 16)
clover.sigmaLo, clover.sigmaHi = getFloatRange("cloverSpread", 0.05, 0.09)
clover.leafletLo, clover.leafletHi = getFloatRange("cloverLeaflet", 0.022, 0.032)

local clumpSpacing = getFloat("clumpSpacing", 0.2)
local clumpRLo, clumpRHi = getFloatRange("clumpRadius", 0.08, 0.24)
local clumpSway = getFloat("clumpSway", 0.12)
local tuftsPerClumpLo, tuftsPerClumpHi = getFloatRange("tuftsPerClump", 7, 13)
local tuftSpread = getFloat("tuftSpread", 0.4)
local tuftLenLo, tuftLenHi = getFloatRange("tuftLength", 0.035, 0.08)
local tuftFanLo, tuftFanHi = getFloatRange("tuftFan", 0.3, 0.55)
local tuftFanOut = getFloat("tuftFanOut", 0.35)
local tuftBend = getFloat("tuftBend", 0.15)
local tuftFill = getFloat("tuftFill", 0.6)
local tuftSpikesLo, tuftSpikesHi = getFloatRange("tuftSpikes", 4, 7)
local tuftHubLo, tuftHubHi = getFloatRange("tuftHubWidth", 0.012, 0.02)
local tuftDark = hex("tuftDark", "#4F6D2D")
local tuftLight = hex("tuftLight", "#5C7F39")
local tuftHiDark = hex("tuftHighlightDark", "#658C3D")
local tuftHiLight = hex("tuftHighlightLight", "#709A44")

local bladePerM2 = getFloat("bladeDensity", 220)
local bladeLenLo, bladeLenHi = getFloatRange("bladeLength", 0.05, 0.12)
local bladeWLo, bladeWHi = getFloatRange("bladeWidth", 0.014, 0.022)
local broadShare = getFloat("broadBladeShare", 0.25)
local broadWLo, broadWHi = getFloatRange("broadBladeWidth", 0.028, 0.038)
local bladeBow = getFloat("bladeBow", 0.15)
local bladeLean = getFloat("bladeLean", 0.3)
local bladeInTufts = getFloat("bladeTuftShare", 0.7)
local bladeGain = getFloat("bladeGain", 1.1)
local bladeTipGain = getFloat("bladeTipGain", 1.12)
local bladeHue = getFloat("bladeHue", 0)
local bladeTipHue = getFloat("bladeTipHue", 2)
local underlayColor = hex("underlayColor", "#161E0E")
local underlayAlpha = getFloat("underlayAlpha", 0.55)
local underlayWidth = getFloat("underlayWidth", 2.2)
local strawShare = getFloat("strawShare", 0.05)
local strawDark = hex("strawDark", "#9C9260")
local strawLight = hex("strawLight", "#B0A46E")
local dryTipShare = getFloat("dryTipShare", 0.2)
local tipHighlightShare = getFloat("tipHighlightShare", 0.1)

local densityContrast = getFloat("densityContrast", 1.6)
local clumpValue = getFloat("clumpValue", 0.12)
local clumpDome = getFloat("clumpDome", 0.12)
local clumpHue = getFloat("clumpHue", 14)
local clumpSaturation = getFloat("clumpSaturation", 0.1)
local patchHue = getFloat("patchHue", 6)
local patchValue = getFloat("patchValue", 0.035)
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

-- No spike or blade leans further than this from vertical, so nothing points down the screen.
local kMaxLean = 1.25

-- Fields -------------------------------------------------------------------------------------

local noiseSeed = floor(rand() * 1000000)

-- Periodic noise with a whole number of cycles across the tile, so it wraps exactly.
local function field(x, y, cycles, salt)
	cycles = max(1, floor(cycles + 0.5))
	local f = cycles / P
	return periodicNoise2(x * f, y * f, cycles, noiseSeed + salt)
end

-- The density and patch fields, cached on a 6 cm grid: they're looked up tens of thousands of times
-- and nothing in them is finer than 0.4 m. Each is centered on its own mean over the tile: a few
-- cycles of noise don't average to zero over one period, and the fields that move color would
-- otherwise drift each variant's mean with its seed, so the hex cells paint a patchwork at far zoom.
--
-- density is grass density, 0 (thin, soil shows) to 1 (lush), at 0.8 m and 0.4 m only: nothing in
-- the tile varies slower than that, the macro tint does. The patch fields are a soft olive-to-green
-- drift over patches of a meter or so, small next to the clump tones.
local kFieldN = 64
local densityGrid, patchVGrid, patchHGrid = {}, {}, {}
do
	local sumD, sumV, sumH = 0, 0, 0
	for j = 0, kFieldN - 1 do
		for i = 0, kFieldN - 1 do
			local x, y = (i + 0.5) * P / kFieldN, (j + 0.5) * P / kFieldN
			local k = j * kFieldN + i + 1
			densityGrid[k] = 0.6 * field(x, y, P / 0.8, 11) + 0.4 * field(x, y, P / 0.4, 23)
			patchVGrid[k], patchHGrid[k] = field(x, y, 6, 59), field(x, y, 3, 53)
			sumD, sumV, sumH = sumD + densityGrid[k], sumV + patchVGrid[k], sumH + patchHGrid[k]
		end
	end
	local nn = kFieldN * kFieldN
	for k = 1, nn do
		densityGrid[k] = clamp(0.5 + (densityGrid[k] - sumD / nn) * densityContrast, 0, 1)
		patchVGrid[k] = 1 + patchValue * (patchVGrid[k] - sumV / nn)
		patchHGrid[k] = patchHue * (patchHGrid[k] - sumH / nn)
	end
end
local function fieldCell(x, y)
	return (floor(y * kFieldN / P) % kFieldN) * kFieldN + floor(x * kFieldN / P) % kFieldN + 1
end
local function density(x, y)
	return densityGrid[fieldCell(x, y)]
end
local function patchTone(x, y)
	local k = fieldCell(x, y)
	return patchVGrid[k], patchHGrid[k]
end

-- A lean off vertical that drifts over a meter or so, so loose blades comb a little in patches.
local function sway(x, y)
	return clumpSway * 1.5 * field(x, y, P, 37)
end

-- tone(c, vMul, hueDeg, sMul) on ground of density d: lush ground is a little darker and more
-- saturated. One HSV round trip: a script this size makes tens of thousands of them.
local function lushTone(c, vMul, hueDeg, sMul, d)
	return tone(c, vMul * (1 - lushDarken * (d - 0.5) * 2), hueDeg, sMul * (1 + lushSaturate * (d - 0.5) * 2))
end

local function jitter(c, vAmp, hueAmp)
	return tone(c, 1 + vAmp * signed(), hueAmp * signed(), 1)
end

-- Emitting -----------------------------------------------------------------------------------

-- The binding methods, looked up once: a script this size makes hundreds of thousands of calls.
local pathProto = Path()
local addVertex, setColor, setLod, closePath = pathProto.addVertex, pathProto.setColor, pathProto.setLod, pathProto.close
local addPath = asset.addPath

local function put(xs, ys, n, c, alpha, lod)
	local p = Path()
	for i = 1, n do
		addVertex(p, xs[i], ys[i])
	end
	setColor(p, c[1], c[2], c[3], alpha)
	if lod > 0 then
		setLod(p, lod)
	end
	closePath(p)
	addPath(asset, p)
end

-- Shapes are built a vertex at a time into one scratch polygon, then emitted.
local SX, SY, sn = {}, {}, 0
local function vtx(x, y)
	sn = sn + 1
	SX[sn], SY[sn] = x, y
end

-- Depth order on a torus. The standing layer is drawn sorted by root y, and a shape crossing the top
-- or bottom edge is split there rather than left to the registry's wrap, which would repeat it in
-- its own place in the order. The part above the top edge lands, tiled, at the bottom of the tile,
-- on a plant rooted past the bottom edge: nearer than anything rooted here, so it is drawn last.
-- The part below the bottom edge lands at the top, behind everything, so it is drawn first: the
-- plants that can reach below the edge are drawn ahead of the rest with their in-tile parts held
-- back to their own place in the order. mode 0 emits as is (the flat ground and the far stack),
-- 1 splits, 2 splits and holds the in-tile part.
local depth = { mode = 0, top = {}, held = {} }

-- The part of a polygon on one side of the line y = y0 (side 1 keeps y >= y0, -1 keeps y <= y0),
-- moved down by shift.
local function clipY(xs, ys, n, y0, side, shift)
	local ox, oy, m = {}, {}, 0
	local px, py = xs[n], ys[n]
	local pin = (py - y0) * side >= 0
	for i = 1, n do
		local cx, cy = xs[i], ys[i]
		local cin = (cy - y0) * side >= 0
		if cin ~= pin then
			m = m + 1
			ox[m], oy[m] = px + (cx - px) * (y0 - py) / (cy - py), y0 + shift
		end
		if cin then
			m = m + 1
			ox[m], oy[m] = cx, cy + shift
		end
		px, py, pin = cx, cy, cin
	end
	return ox, oy, m
end

local function hold(list, xs, ys, n, c, alpha, lod)
	local hx, hy = {}, {}
	for i = 1, n do
		hx[i], hy[i] = xs[i], ys[i]
	end
	list[#list + 1] = { hx, hy, n, c, alpha, lod }
end

local function emitPoly(xs, ys, n, c, alpha, lod)
	local mode = depth.mode
	if mode == 0 then
		put(xs, ys, n, c, alpha, lod)
		return
	end
	local lo, hi = ys[1], ys[1]
	for i = 2, n do
		local y = ys[i]
		if y < lo then
			lo = y
		elseif y > hi then
			hi = y
		end
	end
	if lo >= 0 and hi <= P and mode == 1 then
		put(xs, ys, n, c, alpha, lod)
		return
	end
	local m = n
	if lo < 0 then
		local ax, ay, am = clipY(xs, ys, m, 0, -1, P)
		if am >= 3 then
			hold(depth.top, ax, ay, am, c, alpha, lod)
		end
		xs, ys, m = clipY(xs, ys, m, 0, 1, 0)
	end
	if hi > P then
		local bx, by, bm = clipY(xs, ys, m, P, 1, -P)
		if bm >= 3 then
			put(bx, by, bm, c, alpha, lod)
		end
		xs, ys, m = clipY(xs, ys, m, P, -1, 0)
	end
	if m >= 3 then
		if mode == 2 then
			hold(depth.held, xs, ys, m, c, alpha, lod)
		else
			put(xs, ys, m, c, alpha, lod)
		end
	end
end

local function emit(c, alpha, lod)
	local n = sn
	sn = 0
	emitPoly(SX, SY, n, c, alpha, lod)
end

local function rect(x0, y0, x1, y1, c, lod)
	vtx(x0, y0)
	vtx(x1, y0)
	vtx(x1, y1)
	vtx(x0, y1)
	emit(c, 1, lod)
end

-- An organic blob: radius r modulated by two low harmonics, stretched by `aspect`, turned by `rot`.
local function blob(x, y, r, n, wobble, aspect, rot, c, alpha, lod)
	local a2, a3 = rand() * TAU, rand() * TAU
	local w2, w3 = wobble * rand(), wobble * 0.6 * rand()
	local cr, sr = cos(rot), sin(rot)
	for i = 0, n - 1 do
		local a = TAU * i / n
		local rr = r * (1 + w2 * cos(2 * a + a2) + w3 * cos(3 * a + a3))
		local lx, ly = cos(a) * rr * aspect, sin(a) * rr
		vtx(x + lx * cr - ly * sr, y + lx * sr + ly * cr)
	end
	emit(c, alpha, lod)
end

-- An upright fan rooted at (x, y): k spikes fanned symmetrically about dir with half-angle spread,
-- ascending (left to right through up) and jittered less than half a step so they stay in order,
-- the middle ones longest (len) and the outer ones shorter, curling outward by bend of their length
-- (positive curls clockwise: outward and down for a spike leaning right). One polygon: the root's
-- left corner, each spike (withSides adds a mid point on each side so it tapers with a curve), a
-- valley between neighbors at fill of the shorter one, so a fan is a solid mound with a spiky top,
-- the root's right corner. Written straight into the scratch polygon: fans are most of the vertices.
local FA, FL, FB = {}, {}, {}
local function upFan(x, y, k, dir, spread, len, bend, hubHalf, fill, withSides, c, lod)
	local step = 0.35 * spread / max(1, k - 1)
	for i = 1, k do
		local rel = k > 1 and (2 * (i - 1) / (k - 1) - 1) or 0
		FA[i] = clamp(dir + spread * rel + (rand() - 0.5) * 2 * step, UP - kMaxLean, UP + kMaxLean)
		FL[i] = len * uniform(0.75, 1.1) * (1 - 0.3 * rel * rel)
		FB[i] = bend * rel + 0.1 * (rand() - 0.5)
	end
	local n = 1
	SX[1], SY[1] = x - hubHalf * 1.5, y
	local hw = hubHalf * 0.5
	for i = 1, k do
		local ai = FA[i]
		local dx, dy = cos(ai), sin(ai)
		local L = FL[i]
		local b = FB[i] * L
		local tx, ty = x + dx * L - dy * b, y + dy * L + dx * b
		if withSides then
			local mx, my = x + dx * L * 0.5 - dy * b * 0.3, y + dy * L * 0.5 + dx * b * 0.3
			SX[n + 1], SY[n + 1] = mx + dy * hw, my - dx * hw
			SX[n + 2], SY[n + 2] = tx, ty
			SX[n + 3], SY[n + 3] = mx - dy * hw, my + dx * hw
			n = n + 3
		else
			n = n + 1
			SX[n], SY[n] = tx, ty
		end
		if i < k then
			local an = FA[i + 1]
			local r = fill * min(L, FL[i + 1])
			local am = (ai + an) * 0.5
			n = n + 1
			SX[n], SY[n] = x + cos(am) * r, y + sin(am) * r
		end
	end
	n = n + 1
	SX[n], SY[n] = x + hubHalf * 1.5, y
	emitPoly(SX, SY, n, c, 1, lod)
end

local function polyArea(xs, ys, n)
	local a = 0
	local px, py = xs[n], ys[n]
	for i = 1, n do
		a = a + px * ys[i] - xs[i] * py
		px, py = xs[i], ys[i]
	end
	return math.abs(a) * 0.5
end

-- A blade from (x, y) along angle a: length L, base width w, bowed sideways by bow * L. Paints a
-- proxy strip under it (kept down to mip 3), then a centered dark underlay underlayWidth times as
-- wide, a base segment and a lit tip segment, plus an optional tip highlight. The proxy's color is
-- what the strip averages to at mip 0, in linear light: the base and tip over their share of it and
-- the underlay over the proxy for the rest. So the mips that drop the blade keep mip 0's mean
-- whatever the blade's colors are.
local bladeT = { 0, 0.28, 0.55, 0.78 }
local BX, BY, TX, TY, UX, UY = {}, {}, {}, {}, {}, {}
local bladeHalf, CX, CY, HW = {}, {}, {}, {}
for i = 1, 4 do
	bladeHalf[i] = 0.5 * (1 - bladeT[i]) ^ 0.7
end
local underLin = toLinear(underlayColor)
local function blade(x, y, a, L, w, bow, baseCol, tipCol, highlight, lod)
	local dx, dy = cos(a), sin(a)
	local px, py = -dy, dx
	local cx, cy, hw = CX, CY, HW
	for i = 1, 4 do
		local t = bladeT[i]
		local off = bow * L * t * t
		cx[i] = x + dx * L * t + px * off
		cy[i] = y + dy * L * t + py * off
		hw[i] = w * bladeHalf[i]
	end
	local tx, ty = x + dx * L + px * bow * L, y + dy * L + py * bow * L

	local s = underlayWidth
	local ax, ay = px * hw[1] * s, py * hw[1] * s
	local bx, by = px * hw[3] * s, py * hw[3] * s
	UX[1], UY[1] = cx[1] + ax, cy[1] + ay
	UX[2], UY[2] = cx[3] + bx, cy[3] + by
	UX[3], UY[3] = tx + dx * L * 0.05, ty + dy * L * 0.05
	UX[4], UY[4] = cx[3] - bx, cy[3] - by
	UX[5], UY[5] = cx[1] - ax, cy[1] - ay
	for i = 1, 3 do
		BX[i], BY[i] = cx[i] + px * hw[i], cy[i] + py * hw[i]
		BX[7 - i], BY[7 - i] = cx[i] - px * hw[i], cy[i] - py * hw[i]
	end
	TX[1], TY[1] = cx[3] + px * hw[3], cy[3] + py * hw[3]
	TX[2], TY[2] = cx[4] + px * hw[4], cy[4] + py * hw[4]
	TX[3], TY[3] = tx, ty
	TX[4], TY[4] = cx[4] - px * hw[4], cy[4] - py * hw[4]
	TX[5], TY[5] = cx[3] - px * hw[3], cy[3] - py * hw[3]

	local strip = polyArea(UX, UY, 5)
	local rB = min(polyArea(BX, BY, 6) / strip, 0.95)
	local rT = min(polyArea(TX, TY, 5) / strip, 0.95 - rB)
	local rest = 1 - rB - rT
	local bl, tl = toLinear(baseCol), toLinear(tipCol)
	local proxy = {}
	for ch = 1, 3 do
		proxy[ch] = (rB * bl[ch] + rT * tl[ch] + rest * underlayAlpha * underLin[ch]) / (1 - rest * (1 - underlayAlpha))
	end

	emitPoly(UX, UY, 5, toSrgb(proxy), 1, kCover)
	emitPoly(UX, UY, 5, underlayColor, underlayAlpha, lod)
	emitPoly(BX, BY, 6, baseCol, 1, lod)
	emitPoly(TX, TY, 5, tipCol, 1, lod)

	if highlight then
		local h = 0.6
		vtx(cx[4] + px * hw[4] * h, cy[4] + py * hw[4] * h)
		vtx(tx, ty)
		vtx(cx[4] - px * hw[4] * h, cy[4] - py * hw[4] * h)
		emit(tone(tipCol, 1.06, 0, 1), 1, 96)
	end
end

-- An obcordate clover leaflet from the center along angle a, length l.
local leafletShape = { 0, 0, 0.3, -0.38, 0.62, -0.5, 0.92, -0.36, 1.0, -0.12, 0.84, 0, 1.0, 0.12, 0.92, 0.36, 0.62, 0.5, 0.3, 0.38 }
local function leaflet(x, y, a, l, c, lod)
	local ca, sa = cos(a), sin(a)
	for i = 1, #leafletShape, 2 do
		local lx, ly = leafletShape[i] * l, leafletShape[i + 1] * l
		vtx(x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	emit(c, 1, lod)
end

local chevronShape = { 0.4, -0.24, 0.62, 0, 0.4, 0.24, 0.5, 0 }
local function chevron(x, y, a, l, c, alpha)
	local ca, sa = cos(a), sin(a)
	for i = 1, #chevronShape, 2 do
		local lx, ly = chevronShape[i] * l, chevronShape[i + 1] * l
		vtx(x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	emit(c, alpha, 96)
end

-- A trifoliate stays down to mip 3, where a patch of them averages to the patch's tone.
local function trifoliate(x, y, l, c)
	local a0 = rand() * TAU
	for i = 0, 2 do
		local a = a0 + i * TAU / 3 + (rand() - 0.5) * 0.5
		local ll = l * uniform(0.9, 1.1)
		leaflet(x, y, a, ll, jitter(c, 0.05, 2), kCover)
		chevron(x, y, a, ll, clover.chevron, 0.55)
	end
end

-- A pointed broadleaf leaf from the center along angle a: length l, width w.
local function leaf(x, y, a, l, w, c, lod)
	local ca, sa = cos(a), sin(a)
	local n = 6
	for i = 0, n do
		local t = i / n
		local lx, ly = t * l, -sin(math.pi * t ^ 0.8) * w * 0.5
		vtx(x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	for i = n - 1, 1, -1 do
		local t = i / n
		local lx, ly = t * l, sin(math.pi * t ^ 0.8) * w * 0.5
		vtx(x + lx * ca - ly * sa, y + lx * sa + ly * ca)
	end
	emit(c, 1, lod)
end

local function midrib(x, y, a, l, w, c, lod)
	local ca, sa = cos(a), sin(a)
	local px, py = -sa, ca
	vtx(x + px * w, y + py * w)
	vtx(x + ca * l * 0.8, y + sa * l * 0.8)
	vtx(x - px * w, y - py * w)
	emit(c, 1, lod)
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
local function blueNoise(n, spacing)
	local pts = {}
	local s2 = spacing * spacing
	for _ = 1, n do
		local best, bestD = nil, -1
		for _ = 1, 30 do
			local x, y = rand() * P, rand() * P
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

-- Layout (everything placed before anything is drawn, so the far stack can mirror it) ---------

-- Soil peeks where the grass is thin.
local soils = weighted(count(soil.perM2), function(x, y)
	local d = density(x, y)
	return (1 - d) ^ 3
end)
for _, s in ipairs(soils) do
	s.r = lognormal(soil.lo, soil.hi, 0.5) * 0.5
end

-- Clumps: Poisson-disk centers, each a disc of radius R holding a Thomas cluster of tuft roots that
-- share a tone and a sway, the tufts fanning outward toward the disc's sides. Tufts at the core are
-- lighter than those at the rim, so a clump reads as a mound. Thin ground grows smaller clumps, so
-- more of the ground between them shows.
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
			sway = clumpSway * clamp(gauss(), -2, 2),
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
		c.R = c.base * sizeScale * lerp(0.85, 1.05, c.d)
		c.v = (1 + clumpValue * (c.dv - mv)) * pv
		c.h = clumpHue * (c.dh - mh) + ph
		c.s = 1 + clumpSaturation * (c.ds - ms)
		c.lenScale = lerp(0.8, 1.1, c.d)
		-- The tufts stand up from their roots: the clump's color sits about half a tuft above the disc.
		c.lift = 0.45 * sqrt(tuftLenLo * tuftLenHi) * c.lenScale
		local body = lushTone(mix(tuftDark, tuftLight, 0.5), c.v, c.h, c.s, c.d)
		local hi = lushTone(mix(tuftHiDark, tuftHiLight, 0.5), c.v, c.h, c.s, c.d)
		c.tone = mix(body, hi, 0.45)
	end
end

local tufts = {}
for _, c in ipairs(clumps) do
	local sigma = c.R * tuftSpread
	for _ = 1, floor(lerp(tuftsPerClumpLo, tuftsPerClumpHi, c.d) + rand()) do
		local ox, oy = gauss() * sigma, gauss() * sigma
		local out = min(1, sqrt(ox * ox + oy * oy) / c.R)
		local tilt = c.sway + tuftFanOut * clamp(ox / c.R, -1, 1) + 0.1 * gauss()
		tufts[#tufts + 1] = {
			(c.x + ox) % P,
			(c.y + oy) % P,
			clump = c,
			v = c.v * (1 + clumpDome * (1 - 2 * out)) * (1 + 0.03 * signed()),
			dir = UP + clamp(tilt, -0.7, 0.7),
			len = uniform(tuftLenLo, tuftLenHi) * c.lenScale,
			fan = uniform(tuftFanLo, tuftFanHi),
			blades = {},
		}
	end
end
for _, t in ipairs(tufts) do
	local c = t.clump
	t.k = floor(uniform(tuftSpikesLo, tuftSpikesHi + 1))
	t.hub = uniform(tuftHubLo, tuftHubHi) * 0.5
	t.body = lushTone(mix(tuftDark, tuftLight, rand()), t.v, c.h, c.s, c.d)
	t.hi = lushTone(mix(tuftHiDark, tuftHiLight, rand()), t.v, c.h, c.s, c.d)
	t.by = t[2]
	t.kind = 1
end

-- Clover patches: Thomas clusters around blue-noise parents. Clover is the brightest thing on the
-- ground, so the patch sizes are normalized to the variant's expected total.
local patches = blueNoise(count(clover.patchesPerM2), 0.6 / sqrt(max(clover.patchesPerM2, 0.01)))
local clovers = {}
do
	local sum = 0
	for _, pa in ipairs(patches) do
		pa.raw = uniform(clover.perLo, clover.perHi)
		sum = sum + pa.raw
	end
	local scale = #patches * 0.5 * (clover.perLo + clover.perHi) / max(sum, 1)
	for _, pa in ipairs(patches) do
		pa.n = floor(pa.raw * scale + 0.5)
	end
end
for _, pa in ipairs(patches) do
	pa.sigma = uniform(clover.sigmaLo, clover.sigmaHi)
	pa.c = tone(clover.color, 1 + 0.05 * signed(), 3 * signed(), 1)
	for _ = 1, pa.n do
		local y = (pa[2] + gauss() * pa.sigma) % P
		local l = uniform(clover.leafletLo, clover.leafletHi)
		clovers[#clovers + 1] = { (pa[1] + gauss() * pa.sigma) % P, y, l = l, c = pa.c, by = y + 0.6 * l, kind = 3 }
	end
end

-- Broadleaf rosettes (blue noise), flat on the ground.
local rosettes = blueNoise(count(rosette.perM2), 0.8 / sqrt(max(rosette.perM2, 0.01)))
for _, ro in ipairs(rosettes) do
	ro.k = floor(uniform(5, 9))
	ro.a0 = rand() * TAU
	ro.c = jitter(rosette.color, 0.05, 3)
	ro.inner = jitter(rosette.inner, 0.04, 3)
	ro.leaves = {}
	local sum = 0
	for i = 1, ro.k do
		local l = uniform(rosette.lenLo, rosette.lenHi)
		ro.leaves[i] = { a = ro.a0 + (i - 1 + (rand() - 0.5) * 0.5) * TAU / ro.k, l = l, w = l * uniform(0.3, 0.4), v = uniform(0.95, 1.05) }
		sum = sum + l
	end
	ro.meanLen = sum / ro.k
end

-- Cover --------------------------------------------------------------------------------------

-- A 3.3 cm grid of the ground that what's laid out hides, each fan stamped as its solid sector, so
-- the undergrowth fills the holes until groundShow of the grid is left open and the litter goes
-- only where the ground shows. Every variant leaves the same share open.
local cover = { n = floor(P / 0.033 + 0.5) }
cover.cell = P / cover.n
cover.open = cover.n * cover.n
local function stamp(x, y, dir, spread, r)
	local n, cell = cover.n, cover.cell
	local i0, i1 = floor((x - r) / cell), floor((x + r) / cell)
	local j0, j1 = floor((y - r) / cell), floor((y + r * (spread > 1.6 and 1 or 0)) / cell)
	-- Inside the sector when the cell's direction is within spread of dir: a dot product against
	-- cos(spread) times its distance.
	local dx, dy, cs = cos(dir), sin(dir), spread >= math.pi and -2 or cos(spread)
	for j = j0, j1 do
		local cy = (j + 0.5) * cell - y
		local row = (j % n) * n + 1
		for i = i0, i1 do
			local cx = (i + 0.5) * cell - x
			local d2 = cx * cx + cy * cy
			if d2 <= r * r and cx * dx + cy * dy >= cs * sqrt(d2) then
				local k = row + i % n
				if not cover[k] then
					cover[k] = true
					cover.open = cover.open - 1
				end
			end
		end
	end
end
local function isOpen(x, y)
	return not cover[(floor(y / cover.cell) % cover.n) * cover.n + floor(x / cover.cell) % cover.n + 1]
end

for _, t in ipairs(tufts) do
	stamp(t[1], t[2], t.dir, t.fan, 0.8 * t.len)
end
for _, cl in ipairs(clovers) do
	stamp(cl[1], cl[2], 0, math.pi, cl.l)
end
for _, ro in ipairs(rosettes) do
	stamp(ro[1], ro[2], 0, math.pi, 0.8 * ro.meanLen)
end

-- Dark undergrowth fans: a low sward filling the holes between and inside the clumps, at most
-- undergrowthDensity a square meter, fewer where the grass is thin, so the ground shows more there.
-- Each fan is centered in a cell drawn from the open ones; covered cells leave the list as they turn up.
local undergrowth = {}
do
	local n, cell = cover.n, cover.cell
	local cap = count(underPerM2)
	local target = groundShow * n * n
	local openCells = {}
	for k = 1, n * n do
		if not cover[k] then
			openCells[#openCells + 1] = k
		end
	end
	local guard = cap * 4 + n * n
	while #undergrowth < cap and cover.open > target and #openCells > 0 and guard > 0 do
		guard = guard - 1
		local pick = floor(rand() * #openCells) + 1
		local k = openCells[pick]
		local len = uniform(underLenLo, underLenHi)
		local x, y = ((k - 1) % n + rand()) * cell, ((floor((k - 1) / n) + rand()) * cell + 0.4 * len) % P
		if cover[k] then
			openCells[pick] = openCells[#openCells]
			openCells[#openCells] = nil
		elseif rand() < 0.6 + 0.4 * density(x, y) then
			local pv, ph = patchTone(x, y)
			local u = {
				x,
				y,
				len = len,
				k = floor(uniform(underSpikesLo, underSpikesHi + 1)),
				dir = UP + 0.3 * (rand() - 0.5),
				spread = uniform(underFanLo, underFanHi),
				hub = uniform(underHubLo, underHubHi) * 0.5,
				c = lushTone(mix(underDark, underLight, rand()), pv * (1 + elementValue * signed()), ph + elementHue * signed(), 1, density(x, y)),
				by = y,
				kind = 4,
			}
			stamp(x, y, u.dir, u.spread, 0.8 * len)
			undergrowth[#undergrowth + 1] = u
		end
	end
end

-- Tone model ---------------------------------------------------------------------------------

-- A coarse model of the near stack: linear light on a ~3 cm grid, each feature painted over what's
-- below by the share of its area it covers. The far stack is it, averaged.
--
-- Built in a function of its own: the main chunk is near Lua's 200-local limit.
local function drawFarStack()
local gridN = floor(P / 0.03125 + 0.5)
local cellSize = P / gridN
local gridR, gridG, gridB = {}, {}, {}
do
	local g = toLinear(mix(groundColor, mix(underDark, underLight, 0.5), modelUndergrowth))
	for k = 1, gridN * gridN do
		gridR[k], gridG[k], gridB[k] = g[1], g[2], g[3]
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
	local c = toLinear(soil.color)
	paint(s[1], s[2], s.r * 1.1, function(out)
		return c, 0.6 * (1 - out * out)
	end)
end
for _, ro in ipairs(rosettes) do
	local c = toLinear(mix(ro.c, ro.inner, 0.35))
	paint(ro[1], ro[2], ro.meanLen, function(out)
		return c, 0.7 * (1 - out * out)
	end)
end
-- Clumps, inlined: they are most of the painting. A clump's tone runs from its lit core through the
-- dome to its rim, over 1.2 radii, fading out past 0.75, centered half a tuft above its roots.
do
	local reach = 1.2
	local tr, tg, tb = {}, {}, {}
	for _, c in ipairs(clumps) do
		for i = 0, 12 do
			local out = i / 10
			local col = toLinear(tone(c.tone, 1 + clumpDome * (1 - 2 * min(out, 1)), 0, 1))
			tr[i], tg[i], tb[i] = col[1], col[2], col[3]
		end
		local r = c.R * reach
		local inv = 1 / c.R
		local cy = c.y - c.lift
		local i0, i1 = floor((c.x - r) / cellSize), floor((c.x + r) / cellSize)
		local j0, j1 = floor((cy - r) / cellSize), floor((cy + r) / cellSize)
		for j = j0, j1 do
			local py = ((j + 0.5) * cellSize - cy) * inv
			local row = (j % gridN) * gridN + 1
			for i = i0, i1 do
				local px = ((i + 0.5) * cellSize - c.x) * inv
				local out = sqrt(px * px + py * py)
				if out < reach then
					local t = clamp((out - 0.75) / (reach - 0.75), 0, 1)
					local w = modelClumpFill * (1 - t * t * (3 - 2 * t))
					local q = floor(out * 10 + 0.5)
					local k = row + i % gridN
					gridR[k] = gridR[k] + (tr[q] - gridR[k]) * w
					gridG[k] = gridG[k] + (tg[q] - gridG[k]) * w
					gridB[k] = gridB[k] + (tb[q] - gridB[k]) * w
				end
			end
		end
	end
end
for _, pa in ipairs(patches) do
	local c = toLinear(pa.c)
	-- Trifoliates cover about 0.35 l^2 a leaflet; the patch is Gaussian with sigma pa.sigma.
	local peak = pa.n * 3 * 0.35 * (0.5 * (clover.leafletLo + clover.leafletHi)) ^ 2 / (TAU * pa.sigma * pa.sigma)
	paint(pa[1], pa[2], pa.sigma * 2.5, function(out)
		local r = out * 2.5
		return c, 1 - exp(-peak * exp(-0.5 * r * r))
	end)
end


-- Far stack ----------------------------------------------------------------------------------

-- The model averaged into one rect a mip 4 texel (farCellsPerMeter), scaled so its mean is farMean:
-- what mips 4 to 6 show, in the near stack's places. farMean is the near stack's mean as the bake
-- measures it, the same in every variant, so the far mips' mean doesn't drift from mip 0's with the
-- model's own error. Contrast is set in two bands, so mip 4 and mips 5 and 6 can each match mip 0:
-- detail finer than a few cells (each cell against a blur of its neighbors) by farContrast, and the
-- coarser drift by farCoarseContrast.
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
	-- A wrapped 3-tap box blur along each axis: the fine band is then cell-scale detail only, which
	-- mip 5 averages away, so farContrast sets mip 4 and farCoarseContrast mips 5 and 6.
	local function blur(src)
		local a, b = {}, {}
		for j = 0, farN - 1 do
			for i = 0, farN - 1 do
				local row = j * farN + 1
				a[row + i] = (src[row + (i - 1) % farN] + src[row + i] + src[row + (i + 1) % farN]) / 3
			end
		end
		for j = 0, farN - 1 do
			for i = 0, farN - 1 do
				b[j * farN + i + 1] = (a[((j - 1) % farN) * farN + i + 1] + a[j * farN + i + 1] + a[((j + 1) % farN) * farN + i + 1]) / 3
			end
		end
		return b
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
				lin[ch] = (m + (a - cc) * farContrast + (cc - m) * farCoarseContrast) * farMean[ch] / m
			end
			rect(i * fs, j * fs, (i + 1) * fs, (j + 1) * fs, toSrgb(lin), 0)
		end
	end
end
end
drawFarStack()


-- Near stack: the flat ground ----------------------------------------------------------------

rect(0, 0, P, P, groundColor, kCover)

-- Litter: dark flecks and dead bits lying on the open ground, litterDensity a square meter of it,
-- averaging to the ground color, so the mips that drop them keep its mean.
for _ = 1, count(litter.perM2) do
	local x, y = rand() * P, rand() * P
	if isOpen(x, y) then
		local pick = rand()
		local c = litter.colors[pick < litter.shares[1] and 1 or (pick < litter.shares[1] + litter.shares[2] and 2 or 3)]
		local size = lognormal(litter.lo, litter.hi, 0.5)
		local aspect = uniform(0.3, 0.7)
		blob(x, y, size * 0.5 * aspect, 6, 0.25, 1 / aspect, rand() * TAU, jitter(c, 0.06, 3), 1, lodFor(size * aspect))
	end
end

-- Soil peeks: two or three overlapping blobs, a few darker specks, sometimes a dry edge.
for _, s in ipairs(soils) do
	if rand() < 0.15 then
		blob(s[1], s[2], s.r * 1.12, 12, 0.25, 1, 0, soil.dry, 1, kCover)
	end
	local parts = rand() < 0.5 and 2 or 3
	for _ = 1, parts do
		local ox, oy = gauss() * s.r * 0.35, gauss() * s.r * 0.35
		blob(s[1] + ox, s[2] + oy, s.r * uniform(0.6, 0.9), 12, 0.3, uniform(0.75, 1.3), rand() * TAU, jitter(soil.color, 0.04, 2), 1, kCover)
	end
	for _ = 1, floor(uniform(1, 4)) do
		local ox, oy = gauss() * s.r * 0.4, gauss() * s.r * 0.4
		blob(s[1] + ox, s[2] + oy, s.r * uniform(0.08, 0.16), 6, 0.2, 1, 0, soil.speck, 1, 64)
	end
end

-- Pebbles, mostly on the soil, each over a soft centered halo.
for _ = 1, count(pebble.perM2) do
	local x, y
	if #soils > 0 and rand() < 0.7 then
		local s = soils[floor(rand() * #soils) + 1]
		x, y = s[1] + gauss() * s.r * 0.6, s[2] + gauss() * s.r * 0.6
	else
		x, y = rand() * P, rand() * P
	end
	local r = lognormal(pebble.lo, pebble.hi, 0.5) * 0.5
	local lod = lodFor(r * 2)
	local sides = floor(uniform(6, 10))
	local aspect, rot = uniform(0.7, 1.0), rand() * TAU
	blob(x, y, r * 1.25, sides, 0.15, aspect, rot, pebble.halo, pebble.haloAlpha, lod)
	blob(x, y, r, sides, 0.15, aspect, rot, jitter(pebble.colors[floor(rand() * 3) + 1], 0.04, 0), 1, lod)
end

-- Rosettes lie flat, under everything that stands.
for _, ro in ipairs(rosettes) do
	for _, lf in ipairs(ro.leaves) do
		leaf(ro[1], ro[2], lf.a, lf.l, lf.w, tone(ro.c, lf.v, 0, 1), kCover)
		-- The lit inner leaf, cel-shaded on the same axis.
		leaf(ro[1] + cos(lf.a) * lf.l * 0.12, ro[2] + sin(lf.a) * lf.l * 0.12, lf.a, lf.l * 0.72, lf.w * 0.6, tone(ro.inner, lf.v, 0, 1), kCover)
		midrib(ro[1], ro[2], lf.a, lf.l, lf.w * 0.08, rosette.rib, 48)
	end
end

-- Near stack: what stands, back to front ----------------------------------------------------

local items = {}
for _, t in ipairs(tufts) do
	items[#items + 1] = t
end
for _, cl in ipairs(clovers) do
	items[#items + 1] = cl
end

for _, u in ipairs(undergrowth) do
	items[#items + 1] = u
end

-- Blades: most grow from a tuft's root and lean with it, base in the tuft's body tone and tip in its
-- lit tone; the rest stand anywhere, leaning with the local sway, in the tuft palette.
do
	local n = count(bladePerM2)
	local nTuft = #tufts > 0 and floor(n * bladeInTufts + 0.5) or 0
	for i = 1, n do
		local b = {}
		local baseTone, tipTone, d, pv, ph
		if i <= nTuft then
			local t = tufts[floor(rand() * #tufts) + 1]
			b[1], b[2] = t[1] + gauss() * t.hub, t[2] + math.abs(gauss()) * 0.004
			b.a = t.dir + gauss() * bladeLean * 0.6
			-- The tuft's tones are lush already.
			baseTone, tipTone, d, pv, ph = t.body, t.hi, 0.5, 1, 0
			t.blades[#t.blades + 1] = b
		else
			b[1], b[2] = rand() * P, rand() * P
			b.a = UP + sway(b[1], b[2]) + gauss() * bladeLean
			d = density(b[1], b[2])
			pv, ph = patchTone(b[1], b[2])
			baseTone, tipTone = mix(tuftDark, tuftLight, rand()), mix(tuftHiDark, tuftHiLight, rand())
			b.by = b[2]
			b.kind = 2
			items[#items + 1] = b
		end
		b.a = clamp(b.a, UP - kMaxLean, UP + kMaxLean)
		b.L = lognormal(bladeLenLo, bladeLenHi, 0.3)
		-- A share of broad blades: they stay in the coarser near mips, where the fine ones drop out.
		b.w = rand() < broadShare and uniform(broadWLo, broadWHi) or uniform(bladeWLo, bladeWHi) * uniform(0.6, 1.0)
		-- Leaning blades bow further out, the way they flop.
		b.bow = bladeBow * ((b.a - UP) * 1.2 + gauss() * 0.5)
		if rand() < strawShare then
			b.base = jitter(mix(strawDark, strawLight, rand()), elementValue, elementHue)
			b.tip = tone(b.base, 1.05, 0, 0.95)
			b.w = b.w * 0.7
		else
			local e = bladeGain * pv * (1 + elementValue * signed())
			local eh = elementHue * signed() + ph
			b.base = lushTone(baseTone, e, bladeHue + eh, 1, d)
			b.tip = lushTone(tipTone, e * bladeTipGain, bladeTipHue + eh, 1, d)
			if rand() < dryTipShare then
				b.tip = mix(b.tip, strawDark, 0.45)
			end
		end
		b.highlight = rand() < tipHighlightShare
	end
end

local function drawBlade(b)
	blade(b[1], b[2], b.a, b.L, b.w, b.bow, b.base, b.tip, b.highlight, lodFor(b.w))
end

local drawItem = {
	-- A tuft: its body fan, a lit fan standing a little higher in front of it, then its blades.
	function(t)
		upFan(t[1], t[2], t.k, t.dir, t.fan, t.len, tuftBend, t.hub, tuftFill, true, t.body, kCover)
		local lift = t.len * 0.18
		upFan(t[1] + cos(t.dir) * lift, t[2] + sin(t.dir) * lift, floor(uniform(4, 7)), t.dir, t.fan * 0.7, t.len * 0.75, tuftBend, t.hub * 0.7, tuftFill * 0.55, false, t.hi, kCover)
		for _, b in ipairs(t.blades) do
			drawBlade(b)
		end
	end,
	drawBlade,
	function(cl)
		trifoliate(cl[1], cl[2], cl.l, cl.c)
	end,
	-- Undergrowth: a low, splayed fan.
	function(u)
		upFan(u[1], u[2], u.k, u.dir, u.spread, u.len, tuftBend, u.hub, underFill, false, u.c, kCover)
	end,
}

table.sort(items, function(a, b)
	return a.by < b.by
end)
-- Anything that can reach below its root by more than its root's jitter roots within kEdge of it.
local kEdge = 0.03
local first = #items + 1
while first > 1 and items[first - 1].by > P - kEdge do
	first = first - 1
end
depth.mode = 2
for i = first, #items do
	drawItem[items[i].kind](items[i])
end
depth.mode = 1
for i = 1, first - 1 do
	drawItem[items[i].kind](items[i])
end
for _, s in ipairs(depth.held) do
	put(s[1], s[2], s[3], s[4], s[5], s[6])
end
for _, s in ipairs(depth.top) do
	put(s[1], s[2], s[3], s[4], s[5], s[6])
end
depth.mode = 0
