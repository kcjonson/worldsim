-- Prelude: numeric helpers, in lib.num. Doubles throughout; the clamp global the engine binds is a float one.

lib = lib or {}
lib.num = {}

function lib.num.clamp(v, lo, hi)
	return v < lo and lo or (v > hi and hi or v)
end

function lib.num.smoothstep(e0, e1, x)
	local t = lib.num.clamp((x - e0) / (e1 - e0), 0, 1)
	return t * t * (3 - 2 * t)
end

-- The value at x of a smooth curve through points { {x1, y1}, {x2, y2}, ... } sorted by x: a monotone
-- cubic between them (it never overshoots a point), and straight on past the ends along the end slopes.
function lib.num.curve(points, x)
	local n = #points
	if n == 1 then
		return points[1][2]
	end
	local function slope(k)
		return (points[k + 1][2] - points[k][2]) / (points[k + 1][1] - points[k][1])
	end
	local function tangent(k)
		if k == 1 then
			return slope(1)
		elseif k == n then
			return slope(n - 1)
		end
		local a, b = slope(k - 1), slope(k)
		return a * b <= 0 and 0 or 2 / (1 / a + 1 / b)
	end
	if x <= points[1][1] then
		return points[1][2] + (x - points[1][1]) * tangent(1)
	elseif x >= points[n][1] then
		return points[n][2] + (x - points[n][1]) * tangent(n)
	end
	local k = 1
	while x > points[k + 1][1] do
		k = k + 1
	end
	local h = points[k + 1][1] - points[k][1]
	local t = (x - points[k][1]) / h
	local t2, t3 = t * t, t * t * t
	return (2 * t3 - 3 * t2 + 1) * points[k][2] + (t3 - 2 * t2 + t) * h * tangent(k) + (-2 * t3 + 3 * t2) * points[k + 1][2]
		+ (t3 - t2) * h * tangent(k + 1)
end

-- The curve in the def's param `key`, written "x:y, x:y, ..." in increasing x, or `default` in the same form.
function lib.num.curveParam(key, default)
	local points = {}
	for x, y in getString(key, default):gmatch("([%-%d%.]+)%s*:%s*([%-%d%.]+)") do
		points[#points + 1] = { tonumber(x), tonumber(y) }
	end
	return points
end
