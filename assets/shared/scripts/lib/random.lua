-- Prelude: random distributions, in lib.random. All draw from math.random, which the engine seeds per
-- variant, so a script's draw order is its own and moving a helper here changes nothing.

lib = lib or {}
lib.random = {}

local rand, sqrt, log, cos, exp = math.random, math.sqrt, math.log, math.cos, math.exp
local TAU = math.pi * 2

function lib.random.uniform(a, b)
	return a + (b - a) * rand()
end

-- -1 to 1.
function lib.random.signed()
	return rand() * 2 - 1
end

-- Standard normal.
function lib.random.gauss()
	return sqrt(-2 * log(1 - rand())) * cos(TAU * rand())
end

-- Lognormal around the range's geometric middle, clamped to the range.
function lib.random.lognormal(lo, hi, sigma)
	return lib.num.clamp(sqrt(lo * hi) * exp(sigma * lib.random.gauss()), lo, hi)
end

-- Reseeds math.random from the variant's seed and a fixed id, scrambled so neighboring ids start far
-- apart. Each stage of a generator takes its own stream, so a draw added in one stage cannot move the
-- rest.
function lib.random.stream(id)
	math.randomseed(seed, (id * 0x9E3779B97F4A7C15) ~ 0x2545F4914F6CDD1D)
end
