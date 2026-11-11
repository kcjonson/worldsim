-- Deciduous tree generator, 3/4 top-down, skeleton first (tree-variety.md R5 to R7).
-- A branch skeleton grows from one stem by the species' growth numbers; leaf clusters hang on its tips
-- and their union is the crown. Everything follows one number, age, from 0 (just established) to 1
-- (oldest): how long the tree has grown, and so its size, habit and how it has aged. Each growth stage
-- draws from its own random stream, so the same seed at a slightly greater age is the same tree grown a
-- little. The trunk foot is at the origin, where the collision rect is; light comes from the upper left.

local stream, uniform = lib.random.stream, lib.random.uniform
local clamp, smooth, curve, curveParam = lib.num.clamp, lib.num.smoothstep, lib.num.curve, lib.num.curveParam
local color, shape = lib.color, lib.shape
local sqrt, cos, sin, max, min, abs, floor, ceil, pi = math.sqrt, math.cos, math.sin, math.max, math.min, math.abs, math.floor, math.ceil, math.pi
local rand = math.random

-- The engine names an age class; an `age` param, when a def or a tool sets one, wins.
local age = clamp(getFloat("age", ({ sapling = 0.15, mature = 0.55, veteran = 0.9 })[ageClass] or 0.55), 0, 1)

-- Sizes in meters, smooth in age. The collision rect follows the trunk foot and nothing random.
local crownWidth = curve(curveParam("crownWidth", "0.15:0.71, 0.55:1.6, 0.9:2.18"), age)
local trunkWidth = curve(curveParam("trunkWidth", "0.15:0.08, 0.55:0.2, 0.9:0.32"), age)
asset:setCollisionRect(trunkWidth / 2, trunkWidth / 2, 0, 0)

-- Growth numbers (R6), and how age moves them: every young tree keeps one leader; an old one loses
-- control of its crown, sheds more and sags.
local young, old = 1 - smooth(0.1, 0.45, age), smooth(0.6, 1.0, age)
local speciesApical = getFloat("apicalControl", 0.4)
local apical = speciesApical + (max(speciesApical, 0.78) - speciesApical) * young - 0.06 * old
local opposite = getString("buds", "alternate") == "opposite"
local angleLo, angleHi = getFloatRange("branchAngle", 55, 75)
local lengthRatio = getFloat("lengthRatio", 0.8)
local limbsLo, limbsHi = getFloatRange("mainLimbs", 3, 5)
local shedding = getFloat("shedding", 0.35) + 0.2 * old
local droop = getFloat("droop", 0.25) + 0.15 * old
local stemRise = getFloat("stemRise", 0.5) * (1 + 0.6 * young) -- a young tree is all stem
local crownDepth = getFloat("crownDepth", 0.3) * (1 + 1.2 * young) -- a young crown is taller than wide
local clusterSize = getFloat("clusterSize", 0.15)
local pointed = getString("lobes", "rounded") == "pointed"
local stagHeaded = getString("growingOld", "stagHeaded") == "stagHeaded"
local growth = 1.5 + 6 * age -- growth steps; the last one partly done
local clusterCount = curve(curveParam("clusters", "0.15:4, 0.55:9.5, 0.9:13.5"), age)
local capScale = smooth(0.2, 0.45, age) -- lit caps come in as the crown fills out
local fleckCount = curve(curveParam("flecks", "0.15:0, 0.55:3, 0.9:4"), age)

local edgeTone = color.hex("foliageEdge", "#2f4a26")
local baseTone = color.hex("foliageBase", "#3e5e2c")
local midTone = color.hex("foliageMid", "#4f7536")
local hiTone = color.hex("foliageHighlight", "#6f9a4a")
local barkBase = color.hex("barkBase", "#5a3f26")
local barkShadow = color.hex("barkShadow", "#45301d")
local deadLimb = color.hex("deadLimb", "#6e6450")
local deadLimbEdge = color.hex("deadLimbEdge", "#3a3326")
local shadowTone = color.hex("contactShadow", "#1d2616")
local tuftDark = color.hex("tuftDark", "#527530")
local tuftLight = color.hex("tuftLight", "#5E883A")
local yellowing = color.hex("yellowing", "#9c8a2c")

-- The 3/4 view: ground y is foreshortened and height rises up the screen.
local kGround, kRise = 0.5, 0.72
local function project(n)
	return n.x, n.y * kGround - n.z * kRise
end

local DEG = pi / 180
local GOLDEN = 137.5 * DEG

-- Crown form, a growth event drawn once per seed (R5); lost boughs and ragged crowns grow likelier
-- with age. A form whose event falls later than the tree has grown has not happened yet.
stream(1)
local form
do
	local weights = {
		{ "none", 1.6 },
		{ "lopsided", 0.8 + 1.0 * old },
		{ "twin", 0.8 },
		{ "ragged", 0.3 + 1.5 * old },
		{ "windPruned", 0.8 },
	}
	local total = 0
	for _, w in ipairs(weights) do
		total = total + w[2]
	end
	local pick = rand() * total
	for _, w in ipairs(weights) do
		form = w[1]
		pick = pick - w[2]
		if pick < 0 then
			break
		end
	end
end
local lossStep = floor(uniform(3, 4.999)) -- when a lopsided tree loses its bough
stream(2)
local condition = "none"
if rand() < 0.125 then
	condition = rand() < 0.5 and "thinning" or "yellowing"
end
local windAz = (rand() < 0.5 and 0 or pi) + uniform(-0.5, 0.5) -- across the view, so the lean shows
local yellowAz = rand() * 2 * pi
local raggedness = form == "ragged" and smooth(0.2, 0.6, age) or 0
shedding = shedding + 0.35 * raggedness

-- ---- skeleton ----------------------------------------------------------------------------------
-- A node is a point of the skeleton in tree units (scaled to meters once grown): x and y on the
-- ground, z up. It ends the shoot from its parent, carries that shoot's direction and vigour, and is a
-- tip until it grows. Its id names its place in the tree (a parent has at most four children), so the
-- random numbers a shoot grows by are its own whatever else grew.

local nodes = {}
local function addNode(parent, dx, dy, dz, len, v, order, az)
	local px, py, pz = 0, 0, 0
	if parent then
		px, py, pz = parent.x, parent.y, parent.z
	end
	local n = {
		x = px + dx * len, y = py + dy * len, z = pz + dz * len,
		dx = dx, dy = dy, dz = dz, len = len, v = v, order = order, az = az,
		parent = parent, kids = {}, tip = true, dead = false,
		id = parent and parent.id * 4 + #parent.kids or 1,
	}
	if parent then
		parent.kids[#parent.kids + 1] = n
		parent.tip = false
	end
	nodes[#nodes + 1] = n
	return n
end

local function normalize(x, y, z)
	local l = sqrt(x * x + y * y + z * z)
	return x / l, y / l, z / l
end

-- d turned by angle a toward azimuth az, measured around d.
local function turn(dx, dy, dz, a, az)
	local ux, uy, uz
	if abs(dz) < 0.95 then
		ux, uy, uz = normalize(dy, -dx, 0)
	else
		ux, uy, uz = normalize(0, dz, -dy)
	end
	local wx, wy, wz = dy * uz - dz * uy, dz * ux - dx * uz, dx * uy - dy * ux
	local px, py, pz = ux * cos(az) + wx * sin(az), uy * cos(az) + wy * sin(az), uz * cos(az) + wz * sin(az)
	return normalize(dx * cos(a) + px * sin(a), dy * cos(a) + py * sin(a), dz * cos(a) + pz * sin(a))
end

-- A shoot of this order leans toward the light while young and sags as it gets further out (droop),
-- never much below level.
local function bend(order, dx, dy, dz)
	local x, y, z = normalize(dx, dy, dz + 0.35 / (order + 1) - droop * order * 0.18)
	if z < -0.2 then
		local h = sqrt((1 - 0.04) / (x * x + y * y))
		x, y, z = x * h, y * h, -0.2
	end
	return x, y, z
end

-- Wind-pruned: shoots growing into the wind come out short.
local function windCut(dx, dy)
	local h = sqrt(dx * dx + dy * dy)
	if form ~= "windPruned" or h < 1e-6 then
		return 1
	end
	return 1 - 0.75 * max(0, (dx * cos(windAz) + dy * sin(windAz)) / h)
end

local root = addNode(nil, 0, 0, 1, 0, 1, 0, 0)
stream(3)
local stem = addNode(root, 0, 0, 1, 1, 1, 0, rand() * 2 * pi)

-- Scaffold: the main limbs leave the top of the stem one per node (alternate, 137.5 degrees apart) or
-- in pairs (opposite, each pair square to the last). Apical control below 0.5 dissolves the stem into
-- them; above it the leader goes on with the share it kept. Twin-crowned: the first two are stems.
local scaffold = {}
do
	local count = floor(uniform(limbsLo, limbsHi + 0.999))
	local leaderShare = max(0, (apical - 0.5) * 2)
	local twin = form == "twin"
	local tip, az = stem, stem.az
	if twin then
		az = pi / 2 + uniform(-0.4, 0.4) -- the two stems part across the view, not toward it
	end
	local vigour = {}
	local total = 0
	for i = 1, count do
		vigour[i] = uniform(0.75, 1.25) * (twin and (i <= 2 and 2.4 or 0.45) or 1)
		total = total + vigour[i]
	end
	for i = 1, count do
		local v = (1 - leaderShare) * vigour[i] / total
		local a = ((twin and i <= 2) and uniform(28, 38) or uniform(angleLo, angleHi)) * DEG
		local dx, dy, dz = bend(1, turn(0, 0, 1, a, az))
		local len = uniform(0.85, 1.1) * sqrt(v * count) * windCut(dx, dy)
		scaffold[#scaffold + 1] = addNode(tip, dx, dy, dz, len, v, 1, az)
		local paired = opposite and i % 2 == 1
		az = az + ((paired or (twin and i == 1)) and pi or (opposite and pi / 2 or GOLDEN))
		if i < count and not paired then
			tip = addNode(tip, 0, 0, 1, 0.12, 1, 0, az)
			tip.chain = true
		end
	end
	if leaderShare > 0 then
		addNode(tip, 0, 0, 1, 0.5, leaderShare, 0, az)
	end
end

local kMaxTips = 40
local kMinVigour = 0.006

-- A lopsided tree loses a main bough: of the strongest three, the one reaching furthest across the
-- view, so the cut shows. It grows on without it, all to one side.
local function loseBough()
	local strongest = {}
	for _, s in ipairs(scaffold) do
		strongest[#strongest + 1] = s
	end
	table.sort(strongest, function(a, b)
		return a.v > b.v
	end)
	local lost = strongest[1]
	for i = 2, min(3, #strongest) do
		if abs(strongest[i].dx) > abs(lost.dx) then
			lost = strongest[i]
		end
	end
	local function kill(n)
		n.dead, n.tip = true, false
		for _, k in ipairs(n.kids) do
			kill(k)
		end
	end
	kill(lost)
end

-- Growth steps: every tip forks into a leader, keeping `apical` of its vigour, and one lateral
-- (alternate) or two (opposite), drawing from its own stream. The stronger shoot turns less. A shaded
-- low or inner lateral is shed, and the tree stops adding laterals at kMaxTips tips. The last step has
-- grown only its fraction.
for step = 1, ceil(growth) do
	local extent = min(1, growth - (step - 1))
	if form == "lopsided" and step == lossStep then
		loseBough()
	end
	local height, reach, count = 0.01, 0.01, 0
	local tips = {}
	for _, n in ipairs(nodes) do
		height = max(height, n.z)
		reach = max(reach, sqrt(n.x * n.x + n.y * n.y))
		if n.tip and not n.dead then
			count = count + 1
			if n.v >= kMinVigour then
				tips[#tips + 1] = n
			end
		end
	end
	table.sort(tips, function(a, b)
		return a.v > b.v
	end)
	for _, t in ipairs(tips) do
		stream(1024 + t.id)
		local lat = opposite and 2 or 1
		local latShare = (1 - apical) / lat
		local shade = 0.5 * (1 - t.z / height) + 0.5 * (1 - sqrt(t.x * t.x + t.y * t.y) / reach)
		local az = t.az + (opposite and pi / 2 or GOLDEN)
		local leadV = t.v * apical
		local keep = {}
		for k = 1, lat do
			keep[k] = rand() >= shedding * shade and count < kMaxTips
			if keep[k] then
				count = count + 1
			else
				leadV = leadV + t.v * latShare * 0.5
			end
		end
		-- lengths come from the parent's full length, so a part-grown shoot's children are not short
		local parentLen = t.fullLen or t.len
		local ldx, ldy, ldz = bend(t.order + 1, turn(t.dx, t.dy, t.dz, uniform(angleLo, angleHi) * DEG * (1 - apical) * 0.5, az + pi))
		local leadLen = parentLen * lengthRatio * sqrt(leadV / t.v / 0.5) * windCut(ldx, ldy)
		addNode(t, ldx, ldy, ldz, leadLen * extent, leadV, t.order, az).fullLen = leadLen
		for k = 1, lat do
			if keep[k] then
				local kaz = az + (k - 1) * pi
				local dx, dy, dz = bend(t.order + 1, turn(t.dx, t.dy, t.dz, uniform(angleLo, angleHi) * DEG, kaz))
				local len = parentLen * lengthRatio * sqrt(latShare / 0.5) * windCut(dx, dy)
				addNode(t, dx, dy, dz, len * extent, t.v * latShare, t.order + 1, kaz).fullLen = len
			end
		end
	end
end

-- Subtree tips, and areas for the pipe rule: a parent's cross-section is the sum of its children's.
local function collect(n)
	n.tips, n.area = {}, 0
	if n.dead then
		return
	end
	if #n.kids == 0 then
		n.tips[1], n.area = n, 1
		return
	end
	for _, k in ipairs(n.kids) do
		collect(k)
		for _, t in ipairs(k.tips) do
			n.tips[#n.tips + 1] = t
		end
		n.area = n.area + k.area
	end
end
collect(root)

-- Into meters: the crown spreads crownWidth across, the stem stands stemRise of that tall, and the
-- crown above it is at most crownDepth of it deep (a long-grown tree is pressed down, not stretched).
local function centroid(tips)
	local x, y, z = 0, 0, 0
	for _, t in ipairs(tips) do
		x, y, z = x + t.x / #tips, y + t.y / #tips, z + t.z / #tips
	end
	return x, y, z
end
local rc = clusterSize * crownWidth
do
	local maxRadial, top = 1e-6, 1
	for _, t in ipairs(stem.tips) do
		maxRadial = max(maxRadial, sqrt(t.x * t.x + t.y * t.y))
		top = max(top, t.z)
	end
	local s = (crownWidth * 0.5 - rc) / maxRadial
	local sz = min(s, crownDepth * crownWidth / max(1e-6, top - 1))
	local stemTop = stemRise * crownWidth
	for _, n in ipairs(nodes) do
		n.x, n.y = n.x * s, n.y * s
		n.z = n.z >= 1 and stemTop + (n.z - 1) * sz or n.z * stemTop
		if form == "windPruned" and n.z > stemTop then
			-- the crown leans away from the wind
			local lean = 0.9 * (n.z - stemTop)
			n.x, n.y = n.x - cos(windAz) * lean, n.y - sin(windAz) * lean
		end
	end
end

-- ---- leaf clusters -------------------------------------------------------------------------------
-- A group is a subtree whose tips share one cluster. Start from the main limbs (and the leader, if the
-- stem kept one), split the biggest until there are enough, and fold the smallest into its nearest
-- neighbour while there are too many.

stream(4)
local wantClusters = floor(clusterCount + rand())
if condition == "thinning" then
	wantClusters = floor(wantClusters * 0.7)
end
wantClusters = max(3, wantClusters)
local groups = {}
local function openChain(n)
	for _, k in ipairs(n.kids) do
		if k.chain then
			openChain(k)
		elseif k.area > 0 then
			groups[#groups + 1] = { node = k, tips = k.tips, area = k.area }
		end
	end
end
openChain(stem)
while #groups < wantClusters do
	table.sort(groups, function(a, b)
		return a.area > b.area
	end)
	local split = nil
	for i, g in ipairs(groups) do
		if #g.node.kids > 0 then
			split = i
			break
		end
	end
	if not split then
		break
	end
	local g = table.remove(groups, split)
	for _, k in ipairs(g.node.kids) do
		if k.area > 0 then
			groups[#groups + 1] = { node = k, tips = k.tips, area = k.area }
		end
	end
end
while #groups > wantClusters do
	table.sort(groups, function(a, b)
		return a.area < b.area
	end)
	local small = table.remove(groups, 1)
	local sx, sy, sz = centroid(small.tips)
	local best, bestD = nil, 1e18
	for _, g in ipairs(groups) do
		local gx, gy, gz = centroid(g.tips)
		local d = (gx - sx) ^ 2 + (gy - sy) ^ 2 + (gz - sz) ^ 2
		if d < bestD then
			best, bestD = g, d
		end
	end
	local tips = {}
	for _, t in ipairs(best.tips) do
		tips[#tips + 1] = t
	end
	for _, t in ipairs(small.tips) do
		tips[#tips + 1] = t
	end
	best.tips, best.area = tips, best.area + small.area
end

local clusters = {}
local meanArea = 0
for _, g in ipairs(groups) do
	meanArea = meanArea + g.area / #groups
end
for _, g in ipairs(groups) do
	local cx, cy, cz = centroid(g.tips)
	local spread = 0
	for _, t in ipairs(g.tips) do
		spread = max(spread, sqrt((t.x - cx) ^ 2 + (t.y - cy) ^ 2))
	end
	local c = { x = cx, y = cy, z = cz, node = g.node, tips = g.tips }
	-- a cluster's own fullness is its shoot's, so it holds as the tree grows
	stream(2048 + g.node.id)
	c.r = clamp(rc * (0.55 + 0.6 * sqrt(g.area / meanArea)) * uniform(0.8, 1.2) + spread * 0.5, rc * 0.55, rc * 1.6)
	c.sx, c.sy = project(c)
	clusters[#clusters + 1] = c
end

-- Old age (R6): an oak goes stag-headed, the clusters along the top of its crown dying back one after
-- another late in life, to bare limbs that stand past what is left; a maple broadens (its crownWidth
-- curve) and stays dense.
local dying = {}
if stagHeaded then
	table.sort(clusters, function(a, b)
		return a.sy - a.r < b.sy - b.r
	end)
	for i = 1, min(3, #clusters - 6) do
		local c = clusters[i]
		c.dieback = smooth(0.68 + 0.07 * i, 0.9 + 0.05 * i, age)
		if c.dieback > 0 then
			dying[#dying + 1] = c
			c.r = c.r * (1 - 0.75 * c.dieback)
		end
	end
	for i = #clusters, 1, -1 do
		if clusters[i].dieback and clusters[i].r < rc * 0.35 then
			table.remove(clusters, i)
		end
	end
end
stream(11)
if raggedness > 0 and #clusters > 5 then
	-- heavy shedding: the cluster nearest the trunk and a few at the edge are gone, so the crown is
	-- gappy and a limb shows
	table.sort(clusters, function(a, b)
		return a.x * a.x + a.y * a.y < b.x * b.x + b.y * b.y
	end)
	table.remove(clusters, 1)
	for _ = 1, floor(max(2, #clusters * 0.25) * raggedness + 0.5) do
		table.remove(clusters, #clusters - 2 * floor(rand() * 2))
	end
end

local zLo, zHi = 1e9, -1e9
local midX, midY = 0, 0
for _, c in ipairs(clusters) do
	zLo, zHi = min(zLo, c.z), max(zHi, c.z)
	midX, midY = midX + c.sx / #clusters, midY + c.sy / #clusters
end
-- Back to front: low and far clusters first, high and near last.
table.sort(clusters, function(a, b)
	return a.z + a.y * 0.6 < b.z + b.y * 0.6
end)

-- Palette: the species' tones, jittered +/-3% per channel for the variant.
stream(5)
local function jitter(c)
	return { c[1] * uniform(0.97, 1.03), c[2] * uniform(0.97, 1.03), c[3] * uniform(0.97, 1.03) }
end
edgeTone, baseTone, midTone, hiTone = jitter(edgeTone), jitter(baseTone), jitter(midTone), jitter(hiTone)
-- a yellowing patch sits on the side of the crown its seed points to
local yellowX, yellowY = midX + cos(yellowAz) * crownWidth * 0.3, midY + sin(yellowAz) * crownWidth * 0.3 * kGround

-- ---- drawing -----------------------------------------------------------------------------------

local squash = 0.88
local rimWidth = max(0.035, crownWidth * 0.026)
local stemTopX, stemTopY = project(stem)

-- the drawn width of a shoot by the pipe rule, about twice real so limbs read at game zoom
local function limbWidth(n)
	return trunkWidth * sqrt(n.area / max(1, stem.area))
end

-- A tapered strip through the skeleton from `from` out to `to`, through every node between.
local function limb(from, to, w0, w1, c)
	local chain = {}
	local n = to
	while n and n ~= from do
		table.insert(chain, 1, n)
		n = n.parent
	end
	table.insert(chain, 1, from)
	local px, py = {}, {}
	for _, node in ipairs(chain) do
		px[#px + 1], py[#py + 1] = project(node)
	end
	local count = #px
	local left, right = {}, {}
	for i = 1, count do
		local ax, ay = px[max(1, i - 1)], py[max(1, i - 1)]
		local bx, by = px[min(count, i + 1)], py[min(count, i + 1)]
		local dx, dy = bx - ax, by - ay
		local l = max(1e-6, sqrt(dx * dx + dy * dy))
		local w = (w0 + (w1 - w0) * (i - 1) / max(1, count - 1)) * 0.5
		left[i] = { px[i] - dy / l * w, py[i] + dx / l * w }
		right[i] = { px[i] + dy / l * w, py[i] - dx / l * w }
	end
	shape.emit(shape.stripPart(left, right, 1, count), c)
end

-- 1. contact shadow, root flare, trunk
stream(6)
local flare = smooth(0.3, 1, age)
lib.foot.contactShadow(trunkWidth, shadowTone, 0.3)
lib.foot.rootFlare(trunkWidth, trunkWidth * (0.3 + 0.4 * flare), 1.3 + 0.9 * flare, barkBase)
lib.bark.texturedTrunk(stemTopY, 0, trunkWidth * 0.78, trunkWidth, barkBase[1], barkBase[2], barkBase[3])

-- 2. one dark rim around the union of the clusters, which also backs the crown so a gap reads as
-- deep shade: the farthest the union reaches along rays from the crown's middle, pushed out by the rim
local function inUnion(x, y, grow)
	for _, c in ipairs(clusters) do
		if (x - c.sx) ^ 2 + ((y - c.sy) / squash) ^ 2 < (c.r + grow) ^ 2 then
			return true
		end
	end
	return false
end
do
	local rays = clamp(#clusters * 2 + 10, 16, 34)
	local xs, ys = {}, {}
	for k = 1, rays do
		local a = (k - 1) * 2 * pi / rays
		local far = 0
		for _, c in ipairs(clusters) do
			-- the ray from the middle against the cluster's ellipse, in the space where it is a circle
			local ox, oy = midX - c.sx, (midY - c.sy) / squash
			local R = c.r + rimWidth
			local b = ox * cos(a) + oy * sin(a)
			local disc = b * b - (ox * ox + oy * oy - R * R)
			if disc >= 0 then
				far = max(far, -b + sqrt(disc))
			end
		end
		xs[k], ys[k] = midX + cos(a) * far, midY + sin(a) * squash * far
	end
	shape.emit(shape.polygon(xs, ys), color.shade(edgeTone, 0.45))
end

-- 3. limbs, only where one can show: through a gap the clusters leave
do
	local drawn = 0
	for _, s in ipairs(scaffold) do
		if not s.dead and drawn < 2 then
			local mx, my = project(s)
			local hx, hy = (mx + stemTopX) * 0.5, (my + stemTopY) * 0.5
			if not inUnion(hx, hy, -0.15 * rc) or not inUnion(mx, my, -0.15 * rc) then
				limb(s.parent, s, limbWidth(s) * 1.1, limbWidth(s) * 0.7, barkShadow)
				drawn = drawn + 1
			end
		end
	end
end

-- 4. clusters, back to front: a dark base, a mid cap offset up-left, and a lit cap on the highest.
-- Whatever a later cluster covers whole is left out.
local function hiddenAfter(i, x, y)
	for j = i + 1, #clusters do
		local o = clusters[j]
		if (x - o.sx) ^ 2 + ((y - o.sy) / squash) ^ 2 < (o.r * 0.84) ^ 2 then
			return true
		end
	end
	return false
end
stream(8)
local notch = pointed and 0.66 or 0.84
local verts = getInt("clusterVertices", 8)
for i, c in ipairs(clusters) do
	local h = (zHi > zLo) and (c.z - zLo) / (zHi - zLo) or 1
	local ox, oy = c.sx - midX, c.sy - midY
	local ol = max(1e-6, sqrt(ox * ox + oy * oy))
	local facing = (ox * -0.6 + oy * -0.8) / ol * min(1, ol / rc)
	local lit = clamp(0.15 + 0.6 * h + 0.25 * facing, 0, 1)
	c.lit = lit
	local base, mid, hi = baseTone, midTone, hiTone
	if condition == "yellowing" then
		local k = 1 - smooth(0.25, 0.45, sqrt((c.sx - yellowX) ^ 2 + (c.sy - yellowY) ^ 2) / crownWidth)
		base, mid, hi = color.mixLinear(base, yellowing, 0.35 * k), color.mixLinear(mid, yellowing, 0.45 * k), color.mixLinear(hi, yellowing, 0.45 * k)
	end
	local seen = not hiddenAfter(i, c.sx, c.sy)
	for k = 0, 7 do
		seen = seen or not hiddenAfter(i, c.sx + cos(k * pi / 4) * c.r * 0.85, c.sy + sin(k * pi / 4) * c.r * 0.85 * squash)
	end
	local rot = rand() * 2 * pi
	if seen then
		shape.emit(shape.lobed(c.sx, c.sy, c.r, verts, notch, 0.08, rot, squash), color.mixLinear(edgeTone, base, 0.3 + 0.7 * lit))
	end
	local capX, capY = c.sx - c.r * 0.16, c.sy - c.r * 0.2
	if seen and not hiddenAfter(i, capX, capY) then
		shape.emit(shape.lobed(capX, capY, c.r * 0.66, 6, notch, 0.12, rot + 0.5, squash), color.mixLinear(base, mid, 0.3 + 0.7 * lit))
		local hiX, hiY = c.sx - c.r * 0.3, c.sy - c.r * 0.36
		if capScale > 0.3 and lit > 0.62 and not hiddenAfter(i, hiX, hiY) then
			shape.emit(shape.lobed(hiX, hiY, c.r * 0.34 * capScale, 5, notch, 0.12, rot + 1, squash), color.mixLinear(mid, hi, lit))
		end
	end
end

-- 5. leaf-scale flecks: small chips in the mid and highlight tones along the lit clusters' upper edges
stream(9)
do
	table.sort(clusters, function(a, b)
		return a.lit > b.lit
	end)
	for i = 1, min(floor(fleckCount + 0.5), #clusters) do
		local c = clusters[i]
		local a = uniform(-2.4, -0.7)
		local d = c.r * uniform(0.5, 0.8)
		local x, y = c.sx + cos(a) * d, c.sy + sin(a) * d * squash
		local k = c.r * 0.18
		local t = uniform(0, pi)
		shape.emit(shape.polygon({ x + cos(t) * k, x - sin(t) * k * 0.5, x - cos(t) * k },
			{ y + sin(t) * k, y + cos(t) * k * 0.5, y - sin(t) * k }), (i % 2 == 0) and midTone or hiTone)
	end
end

-- 6. what died back: bare limbs standing past the crown, longer the further the dieback has gone
for _, c in ipairs(dying) do
	local tip = c.tips[1]
	for _, t in ipairs(c.tips) do
		if select(2, project(t)) < select(2, project(tip)) then
			tip = t
		end
	end
	local from = c.node.parent or stem
	local snag = addNode(tip, tip.dx, tip.dy, max(tip.dz, 0.3), rc * 2.4 * c.dieback, 0, tip.order, tip.az)
	local w = limbWidth(c.node) * 0.6
	limb(from, snag, w + rimWidth * 1.2, w * 0.25 + rimWidth, deadLimbEdge)
	limb(from, snag, w, w * 0.25, deadLimb)
end

-- 7. grass over the trunk foot
stream(10)
lib.foot.tufts(trunkWidth, floor(uniform(2, 3.999)), getFloat("tuftLength", 0.09), tuftDark, tuftLight)
