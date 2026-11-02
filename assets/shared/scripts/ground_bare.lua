-- Ground, bare: one seamless period x period meter tile variant (role ground) for the unvegetated
-- surfaces. Dirt, Sand, Mud, Rock and Snow share it and differ only in params. Seen like the flora,
-- from a hair off top-down and lit from the top of the screen: lit edges face up the screen and
-- shadows fall down it, which holds in every hex cell because the shader only mirrors these cells
-- left to right.
--
-- The ground itself (the base, broad tone patches and rock plates) is laid out first and painted into
-- a 6.25 cm tone grid, which every ground-born layer above takes its color from (grain, crests,
-- cracks, lumps, shadows), so those keep the patches' contrast instead of pulling them toward the
-- base, and which is scaled so each variant averages to the base exactly. Paint order: the far ground,
-- the near ground (base, patches, plates), drifts and ripples (a lit crest over a shadowed lee),
-- cracks, grain, pits, lichen, puddles, lumps, then the loose pieces (roots, straw, leaves, twigs,
-- shells, stones) back to front, grass tips poking through, and sparkle. Every layer is off at
-- density 0.
--
-- LOD: the far ground, the tone grid (puddles and lichen averaged in) as one rect a mip 4 texel, is
-- all mips 4 to 6 show; it's scaled to set their contrast and mean from the bake's report, as Grass's
-- far stack is. The near ground covers it from mip 3 up. Pieces keep to the mips where they're a
-- texel across (tag 1 / width; the ground's mass, lumps and crests, to half a texel) and never past
-- kFar. What drops out is balanced where it can be, so the mips that drop it keep mip 0's mean: each
-- light grain has a dark twin, a crest's lit strip cancels its lee, a lump's cap its darkened body,
-- and a stone sits on a proxy in its own average.
--
-- Meters, +Y down, sRGB colors 0..1. Densities are per square meter and counts are fixed per variant,
-- and every noise field has a whole number of cycles across the tile, so all variants share the same
-- statistics.

local P = getFloat("period", 4)
local AREA = P * P
local TAU = math.pi * 2

local rand, floor, sqrt, sin, cos, exp, log, min, max, abs =
	math.random, math.floor, math.sqrt, math.sin, math.cos, math.exp, math.log, math.min, math.max, math.abs

local function clamp(v, lo, hi)
	return v < lo and lo or (v > hi and hi or v)
end
local function uniform(a, b)
	return a + (b - a) * rand()
end
local function gauss()
	return sqrt(-2 * log(1 - rand())) * cos(TAU * rand())
end
local function lognormal(lo, hi, sigma)
	return clamp(sqrt(lo * hi) * exp(sigma * gauss()), lo, hi)
end
local function count(perSquareMeter)
	return floor(perSquareMeter * AREA + 0.5)
end

-- Colors -------------------------------------------------------------------------------------

local function parseHex(text)
	local s = text:gsub("#", "")
	return { tonumber(s:sub(1, 2), 16) / 255, tonumber(s:sub(3, 4), 16) / 255, tonumber(s:sub(5, 6), 16) / 255 }
end
local function hex(key, default)
	return parseHex(getString(key, default))
end
-- A comma-separated list of colors; fallback when the param is absent.
local function hexList(key, fallback)
	local out = {}
	for c in getString(key, ""):gmatch("[^,%s]+") do
		out[#out + 1] = parseHex(c)
	end
	if #out == 0 and fallback then
		out[1] = fallback
	end
	return out
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
local function luminance(c)
	local l = toLinear(c)
	return 0.2126 * l[1] + 0.7152 * l[2] + 0.0722 * l[3]
end

-- c with its linear value scaled by k: the same hue, k times the light.
local function shade(c, k)
	local l = toLinear(c)
	return toSrgb({ l[1] * k, l[2] * k, l[3] * k })
end
-- Linear-light mix.
local function mix(a, b, t)
	local la, lb = toLinear(a), toLinear(b)
	return toSrgb({ la[1] + (lb[1] - la[1]) * t, la[2] + (lb[2] - la[2]) * t, la[3] + (lb[3] - la[3]) * t })
end
local function jitter(c, amp)
	return shade(c, 1 + amp * (rand() * 2 - 1))
end
local function pick(list)
	return list[floor(rand() * #list) + 1]
end

local base = hex("baseColor", "#808080")
local baseLum = luminance(base)

-- The width of a lit strip that cancels a shadow strip of width w over the base, in linear light,
-- kept within a third and three times w.
local function balancedWidth(w, lit, shadow)
	local up, down = luminance(lit) - baseLum, baseLum - luminance(shadow)
	if up <= 1e-6 then
		return w
	end
	return w * clamp(down / up, 1 / 3, 3)
end

-- Emitting -----------------------------------------------------------------------------------

local kFar = 6 -- no piece is drawn below 6 px/m: mips 4 to 6 (4, 2, 1 px/m) are the far ground alone
local function lodFor(width)
	return clamp(1 / width, kFar, 64)
end

local pathProto = Path()
local addVertex, setColor, setLod, closePath = pathProto.addVertex, pathProto.setColor, pathProto.setLod, pathProto.close
local addPath = asset.addPath

local function put(xs, ys, n, c, lod)
	local p = Path()
	for i = 1, n do
		addVertex(p, xs[i], ys[i])
	end
	setColor(p, c[1], c[2], c[3], 1)
	if lod > 0 then
		setLod(p, lod)
	end
	closePath(p)
	addPath(asset, p)
end

-- A polygon kept as point lists, so it can be emitted again moved and scaled (shadows, lit facets).
local function shape(xs, ys)
	return { xs = xs, ys = ys, n = #xs }
end
-- s moved by (dx, dy) and scaled by k about (cx, cy), emitted in color c.
local function emitShape(s, c, lod, dx, dy, k, cx, cy)
	local xs, ys = {}, {}
	k = k or 1
	cx, cy = cx or 0, cy or 0
	for i = 1, s.n do
		xs[i] = cx + (s.xs[i] - cx) * k + (dx or 0)
		ys[i] = cy + (s.ys[i] - cy) * k + (dy or 0)
	end
	put(xs, ys, s.n, c, lod)
end

-- An organic outline: radius r modulated by two low harmonics, stretched by aspect along rot.
local function blobShape(x, y, r, n, wobble, aspect, rot)
	local a2, a3 = rand() * TAU, rand() * TAU
	local w2, w3 = wobble * rand(), wobble * 0.6 * rand()
	local cr, sr = cos(rot), sin(rot)
	local xs, ys = {}, {}
	for i = 0, n - 1 do
		local a = TAU * i / n
		local rr = r * (1 + w2 * cos(2 * a + a2) + w3 * cos(3 * a + a3))
		local lx, ly = cos(a) * rr * aspect, sin(a) * rr
		xs[i + 1], ys[i + 1] = x + lx * cr - ly * sr, y + lx * sr + ly * cr
	end
	return shape(xs, ys)
end

-- An angular outline for stones: n corners at jittered angles and radii.
local function stoneShape(x, y, r, n, aspect, rot)
	local cr, sr = cos(rot), sin(rot)
	local xs, ys = {}, {}
	for i = 0, n - 1 do
		local a = TAU * (i + (rand() - 0.5) * 0.5) / n
		local rr = r * uniform(0.8, 1.1)
		local lx, ly = cos(a) * rr * aspect, sin(a) * rr
		xs[i + 1], ys[i + 1] = x + lx * cr - ly * sr, y + lx * sr + ly * cr
	end
	return shape(xs, ys)
end

-- The two edges of a strip of width w along the polyline (px, py), tapering to its ends over taper of
-- its length. side 0 centers it on the line, -1 puts it above the line on screen, 1 below.
local function stripSides(px, py, w, taper, side)
	local n = #px
	local left, right = {}, {}
	for i = 1, n do
		local ax, ay = px[max(1, i - 1)], py[max(1, i - 1)]
		local bx, by = px[min(n, i + 1)], py[min(n, i + 1)]
		local dx, dy = bx - ax, by - ay
		local len = sqrt(dx * dx + dy * dy)
		if len < 1e-9 then
			dx, dy, len = 1, 0, 1
		end
		local nx, ny = -dy / len, dx / len
		if side ~= 0 and ny < 0 then
			nx, ny = -nx, -ny -- a one-sided strip's normal points down the screen
		end
		local t = (i - 1) / max(1, n - 1)
		local f = min(1, min(t, 1 - t) / max(taper, 1e-6))
		local h = w * f
		local lo, hi
		if side < 0 then
			lo, hi = -h, 0
		elseif side > 0 then
			lo, hi = 0, h
		else
			lo, hi = -h * 0.5, h * 0.5
		end
		left[i] = { px[i] + nx * lo, py[i] + ny * lo }
		right[i] = { px[i] + nx * hi, py[i] + ny * hi }
	end
	return left, right
end

-- The part of a strip between points i0 and i1 as one polygon.
local function stripPart(left, right, i0, i1)
	local xs, ys = {}, {}
	for i = i0, i1 do
		xs[#xs + 1], ys[#ys + 1] = left[i][1], left[i][2]
	end
	for i = i1, i0, -1 do
		xs[#xs + 1], ys[#ys + 1] = right[i][1], right[i][2]
	end
	return shape(xs, ys)
end

local function stripShape(px, py, w, taper, side)
	local left, right = stripSides(px, py, w, taper, side)
	return stripPart(left, right, 1, #px)
end

-- Fields and placement -------------------------------------------------------------------------

local noiseSeed = floor(rand() * 1000000)

-- Periodic noise with a whole number of cycles across the tile, so it wraps exactly.
local function field(x, y, cycles, salt)
	cycles = max(1, floor(cycles + 0.5))
	local f = cycles / P
	return periodicNoise2(x * f, y * f, cycles, noiseSeed + salt)
end

local function wrapDelta(d)
	if d > P * 0.5 then
		return d - P
	elseif d < -P * 0.5 then
		return d + P
	end
	return d
end

-- n points by dart throwing with a minimum spacing across the wrap (falls back to the farthest fit).
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
		pts[#pts + 1] = best
	end
	return pts
end

-- Base and patches -----------------------------------------------------------------------------

-- Tone patches: blue-noise blobs stretched by stretch along x, half in the light tone and half in the
-- dark, each a few nested steps from the base's tone to the patch's, so they read as flat cel steps.
-- Inner steps sit a little up the screen: a mound is lit on its upper side, a hollow shaded there.
-- Their sizes are normalized per variant, so every variant covers about the same area with each tone.
local steps = {} -- every step polygon in paint order: { shape, linear color }
local function layoutPatches(prefix)
	local density = getFloat(prefix .. "Density", 0)
	if density <= 0 then
		return
	end
	local rLo, rHi = getFloatRange(prefix .. "Size", 0.2, 0.6)
	local tones = { hex(prefix .. "Light", "#FFFFFF"), hex(prefix .. "Dark", "#000000") }
	local stepCount = max(1, getInt(prefix .. "Steps", 2))
	local stretch = getFloat(prefix .. "Stretch", 1)
	local wobble = getFloat(prefix .. "Wobble", 0.35)
	local pts = blueNoise(count(density), 0.7 / sqrt(density))
	local sum = { 0, 0 }
	for i, p in ipairs(pts) do
		p.r = lognormal(rLo, rHi, 0.4)
		p.tone = (i % 2) + 1
		sum[p.tone] = sum[p.tone] + p.r * p.r
	end
	local target = #pts * 0.5 * rLo * rHi * exp(2 * 0.4 * 0.4)
	for _, p in ipairs(pts) do
		local r = p.r * sqrt(target / max(sum[p.tone], 1e-9))
		local outline = blobShape(p[1], p[2], r, 16, wobble, stretch, (rand() - 0.5) * 0.5)
		for s = 1, stepCount do
			local k = 1 - 0.45 * (s - 1) / stepCount
			local xs, ys = {}, {}
			for i = 1, outline.n do
				xs[i] = p[1] + (outline.xs[i] - p[1]) * k
				ys[i] = p[2] + (outline.ys[i] - p[2]) * k - (s - 1) * r * 0.08
			end
			steps[#steps + 1] = { shape(xs, ys), toLinear(mix(base, tones[p.tone], s / stepCount)) }
		end
	end
end
layoutPatches("patch")
layoutPatches("tint")

-- The part of convex polygon (xs, ys) where nx * x + ny * y <= d.
local function clipHalf(xs, ys, nx, ny, d)
	local ox, oy = {}, {}
	local n = #xs
	if n == 0 then
		return ox, oy
	end
	local px, py = xs[n], ys[n]
	local pd = nx * px + ny * py - d
	for i = 1, n do
		local cx, cy = xs[i], ys[i]
		local cd = nx * cx + ny * cy - d
		if (cd <= 0) ~= (pd <= 0) then
			local t = pd / (pd - cd)
			ox[#ox + 1], oy[#oy + 1] = px + (cx - px) * t, py + (cy - py) * t
		end
		if cd <= 0 then
			ox[#ox + 1], oy[#oy + 1] = cx, cy
		end
		px, py, pd = cx, cy, cd
	end
	return ox, oy
end

-- Rock plates: the ground broken into slabs along joints. Voronoi cells of loosely spaced sites on the
-- torus, each inset by half its joint's width (plateJoint, varying edge to edge) so plateJointColor
-- shows between them, shaded as a slab lit from the top: a lit band plateBevel wide along its upper
-- edges, a shaded band along its lower ones, the face between. A plateMerge share of neighbors have
-- no joint or bevel between them, so slabs come in irregular sizes, and face tones follow a broad
-- field so merged neighbors match. Every edge then wiggles by plateWiggle, the same way in all three
-- layers. They're ground: the tone grid and the far mips see them.
do
	local density = getFloat("plateDensity", 0)
	if density > 0 then
		local joint = getFloat("plateJoint", 0.02)
		local colors = hexList("plateColors", base)
		local spread = getFloat("plateJitter", 0.06)
		local bevelLo, bevelHi = getFloatRange("plateBevel", 0.01, 0.02)
		local litK, darkK = getFloat("plateLit", 1.3), getFloat("plateDark", 0.7)
		local mergeShare = getFloat("plateMerge", 0)
		local wiggle = getFloat("plateWiggle", 0)
		local toneCycles = getFloat("plateToneCycles", 4)
		local spacing = 1 / sqrt(density)
		local reach = 2.5 * spacing
		steps[#steps + 1] = { shape({ 0, P, P, 0 }, { 0, 0, P, P }), toLinear(hex("plateJointColor", "#404040")) }
		local sites = blueNoise(count(density), 0.5 * spacing)
		-- Per pair of neighbors, decided once for both: merged or not, and the joint's width.
		local edges = {}
		local function edge(i, j)
			local key = min(i, j) * 65536 + max(i, j)
			local e = edges[key]
			if not e then
				e = { merged = rand() < mergeShare, width = joint * uniform(0.5, 1.6) }
				edges[key] = e
			end
			return e
		end
		-- Edges subdivided to 3 cm and pushed by a periodic vector field, so joints wander.
		local function wiggled(s)
			if wiggle <= 0 then
				return s
			end
			local xs, ys = {}, {}
			for k = 1, s.n do
				local ax, ay = s.xs[k], s.ys[k]
				local bx, by = s.xs[k % s.n + 1], s.ys[k % s.n + 1]
				local parts = max(1, floor(sqrt((bx - ax) ^ 2 + (by - ay) ^ 2) / 0.03 + 0.5))
				for q = 0, parts - 1 do
					local t = q / parts
					local x, y = ax + (bx - ax) * t, ay + (by - ay) * t
					xs[#xs + 1] = x + wiggle * field(x, y, 16, 211)
					ys[#ys + 1] = y + wiggle * field(x, y, 16, 223)
				end
			end
			return shape(xs, ys)
		end
		for i, si in ipairs(sites) do
			-- The inset cell as half-planes n . p <= d: each neighbor's bisector moved toward the site.
			local planes = {}
			for j, sj in ipairs(sites) do
				local dx, dy = wrapDelta(sj[1] - si[1]), wrapDelta(sj[2] - si[2])
				local d2 = dx * dx + dy * dy
				if j ~= i and d2 < reach * reach then
					local len = sqrt(d2)
					local nx, ny = dx / len, dy / len
					local e = edge(i, j)
					local inset = e.merged and 0 or e.width * 0.5
					planes[#planes + 1] = { nx, ny, (si[1] + dx * 0.5) * nx + (si[2] + dy * 0.5) * ny - inset, e.merged }
				end
			end
			-- The cell, and its intersection with copies of itself moved down by each of shifts (only
			-- the jointed edges move, so merged ones get no bevel).
			local function cell(shifts)
				local xs = { si[1] - reach, si[1] + reach, si[1] + reach, si[1] - reach }
				local ys = { si[2] - reach, si[2] - reach, si[2] + reach, si[2] + reach }
				for _, pl in ipairs(planes) do
					xs, ys = clipHalf(xs, ys, pl[1], pl[2], pl[3])
				end
				for _, t in ipairs(shifts) do
					for _, pl in ipairs(planes) do
						if not pl[4] then
							xs, ys = clipHalf(xs, ys, pl[1], pl[2], pl[3] + pl[2] * t)
						end
					end
				end
				return wiggled(shape(xs, ys))
			end
			local tone = clamp(0.5 + 0.8 * field(si[1], si[2], toneCycles, 227), 0, 0.999)
			local c = colors[floor(tone * #colors) + 1]
			local face = toLinear(shade(c, 1 + spread * (rand() * 2 - 1)))
			local bevel = uniform(bevelLo, bevelHi)
			local layers = {
				{ cell({}), litK },
				{ cell({ bevel }), darkK },
				{ cell({ bevel, -bevel * 0.8 }), 1 },
			}
			for _, layer in ipairs(layers) do
				if layer[1].n >= 3 then
					steps[#steps + 1] = { layer[1], { face[1] * layer[2], face[2] * layer[2], face[3] * layer[2] } }
				end
			end
		end
	end
end

-- Puddles: small standing water in a hollow, lit from the top of the screen: a dark wet rim, the
-- lower bank lit along the bottom edge, the water with the upper bank's shade across its top, a few
-- streaks of sky reflection and a glint. Laid out here, so the far ground can average them in, and
-- drawn in paint order further down. Their sizes are normalized per variant like the patches.
local puddles = {}
do
	local density = getFloat("puddleDensity", 0)
	if density > 0 then
		local rLo, rHi = getFloatRange("puddleSize", 0.04, 0.12)
		local rim, water = hex("puddleRimColor", "#000000"), hex("puddleColor", "#202020")
		local bank, hollow = hex("puddleBankColor", "#808080"), hex("puddleShadeColor", "#101010")
		local sheen = hex("puddleSheenColor", "#808080")
		local pts = blueNoise(count(density), 0.6 / sqrt(density))
		local sum = 0
		for _, p in ipairs(pts) do
			p.r = lognormal(rLo, rHi, 0.4)
			sum = sum + p.r * p.r
		end
		local scale = sqrt(#pts * rLo * rHi * exp(2 * 0.4 * 0.4) / max(sum, 1e-9))
		for _, p in ipairs(pts) do
			local r = p.r * scale
			local x, y = p[1], p[2]
			local outline = blobShape(x, y, r, 16, 0.35, uniform(1.2, 1.7), (rand() - 0.5) * 0.4)
			local function step(k, dy)
				local xs, ys = {}, {}
				for i = 1, outline.n do
					xs[i], ys[i] = x + (outline.xs[i] - x) * k, y + (outline.ys[i] - y) * k + dy
				end
				return shape(xs, ys)
			end
			local layers = {
				{ step(1.2, 0), rim, onBase = true },
				{ step(1.1, r * 0.1), bank, onBase = true },
				{ outline, hollow },
				{ step(0.94, r * 0.12), water },
			}
			-- Sky streaks: short, thin and level, in the lower part of the water.
			for _ = 1, floor(uniform(1, 4)) do
				local sx, sy = x + (rand() - 0.5) * r, y + uniform(0.05, 0.45) * r
				local len, hw = r * uniform(0.25, 0.55), r * uniform(0.05, 0.08)
				layers[#layers + 1] = { shape({ sx - len, sx, sx + len, sx }, { sy, sy - hw, sy, sy + hw }), sheen }
			end
			puddles[#puddles + 1] = { x = x, y = y, r = r, layers = layers }
		end
	end
end

-- Lichen: crustose colonies, an irregular crust in lichenColors (taken in turn) kept down to the mip
-- where the colony is a texel across, spotted with light and dark dots in pairs around its color.
-- Laid out here, so the far ground can average them in like the puddles, and drawn after the grain.
local lichens = {}
do
	local density = getFloat("lichenDensity", 0)
	if density > 0 then
		local rLo, rHi = getFloatRange("lichenSize", 0.03, 0.08)
		local dLo, dHi = getFloatRange("lichenDot", 0.008, 0.018)
		local colors = hexList("lichenColors", base)
		for i, p in ipairs(blueNoise(count(density), 0.6 / sqrt(density))) do
			local c = colors[(i - 1) % #colors + 1]
			local r = lognormal(rLo, rHi, 0.4)
			local l = {
				crust = blobShape(p[1], p[2], r, 14, 0.5, uniform(0.8, 1.25), rand() * TAU),
				crustColor = shade(c, 0.9),
				lod = lodFor(r * 2),
				dots = {},
			}
			for _ = 1, floor(r * r * 4000) do
				local a, d = rand() * TAU, r * sqrt(rand()) * 0.9
				local size = uniform(dLo, dHi)
				local k = rand() < 0.5 and 1.15 or 0.82
				l.dots[#l.dots + 1] = { blobShape(p[1] + cos(a) * d, p[2] + sin(a) * d, size * 0.5, 6, 0.2, 1, 0), shade(c, k), lodFor(size) }
			end
			lichens[#lichens + 1] = l
		end
	end
end

-- The ground's tone (base and patches) on a 6.25 cm grid, linear: what the ground-born layers above
-- (grain, ripples, cracks, lumps, shadows) are colored from, so they keep the patches' contrast at
-- every mip rather than pulling them toward the base. Then the whole ground is scaled so each
-- variant's tone averages to the base exactly, however its patches happened to overlap.
local kGridN = 64
local gridCell = P / kGridN
local grid = {}
-- Whether (x, y) is inside shape s (even-odd), in the shape's own coordinates.
local function insideShape(s, x, y)
	local xs, ys, n = s.xs, s.ys, s.n
	local hit = false
	local jx, jy = xs[n], ys[n]
	for k = 1, n do
		local ix, iy = xs[k], ys[k]
		if (iy > y) ~= (jy > y) and x < (jx - ix) * (y - iy) / (jy - iy) + ix then
			hit = not hit
		end
		jx, jy = ix, iy
	end
	return hit
end
-- Paints shape s into a grid of linear colors (cell centers inside it take color c), wrapped.
local function fillGrid(target, s, c)
	local x0, x1, y0, y1 = math.huge, -math.huge, math.huge, -math.huge
	for i = 1, s.n do
		x0, x1 = min(x0, s.xs[i]), max(x1, s.xs[i])
		y0, y1 = min(y0, s.ys[i]), max(y1, s.ys[i])
	end
	for j = floor(y0 / gridCell), floor(y1 / gridCell) do
		local cy = (j + 0.5) * gridCell
		for i = floor(x0 / gridCell), floor(x1 / gridCell) do
			if insideShape(s, (i + 0.5) * gridCell, cy) then
				target[(j % kGridN) * kGridN + i % kGridN + 1] = c
			end
		end
	end
end
-- Whether (x, y) lies in a puddle's rim, across the wrap: lumps and grain keep out of the water.
local function inPuddle(x, y)
	for _, p in ipairs(puddles) do
		local dx, dy = wrapDelta(x - p.x), wrapDelta(y - p.y)
		if abs(dx) < p.r * 3 and abs(dy) < p.r * 3 and insideShape(p.layers[1][1], p.x + dx, p.y + dy) then
			return true
		end
	end
	return false
end
do
	local b = toLinear(base)
	for k = 1, kGridN * kGridN do
		grid[k] = b
	end
	for _, st in ipairs(steps) do
		fillGrid(grid, st[1], st[2])
	end
	local m = { 0, 0, 0 }
	for k = 1, kGridN * kGridN do
		for c = 1, 3 do
			m[c] = m[c] + grid[k][c]
		end
	end
	local f = {}
	for c = 1, 3 do
		f[c] = b[c] * kGridN * kGridN / max(m[c], 1e-9)
	end
	local scaled = {}
	for k = 1, kGridN * kGridN do
		local g = grid[k]
		if not scaled[g] then
			scaled[g] = { g[1] * f[1], g[2] * f[2], g[3] * f[3] }
		end
		grid[k] = scaled[g]
	end
	for _, st in ipairs(steps) do
		st[2] = { st[2][1] * f[1], st[2][2] * f[2], st[2][3] * f[3] }
	end
	local ground = { b[1] * f[1], b[2] * f[2], b[3] * f[3] }

	-- The far ground (mips 4 to 6), under the near one: the tone grid averaged into one rect a mip 4
	-- texel (farCellsPerMeter), so the far mips render exactly instead of aliasing the patch outlines.
	-- Contrast is set in two bands against the bake's reference, so mip 4 and mips 5 and 6 can each
	-- match it: a cell against a blur of its neighbors by farContrast, the blur's drift by
	-- farCoarseContrast. The whole is times farGain (linear, per channel), which sets the far mips'
	-- mean to mip 0's, the pieces above included.
	local farContrast = getFloat("farContrast", 1)
	local farCoarseContrast = getFloat("farCoarseContrast", farContrast)
	local farGain = { 1, 1, 1 }
	do
		local i = 1
		for v in getString("farGain", "1,1,1"):gmatch("[^,]+") do
			farGain[i] = tonumber(v)
			i = i + 1
		end
	end
	-- Lichen and puddles are averaged in too: they're the other things big enough to show at mip 4.
	local farGrid = {}
	for k = 1, kGridN * kGridN do
		farGrid[k] = grid[k]
	end
	for _, l in ipairs(lichens) do
		fillGrid(farGrid, l.crust, toLinear(l.crustColor))
	end
	for _, p in ipairs(puddles) do
		local g = grid[(floor(p.y / gridCell) % kGridN) * kGridN + floor(p.x / gridCell) % kGridN + 1]
		for _, layer in ipairs(p.layers) do
			local c = toLinear(layer[2])
			if layer.onBase then
				c = { c[1] / b[1] * g[1], c[2] / b[2] * g[2], c[3] / b[3] * g[3] }
			end
			fillGrid(farGrid, layer[1], c)
		end
	end
	local farN = max(1, floor(P * getFloat("farCellsPerMeter", 4) + 0.5))
	local per = kGridN / farN
	local fs = P / farN
	local avg = {}
	for fj = 0, farN - 1 do
		for fi = 0, farN - 1 do
			local sum = { 0, 0, 0 }
			for j = floor(fj * per), floor((fj + 1) * per) - 1 do
				for i = floor(fi * per), floor((fi + 1) * per) - 1 do
					local g = farGrid[j * kGridN + i + 1]
					sum[1], sum[2], sum[3] = sum[1] + g[1], sum[2] + g[2], sum[3] + g[3]
				end
			end
			local n = floor(per) * floor(per)
			avg[fj * farN + fi + 1] = { sum[1] / n, sum[2] / n, sum[3] / n }
		end
	end
	for fj = 0, farN - 1 do
		for fi = 0, farN - 1 do
			local blur = { 0, 0, 0 }
			for dj = -1, 1 do
				for di = -1, 1 do
					local a = avg[((fj + dj) % farN) * farN + (fi + di) % farN + 1]
					blur[1], blur[2], blur[3] = blur[1] + a[1] / 9, blur[2] + a[2] / 9, blur[3] + a[3] / 9
				end
			end
			local a = avg[fj * farN + fi + 1]
			local c = {}
			for ch = 1, 3 do
				c[ch] = (ground[ch] + (a[ch] - blur[ch]) * farContrast + (blur[ch] - ground[ch]) * farCoarseContrast) * farGain[ch]
			end
			put({ fi * fs, (fi + 1) * fs, (fi + 1) * fs, fi * fs }, { fj * fs, fj * fs, (fj + 1) * fs, (fj + 1) * fs }, 4, toSrgb(c), 0)
		end
	end
	-- The near ground covers it from mip 3 up.
	put({ 0, P, P, 0 }, { 0, 0, P, P }, 4, toSrgb(ground), kFar)
	for _, st in ipairs(steps) do
		emitShape(st[1], toSrgb(st[2]), kFar)
	end
end

-- The ground's linear tone at (x, y).
local function groundAt(x, y)
	return grid[(floor(y / gridCell) % kGridN) * kGridN + floor(x / gridCell) % kGridN + 1]
end
-- c, a color designed against the base, carried onto the ground at (x, y): the same ratio to the
-- local tone that c has to the base, per channel, in linear light.
local baseLin = toLinear(base)
local function onGround(c, x, y)
	local l, g = toLinear(c), groundAt(x, y)
	return toSrgb({ l[1] / baseLin[1] * g[1], l[2] / baseLin[2] * g[2], l[3] / baseLin[3] * g[3] })
end
-- The ground at (x, y) with its light scaled by k.
local function groundShade(x, y, k)
	local g = groundAt(x, y)
	return toSrgb({ g[1] * k, g[2] * k, g[3] * k })
end

-- Ripples and drifts ---------------------------------------------------------------------------

-- Rows of crests across the tile, wavelength apart (rounded so a whole number fits), each a wavy line
-- broken into runs where a periodic field is high. A run is a shadowed lee strip below the crest and
-- a lit strip above it, as wide as balances the shadow, tapering at both ends; soft ones step to it
-- in two.
local function ripples(prefix)
	local wavelength = getFloat(prefix .. "Wavelength", 0)
	if wavelength <= 0 then
		return
	end
	local rows = max(1, floor(P / wavelength + 0.5))
	wavelength = P / rows
	local wLo, wHi = getFloatRange(prefix .. "Width", 0.015, 0.03)
	local coverage = getFloat(prefix .. "Coverage", 0.6)
	local amp = getFloat(prefix .. "Amp", 0.03)
	local waveCycles = getFloat(prefix .. "WaveCycles", 3)
	local breakCycles = getFloat(prefix .. "BreakCycles", 8)
	local wander = clamp(getFloat(prefix .. "Wander", 0.3), 0, 1)
	local soft = getInt(prefix .. "Soft", 0) ~= 0
	local lit, shadow = hex(prefix .. "Lit", "#FFFFFF"), hex(prefix .. "Shadow", "#000000")
	local step = min(0.02, wavelength * 0.25)
	local nx = floor(P / step + 0.5)
	step = P / nx
	-- The break field's threshold for the coverage asked: noise is about -0.7..0.7, so map linearly.
	local threshold = 0.7 - 1.4 * clamp(coverage, 0, 1)
	-- Every row bends by the same two waves along x (waveCycles and about 1.7 times as many a period),
	-- their phases drifting a cycle or two down the tile, so neighboring crests run parallel and the
	-- pattern meanders as a whole; wander of the bend is noise instead, each row its own.
	local n1 = max(1, floor(waveCycles + 0.5))
	local n2 = max(1, floor(waveCycles * 1.7 + 0.5))
	local ph1, ph2 = rand() * TAU, rand() * TAU
	for j = 0, rows - 1 do
		local y0 = (j + 0.5 + (rand() - 0.5) * 0.2) * wavelength
		local w = uniform(wLo, wHi)
		local salt = 101 + j * 7
		local function crestY(x)
			local waves = 0.65 * sin(TAU * (n1 * x + y0) / P + ph1) + 0.35 * sin(TAU * (n2 * x - 2 * y0) / P + ph2)
			local noise = 1.4 * field(x, y0, n1 * 2, 71) + 0.6 * field(x, y0, n2 * 2, 73)
			return y0 + amp * ((1 - wander) * waves + wander * noise)
		end
		-- Runs along the row, wrapping: each starts where the break field rises through the threshold;
		-- a row it never breaks is one run around the tile with no ends to taper.
		local on = {}
		local start = nil
		for i = 0, nx - 1 do
			on[i] = field(i * step, y0, breakCycles, salt) > threshold
		end
		for i = 0, nx - 1 do
			if not on[i] and on[(i + 1) % nx] then
				start = (i + 1) % nx
				break
			end
		end
		local runs = {}
		if start == nil then
			if on[0] then
				runs[1] = { 0, nx, 0 }
			end
		else
			local i, visited = start, 0
			while visited < nx do
				if on[i % nx] then
					local first = i
					while on[i % nx] and i - first < nx do
						i, visited = i + 1, visited + 1
					end
					runs[#runs + 1] = { first, i - first, 0.3 }
				else
					i, visited = i + 1, visited + 1
				end
			end
		end
		for _, run in ipairs(runs) do
			local px, py = {}, {}
			for i = run[1], run[1] + run[2] do
				local x = i * step
				px[#px + 1], py[#py + 1] = x, crestY(x % P)
			end
			if #px >= 4 then
				-- Emitted a few points at a time, each part in the ground's tone where it lies.
				local lw = balancedWidth(w, lit, shadow)
				local lod = lodFor(2 * min(w, lw))
				local litL, litR = stripSides(px, py, lw, run[3], -1)
				local shL, shR = stripSides(px, py, w, run[3], 1)
				local litInL, litInR, shInL, shInR
				if soft then
					litInL, litInR = stripSides(px, py, lw * 0.5, run[3], -1)
					shInL, shInR = stripSides(px, py, w * 0.5, run[3], 1)
				end
				local i0 = 1
				while i0 < #px do
					local i1 = min(#px, i0 + 5)
					local m = floor((i0 + i1) / 2)
					local mx, my = px[m] % P, py[m] % P
					local litC, shC = onGround(lit, mx, my), onGround(shadow, mx, my)
					if soft then
						-- A half step over the whole strip, the full step nearer the crest.
						local g = toSrgb(groundAt(mx, my))
						emitShape(stripPart(litL, litR, i0, i1), mix(g, litC, 0.5), lod)
						emitShape(stripPart(shL, shR, i0, i1), mix(g, shC, 0.5), lod)
						emitShape(stripPart(litInL, litInR, i0, i1), litC, lod)
						emitShape(stripPart(shInL, shInR, i0, i1), shC, lod)
					else
						emitShape(stripPart(litL, litR, i0, i1), litC, lod)
						emitShape(stripPart(shL, shR, i0, i1), shC, lod)
					end
					i0 = i1
				end
			end
		end
	end
end
ripples("drift")
ripples("ripple")

-- Cracks ---------------------------------------------------------------------------------------

-- Random walks that sometimes branch: a dark crack with a lit lip above it, balanced in width.
do
	local density = getFloat("crackDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("crackLength", 0.15, 0.4)
		local wLo, wHi = getFloatRange("crackWidth", 0.01, 0.015)
		local dark, lip = hex("crackColor", "#000000"), hex("crackLipColor", "#FFFFFF")
		local function walk(x, y, a, len, w, depth)
			local px, py = { x }, { y }
			local stepLen = 0.025
			local n = max(2, floor(len / stepLen))
			for k = 1, n do
				a = a + gauss() * 0.35
				x, y = x + cos(a) * stepLen, y + sin(a) * stepLen
				px[#px + 1], py[#py + 1] = x, y
				if depth < 2 and k > 1 and k < n - 1 and rand() < 0.08 then
					walk(x, y, a + (rand() < 0.5 and -1 or 1) * uniform(0.6, 1.1), len * uniform(0.3, 0.5), w * 0.7, depth + 1)
				end
			end
			-- The lip is the crack again, wider and moved up: the part the crack doesn't cover is the lit
			-- edge, widest where the crack runs across the screen.
			local lw = balancedWidth(w, lip, dark)
			local lod = lodFor(w)
			local mx, my = px[floor(#px / 2) + 1] % P, py[floor(#py / 2) + 1] % P
			emitShape(stripShape(px, py, w + lw, 0.25, 0), onGround(lip, mx, my), lod, 0, -lw * 0.6)
			emitShape(stripShape(px, py, w, 0.25, 0), onGround(dark, mx, my), lod)
		end
		for _ = 1, count(density) do
			walk(rand() * P, rand() * P, rand() * TAU, lognormal(lenLo, lenHi, 0.4), uniform(wLo, wHi), 0)
		end
	end
end

-- Grain ----------------------------------------------------------------------------------------

-- Specks in pairs, a light one and a dark twin of the same size, grainContrast either way in linear
-- light; plus a share of tinted grains from grainTints.
do
	local density = getFloat("grainDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("grainSize", 0.008, 0.016)
		local contrast = getFloat("grainContrast", 0.25)
		local tints = hexList("grainTints")
		local tintShare = getFloat("grainTintShare", 0)
		for _ = 1, floor(count(density) / 2) do
			local size = lognormal(sLo, sHi, 0.4)
			local lod = lodFor(size)
			for k = 1, 2 do
				local x, y = rand() * P, rand() * P
				local c
				if #tints > 0 and rand() < tintShare then
					c = jitter(pick(tints), 0.05)
				else
					c = groundShade(x, y, (k == 1 and 1 + contrast or 1 - contrast) * (1 + 0.05 * (rand() * 2 - 1)))
				end
				emitShape(blobShape(x, y, size * 0.5, 6, 0.3, uniform(0.6, 1), rand() * TAU), c, lod)
			end
		end
	end
end

-- Pits and lichen ------------------------------------------------------------------------------

-- Pits: small hollows lit from the top, shaded on their upper wall and lit on their lower one, about
-- as much of each, down to half a texel.
do
	local density = getFloat("pitDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("pitSize", 0.01, 0.025)
		local darkK, litK = getFloat("pitDark", 0.6), getFloat("pitLit", 1.3)
		for _ = 1, count(density) do
			local x, y = rand() * P, rand() * P
			local size = lognormal(sLo, sHi, 0.4)
			local r = size * 0.5
			local lod = lodFor(size * 2)
			local s = blobShape(x, y, r, 7, 0.3, uniform(0.8, 1.3), rand() * TAU)
			emitShape(s, groundShade(x, y, litK), lod)
			emitShape(s, groundShade(x, y, darkK), lod, 0, -r * 0.3, 0.85, x, y)
		end
	end
end

-- Lichen: the colonies laid out above, a crust and its dots.
for _, l in ipairs(lichens) do
	emitShape(l.crust, l.crustColor, l.lod)
	for _, d in ipairs(l.dots) do
		emitShape(d[1], d[2], d[3])
	end
end

-- Puddles --------------------------------------------------------------------------------------

-- The puddles laid out above, down to the mip where one is a texel across (the far ground has them
-- averaged in below that).
do
	local glint = hex("puddleGlintColor", "#FFFFFF")
	for _, p in ipairs(puddles) do
		local lod = lodFor(p.r * 2)
		for _, layer in ipairs(p.layers) do
			emitShape(layer[1], layer.onBase and onGround(layer[2], p.x, p.y) or layer[2], lod)
		end
		local g = blobShape(p.x + (rand() - 0.5) * p.r, p.y - p.r * 0.45, p.r * 0.12, 6, 0.2, 2.2, 0)
		emitShape(g, glint, 64)
	end
end

-- Lumps ----------------------------------------------------------------------------------------

-- Clods and churned lumps, lit from the top, back to front: a shadow falling below, the body, a lit cap
-- toward the top, and on wet ground a gloss dab. Packed densely, a nearer lump covers most of the
-- shadow of the one behind, so the body is darkened just enough that body and cap average to the
-- lump's color, and the mips that drop small lumps keep the mean. Lumps are the ground's mass, so they
-- stay down to half a texel.
do
	local density = getFloat("lumpDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("lumpSize", 0.02, 0.06)
		local colors = hexList("lumpColors", base)
		local shadowK = getFloat("lumpShadow", 0.55)
		local capK = getFloat("lumpLit", 1.4)
		local spread = getFloat("lumpJitter", 0.1)
		-- The coarsest density a lump is drawn at: 12 keeps them out of mip 3 (8 px/m), 8 lets the biggest in.
		local farthest = getFloat("lumpFar", 12)
		local gloss = hex("lumpGlossColor", "#FFFFFF")
		local glossShare = getFloat("lumpGlossShare", 0)
		local capScale = 0.58
		local bodyK = 1 / (1 + capScale * capScale * (capK - 1))
		local lumps = {}
		for _ = 1, count(density) do
			local x, y, size = rand() * P, rand() * P, lognormal(sLo, sHi, 0.45)
			if not inPuddle(x, y) then
				lumps[#lumps + 1] = { x, y, size = size }
			end
		end
		table.sort(lumps, function(a, b)
			return a[2] < b[2]
		end)
		for _, l in ipairs(lumps) do
			local r = l.size * 0.5
			local lod = max(lodFor(l.size * 2), farthest)
			local c = shade(onGround(pick(colors), l[1] % P, l[2] % P), bodyK * (1 + spread * (rand() * 2 - 1)))
			local s = blobShape(l[1], l[2], r, 8, 0.3, uniform(0.8, 1.3), rand() * TAU)
			emitShape(s, groundShade(l[1] % P, (l[2] + r * 0.3) % P, shadowK), lod, 0, r * 0.3, 1.04, l[1], l[2])
			emitShape(s, c, lod)
			emitShape(s, shade(c, capK), lod, 0, -r * 0.22, capScale, l[1], l[2])
			if rand() < glossShare then
				local g = blobShape(l[1] + (rand() - 0.5) * r * 0.4, l[2] - r * 0.4, r * 0.14, 6, 0.2, 1.8, 0)
				emitShape(g, gloss, lod) -- with its lump: the dabs are bright, dropping them would shift the hue
			end
		end
	end
end

-- Pieces ---------------------------------------------------------------------------------------

-- Everything lying loose, drawn back to front by its lowest point so a nearer piece covers the
-- shadow of the one behind it.
local pieces = {}
local function addPiece(y, draw)
	pieces[#pieces + 1] = { y = y, draw = draw }
end

-- A thin bent stroke from (x, y) along angle a: length L, width w, bowed by bow of its length.
local function strokeLine(x, y, a, L, bow, n)
	local px, py = {}, {}
	local dx, dy = cos(a), sin(a)
	for i = 0, n do
		local t = i / n
		local off = bow * L * (t * t - t) * 4
		px[i + 1], py[i + 1] = x + dx * L * t - dy * off, y + dy * L * t + dx * off
	end
	return px, py
end

-- Roots: wiggly dark threads half sunk in the ground.
do
	local density = getFloat("rootDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("rootLength", 0.06, 0.18)
		local wLo, wHi = getFloatRange("rootWidth", 0.006, 0.01)
		local color = hex("rootColor", "#000000")
		for _ = 1, count(density) do
			local x, y, a = rand() * P, rand() * P, rand() * TAU
			local len, w = lognormal(lenLo, lenHi, 0.4), uniform(wLo, wHi)
			local px, py = { x }, { y }
			for _ = 1, 8 do
				a = a + gauss() * 0.5
				x, y = x + cos(a) * len / 8, y + sin(a) * len / 8
				px[#px + 1], py[#py + 1] = x, y
			end
			addPiece(py[1], function()
				emitShape(stripShape(px, py, w, 0.4, 0), jitter(color, 0.08), lodFor(w))
			end)
		end
	end
end

-- Straw: dead grass bits, thin bent strokes in a few dry tones.
do
	local density = getFloat("strawDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("strawLength", 0.03, 0.09)
		local wLo, wHi = getFloatRange("strawWidth", 0.006, 0.009)
		local colors = hexList("strawColors", base)
		for _ = 1, count(density) do
			local x, y, a = rand() * P, rand() * P, rand() * TAU
			local L, w, bow = lognormal(lenLo, lenHi, 0.4), uniform(wLo, wHi), (rand() - 0.5) * 0.3
			local c = jitter(pick(colors), 0.06)
			addPiece(y + abs(sin(a)) * L, function()
				local px, py = strokeLine(x, y, a, L, bow, 4)
				emitShape(stripShape(px, py, w, 0.2, 0), c, lodFor(w))
			end)
		end
	end
end

-- Leaves: fallen leaves, pointed ovals with a darker midrib.
do
	local density = getFloat("leafDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("leafLength", 0.02, 0.04)
		local colors = hexList("leafColors", base)
		local shadowK = getFloat("leafShadow", 0.6)
		for _ = 1, count(density) do
			local x, y, a = rand() * P, rand() * P, rand() * TAU
			local L = lognormal(lenLo, lenHi, 0.3)
			local W = L * uniform(0.4, 0.55)
			local c = jitter(pick(colors), 0.08)
			addPiece(y + L * 0.5, function()
				local xs, ys = {}, {}
				local ca, sa = cos(a), sin(a)
				for i = 0, 6 do
					local t = i / 6
					local lx, ly = (t - 0.5) * L, -sin(math.pi * t ^ 0.8) * W * 0.5
					xs[#xs + 1], ys[#ys + 1] = x + lx * ca - ly * sa, y + lx * sa + ly * ca
				end
				for i = 5, 1, -1 do
					local t = i / 6
					local lx, ly = (t - 0.5) * L, sin(math.pi * t ^ 0.8) * W * 0.5
					xs[#xs + 1], ys[#ys + 1] = x + lx * ca - ly * sa, y + lx * sa + ly * ca
				end
				local s = shape(xs, ys)
				local lod = lodFor(W)
				emitShape(s, groundShade(x, y, shadowK), lod, 0, W * 0.25)
				emitShape(s, c, lod)
				local px, py = { x - ca * L * 0.45, x + ca * L * 0.4 }, { y - sa * L * 0.45, y + sa * L * 0.4 }
				emitShape(stripShape(px, py, W * 0.12, 0.1, 0), shade(c, 0.6), lodFor(W * 0.24))
			end)
		end
	end
end

-- Twigs: a bent stick with up to two side branches, over its shadow.
do
	local density = getFloat("twigDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("twigLength", 0.05, 0.14)
		local wLo, wHi = getFloatRange("twigWidth", 0.007, 0.012)
		local colors = hexList("twigColors", base)
		local shadowK = getFloat("twigShadow", 0.6)
		for _ = 1, count(density) do
			local x, y, a = rand() * P, rand() * P, rand() * TAU
			local L, w, bow = lognormal(lenLo, lenHi, 0.4), uniform(wLo, wHi), (rand() - 0.5) * 0.25
			local c = jitter(pick(colors), 0.08)
			local branches = {}
			for _ = 1, floor(rand() * 3) do
				local t = uniform(0.3, 0.7)
				branches[#branches + 1] = { t = t, a = a + (rand() < 0.5 and -1 or 1) * uniform(0.4, 0.8), L = L * uniform(0.25, 0.45) }
			end
			addPiece(y + abs(sin(a)) * L, function()
				local lod = lodFor(w)
				local shadow = groundShade(x % P, y % P, shadowK)
				local px, py = strokeLine(x, y, a, L, bow, 5)
				local strokes = { stripShape(px, py, w, 0.15, 0) }
				for _, b in ipairs(branches) do
					local i = floor(b.t * 5) + 1
					local bx, by = strokeLine(px[i], py[i], b.a, b.L, 0.1, 3)
					strokes[#strokes + 1] = stripShape(bx, by, w * 0.65, 0.3, 0)
				end
				for _, s in ipairs(strokes) do
					emitShape(s, shadow, lod, 0, w * 0.7)
				end
				for _, s in ipairs(strokes) do
					emitShape(s, c, lod)
				end
				-- A lit line along the top of the main stick.
				emitShape(stripShape(px, py, w * 0.35, 0.2, -1), shade(c, 1.45), lodFor(w * 0.7), 0, -w * 0.1)
			end)
		end
	end
end

-- Shells: little fans with a darker hinge and a couple of ridges.
do
	local density = getFloat("shellDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("shellSize", 0.01, 0.02)
		local colors = hexList("shellColors", base)
		local ridge = hex("shellRidgeColor", "#808080")
		for _ = 1, count(density) do
			local x, y = rand() * P, rand() * P
			local r = uniform(sLo, sHi) * 0.5
			local a = rand() * TAU
			local c = jitter(pick(colors), 0.04)
			addPiece(y + r, function()
				local xs, ys = { x }, { y }
				for i = 0, 8 do
					local t = a - 1.2 + 2.4 * i / 8
					local rr = r * (0.92 + 0.08 * cos(i * math.pi))
					xs[#xs + 1], ys[#ys + 1] = x + cos(t) * rr, y + sin(t) * rr
				end
				local lod = lodFor(r * 2)
				emitShape(shape(xs, ys), groundShade(x, y, 0.62), lod, 0, r * 0.25)
				emitShape(shape(xs, ys), c, lod)
				for k = -1, 1 do
					local t = a + k * 0.6
					emitShape(stripShape({ x, x + cos(t) * r * 0.85 }, { y, y + sin(t) * r * 0.85 }, r * 0.14, 0.2, 0), ridge, lodFor(r * 0.28))
				end
			end)
		end
	end
end

-- Stones: angular pebbles over a shadow falling below them, with a lit facet toward the top. Each sits
-- on a proxy, its shadow's outline in what the stone averages to there, kept down to mip 3: the shadow
-- covers it exactly wherever the stone is drawn, and where the stone has dropped out the proxy keeps
-- its share of the mean (stones are grey on colored ground, so they'd otherwise shift its hue).
do
	local density = getFloat("stoneDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("stoneSize", 0.01, 0.04)
		local colors = hexList("stoneColors", base)
		local shadowK = getFloat("stoneShadow", 0.5)
		local litK = getFloat("stoneLit", 1.35)
		for _ = 1, count(density) do
			local x, y = rand() * P, rand() * P
			local size = lognormal(sLo, sHi, 0.5)
			local r = size * 0.5
			local c = jitter(pick(colors), 0.05)
			local s = stoneShape(x, y, r, floor(uniform(5, 9)), uniform(0.7, 1), rand() * TAU)
			addPiece(y + r, function()
				local lod = lodFor(size)
				local shadow = groundShade(x, (y + r * 0.32) % P, shadowK)
				local lit = shade(c, litK)
				-- Over the shadow's outline: about a quarter shadow, the facet a third, the body the rest.
				local sl, cl, ll = toLinear(shadow), toLinear(c), toLinear(lit)
				local proxy = {}
				for ch = 1, 3 do
					proxy[ch] = 0.26 * sl[ch] + 0.34 * ll[ch] + 0.4 * cl[ch]
				end
				emitShape(s, toSrgb(proxy), kFar, r * 0.05, r * 0.32, 1.06, x, y)
				emitShape(s, shadow, lod, r * 0.05, r * 0.32, 1.06, x, y)
				emitShape(s, c, lod)
				emitShape(s, lit, lodFor(size * 0.55), 0, -r * 0.2, 0.62, x, y)
			end)
		end
	end
end

table.sort(pieces, function(a, b)
	return a.y < b.y
end)
for _, p in ipairs(pieces) do
	p.draw()
end

-- Grass tips --------------------------------------------------------------------------------------

-- A few blades poking up through the ground (snow): two to four short strokes fanning up the screen
-- from one root, over a soft shadow, back to front.
do
	local density = getFloat("tipDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("tipLength", 0.03, 0.07)
		local wLo, wHi = getFloatRange("tipWidth", 0.006, 0.009)
		local colors = hexList("tipColors", base)
		local shadowK = getFloat("tipShadow", 0.8)
		local tips = {}
		for _ = 1, count(density) do
			tips[#tips + 1] = { rand() * P, rand() * P }
		end
		table.sort(tips, function(a, b)
			return a[2] < b[2]
		end)
		for _, t in ipairs(tips) do
			local k = floor(uniform(2, 5))
			local w = uniform(wLo, wHi)
			local sh = blobShape(t[1], t[2] + w, w * 2.2, 8, 0.2, 1.8, 0)
			emitShape(sh, groundShade(t[1], t[2], shadowK), lodFor(w * 2))
			for i = 1, k do
				local rel = k > 1 and (2 * (i - 1) / (k - 1) - 1) or 0
				local a = -math.pi / 2 + rel * 0.45 + gauss() * 0.12
				local px, py = strokeLine(t[1] + rel * w, t[2], a, lognormal(lenLo, lenHi, 0.3), rel * 0.3, 4)
				emitShape(stripShape(px, py, w, 0.6, 0), jitter(pick(colors), 0.08), lodFor(w))
			end
		end
	end
end

-- Sparkle ----------------------------------------------------------------------------------------

-- Ice glints (snow): small four-point stars, only where mip 0 shows them.
do
	local density = getFloat("sparkleDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("sparkleSize", 0.012, 0.018)
		local colors = hexList("sparkleColors", { 1, 1, 1 })
		for _ = 1, count(density) do
			local x, y = rand() * P, rand() * P
			local r = uniform(sLo, sHi) * 0.5
			local a = rand() * 0.6
			local xs, ys = {}, {}
			for i = 0, 7 do
				local rr = i % 2 == 0 and r or r * 0.32
				local t = a + i * TAU / 8
				xs[#xs + 1], ys[#ys + 1] = x + cos(t) * rr, y + sin(t) * rr
			end
			put(xs, ys, 8, pick(colors), 64)
		end
	end
end
