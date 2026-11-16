-- Prelude: color helpers, in lib.color. A color is a table {r, g, b}, sRGB 0..1.

lib = lib or {}
lib.color = {}

local color = lib.color
local floor, max, min = math.floor, math.max, math.min

function color.parseHex(text)
	local s = text:gsub("#", "")
	return { tonumber(s:sub(1, 2), 16) / 255, tonumber(s:sub(3, 4), 16) / 255, tonumber(s:sub(5, 6), 16) / 255 }
end

-- The color in the def's param `key`, a hex string.
function color.hex(key, default)
	return color.parseHex(getString(key, default))
end

function color.toLinear(c)
	local out = {}
	for i = 1, 3 do
		local v = c[i]
		out[i] = v <= 0.04045 and v / 12.92 or ((v + 0.055) / 1.055) ^ 2.4
	end
	return out
end

function color.toSrgb(l)
	local out = {}
	for i = 1, 3 do
		local v = lib.num.clamp(l[i], 0, 1)
		out[i] = v <= 0.0031308 and v * 12.92 or 1.055 * v ^ (1 / 2.4) - 0.055
	end
	return out
end

function color.luminance(c)
	local l = color.toLinear(c)
	return 0.2126 * l[1] + 0.7152 * l[2] + 0.0722 * l[3]
end

-- c with its linear value scaled by k: the same hue, k times the light.
function color.shade(c, k)
	local l = color.toLinear(c)
	return color.toSrgb({ l[1] * k, l[2] * k, l[3] * k })
end

-- Linear-light mix.
function color.mixLinear(a, b, t)
	local la, lb = color.toLinear(a), color.toLinear(b)
	return color.toSrgb({ la[1] + (lb[1] - la[1]) * t, la[2] + (lb[2] - la[2]) * t, la[3] + (lb[3] - la[3]) * t })
end

-- Per-channel mix of the sRGB values.
function color.mixSrgb(a, b, t)
	return { a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t }
end

-- Hue 0..1, saturation and value.
function color.toHsv(c)
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

function color.fromHsv(h, s, v)
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
function color.tone(c, vMul, hueDeg, sMul)
	local h, s, v = color.toHsv(c)
	return color.fromHsv((h - hueDeg / 360) % 1, lib.num.clamp(s * sMul, 0, 1), lib.num.clamp(v * vMul, 0, 1))
end
