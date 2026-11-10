-- Prelude: numeric helpers, in lib.num. Doubles throughout; the clamp global the engine binds is a float one.

lib = lib or {}
lib.num = {}

function lib.num.clamp(v, lo, hi)
	return v < lo and lo or (v > hi and hi or v)
end
