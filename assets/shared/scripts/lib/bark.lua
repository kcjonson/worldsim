-- Prelude: textured bark, in lib.bark.

lib = lib or {}
lib.bark = {}

local function quad(x0, y0, x1, y1, x2, y2, x3, y3, r, g, b)
	local p = Path()
	p:addVertex(x0, y0); p:addVertex(x1, y1); p:addVertex(x2, y2); p:addVertex(x3, y3)
	p:setColor(r, g, b, 1.0); p:close(); asset:addPath(p)
end

-- A short cel-shaded trunk, a slightly tapered cylinder: the mid base, a lit left band, a shadow right
-- edge and one dark vertical crack. Reads as bark, not a flat stick. Spans topY to botY (topY is the
-- smaller), centered on x = 0, topW wide at the top and botW at the bottom, in the bark color r, g, b.
-- Draws one random number, the crack's place.
function lib.bark.texturedTrunk(topY, botY, topW, botW, barkR, barkG, barkB)
	-- x at side fraction f in [-0.5, 0.5], interpolating top->bot width at height t (0=top,1=bot)
	local function edge(f, t) return f * (topW + (botW - topW) * t) end
	local function yAt(t) return topY + (botY - topY) * t end
	quad(edge(-0.5, 0), topY, edge(0.5, 0), topY, edge(0.5, 1), botY, edge(-0.5, 1), botY, barkR, barkG, barkB)
	quad(edge(-0.5, 0), topY, edge(-0.1, 0), topY, edge(-0.1, 1), botY, edge(-0.5, 1), botY,
		barkR * 1.38, barkG * 1.38, barkB * 1.32)                       -- lit left band
	quad(edge(0.24, 0), topY, edge(0.5, 0), topY, edge(0.5, 1), botY, edge(0.24, 1), botY,
		barkR * 0.6, barkG * 0.6, barkB * 0.58)                         -- shadow right edge
	local cf = -0.12 + (math.random() - 0.5) * 0.2                       -- crack offset
	quad(edge(cf - 0.1, 0.12), yAt(0.12), edge(cf + 0.1, 0.12), yAt(0.12),
		edge(cf + 0.1, 1), botY, edge(cf - 0.1, 1), botY,
		barkR * 0.48, barkG * 0.48, barkB * 0.45)                       -- vertical bark crack
end
