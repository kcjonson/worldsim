-- Ground, bare: one seamless period x period meter tile variant (role ground) for the unvegetated
-- surfaces. Dirt, Sand, Mud, Rock and Snow share it and differ only in params. Seen like the flora,
-- from a hair off top-down and lit from the top of the screen: lit edges face up the screen and
-- shadows fall down it, which holds in every hex cell because the shader only mirrors these cells
-- left to right.
--
-- The ground itself (the base, blob tone patches, and regions cut along the contours of a spectral
-- field, with joints staining it) is laid out first and painted into a 6.25 cm tone grid, which every
-- ground-born layer above takes its color from (grain, crests, cracks, lumps, shadows), so those keep
-- the ground's contrast instead of pulling them toward the base, and which is scaled so each variant
-- averages to the base exactly. Paint order: the far ground, the near ground (base, patches,
-- regions), ripple fragments (a lit crest over a shadowed lee), wind streaks, cracks, joints, grain,
-- pits, lichen, puddles, lumps, then the loose pieces (roots, straw, leaves, twigs, shells, stones)
-- back to front, grass tips poking through, and sparkle. Every layer is off at density 0.
--
-- LOD: the far ground, the tone grid (puddles and lichen averaged in) as one rect a mip 4 texel, is
-- all mips 4 to 6 show; it's scaled to set their contrast and mean from the bake's report, as Grass's
-- far stack is. The near ground covers it from mip 3 up. Pieces keep to the mips where they're a
-- texel across (tag 1 / width; the ground's mass, lumps and crests, to half a texel) and never past
-- kFar. What drops out is balanced where it can be, so the mips that drop it keep mip 0's mean: each
-- light grain or streak has a dark twin, a crest's lit strip cancels its lee, a region's lit edge
-- its shaded one, a lump's cap its darkened body, and a stone sits on a proxy in its own average.
--
-- Meters, +Y down, sRGB colors 0..1. Densities are per square meter and counts are fixed per variant,
-- and every noise field has a whole number of cycles across the tile, so all variants share the same
-- statistics.

local P = period -- the def's <period>, meters
local AREA = P * P
local TAU = math.pi * 2

local rand, floor, sqrt, sin, cos, exp, min, max, abs =
	math.random, math.floor, math.sqrt, math.sin, math.cos, math.exp, math.min, math.max, math.abs

local clamp = lib.num.clamp
local uniform, gauss, lognormal = lib.random.uniform, lib.random.gauss, lib.random.lognormal
local parseHex, hex, toLinear, toSrgb, luminance, shade, mix =
	lib.color.parseHex, lib.color.hex, lib.color.toLinear, lib.color.toSrgb, lib.color.luminance, lib.color.shade, lib.color.mixLinear
local shape, stripSides, stripPart, stripShape = lib.shape.polygon, lib.shape.stripSides, lib.shape.stripPart, lib.shape.strip

local function count(perSquareMeter)
	return floor(perSquareMeter * AREA + 0.5)
end

-- Colors -------------------------------------------------------------------------------------

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
-- A comma-separated list of numbers.
local function numberList(key)
	local out = {}
	for v in getString(key, ""):gmatch("[^,%s]+") do
		out[#out + 1] = tonumber(v)
	end
	return out
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
local addVertex, setColor, setLod, setHeight, closePath =
	pathProto.addVertex, pathProto.setColor, pathProto.setLod, pathProto.setHeight, pathProto.close
local addPath = asset.addPath

-- Heights (path:setHeight, 0 to 1): what the bake puts in the ground arrays' alpha for the shader's
-- transition contest. Only their order within the surface counts, since the bake ranks them, and a
-- layer's paths share one height, or take one from their own size, never from where they sit: the depth
-- sorted layers would otherwise rise down the tile. The ground is at heightBase, and its small relief
-- (patches, crests, cracks, grain, pits) is heightRelief times a fixed bump, so 0 leaves it one flat
-- height; what lies on it (lumps, pieces, stones, grass tips) stands a fixed step above heightBase. No
-- height draws a random number, so the colors are the same as without them.
local H = { base = getFloat("heightBase", 0.3), relief = getFloat("heightRelief", 1), now = 0 }
H.puddleRim = getFloat("heightPuddleRim", H.base * 0.5)
local function at(h)
	H.now = clamp(h, 0, 1)
end
local function bump(d)
	return clamp(H.base + H.relief * d, 0, 1)
end
local function above(d)
	return clamp(H.base + d, 0, 1)
end
-- t in 0..1 within a size range: the bigger stand a little higher.
local function sized(size, lo, hi)
	return hi > lo and clamp((size - lo) / (hi - lo), 0, 1) - 0.5 or 0
end

local function put(xs, ys, n, c, lod)
	local p = Path()
	for i = 1, n do
		addVertex(p, xs[i], ys[i])
	end
	setColor(p, c[1], c[2], c[3], 1)
	if lod > 0 then
		setLod(p, lod)
	end
	setHeight(p, H.now)
	closePath(p)
	addPath(asset, p)
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

-- Spectral fields and contours -------------------------------------------------------------------

-- A periodic random field on a kFieldN x kFieldN grid of nodes over the tile, indexed [j * N + i + 1]:
-- a sum of waves with whole numbers of cycles across the tile, so it wraps exactly and its shapes run
-- whichever way the spectrum says (periodicNoise2 has one period for both axes, so it can't stretch).
-- Wave vectors come from an annulus of cyclesLo to cyclesHi a period, measured with the x component
-- times stretch, so stretch 2 makes features twice as long across the screen as down it; none is
-- under 3 cycles (1.33 m). Each wave is drawn with weight cycles^-slope and a random phase, every
-- variant from the same spectrum, so all share its statistics. Normalized to zero mean, unit RMS.
local kFieldN = 128
local fieldCos, fieldSin = {}, {}
for k = 0, kFieldN - 1 do
	fieldCos[k], fieldSin[k] = cos(TAU * k / kFieldN), sin(TAU * k / kFieldN)
end
local function spectralGrid(cyclesLo, cyclesHi, stretch, waves, slope)
	local n = kFieldN
	local cand, total = {}, 0
	local kmax = math.ceil(cyclesHi)
	for ky = 0, kmax do
		for kx = -kmax, kmax do
			if ky > 0 or kx > 0 then
				local r = sqrt((kx * stretch) ^ 2 + ky * ky)
				if r >= cyclesLo and r <= cyclesHi and kx * kx + ky * ky >= 9 then
					total = total + r ^ -slope
					cand[#cand + 1] = { kx, ky, total }
				end
			end
		end
	end
	local g = {}
	for k = 1, n * n do
		g[k] = 0
	end
	for _ = 1, waves do
		local pickAt, lo, hi = rand() * total, 1, #cand
		while lo < hi do
			local mid = floor((lo + hi) / 2)
			if cand[mid][3] < pickAt then
				lo = mid + 1
			else
				hi = mid
			end
		end
		local kx, ky = cand[lo][1] % n, cand[lo][2] % n
		local phase = rand() * TAU
		local cp, sp = cos(phase), sin(phase)
		-- The wave at every phase index, twice over, so a row's column index plus its offset needs no wrap.
		local wave, column = {}, {}
		for t = 0, n - 1 do
			wave[t] = fieldCos[t] * cp - fieldSin[t] * sp
			wave[t + n] = wave[t]
			column[t] = (kx * t) % n
		end
		for j = 0, n - 1 do
			local offset = (ky * j) % n
			local row = j * n + 1
			for i = 0, n - 1 do
				g[row + i] = g[row + i] + wave[column[i] + offset]
			end
		end
	end
	local sum, sum2 = 0, 0
	for k = 1, n * n do
		sum = sum + g[k]
	end
	local mean = sum / (n * n)
	for k = 1, n * n do
		g[k] = g[k] - mean
		sum2 = sum2 + g[k] * g[k]
	end
	local scale = 1 / sqrt(max(sum2 / (n * n), 1e-12))
	for k = 1, n * n do
		g[k] = g[k] * scale
	end
	return g
end

-- A grid's values in order, and the one below which share of them lie.
local function sortedValues(g)
	local sorted = {}
	for k = 1, #g do
		sorted[k] = g[k]
	end
	table.sort(sorted)
	return sorted
end
local function quantile(sorted, share)
	return sorted[clamp(floor(share * #sorted) + 1, 1, #sorted)]
end

-- Drops loop points within tol of the line through their kept neighbors.
local function simplifyLoop(xs, ys, tol)
	local n = #xs
	local ox, oy = { xs[1] }, { ys[1] }
	for i = 2, n do
		local ax, ay = ox[#ox], oy[#oy]
		local bx, by = xs[i % n + 1], ys[i % n + 1]
		local dx, dy = bx - ax, by - ay
		local len = sqrt(dx * dx + dy * dy)
		local d = len > 1e-9 and abs((xs[i] - ax) * dy - (ys[i] - ay) * dx) / len or sqrt((xs[i] - ax) ^ 2 + (ys[i] - ay) ^ 2)
		if d > tol then
			ox[#ox + 1], oy[#oy + 1] = xs[i], ys[i]
		end
	end
	return ox, oy
end

-- The loops bounding the part of the tile where sign * value > sign * t, traced by marching squares
-- on a field grid. Outside the tile counts as out, so a region reaching an edge closes along it; the
-- field wraps, so the next tile's copy carries the region on, and a copy moved across an edge comes
-- back on the far side the way the field does. Each loop is { xs, ys }; the region is their even-odd
-- union (regionShape).
local function contourLoops(g, t, sign)
	local n = kFieldN
	local h = P / n
	local function inside(i, j)
		if i < 0 or j < 0 or i > n or j > n then
			return false
		end
		return sign * g[(j % n) * n + i % n + 1] > sign * t
	end
	local function value(i, j)
		return g[(j % n) * n + i % n + 1]
	end
	-- An edge's key: its first node and whether it runs along x (0) or y (1).
	local stride = n + 4
	local function key(i, j, axis)
		return ((j + 2) * stride + (i + 2)) * 2 + axis
	end
	local vertex = {}
	local function edgeVertex(i, j, axis)
		local k = key(i, j, axis)
		if not vertex[k] then
			local i2, j2 = i + (axis == 0 and 1 or 0), j + (axis == 1 and 1 or 0)
			local real1 = i >= 0 and j >= 0 and i <= n and j <= n
			local real2 = i2 >= 0 and j2 >= 0 and i2 <= n and j2 <= n
			local s
			if real1 and real2 then
				local v1, v2 = value(i, j), value(i2, j2)
				s = clamp((t - v1) / (v2 - v1), 0, 1)
			else
				s = real1 and 0 or 1 -- outside the tile: the crossing is at the real node
			end
			vertex[k] = { (i + (i2 - i) * s) * h, (j + (j2 - j) * s) * h }
		end
		return k
	end
	local links = {}
	local function link(a, b)
		links[a] = links[a] or {}
		links[b] = links[b] or {}
		local la, lb = links[a], links[b]
		la[#la + 1], lb[#lb + 1] = b, a
	end
	for j = -1, n do
		for i = -1, n do
			local a, b, c, d = inside(i, j), inside(i + 1, j), inside(i + 1, j + 1), inside(i, j + 1)
			local case = (a and 1 or 0) + (b and 2 or 0) + (c and 4 or 0) + (d and 8 or 0)
			if case ~= 0 and case ~= 15 then
				local top, bottom = edgeVertex(i, j, 0), edgeVertex(i, j + 1, 0)
				local left, right = edgeVertex(i, j, 1), edgeVertex(i + 1, j, 1)
				if case == 5 or case == 10 then
					local centerIn = sign * (value(i, j) + value(i + 1, j) + value(i + 1, j + 1) + value(i, j + 1)) * 0.25 > sign * t
					if (case == 5) == centerIn then
						link(top, right) -- the outside corners b and d cut off
						link(bottom, left)
					else
						link(left, top) -- the inside corners a and c cut off
						link(right, bottom)
					end
				elseif case == 1 or case == 14 then
					link(left, top)
				elseif case == 2 or case == 13 then
					link(top, right)
				elseif case == 3 or case == 12 then
					link(left, right)
				elseif case == 4 or case == 11 then
					link(right, bottom)
				elseif case == 6 or case == 9 then
					link(top, bottom)
				else -- 7 or 8
					link(left, bottom)
				end
			end
		end
	end
	local loops, seen = {}, {}
	local starts = {}
	for k in pairs(links) do
		starts[#starts + 1] = k
	end
	table.sort(starts) -- pairs() order isn't fixed; the loops' order must be
	for _, k0 in ipairs(starts) do
		if not seen[k0] then
			local xs, ys = {}, {}
			local prev, cur = nil, k0
			repeat
				seen[cur] = true
				local v = vertex[cur]
				if #xs == 0 or abs(v[1] - xs[#xs]) + abs(v[2] - ys[#ys]) > 1e-7 then
					xs[#xs + 1], ys[#ys + 1] = v[1], v[2]
				end
				local l = links[cur]
				local nxt = l[1] ~= prev and l[1] or l[2]
				if l[1] == l[2] then
					nxt = l[1]
				end
				prev, cur = cur, nxt
			until cur == k0 or cur == nil
			xs, ys = simplifyLoop(xs, ys, 0.002)
			if #xs >= 3 then
				loops[#loops + 1] = { xs = xs, ys = ys }
			end
		end
	end
	return loops
end

-- One polygon holding every loop, filled even-odd: each loop after the first is reached from the
-- first one's start and left the same way, so the bridges there and back cancel.
local function regionShape(loops)
	local xs, ys = {}, {}
	local ax, ay = loops[1].xs[1], loops[1].ys[1]
	for k, loop in ipairs(loops) do
		if k > 1 then
			xs[#xs + 1], ys[#ys + 1] = ax, ay
		end
		for q = 1, #loop.xs do
			xs[#xs + 1], ys[#ys + 1] = loop.xs[q], loop.ys[q]
		end
		if k > 1 then
			xs[#xs + 1], ys[#ys + 1] = loop.xs[1], loop.ys[1]
		end
	end
	return shape(xs, ys)
end

-- Base and patches -----------------------------------------------------------------------------

-- Tone patches: blue-noise blobs stretched by stretch along x, half in the light tone and half in the
-- dark, each a few nested steps from the base's tone to the patch's, so they read as flat cel steps.
-- Inner steps sit a little up the screen: a mound is lit on its upper side, a hollow shaded there.
-- Their sizes are normalized per variant, so every variant covers about the same area with each tone.
-- A light patch is a mound and a dark one a hollow, each step a little higher or lower.
local steps = {} -- every step polygon in paint order: { shape, linear color, height }
local patchRelief = { patch = 0.06, tint = 0.03 }
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
			local rise = (p.tone == 1 and 1 or -1) * patchRelief[prefix] * s / stepCount
			steps[#steps + 1] = { shape(xs, ys), toLinear(mix(base, tones[p.tone], s / stepCount)), bump(rise) }
		end
	end
end
layoutPatches("patch")
layoutPatches("tint")

-- Regions: the ground split along the level sets of a spectral field into nested cel steps, shapes
-- that are a field's contours and so never read as blobs, cells or a repeat. <prefix>Levels lists
-- area shares in paint order: a positive share is the highest that much of the tile (a rise), a
-- negative one the lowest (a hollow), so every variant gives each level the same area. Level k takes
-- the k-th of <prefix>Colors and its height bump from <prefix>Relief. With <prefix>Bevel a rise is
-- also lit along its upper edge and shaded along its lower one, and a hollow the other way: copies
-- of the level moved that far up and down under it, the lit one <prefix>Lit times its color and the
-- shaded one what cancels it, or the shaded one <prefix>Shade when given (snow's lee is blue, not
-- grey) and the lit one what cancels that.
-- The field is <prefix>Waves waves of <prefix>Cycles a period, stretched <prefix>Stretch along x,
-- falling off as cycles^-<prefix>Slope. The levels are ground: the tone grid and the far mips see them.
local function moved(s, dx, dy)
	local xs, ys = {}, {}
	for i = 1, s.n do
		xs[i], ys[i] = s.xs[i] + dx, s.ys[i] + dy
	end
	return shape(xs, ys)
end
local function layoutRegions(prefix)
	local waves = getInt(prefix .. "Waves", 0)
	if waves <= 0 then
		return
	end
	local lo, hi = getFloatRange(prefix .. "Cycles", 3, 10)
	local g = spectralGrid(lo, hi, getFloat(prefix .. "Stretch", 1), waves, getFloat(prefix .. "Slope", 1))
	local levels = numberList(prefix .. "Levels")
	local colors = hexList(prefix .. "Colors", base)
	local relief = numberList(prefix .. "Relief")
	local bevel = getFloat(prefix .. "Bevel", 0)
	local sorted = sortedValues(g)
	local litK = getFloat(prefix .. "Lit", 1.15)
	local shade = getString(prefix .. "Shade", "")
	for k, share in ipairs(levels) do
		local sign = share > 0 and 1 or -1
		local loops = contourLoops(g, quantile(sorted, share > 0 and 1 - share or -share), sign)
		if #loops > 0 then
			local s = regionShape(loops)
			local c = toLinear(colors[(k - 1) % #colors + 1])
			local rise = relief[k] or 0
			if bevel > 0 then
				-- The lit and shaded edges are as wide and cancel in linear light, so however far a level's
				-- outline runs they leave the mean be, and they're too thin for the tone grid to see.
				local lit, dark = { c[1] * litK, c[2] * litK, c[3] * litK }, nil
				if shade ~= "" then
					dark = toLinear(parseHex(shade))
					lit = { 2 * c[1] - dark[1], 2 * c[2] - dark[2], 2 * c[3] - dark[3] }
				else
					dark = { 2 * c[1] - lit[1], 2 * c[2] - lit[2], 2 * c[3] - lit[3] }
				end
				local edge = bump(rise * 0.5)
				steps[#steps + 1] = { moved(s, 0, -bevel), sign > 0 and lit or dark, edge, thin = true }
				steps[#steps + 1] = { moved(s, 0, bevel), sign > 0 and dark or lit, edge, thin = true }
			end
			steps[#steps + 1] = { s, c, bump(rise) }
		end
	end
end
layoutRegions("tone")
layoutRegions("ledge")

-- Joints: long fractures that wander across the rock, a few to a tile, with short side fractures
-- branching off them. Laid out here as polylines, so lichen can favor them, and drawn after the
-- cracks: a band of weathering stain, the lit lip of the crack's lower wall (it faces up the screen,
-- into the light) and the dark crack. "fracture" is a second, finer set. Every walk is jointWidth
-- wide at its middle, tapering to its ends.
local joints = {}
local function layoutJoints(prefix)
	local density = getFloat(prefix .. "Density", 0)
	if density <= 0 then
		return
	end
	local lenLo, lenHi = getFloatRange(prefix .. "Length", 0.8, 2)
	local wLo, wHi = getFloatRange(prefix .. "Width", 0.012, 0.025)
	local turn = getFloat(prefix .. "Turn", 0.1)
	local kink = getFloat(prefix .. "Kink", 0.03)
	local branch = getFloat(prefix .. "Branch", 0.05)
	local stain = getFloat(prefix .. "Stain", 0)
	local stainK = getFloat(prefix .. "StainDark", 0.9)
	local dark, lip = hex(prefix .. "Color", "#000000"), hex(prefix .. "LipColor", "#FFFFFF")
	local stepLen = 0.03
	local function walk(x, y, a, len, w, depth)
		local px, py = { x }, { y }
		local n = max(3, floor(len / stepLen))
		for k = 1, n do
			a = a + gauss() * turn
			if rand() < kink then
				a = a + (rand() < 0.5 and -1 or 1) * uniform(0.3, 0.7)
			end
			x, y = x + cos(a) * stepLen, y + sin(a) * stepLen
			px[#px + 1], py[#py + 1] = x, y
			if depth < 2 and k > 2 and k < n - 2 and rand() < branch then
				walk(x, y, a + (rand() < 0.5 and -1 or 1) * uniform(0.5, 1.2), len * uniform(0.15, 0.4), w * 0.55, depth + 1)
			end
		end
		local band = depth == 0 and stain or stain * 0.5
		joints[#joints + 1] = { px = px, py = py, w = w, stain = band, stainK = stainK, dark = dark, lip = lip }
	end
	-- Lengths are normalized per variant, so every variant has as much joint.
	local lens, sum = {}, 0
	for i = 1, count(density) do
		lens[i] = lognormal(lenLo, lenHi, 0.35)
		sum = sum + lens[i]
	end
	local scale = #lens * sqrt(lenLo * lenHi) / max(sum, 1e-9)
	for i = 1, #lens do
		walk(rand() * P, rand() * P, rand() * TAU, lens[i] * scale, uniform(wLo, wHi), 0)
	end
end
layoutJoints("joint")
layoutJoints("fracture")

-- Puddles: small standing water in a hollow, holding the sky: flat, with none of the top-lit shading
-- that would read as a bump. The water is puddleColors' first, set a little down the screen inside
-- its outline, which leaves a thin light lip in puddleLipColor along the far edge (the top of the
-- screen); later colors are smaller steps set further down, the murkier near side. Two streaks of
-- puddleSheenColor lie across it, and around it the mud is wet, darker and glossy: rings in
-- puddleWetColors (designed against the base, carried onto the local tone) reaching puddleWetReach of
-- the puddle's size beyond it, with three dabs of the sheen along the far bank. Laid out here, so the
-- far ground can average them in, and drawn in paint order further down. Their area (size and
-- aspect) is normalized per variant like the patches.
local puddles = {}
do
	local density = getFloat("puddleDensity", 0)
	if density > 0 then
		local rLo, rHi = getFloatRange("puddleSize", 0.04, 0.12)
		local aLo, aHi = getFloatRange("puddleAspect", 1.2, 1.7)
		local water = hexList("puddleColors", { 0.25, 0.25, 0.25 })
		local wet = hexList("puddleWetColors", base)
		local lip, sheen = hex("puddleLipColor", "#C0C0C0"), hex("puddleSheenColor", "#808080")
		local wetReach = getFloat("puddleWetReach", 0.7)
		-- Water gathers in the low ground: each puddle takes the lowest of puddleGather random spots on a
		-- field of 3 cycles a period, kept a little apart, so they come in loose groups, not evenly.
		local gather = max(1, getInt("puddleGather", 1))
		local pts = {}
		for _ = 1, count(density) do
			local best, bestV = nil, math.huge
			for _ = 1, gather do
				local x, y = rand() * P, rand() * P
				local v = field(x, y, 3, 307)
				for _, q in ipairs(pts) do
					local dx, dy = wrapDelta(q[1] - x), wrapDelta(q[2] - y)
					if dx * dx + dy * dy < (2.5 * rHi) ^ 2 then
						v = v + 1
					end
				end
				if v < bestV then
					best, bestV = { x, y }, v
				end
			end
			pts[#pts + 1] = best
		end
		local sum = 0
		for _, p in ipairs(pts) do
			p.r, p.aspect = lognormal(rLo, rHi, 0.5), uniform(aLo, aHi)
			sum = sum + p.r * p.r * p.aspect
		end
		local scale = sqrt(#pts * rLo * rHi * exp(2 * 0.5 * 0.5) * (aLo + aHi) * 0.5 / max(sum, 1e-9))
		for _, p in ipairs(pts) do
			local r, aspect = p.r * scale, p.aspect
			local x, y = p[1], p[2]
			local outline = blobShape(x, y, r, 16, 0.5, aspect, (rand() - 0.5) * 0.3)
			local function step(k, dy)
				local xs, ys = {}, {}
				for i = 1, outline.n do
					xs[i], ys[i] = x + (outline.xs[i] - x) * k, y + (outline.ys[i] - y) * k + dy
				end
				return shape(xs, ys)
			end
			local layers = {}
			for k, c in ipairs(wet) do
				layers[#layers + 1] = { step(1 + wetReach * (#wet - k + 1) / #wet, r * 0.05), c, onBase = true }
			end
			-- Gloss on the wet mud along the far bank: three small dabs of the sheen.
			for _ = 1, 3 do
				local a = -math.pi * uniform(0.15, 0.85)
				local d = uniform(1.15, 1 + wetReach * 0.8)
				local gx, gy = x + cos(a) * r * aspect * d, y + sin(a) * r * d
				layers[#layers + 1] = { blobShape(gx, gy, r * uniform(0.08, 0.14), 6, 0.3, uniform(1.4, 2.2), 0), sheen }
			end
			layers[#layers + 1] = { outline, lip }
			for k, c in ipairs(water) do
				layers[#layers + 1] = { step(0.97 - 0.22 * (k - 1), r * (0.08 + 0.06 * (k - 1))), c }
			end
			-- Sky streaks: thin and level, across the sky.
			for _ = 1, 2 do
				local sx, sy = x + (rand() - 0.5) * r * 0.7, y + uniform(-0.1, 0.25) * r
				local len, hw = r * uniform(0.35, 0.7), r * uniform(0.035, 0.06)
				layers[#layers + 1] = { shape({ sx - len, sx, sx + len, sx }, { sy, sy - hw, sy, sy + hw }), sheen }
			end
			puddles[#puddles + 1] = { x = x, y = y, r = r, layers = layers }
		end
	end
end

-- Lichen: sparse colonies of crustose lichen, each a loose cluster of small rosettes, so a colony's
-- outline is ragged rather than round. A colony takes one of lichenColors, sits within a few
-- centimeters of a joint lichenNearJoint of the time (water runs there), and scatters rosettes about
-- its center, larger toward it, each a lighter growing rim around its body and, when big enough, a
-- dark fruiting dot or two. Laid out here, so the far ground can average them in, and drawn after the
-- grain.
local lichens = {}
do
	local density = getFloat("lichenDensity", 0)
	if density > 0 then
		local rLo, rHi = getFloatRange("lichenSize", 0.05, 0.15)
		local dLo, dHi = getFloatRange("lichenDot", 0.006, 0.025)
		local colors = hexList("lichenColors", base)
		local nearJoint = getFloat("lichenNearJoint", 0)
		local perArea = getFloat("lichenRosettes", 900)
		local fruit = hex("lichenFruitColor", "#3A3631")
		-- Radii are normalized per variant, so every variant has as much lichen.
		local radii, sum = {}, 0
		for i = 1, count(density) do
			radii[i] = lognormal(rLo, rHi, 0.4)
			sum = sum + radii[i] * radii[i]
		end
		local scale = sqrt(#radii * rLo * rHi / max(sum, 1e-9))
		for i = 1, #radii do
			local cx, cy = rand() * P, rand() * P
			local along = rand() * TAU
			if #joints > 0 and rand() < nearJoint then
				local j = joints[floor(rand() * #joints) + 1]
				local q = floor(rand() * #j.px) + 1
				cx, cy = j.px[q] + gauss() * 0.04, j.py[q] + gauss() * 0.04
				local q2 = min(#j.px, q + 1)
				along = math.atan(j.py[q2] - j.py[max(1, q2 - 1)], j.px[q2] - j.px[max(1, q2 - 1)])
			end
			local c = colors[(i - 1) % #colors + 1]
			local R = radii[i] * scale
			local stretch = uniform(1, 1.8)
			local ca, sa = cos(along), sin(along)
			-- Positions and sizes first, the sizes normalized so every colony of a radius covers as much.
			local spots, sum2 = {}, 0
			for k = 1, max(3, floor(R * R * perArea)) do
				local u, v = gauss() * 0.5 * R * stretch, gauss() * 0.5 * R / stretch
				local d = sqrt((u / stretch) ^ 2 + (v * stretch) ^ 2) / R
				spots[k] = { u, v, lognormal(dLo, dHi, 0.45) * clamp(1.1 - 0.6 * d, 0.35, 1) }
				sum2 = sum2 + spots[k][3] ^ 2
			end
			local sizeScale = sqrt(#spots * dLo * dHi * 0.55 / max(sum2, 1e-12))
			local rosettes = {}
			for _, spot in ipairs(spots) do
				local u, v, size = spot[1], spot[2], spot[3] * sizeScale
				local x, y = cx + u * ca - v * sa, cy + u * sa + v * ca
				local body = jitter(c, 0.07)
				local r = {
					rim = blobShape(x, y, size * 0.5, 9, 0.45, uniform(0.8, 1.25), rand() * TAU),
					rimColor = shade(body, 1.18),
					color = body,
					dots = {},
				}
				r.body = blobShape(x + (rand() - 0.5) * size * 0.1, y + size * 0.04, size * 0.34, 8, 0.35, 1, rand() * TAU)
				if size > 0.012 then
					for _ = 1, floor(uniform(0, 3)) do
						local a, dd = rand() * TAU, size * 0.2 * sqrt(rand())
						r.dots[#r.dots + 1] = blobShape(x + cos(a) * dd, y + sin(a) * dd, size * 0.07, 5, 0.2, 1, 0)
					end
				end
				rosettes[#rosettes + 1] = r
			end
			lichens[#lichens + 1] = { rosettes = rosettes, fruit = fruit, lod = lodFor(2 * R) }
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
-- Calls visit with the wrapped index of every grid cell whose center is inside shape s (even-odd),
-- a row at a time: the row's crossings once, then the cells between them.
local function gridCells(s, visit)
	local xs, ys, n = s.xs, s.ys, s.n
	local x0, x1, y0, y1 = math.huge, -math.huge, math.huge, -math.huge
	for i = 1, n do
		x0, x1 = min(x0, xs[i]), max(x1, xs[i])
		y0, y1 = min(y0, ys[i]), max(y1, ys[i])
	end
	for j = floor(y0 / gridCell), floor(y1 / gridCell) do
		local cy = (j + 0.5) * gridCell
		local cuts = {}
		local jx, jy = xs[n], ys[n]
		for k = 1, n do
			local ix, iy = xs[k], ys[k]
			if (iy > cy) ~= (jy > cy) then
				cuts[#cuts + 1] = (jx - ix) * (cy - iy) / (jy - iy) + ix
			end
			jx, jy = ix, iy
		end
		if #cuts > 0 then
			table.sort(cuts)
			local q = 1
			for i = floor(x0 / gridCell), floor(x1 / gridCell) do
				local cx = (i + 0.5) * gridCell
				while q <= #cuts and cuts[q] <= cx do
					q = q + 1
				end
				if (#cuts - q) % 2 == 0 then
					visit((j % kGridN) * kGridN + i % kGridN + 1)
				end
			end
		end
	end
end
-- Paints shape s into a grid of linear colors (cell centers inside it take color c), wrapped.
local function fillGrid(target, s, c)
	gridCells(s, function(k)
		target[k] = c
	end)
end
-- Scales the light of the grid cells inside shape s by k.
local function shadeGrid(target, s, k)
	gridCells(s, function(i)
		local g = target[i]
		target[i] = { g[1] * k, g[2] * k, g[3] * k }
	end)
end
-- Whether (x, y) lies in a puddle's rim, across the wrap: lumps and grain keep out of the water.
local function inPuddle(x, y)
	for _, p in ipairs(puddles) do
		local dx, dy = wrapDelta(x - p.x), wrapDelta(y - p.y)
		if abs(dx) < p.r * 4 and abs(dy) < p.r * 4 and insideShape(p.layers[1][1], p.x + dx, p.y + dy) then
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
		if not st.thin then
			fillGrid(grid, st[1], st[2])
		end
	end
	-- Joints stain the ground about them, so the far mips keep them as faint dark lines; before the
	-- scaling, so it holds each variant's mean too.
	for _, jt in ipairs(joints) do
		if jt.stain > 0 then
			shadeGrid(grid, stripShape(jt.px, jt.py, jt.w + 2 * jt.stain, 0.3, 0), jt.stainK)
		end
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
		for _, r in ipairs(l.rosettes) do
			fillGrid(farGrid, r.rim, toLinear(mix(r.rimColor, r.color, 0.45)))
		end
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
	at(H.base) -- under the near ground at mip 0, where heights are drawn
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
	at(H.base)
	put({ 0, P, P, 0 }, { 0, 0, P, P }, 4, toSrgb(ground), kFar)
	for _, st in ipairs(steps) do
		at(st[3])
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

-- Ripples ------------------------------------------------------------------------------------------

-- Wind ripples as short fragments: crests in rows wavelength apart (rounded so a whole number fits),
-- every row bending by the same two waves (so neighbors run parallel, a field of ripples) with wander
-- of the bend noise of its own, broken into fragments wherever a periodic field of breakCycles a period
-- falls under coverage, which patchiness moves with a field of 3 cycles a period, so the ripples come
-- in fields with smooth sand between. Fragments run a few to a few tens of centimeters, so no crest is
-- long enough for a hex cell's edge to cut it as a line: the edge reads as one more break. Each
-- fragment sits a little off its row, bows down the screen toward its ends and tapers to both, and is
-- a lit strip above the crest over a shadowed lee below, as wide as cancels it in linear light, each
-- over a half step soft times as wide. The crest stands a little above the ground and the lee a little
-- below it, less than the ground's own rises and hollows, so a transition follows those and the rows
-- only break its edge up.
local function ripples(prefix)
	local wavelength = getFloat(prefix .. "Wavelength", 0)
	if wavelength <= 0 then
		return
	end
	local rows = max(1, floor(P / wavelength + 0.5))
	wavelength = P / rows
	local wLo, wHi = getFloatRange(prefix .. "Width", 0.015, 0.03)
	local coverage = getFloat(prefix .. "Coverage", 0.5)
	local amp = getFloat(prefix .. "Amp", 0.03)
	local waveCycles = getFloat(prefix .. "WaveCycles", 3)
	local breakCycles = getFloat(prefix .. "BreakCycles", 20)
	local wander = clamp(getFloat(prefix .. "Wander", 0.3), 0, 1)
	local bow = getFloat(prefix .. "Bow", 0.4)
	local patchiness = getFloat(prefix .. "Patchiness", 0)
	local soft = max(1, getFloat(prefix .. "Soft", 1))
	local lit, shadow = hex(prefix .. "Lit", "#FFFFFF"), hex(prefix .. "Shadow", "#000000")
	local step = min(0.02, wavelength * 0.25)
	local nx = floor(P / step + 0.5)
	step = P / nx
	-- The break field's threshold for the coverage asked: noise is about -0.7..0.7, so map linearly.
	local threshold = 0.7 - 1.4 * clamp(coverage, 0, 1)
	local n1 = max(1, floor(waveCycles + 0.5))
	local n2 = max(1, floor(waveCycles * 1.7 + 0.5))
	local ph1, ph2 = rand() * TAU, rand() * TAU
	for j = 0, rows - 1 do
		local y0 = (j + 0.5 + (rand() - 0.5) * 0.2) * wavelength
		local salt = 101 + j * 7
		local function crestY(x)
			local waves = 0.65 * sin(TAU * (n1 * x + y0) / P + ph1) + 0.35 * sin(TAU * (n2 * x - 2 * y0) / P + ph2)
			local noise = 1.4 * field(x, y0, n1 * 2, 71) + 0.6 * field(x, y0, n2 * 2, 73)
			return y0 + amp * ((1 - wander) * waves + wander * noise)
		end
		-- Fragments along the row, wrapping: each starts where the break field rises through the
		-- threshold; a row it never breaks is one run around the tile with no ends to taper.
		local on = {}
		local start = nil
		for i = 0, nx - 1 do
			local x = i * step
			on[i] = field(x, y0, breakCycles, salt) > threshold - patchiness * field(x, y0, 3, 89)
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
					runs[#runs + 1] = { first, i - first, 0.45 }
				else
					i, visited = i + 1, visited + 1
				end
			end
		end
		for _, run in ipairs(runs) do
			local w = uniform(wLo, wHi)
			local dy = (rand() - 0.5) * 0.3 * wavelength
			local sag = run[3] > 0 and bow * w * uniform(0.5, 1.5) or 0
			local px, py = {}, {}
			for i = run[1], run[1] + run[2] do
				local x = i * step
				local t = (i - run[1]) / max(1, run[2])
				px[#px + 1], py[#py + 1] = x, crestY(x % P) + dy + sag * (2 * t - 1) ^ 2
			end
			if #px >= 3 then
				-- Emitted a few points at a time, each part in the ground's tone where it lies.
				local lw = balancedWidth(w, lit, shadow)
				local lod = lodFor(2 * min(w, lw))
				-- The half step as a pair, a lit strip over a shadowed one, is the ripple a coarser mip shows.
				local softLod = lodFor(soft * (w + lw))
				local litL, litR = stripSides(px, py, lw * soft, run[3], -1)
				local shL, shR = stripSides(px, py, w * soft, run[3], 1)
				local litInL, litInR = stripSides(px, py, lw, run[3], -1)
				local shInL, shInR = stripSides(px, py, w, run[3], 1)
				local i0 = 1
				while i0 < #px do
					local i1 = min(#px, i0 + 5)
					local m = floor((i0 + i1) / 2)
					local mx, my = px[m] % P, py[m] % P
					local g = toSrgb(groundAt(mx, my))
					local litC, shC = onGround(lit, mx, my), onGround(shadow, mx, my)
					if soft > 1 then
						at(bump(0.01))
						emitShape(stripPart(litL, litR, i0, i1), mix(g, litC, 0.5), softLod)
						at(bump(-0.01))
						emitShape(stripPart(shL, shR, i0, i1), mix(g, shC, 0.5), softLod)
					end
					at(bump(0.02))
					emitShape(stripPart(litInL, litInR, i0, i1), litC, lod)
					at(bump(-0.02))
					emitShape(stripPart(shInL, shInR, i0, i1), shC, lod)
					i0 = i1
				end
			end
		end
	end
end
ripples("ripple")

-- Streaks: wind-combed grain, thin lenses lying along the wind (across the screen, within streakAngle),
-- in pairs of a light one and a dark twin of the same size, streakContrast either way in linear light,
-- so they cancel where they drop out. Each is placed on its own, so nothing lines up across a cell
-- edge. A light streak is a little high, its dark twin a little low.
do
	local density = getFloat("streakDensity", 0)
	if density > 0 then
		local lenLo, lenHi = getFloatRange("streakLength", 0.04, 0.12)
		local wLo, wHi = getFloatRange("streakWidth", 0.005, 0.01)
		local contrast = getFloat("streakContrast", 0.08)
		local angle = getFloat("streakAngle", 0.15)
		for _ = 1, floor(count(density) / 2) do
			local L, w = lognormal(lenLo, lenHi, 0.4), uniform(wLo, wHi)
			local lod = lodFor(w)
			for k = 1, 2 do
				local x, y, a = rand() * P, rand() * P, (rand() * 2 - 1) * angle
				local ca, sa = cos(a), sin(a)
				local xs, ys = {}, {}
				for i = 0, 7 do
					local t = TAU * i / 8
					local lx, ly = cos(t) * L * 0.5, sin(t) * w * 0.5
					xs[i + 1], ys[i + 1] = x + lx * ca - ly * sa, y + lx * sa + ly * ca
				end
				at(bump(k == 1 and 0.02 or -0.02))
				emitShape(shape(xs, ys), groundShade(x, y, k == 1 and 1 + contrast or 1 - contrast), lod)
			end
		end
	end
end

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
			at(bump(0.02))
			emitShape(stripShape(px, py, w + lw, 0.25, 0), onGround(lip, mx, my), lod, 0, -lw * 0.6)
			at(bump(-0.2))
			emitShape(stripShape(px, py, w, 0.25, 0), onGround(dark, mx, my), lod)
		end
		for _ = 1, count(density) do
			walk(rand() * P, rand() * P, rand() * TAU, lognormal(lenLo, lenHi, 0.4), uniform(wLo, wHi), 0)
		end
	end
end

-- Joints ---------------------------------------------------------------------------------------

-- The walks laid out above, a few points at a time in the ground's tone where they lie: the stain
-- band (already in the tone grid, so drawn in its color down to the far ground), the lit lip below
-- the crack, as wide as balances it, and the crack, the lowest thing on the surface.
for _, jt in ipairs(joints) do
	local px, py, w = jt.px, jt.py, jt.w
	local lw = balancedWidth(w, jt.lip, jt.dark)
	local lod = lodFor(w)
	local stL, stR
	if jt.stain > 0 then
		stL, stR = stripSides(px, py, w + 2 * jt.stain, 0.3, 0)
	end
	local lipL, lipR = stripSides(px, py, w + lw, 0.25, 0)
	local crL, crR = stripSides(px, py, w, 0.25, 0)
	local i0 = 1
	while i0 < #px do
		local i1 = min(#px, i0 + 5)
		local m = floor((i0 + i1) / 2)
		local mx, my = px[m] % P, py[m] % P
		if stL then
			at(bump(-0.08))
			emitShape(stripPart(stL, stR, i0, i1), toSrgb(groundAt(mx, my)), kFar)
		end
		at(bump(0.01))
		emitShape(stripPart(lipL, lipR, i0, i1), onGround(jt.lip, mx, my), lod, 0, lw * 0.6)
		at(bump(-0.45))
		emitShape(stripPart(crL, crR, i0, i1), onGround(jt.dark, mx, my), lod)
		i0 = i1
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
					at(bump(0.03))
				else
					c = groundShade(x, y, (k == 1 and 1 + contrast or 1 - contrast) * (1 + 0.05 * (rand() * 2 - 1)))
					at(bump(k == 1 and 0.06 or -0.06)) -- a light grain is a top, its dark twin a hollow
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
			at(bump(-0.12))
			emitShape(s, groundShade(x, y, litK), lod)
			at(bump(-0.18))
			emitShape(s, groundShade(x, y, darkK), lod, 0, -r * 0.3, 0.85, x, y)
		end
	end
end

-- Lichen: the colonies laid out above, each rosette a rim, its body and its fruiting dots, all kept
-- down to the mip where the colony is a texel across, so a colony fades as a tint rather than its
-- rosettes dropping out one by one.
for _, l in ipairs(lichens) do
	for _, r in ipairs(l.rosettes) do
		at(bump(0.03))
		emitShape(r.rim, r.rimColor, l.lod)
		at(bump(0.04))
		emitShape(r.body, r.color, l.lod)
		for _, d in ipairs(r.dots) do
			emitShape(d, l.fruit, l.lod)
		end
	end
end

-- Puddles --------------------------------------------------------------------------------------

-- The puddles laid out above, down to the mip where one is a texel across (the far ground has them
-- averaged in below that).
do
	local glint = hex("puddleGlintColor", "#FFFFFF")
	for _, p in ipairs(puddles) do
		local lod = lodFor(p.r * 2)
		-- The rim and bank sit low, the water lowest of everything.
		for _, layer in ipairs(p.layers) do
			at(layer.onBase and H.puddleRim or 0)
			emitShape(layer[1], layer.onBase and onGround(layer[2], p.x, p.y) or layer[2], lod)
		end
		local g = blobShape(p.x + (rand() - 0.5) * p.r, p.y - p.r * 0.12, p.r * 0.1, 6, 0.2, 2.2, 0)
		at(0)
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
			local body = above(0.2 + 0.1 * sized(l.size, sLo, sHi))
			at(bump(0))
			emitShape(s, groundShade(l[1] % P, (l[2] + r * 0.3) % P, shadowK), lod, 0, r * 0.3, 1.04, l[1], l[2])
			at(body)
			emitShape(s, c, lod)
			at(body + 0.12) -- the lit cap is the clod's top, and its gloss dab with it
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
				at(bump(0.02)) -- half sunk
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
				at(above(0.25))
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
				at(bump(0))
				emitShape(s, groundShade(x, y, shadowK), lod, 0, W * 0.25)
				at(above(0.25))
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
				at(bump(0))
				for _, s in ipairs(strokes) do
					emitShape(s, shadow, lod, 0, w * 0.7)
				end
				at(above(0.3))
				for _, s in ipairs(strokes) do
					emitShape(s, c, lod)
				end
				-- A lit line along the top of the main stick.
				at(above(0.32))
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
				at(bump(0))
				emitShape(shape(xs, ys), groundShade(x, y, 0.62), lod, 0, r * 0.25)
				at(above(0.25))
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
-- stoneNearJoint of them lie in a joint, where grit collects.
do
	local density = getFloat("stoneDensity", 0)
	if density > 0 then
		local sLo, sHi = getFloatRange("stoneSize", 0.01, 0.04)
		local colors = hexList("stoneColors", base)
		local shadowK = getFloat("stoneShadow", 0.5)
		local litK = getFloat("stoneLit", 1.35)
		local nearJoint = #joints > 0 and getFloat("stoneNearJoint", 0) or 0
		for _ = 1, count(density) do
			local x, y = rand() * P, rand() * P
			if nearJoint > 0 and rand() < nearJoint then
				local jt = joints[floor(rand() * #joints) + 1]
				local q = floor(rand() * #jt.px) + 1
				x, y = jt.px[q] % P, jt.py[q] % P
			end
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
				local top = above(0.4 + 0.1 * sized(size, sLo, sHi))
				at(bump(0))
				emitShape(s, toSrgb(proxy), kFar, r * 0.05, r * 0.32, 1.06, x, y)
				emitShape(s, shadow, lod, r * 0.05, r * 0.32, 1.06, x, y)
				at(top)
				emitShape(s, c, lod)
				at(top + 0.05)
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
			at(bump(0))
			emitShape(sh, groundShade(t[1], t[2], shadowK), lodFor(w * 2))
			at(above(0.35)) -- blades poke up through the ground
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
			at(bump(0.02))
			put(xs, ys, 8, pick(colors), 64)
		end
	end
end
