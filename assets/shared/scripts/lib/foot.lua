-- Prelude: where a plant meets the ground, in lib.foot. Meters, +Y down, the foot at the origin.

lib = lib or {}
lib.foot = {}

-- A small lobed contact shadow under a trunk w wide: about 1.4 w across, nudged away from the upper-left
-- light, in color c at alpha (under the silhouette threshold, so it is drawn but not clicked).
function lib.foot.contactShadow(w, c, alpha)
	local p = lib.shape.lobed(w * 0.12, w * 0.04, w * 0.7, 7, 0.82, 0.12, math.random() * math.pi, 0.62)
	lib.shape.emit(p, c, alpha)
end

-- Root flare: the trunk foot w wide spreading into a few short roots reaching spread trunk widths
-- across, rising rise above the foot, in c.
function lib.foot.rootFlare(w, rise, spread, c)
	local j = function(v)
		return v * (1 + (math.random() * 2 - 1) * 0.15)
	end
	local xs = { -w * 0.42, j(-w * 0.6), j(-w * spread * 0.5), -w * 0.3, 0, w * 0.32, j(w * spread * 0.5), j(w * 0.58), w * 0.42 }
	local ys = { -rise, -rise * 0.35, j(w * 0.1), j(w * 0.14), j(w * 0.2), j(w * 0.14), j(w * 0.08), -rise * 0.4, -rise }
	lib.shape.emit(lib.shape.polygon(xs, ys), c)
end

local TX, TY = {}, {}

-- count grass tufts standing over the foot of a trunk w wide, len tall, each a three-spike fan in dark
-- or light (alternating, so neighbours part).
function lib.foot.tufts(w, count, len, dark, light)
	for i = 1, count do
		local side = (i % 2 == 0) and 1 or -1
		local x = side * w * lib.random.uniform(0.25, 0.75)
		local y = w * lib.random.uniform(0.02, 0.16)
		local l = len * lib.random.uniform(0.75, 1.15)
		local n = lib.shape.upFan(TX, TY, x, y, 3, lib.shape.up + side * 0.15, 0.55, l, 0.2, l * 0.18, 0.45, false, 0.9)
		local p = Path()
		for k = 1, n do
			p:addVertex(TX[k], TY[k])
		end
		local c = (i % 2 == 0) and light or dark
		p:setColor(c[1], c[2], c[3], 1)
		p:close()
		asset:addPath(p)
	end
end
