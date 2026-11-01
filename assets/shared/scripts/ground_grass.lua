-- Ground grass: one seamless period x period meter tile variant (role ground), painted dark to
-- light, low to high. A far stack (mean base plus soil, clover and tuft proxies) comes first and
-- is what the coarse mips show; a full-tile gap cover tagged setLod(20) hides it from 20 px/m up,
-- and the near stack above it carries soil peeks, pebbles, undergrowth fans, rosettes, clover,
-- two-tone tuft fans and top blades with soft underlays.
--
-- Meters, +Y down, sRGB colors 0..1. Shapes may cross the tile edge; the registry wraps them.
-- Every density field is periodicNoise2 at a wavelength that divides the period, and every count
-- is fixed per square meter, so all variants share the same statistics. No light direction is
-- baked in (the shader rotates tiles freely): underlays and halos are centered.

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

local farBase = hex("farBaseColor", "#43602F")
local gapColor = hex("gapColor", "#283A20")

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

local underPerM2 = getFloat("undergrowthDensity", 340)
local underLenLo, underLenHi = getFloatRange("undergrowthLength", 0.045, 0.085)
local underHubLo, underHubHi = getFloatRange("undergrowthHubWidth", 0.009, 0.014)
local underSpikesLo, underSpikesHi = getFloatRange("undergrowthSpikes", 4, 7)
local underDark = hex("undergrowthDark", "#3C5C31")
local underLight = hex("undergrowthLight", "#4A6C38")

local rosettePerM2 = getFloat("rosetteDensity", 0.6)
local rosetteLenLo, rosetteLenHi = getFloatRange("rosetteLeafLength", 0.06, 0.14)
local rosetteColor = hex("rosetteColor", "#46703A")
local rosetteInner = hex("rosetteInnerColor", "#5C8A48")
local rosetteRib = hex("rosetteMidribColor", "#8AAE6E")

local cloverPatchPerM2 = getFloat("cloverPatches", 1.2)
local cloverPerLo, cloverPerHi = getFloatRange("cloverPerPatch", 6, 16)
local cloverSigmaLo, cloverSigmaHi = getFloatRange("cloverSpread", 0.05, 0.09)
local leafletLo, leafletHi = getFloatRange("cloverLeaflet", 0.022, 0.032)
local cloverColor = hex("cloverColor", "#5C9A4C")
local chevronColor = hex("cloverChevronColor", "#A8CC90")
local cloverTopShare = getFloat("cloverTopShare", 0.6)

local clumpSpacing = getFloat("clumpSpacing", 0.22)
local clumpSpreadLo, clumpSpreadHi = getFloatRange("clumpSpread", 0.05, 0.085)
local tuftsPerClumpLo, tuftsPerClumpHi = getFloatRange("tuftsPerClump", 5, 11)
local tuftLenLo, tuftLenHi = getFloatRange("tuftLength", 0.06, 0.14)
local tuftSpikesLo, tuftSpikesHi = getFloatRange("tuftSpikes", 4, 7)
local tuftHubLo, tuftHubHi = getFloatRange("tuftHubWidth", 0.012, 0.02)
local tuftDark = hex("tuftDark", "#4C6E31")
local tuftLight = hex("tuftLight", "#5B823B")
local tuftHiDark = hex("tuftHighlightDark", "#62903E")
local tuftHiLight = hex("tuftHighlightLight", "#70A046")
local tuftLean = getFloat("tuftLean", 0.85)

local bladePerM2 = getFloat("bladeDensity", 280)
local bladeLenLo, bladeLenHi = getFloatRange("bladeLength", 0.09, 0.24)
local bladeWLo, bladeWHi = getFloatRange("bladeWidth", 0.012, 0.02)
local broadShare = getFloat("broadBladeShare", 0.25)
local broadWLo, broadWHi = getFloatRange("broadBladeWidth", 0.028, 0.038)
local bladeBow = getFloat("bladeBow", 0.15)
local bladeInTufts = getFloat("bladeTuftShare", 0.7)
local bladeFanSpread = getFloat("bladeFanSpread", 0.55)
local bladeRadialShare = getFloat("bladeRadialShare", 0.25)
local bladeBase = hex("bladeBaseColor", "#628B3C")
local bladeTipDark = hex("bladeTipDark", "#6F9844")
local bladeTipLight = hex("bladeTipLight", "#76A048")
local underlayColor = hex("underlayColor", "#1C2814")
local underlayAlpha = getFloat("underlayAlpha", 0.35)
local strawShare = getFloat("strawShare", 0.035)
local strawDark = hex("strawDark", "#9C9260")
local strawLight = hex("strawLight", "#B0A46E")
local dryTipShare = getFloat("dryTipShare", 0.1)
local tipHighlightShare = getFloat("tipHighlightShare", 0.2)
local flowSpread = getFloat("flowSpread", 0.5)

local densityContrast = getFloat("densityContrast", 1.6)
local clumpValue = getFloat("clumpValue", 0.06)
local clumpDome = getFloat("clumpDome", 0.07)
local clumpHue = getFloat("clumpHue", 8)
local elementValue = getFloat("elementValue", 0.04)
local elementHue = getFloat("elementHue", 3)
local lushDarken = getFloat("lushDarken", 0.03)
local lushSaturate = getFloat("lushSaturate", 0.05)

-- LOD tags: a path is baked into mip k only when its tag is at most that mip's px/m, and the
-- whole near stack sits at or above the cover's 20 so the far stack shows alone below it.
local kCover = 20
local function lodFor(width)
	return clamp(1 / width, kCover, 128)
end

-- Fields -------------------------------------------------------------------------------------

local noiseSeed = floor(rand() * 1000000)

local function field(x, y, wavelength, salt)
	return periodicNoise2(x / wavelength, y / wavelength, P / wavelength, noiseSeed + salt)
end

-- Grass density, 0 (thin, soil shows) to 1 (lush). 0.8 m and 0.4 m only: nothing in the tile
-- varies slower than that, the macro tint does.
local function density(x, y)
	local v = 0.6 * field(x, y, 0.8, 11) + 0.4 * field(x, y, 0.4, 23)
	return clamp(0.5 + v * densityContrast, 0, 1)
end

-- Local blade direction: an isotropic vector field, so blades comb in patches with no global grain.
local function flowAngle(x, y)
	return atan(field(x, y, 1.0, 37), field(x, y, 1.0, 41))
end

-- Lush ground is a little darker and more saturated.
local function lush(c, d)
	return tone(c, 1 - lushDarken * (d - 0.5) * 2, 0, 1 + lushSaturate * (d - 0.5) * 2)
end

local function jitter(c, vAmp, hueAmp)
	return tone(c, 1 + vAmp * (rand() * 2 - 1), hueAmp * (rand() * 2 - 1), 1)
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

-- A blade from (x, y) along angle a: length L, base width w, bowed sideways by bow * L. Paints an
-- optional centered underlay, a base segment and a lit tip segment, plus an optional tip highlight.
local bladeT = { 0, 0.28, 0.55, 0.78 }
local function blade(x, y, a, L, w, bow, baseCol, tipCol, underlay, highlight, lod)
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

	if underlay then
		local s = 1.7
		local ex, ey = tx + dx * L * 0.05, ty + dy * L * 0.05
		local p = Path()
		addVertex(p, cx[1] + px * hw[1] * s, cy[1] + py * hw[1] * s)
		addVertex(p, cx[3] + px * hw[3] * s, cy[3] + py * hw[3] * s)
		addVertex(p, ex, ey)
		addVertex(p, cx[3] - px * hw[3] * s, cy[3] - py * hw[3] * s)
		addVertex(p, cx[1] - px * hw[1] * s, cy[1] - py * hw[1] * s)
		finish(p, underlayColor, underlayAlpha, lod)
	end

	local p = Path()
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

local function trifoliate(x, y, l, c)
	local a0 = rand() * TAU
	local tag = lodFor(l * 2.2)
	for i = 0, 2 do
		local a = a0 + i * TAU / 3 + (rand() - 0.5) * 0.5
		local ll = l * uniform(0.9, 1.1)
		leaflet(x, y, a, ll, jitter(c, 0.05, 2), tag)
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

-- Layout (everything placed before anything is drawn, so the far stack can proxy it) ----------

-- Soil peeks where the grass is thin.
local soils = weighted(count(soilPerM2), function(x, y)
	local d = density(x, y)
	return (1 - d) ^ 3
end)
for _, s in ipairs(soils) do
	s.r = lognormal(soilLo, soilHi, 0.5) * 0.5
end

-- Clumps: Poisson-disk centers, each a Thomas cluster of tufts that share a tone and a lean, so
-- the ground reads as clumps of grass with darker crevices between them. Thin ground grows fewer
-- tufts a clump.
local tufts = {}
for _, cl in ipairs(poissonTorus(clumpSpacing)) do
	local d = density(cl[1], cl[2])
	local v = 1 + clumpValue * (rand() * 2 - 1)
	local h = clumpHue * (rand() * 2 - 1)
	local lean = tuftLean * uniform(0.4, 1)
	local phi = flowAngle(cl[1], cl[2]) + gauss() * 0.8
	local sigma = uniform(clumpSpreadLo, clumpSpreadHi)
	for _ = 1, floor(lerp(tuftsPerClumpLo, tuftsPerClumpHi, d) + rand()) do
		local ox, oy = gauss() * sigma, gauss() * sigma
		-- Tufts at the clump's rim flop outward and sit lower, so a clump shades like a dome seen
		-- from straight above: light core, darker rim, no light direction.
		local out = min(1, sqrt(ox * ox + oy * oy) / (2 * sigma))
		local ux, uy = cos(phi) * lean + ox / sigma * 0.3 * out, sin(phi) * lean + oy / sigma * 0.3 * out
		tufts[#tufts + 1] = {
			(cl[1] + ox) % P,
			(cl[2] + oy) % P,
			d = d,
			v = v * (1 + clumpDome * (1 - 2 * out)) * (1 + 0.03 * (rand() * 2 - 1)),
			h = h,
			lean = min(0.9, sqrt(ux * ux + uy * uy)),
			phi = atan(uy, ux) + gauss() * 0.35,
		}
	end
end
shuffle(tufts)
for _, t in ipairs(tufts) do
	t.len = lognormal(tuftLenLo, tuftLenHi, 0.35)
	t.k = floor(uniform(tuftSpikesLo, tuftSpikesHi + 1))
	t.hub = uniform(tuftHubLo, tuftHubHi) * 0.5
	t.body = lush(tone(mix(tuftDark, tuftLight, rand()), t.v, t.h, 1), t.d)
	t.hi = lush(tone(mix(tuftHiDark, tuftHiLight, rand()), t.v, t.h, 1), t.d)
end

-- Clover patches: Thomas clusters around blue-noise parents.
local patches = blueNoise(count(cloverPatchPerM2), 0.6 / sqrt(max(cloverPatchPerM2, 0.01)))
local clovers = {}
for _, pa in ipairs(patches) do
	pa.sigma = uniform(cloverSigmaLo, cloverSigmaHi)
	pa.c = tone(cloverColor, 1 + 0.05 * (rand() * 2 - 1), 3 * (rand() * 2 - 1), 1)
	local n = floor(uniform(cloverPerLo, cloverPerHi + 1))
	for _ = 1, n do
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

-- Far stack ----------------------------------------------------------------------------------

rect(0, 0, P, P, farBase, 0)
for _, s in ipairs(soils) do
	blob(s[1], s[2], s.r * 0.7, 10, 0.25, 1, 0, soilColor, 1, 0)
end
for _, pa in ipairs(patches) do
	blob(pa[1], pa[2], pa.sigma * 1.3, 10, 0.3, uniform(0.8, 1.2), rand() * TAU, tone(pa.c, 0.94, 0, 1), 1, 0)
end
for _, t in ipairs(tufts) do
	local area = 0.7 * t.k * 0.5 * t.len * t.hub * 2
	blob(t[1], t[2], sqrt(area / math.pi), floor(uniform(5, 9)), 0.3, uniform(0.8, 1.2), rand() * TAU, mix(t.body, t.hi, 0.25), 1, 0)
end

-- Near stack ---------------------------------------------------------------------------------

rect(0, 0, P, P, gapColor, kCover)

-- Soil peeks: two or three overlapping blobs, a few darker specks, sometimes a dry edge.
for _, s in ipairs(soils) do
	local lod = lodFor(s.r * 2)
	if rand() < 0.15 then
		blob(s[1], s[2], s.r * 1.12, 12, 0.25, 1, 0, soilDry, 1, lod)
	end
	local parts = rand() < 0.5 and 2 or 3
	for _ = 1, parts do
		local ox, oy = gauss() * s.r * 0.35, gauss() * s.r * 0.35
		blob(s[1] + ox, s[2] + oy, s.r * uniform(0.6, 0.9), 12, 0.3, uniform(0.75, 1.3), rand() * TAU, jitter(soilColor, 0.04, 2), 1, lod)
	end
	for _ = 1, floor(uniform(1, 4)) do
		local ox, oy = gauss() * s.r * 0.4, gauss() * s.r * 0.4
		blob(s[1] + ox, s[2] + oy, s.r * uniform(0.08, 0.16), 6, 0.2, 1, 0, soilDark, 1, max(lod, 64))
	end
end

-- Pebbles, mostly on the soil, each over a soft centered halo.
do
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
end

-- Dark undergrowth fans, thinner where the grass is thin.
for _, u in ipairs(weighted(count(underPerM2), function(x, y)
	return 0.6 + 0.4 * density(x, y)
end)) do
	local k = floor(uniform(underSpikesLo, underSpikesHi + 1))
	local angles = fanAngles(k, uniform(0.4, 0.8), rand() * TAU)
	local lens, bends = {}, {}
	for i = 1, k do
		lens[i] = uniform(underLenLo, underLenHi) * uniform(0.7, 1)
		bends[i] = gauss() * 0.2
	end
	local c = lush(jitter(mix(underDark, underLight, rand()), elementValue, elementHue), density(u[1], u[2]))
	fan(u[1], u[2], angles, lens, bends, uniform(underHubLo, underHubHi) * 0.5, false, c, 1, kCover)
end

-- Clover under the tufts.
for _, cl in ipairs(clovers) do
	if not cl.top then
		trifoliate(cl[1], cl[2], cl.l, cl.c)
	end
end

-- Mid tufts: a body fan in the clump tone and a lighter fan on top, turned off the body's spikes.
for _, t in ipairs(tufts) do
	local angles = fanAngles(t.k, t.lean, t.phi)
	local lens, bends = {}, {}
	for i = 1, t.k do
		local along = cos(angles[i] - t.phi)
		lens[i] = t.len * uniform(0.6, 1.15) * (1 + 0.5 * t.lean * along)
		bends[i] = gauss() * 0.12
	end
	fan(t[1], t[2], angles, lens, bends, t.hub, true, t.body, 1, kCover)

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

-- Broadleaf rosettes (blue noise), over the tuft fans and under the top blades that cross them.
for _, ro in ipairs(blueNoise(count(rosettePerM2), 0.8 / sqrt(max(rosettePerM2, 0.01)))) do
	local k = floor(uniform(5, 9))
	local a0 = rand() * TAU
	local c = jitter(rosetteColor, 0.05, 3)
	local inner = jitter(rosetteInner, 0.04, 3)
	for i = 1, k do
		local a = a0 + (i - 1 + (rand() - 0.5) * 0.5) * TAU / k
		local l = uniform(rosetteLenLo, rosetteLenHi)
		local w = l * uniform(0.3, 0.4)
		local v = uniform(0.95, 1.05)
		leaf(ro[1], ro[2], a, l, w, tone(c, v, 0, 1), kCover)
		-- The lit inner leaf, cel-shaded on the same axis.
		leaf(ro[1] + cos(a) * l * 0.12, ro[2] + sin(a) * l * 0.12, a, l * 0.72, w * 0.6, tone(inner, v, 0, 1), lodFor(w * 0.6))
		midrib(ro[1], ro[2], a, l, w * 0.08, rosetteRib, 48)
	end
end

-- Clover over the tufts.
for _, cl in ipairs(clovers) do
	if cl.top then
		trifoliate(cl[1], cl[2], cl.l, cl.c)
	end
end

-- Top blades: most grow from tuft crowns and fall with the tuft's lean (a few radiate), the rest
-- follow the local flow anywhere.
do
	local n = count(bladePerM2)
	local nTuft = #tufts > 0 and floor(n * bladeInTufts + 0.5) or 0
	for i = 1, n do
		local x, y, a, d, v, h
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
			d, v, h = t.d, t.v, t.h
		else
			x, y = rand() * P, rand() * P
			a = flowAngle(x, y) + gauss() * flowSpread
			d, v, h = density(x, y), 1 + clumpValue * (rand() * 2 - 1), clumpHue * (rand() * 2 - 1)
		end
		local L = lognormal(bladeLenLo, bladeLenHi, 0.3)
		-- A share of broad blades: they stay in the coarser near mips, where the fine ones drop out.
		local w = rand() < broadShare and uniform(broadWLo, broadWHi) or uniform(bladeWLo, bladeWHi) * uniform(0.6, 1.0)
		local bow = bladeBow * gauss()
		local baseCol, tipCol
		if rand() < strawShare then
			baseCol = jitter(mix(strawDark, strawLight, rand()), elementValue, elementHue)
			tipCol = tone(baseCol, 1.05, 0, 0.95)
			w = w * 0.7
		else
			local e = 1 + elementValue * (rand() * 2 - 1)
			local eh = elementHue * (rand() * 2 - 1)
			baseCol = lush(tone(bladeBase, v * e, h + eh, 1), d)
			tipCol = lush(tone(mix(bladeTipDark, bladeTipLight, rand()), v * e, h + eh, 1), d)
			if rand() < dryTipShare then
				tipCol = mix(tipCol, strawDark, 0.45)
			end
		end
		blade(x, y, a, L, w, bow, baseCol, tipCol, true, rand() < tipHighlightShare, lodFor(w))
	end
end
