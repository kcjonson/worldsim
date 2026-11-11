-- Prelude: polygon shapes, in lib.shape. Meters, +Y down.

lib = lib or {}
lib.shape = {}

local shape = lib.shape
local max, min, sqrt, sin, cos = math.max, math.min, math.sqrt, math.sin, math.cos

-- Straight up the screen, as an angle.
shape.up = -math.pi / 2

-- A polygon kept as point lists, so it can be emitted again moved and scaled.
function shape.polygon(xs, ys)
	return { xs = xs, ys = ys, n = #xs }
end

-- The two edges of a strip of width w along the polyline (px, py), tapering to its ends over taper of
-- its length. side 0 centers it on the line, -1 puts it above the line on screen, 1 below.
function shape.stripSides(px, py, w, taper, side)
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
function shape.stripPart(left, right, i0, i1)
	local xs, ys = {}, {}
	for i = i0, i1 do
		xs[#xs + 1], ys[#ys + 1] = left[i][1], left[i][2]
	end
	for i = i1, i0, -1 do
		xs[#xs + 1], ys[#ys + 1] = right[i][1], right[i][2]
	end
	return shape.polygon(xs, ys)
end

-- A tapered strip along the whole polyline, as one polygon.
function shape.strip(px, py, w, taper, side)
	local left, right = shape.stripSides(px, py, w, taper, side)
	return shape.stripPart(left, right, 1, #px)
end

local FA, FL, FB = {}, {}, {}

-- An upright fan rooted at (x, y): k spikes fanned symmetrically about dir with half-angle spread,
-- ascending (left to right through up) and jittered less than half a step so they stay in order,
-- the middle ones longest (len) and the outer ones shorter, curling outward by bend of their length
-- (positive curls clockwise: outward and down for a spike leaning right). No spike leans further than
-- maxLean from up. One polygon: the root's left corner, each spike (withSides adds a mid point on each
-- side so it tapers with a curve), a valley between neighbors at fill of the shorter one, so a fan is a
-- solid mound with a spiky top, the root's right corner. Written into the lists xs and ys, which the
-- caller reuses (fans are most of a grass tile's vertices); returns the vertex count.
function shape.upFan(xs, ys, x, y, k, dir, spread, len, bend, hubHalf, fill, withSides, maxLean)
	local step = 0.35 * spread / max(1, k - 1)
	for i = 1, k do
		local rel = k > 1 and (2 * (i - 1) / (k - 1) - 1) or 0
		FA[i] = lib.num.clamp(dir + spread * rel + (math.random() - 0.5) * 2 * step, shape.up - maxLean, shape.up + maxLean)
		FL[i] = len * lib.random.uniform(0.75, 1.1) * (1 - 0.3 * rel * rel)
		FB[i] = bend * rel + 0.1 * (math.random() - 0.5)
	end
	local n = 1
	xs[1], ys[1] = x - hubHalf * 1.5, y
	local hw = hubHalf * 0.5
	for i = 1, k do
		local ai = FA[i]
		local dx, dy = cos(ai), sin(ai)
		local L = FL[i]
		local b = FB[i] * L
		local tx, ty = x + dx * L - dy * b, y + dy * L + dx * b
		if withSides then
			local mx, my = x + dx * L * 0.5 - dy * b * 0.3, y + dy * L * 0.5 + dx * b * 0.3
			xs[n + 1], ys[n + 1] = mx + dy * hw, my - dx * hw
			xs[n + 2], ys[n + 2] = tx, ty
			xs[n + 3], ys[n + 3] = mx - dy * hw, my + dx * hw
			n = n + 3
		else
			n = n + 1
			xs[n], ys[n] = tx, ty
		end
		if i < k then
			local an = FA[i + 1]
			local r = fill * min(L, FL[i + 1])
			local am = (ai + an) * 0.5
			n = n + 1
			xs[n], ys[n] = x + cos(am) * r, y + sin(am) * r
		end
	end
	n = n + 1
	xs[n], ys[n] = x + hubHalf * 1.5, y
	return n
end
