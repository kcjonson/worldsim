-- Placeholder ground tile (role ground) for the land surfaces with no real ground art yet: a flat base
-- in the surface's color under a light, low-contrast speckle. One seamless period x period meter
-- variant; speckles that cross the edge are wrapped by the registry.
--
-- Meters, +Y down, sRGB colors 0..1. Each speckle is tagged with its own width as an LOD, so a mip only
-- keeps speckles at least a texel across.

local P = getFloat("period", 4)
local TAU = math.pi * 2
local rand, floor, cos, sin, min = math.random, math.floor, math.cos, math.sin, math.min

local function hex(key, default)
	local s = getString(key, default):gsub("#", "")
	return { tonumber(s:sub(1, 2), 16) / 255, tonumber(s:sub(3, 4), 16) / 255, tonumber(s:sub(5, 6), 16) / 255 }
end

local base = hex("baseColor", "#808080")
local perSquareMeter = getFloat("speckleDensity", 40)
local sizeLo, sizeHi = getFloatRange("speckleSize", 0.03, 0.09) -- diameters
local contrast = getFloat("speckleContrast", 0.06) -- value swing either way, a fraction

local pathProto = Path()
local addVertex, setColor, setLod, closePath = pathProto.addVertex, pathProto.setColor, pathProto.setLod, pathProto.close
local addPath = asset.addPath

local function emit(p, c, lod)
	setColor(p, c[1], c[2], c[3], 1)
	if lod > 0 then
		setLod(p, lod)
	end
	closePath(p)
	addPath(asset, p)
end

local p = Path()
addVertex(p, 0, 0)
addVertex(p, P, 0)
addVertex(p, P, P)
addVertex(p, 0, P)
emit(p, base, 0)

for _ = 1, floor(perSquareMeter * P * P + 0.5) do
	local d = sizeLo + (sizeHi - sizeLo) * rand()
	local k = 1 + contrast * (rand() * 2 - 1)
	local c = { min(1, base[1] * k), min(1, base[2] * k), min(1, base[3] * k) }
	local x, y, phase = rand() * P, rand() * P, rand() * TAU
	local s = Path()
	for i = 0, 6 do
		local a = phase + TAU * i / 7
		local r = 0.5 * d * (0.8 + 0.4 * rand())
		addVertex(s, x + cos(a) * r, y + sin(a) * r)
	end
	emit(s, c, 1 / d)
end
