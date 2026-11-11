-- Deciduous tree generator, 3/4 top-down, skeleton first (tree-variety.md R5 to R7).
-- A branch skeleton grows from one stem by the species' growth numbers; sprays of small leaves hang
-- along its outer shoots and their union is the crown. Everything follows one number, age, from 0 (just established) to 1
-- (oldest): how long the tree has grown, and so its size, habit and how it has aged. Each growth stage
-- draws from its own random stream, so the same seed at a slightly greater age is the same tree grown a
-- little. The trunk foot is at the origin, where the collision rect is; light comes from the upper left.

local stream, uniform, gauss = lib.random.stream, lib.random.uniform, lib.random.gauss
local clamp, smooth, curve, curveParam = lib.num.clamp, lib.num.smoothstep, lib.num.curve, lib.num.curveParam
local color, shape = lib.color, lib.shape
local sqrt, cos, sin, max, min, abs, floor, ceil, pi = math.sqrt, math.cos, math.sin, math.max, math.min, math.abs, math.floor, math.ceil, math.pi
local rand = math.random

-- The engine names an age class; an `age` param, when a def or a tool sets one, wins.
local age = clamp(getFloat("age", ({ sapling = 0.15, mature = 0.55, veteran = 0.9 })[ageClass] or 0.55), 0, 1)

-- Sizes in meters, smooth in age. The collision rect follows the trunk foot and nothing random.
local crownWidth = curve(curveParam("crownWidth", "0.15:1.6, 0.55:3.6, 0.9:4.9"), age)
local trunkWidth = curve(curveParam("trunkWidth", "0.15:0.18, 0.55:0.45, 0.9:0.72"), age)
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

-- Foliage runs from shade through base and light to highlight; each individual turns all four by up
-- to hueSpread degrees and scales saturation and value by up to satSpread and valueSpread.
local shadeTone = color.hex("foliageShade", "#4a6a2e")
local baseTone = color.hex("foliageBase", "#5b7f36")
local lightTone = color.hex("foliageLight", "#729a43")
local hiTone = color.hex("foliageHighlight", "#93b85a")
local hueSpread = getFloat("hueSpread", 14)
local satSpread = getFloat("saturationSpread", 0.18)
local valueSpread = getFloat("valueSpread", 0.1)
local barkTone = color.hex("bark", "#6b4a2d")
local shadowTone = color.hex("contactShadow", "#1d2616")
local yellowing = color.hex("yellowing", "#a8973a")

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
rand() -- a draw kept so later values in this stream stay put
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
	local s = (crownWidth * 0.5 - rc * 0.8) / maxRadial -- the outer sprays reach about 0.8 rc past their tips
	local sz = min(s, crownDepth * crownWidth / max(1e-6, top - 1))
	local stemTop = stemRise * crownWidth
	for _, n in ipairs(nodes) do
		n.x, n.y = n.x * s, n.y * s
		n.z = n.z >= 1 and stemTop + (n.z - 1) * sz or n.z * stemTop
		if form == "windPruned" and n.z > stemTop then
			-- the crown leans away from the wind
			local lean = 0.35 * (n.z - stemTop)
			n.x, n.y = n.x - cos(windAz) * lean, n.y - sin(windAz) * lean
		end
	end
end


-- ---- clumps -------------------------------------------------------------------------------------
-- A clump is a subtree whose tips light as one mass: bright on top and toward the sun, dark beneath.
-- Start from the main limbs (and the leader, if the stem kept one), split the biggest until there are
-- enough, and fold the smallest into its nearest neighbour while there are too many. Clumps light the
-- foliage and are what dieback and ragged crowns take away; they are never drawn as shapes.

stream(4)
local wantClusters = max(3, floor(clusterCount + rand()))
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
for _, g in ipairs(groups) do
	local cx, cy, cz = centroid(g.tips)
	local spread = 0
	for _, t in ipairs(g.tips) do
		spread = max(spread, sqrt((t.x - cx) ^ 2 + (t.y - cy) ^ 2 + (t.z - cz) ^ 2))
	end
	local c = { x = cx, y = cy, z = cz, node = g.node, tips = g.tips, fade = 1 }
	c.r = max(rc, spread)
	c.sx, c.sy = project(c)
	clusters[#clusters + 1] = c
	for _, t in ipairs(g.tips) do
		t.clump = c
	end
end

-- Old age (R6): an oak goes stag-headed, the clumps along the top of its crown dying back one after
-- another late in life, leaving their limbs bare past what is left; a maple broadens (its crownWidth
-- curve) and stays dense.
local dying = {}
if stagHeaded then
	local order = {}
	for _, c in ipairs(clusters) do
		order[#order + 1] = c
	end
	table.sort(order, function(a, b)
		return a.sy - a.r < b.sy - b.r
	end)
	for i = 1, min(3, #order - 6) do
		local c = order[i]
		c.dieback = smooth(0.68 + 0.07 * i, 0.9 + 0.05 * i, age)
		if c.dieback > 0 then
			dying[#dying + 1] = c
			c.fade = 1 - 0.85 * c.dieback
		end
	end
end
stream(11)
if raggedness > 0 and #clusters > 5 then
	-- heavy shedding: the clump nearest the trunk and a few at the edge thin out, so the crown is gappy
	-- and limbs show
	local order = {}
	for _, c in ipairs(clusters) do
		order[#order + 1] = c
	end
	table.sort(order, function(a, b)
		return a.x * a.x + a.y * a.y < b.x * b.x + b.y * b.y
	end)
	order[1].fade = order[1].fade * (1 - 0.9 * raggedness)
	for k = 1, floor(max(2, #order * 0.25) + 0.5) do
		local c = order[#order - (k - 1) * 2 - floor(rand() * 2)]
		if c then
			c.fade = c.fade * (1 - 0.8 * raggedness)
		end
	end
end

-- Distance in meters from each node out to its nearest live tip, and that tip's clump.
local function reachOut(n)
	if n.dead then
		n.tipDist = 1e9
		return
	end
	if #n.kids == 0 then
		n.tipDist = 0
		return
	end
	n.tipDist = 1e9
	for _, k in ipairs(n.kids) do
		reachOut(k)
		local seg = sqrt((k.x - n.x) ^ 2 + (k.y - n.y) ^ 2 + (k.z - n.z) ^ 2)
		if k.tipDist + seg < n.tipDist then
			n.tipDist, n.clump = k.tipDist + seg, k.clump
		end
	end
end
reachOut(root)

-- ---- level of detail -----------------------------------------------------------------------------
-- A mesh is cut for one screen density, detailPxPerMeter (the camera's 8 px/m times its zoom), which the
-- engine sets to the def's <detailLevels> level being built; the game draws it from there up to the next
-- level's. Everything it draws is sized to what that density shows:
-- a mark (a leaf, or leaves merged into one) is about kMarkPx pixels long and never finer than
-- kFinestLeaf, and wood under kWoodPx pixels wide is left out. The structure (skeleton, clumps, light)
-- is the same at every density; only how finely it is cut changes.

if detailPxPerMeter == nil then
	error("deciduous.lua cuts its mesh for a screen density: give the def <detailLevels>")
end
local ppm = max(0.5, detailPxPerMeter)
-- kMarkPx is the 2.7 px the crown was cut at before trees grew 2.25x, grown with them, so below the
-- finest leaf a tree is that tree at its new size. kFinestLeaf grew less (from 0.053 m): it is as fine
-- as the oldest oak's finest mesh can be and stay under 65,535 vertices (uint16 indices).
local kMarkPx, kWoodPx = 6, 0.6
local kFinestLeaf, kCoarsestLeaf = 0.09, 0.25
local mark = max(kFinestLeaf, kMarkPx / ppm)
-- Leaves stop growing at kCoarsestLeaf (the mark at 24 px/m), past which they would overrun their
-- sprays; coarser marks are that layout's leaves merged, so every coarse level shows one layout and
-- keeps its outline and tone.
local leafLen = min(mark, kCoarsestLeaf)
local mergeReach = max(0, mark - leafLen) * 0.5

-- ---- foliage -------------------------------------------------------------------------------------
-- Leaves hang in sprays along the outer shoots, the last foliageDepth meters before each tip. Each
-- shoot carries sprays by its length (sprayDensity a meter for a leafSize leaf, more for a smaller
-- one), each spray a few leaf marks fanned outward from its clump. Spray density falls as the leaf
-- grows so the crown covers the same, and a larger leaf reaches back inward from where the finest
-- would end, so the outline stays put: the leaf size sets how finely the crown is cut, not how full
-- or how wide it is. A spray belongs to its shoot and draws from that shoot's stream, so as the tree
-- grows it stays where it was and new shoots bring their own in; a spray past the foliage depth fades
-- out, smallest first.

local sprayDensity = getFloat("sprayDensity", 9.8) * (getFloat("leafSize", 0.34) / leafLen) ^ 2
local leavesLo, leavesHi = getFloatRange("sprayLeaves", 4, 7)
local kMaxLeaves = 9
if floor(leavesHi + 0.999) > kMaxLeaves then
	error("deciduous.lua: sprayLeaves tops out at " .. kMaxLeaves .. " leaves a spray, the def asks for " .. leavesHi)
end
local foliageDepth = getFloat("foliageDepth", 0.26) * crownWidth + 0.135
local sprayReach = getFloat("sprayReach", 0.5) * rc
-- a young tree carries its leaves in a few sprays you can count; a thinning one has lost some
local thin = max(condition == "thinning" and 0.45 or 0, 0.4 * young)

local stemTop = stemRise * crownWidth
local zTop, halfWidth = stemTop + 1e-3, 1e-3
for _, n in ipairs(nodes) do
	if not n.dead then
		zTop = max(zTop, n.z)
		halfWidth = max(halfWidth, sqrt(n.x * n.x + n.y * n.y))
	end
end
local crownMidZ = (stemTop + zTop) * 0.5
local crownHalfH = max(0.05, (zTop - stemTop) * 0.5 + rc)
halfWidth = halfWidth + rc

-- Toward the light, from the upper left and a little behind.
local lx, ly, lz = normalize(-0.55, -0.25, 0.8)
-- Toward the camera; the depth of a point is how far it sits along this.
local function depthOf(x, y, z)
	return 0.87 * y + 0.5 * z
end

local leaves, sprays = {}, {}
for _, n in ipairs(nodes) do
	local p = n.parent
	if p and not n.dead and n.clump and n.tipDist < foliageDepth then
		local c = n.clump
		local seg = sqrt((n.x - p.x) ^ 2 + (n.y - p.y) ^ 2 + (n.z - p.z) ^ 2)
		local tip = #n.kids == 0
		-- a tip's sprays run on a little past it: leaves cluster at the end of a shoot
		local span = tip and 1.3 or 1
		local count = seg * span * sprayDensity
		stream(1000000 + n.id * 4)
		for k = 1, ceil(count) do
			local u = rand() * span
			local thr = rand()
			local ox, oy, oz = gauss(), gauss(), gauss()
			local reach = uniform(0.35, 1)
			local leafCount = floor(uniform(leavesLo, leavesHi + 0.999))
			local sprayJ, sprayTurn = uniform(-1, 1), uniform(-0.6, 0.6)
			local draws = {}
			for j = 1, kMaxLeaves * 5 do
				draws[j] = rand()
			end
			local bx, by, bz = p.x + (n.x - p.x) * u, p.y + (n.y - p.y) * u, p.z + (n.z - p.z) * u
			local dist = n.tipDist + max(0, 1 - u) * seg
			local w = 1 - smooth(0.35 * foliageDepth, foliageDepth, dist)
			-- the low inside of the crown is too shaded to hold leaves, so the boughs show there
			local radial = sqrt(bx * bx + by * by) / halfWidth
			local lift = (bz - stemTop) / (2 * crownHalfH)
			w = w * smooth(0.2, 0.6, radial + 1.2 * lift) * c.fade
			w = w * min(1, count - (k - 1))
			thr = thin + (1 - thin) * thr * 0.85
			local size = smooth(thr, thr + 0.2, w)
			if size > 0.05 then
				-- sprays hang around the shoot, pushed outward from their clump and up toward the light
				local ol = max(1e-6, sqrt(ox * ox + oy * oy + oz * oz))
				local cx, cy, cz = normalize(bx - c.x + 1e-6, by - c.y, bz - c.z + 0.3 * c.r)
				local r = sprayReach * reach
				local ax = bx + (ox / ol * 0.7 + cx * 0.5) * r
				local ay = by + (oy / ol * 0.7 + cy * 0.5) * r
				local az = bz + (oz / ol * 0.7 + cz * 0.5) * r
				-- light: the clump's own roundness and the crown's, then how deep in from the tip it sits
				local nx, ny, nz = normalize(ax - c.x + 1e-6, ay - c.y, az - c.z)
				local tx, ty, tz = normalize(ax / halfWidth + 1e-6, ay / halfWidth, (az - crownMidZ) / crownHalfH)
				nx, ny, nz = normalize(0.7 * nx + 0.3 * tx + 1e-6, 0.7 * ny + 0.3 * ty, 0.7 * nz + 0.3 * tz)
				local lit = nx * lx + ny * ly + nz * lz
				local h = clamp((az - stemTop) / (2 * crownHalfH), 0, 1)
				local inner = clamp(dist / foliageDepth, 0, 1)
				local tone = 0.47 + 0.5 * lit + 0.18 * (h - 0.5) - 0.4 * inner + 0.06 * sprayJ
				local sx, sy = project({ x = ax, y = ay, z = az })
				local fx, fy = project({ x = ax + nx, y = ay + ny, z = az + nz })
				local outAngle = math.atan(fy - sy, fx - sx) + sprayTurn
				local depth = depthOf(ax, ay, az)
				local back = depth < depthOf(bx, by, bz)
				local spray = { x = sx, y = sy, shoot = n }
				sprays[#sprays + 1] = spray
				for j = 1, leafCount do
					local d1, d2, d3, d4, d5 = draws[j * 5 - 4], draws[j * 5 - 3], draws[j * 5 - 2], draws[j * 5 - 1], draws[j * 5]
					local a = outAngle + (d1 - 0.5) * 2.6
					-- leaves hang: their fan tips toward the ground
					local ca, sa = cos(a), sin(a) + 0.35
					local al = sqrt(ca * ca + sa * sa)
					local len = leafLen * (0.75 + 0.45 * d2) * (0.4 + 0.6 * size)
					local spread = kFinestLeaf * 0.55
					-- the stalk sits back, so the tip lands most of the way back to where the finest leaf's would
					local sink = len * (1 - kFinestLeaf / leafLen) * 0.6
					ca, sa = ca / al, sa / al
					leaves[#leaves + 1] = {
						x = sx + (d3 - 0.5) * spread - ca * sink, y = sy + (d4 - 0.5) * spread * 0.8 - sa * sink,
						dx = ca, dy = sa, len = len, wide = 1.2 - 0.4 * d2,
						tone = tone + (d5 - 0.5) * 0.14,
						key = depth + 0.12 * tone - 0.25 * smooth(0.5, 0.8, inner) + (d5 - 0.5) * 0.01,
						back = back, jitter = sprayJ, spray = spray,
					}
				end
			end
		end
	end
end

-- A shoot that kept its leaves where its neighbours lost theirs is a speck beside the crown: a spray
-- with fewer than kCrowd leaves of other shoots within kLonely of it goes.
do
	local kLonely, kCrowd = 2 * leafLen, 8
	local grid = {}
	local function cellOf(x, y)
		return floor(x / kLonely) * 8192 + floor(y / kLonely)
	end
	for _, l in ipairs(leaves) do
		local k = cellOf(l.x, l.y)
		grid[k] = grid[k] or {}
		grid[k][#grid[k] + 1] = l
	end
	for _, s in ipairs(sprays) do
		local gx, gy, near = floor(s.x / kLonely), floor(s.y / kLonely), 0
		for i = -1, 1 do
			for j = -1, 1 do
				for _, l in ipairs(grid[(gx + i) * 8192 + gy + j] or {}) do
					if l.spray.shoot ~= s.shoot and (l.x - s.x) ^ 2 + (l.y - s.y) ^ 2 < kLonely * kLonely then
						near = near + 1
					end
				end
				if near >= kCrowd then
					break
				end
			end
			if near >= kCrowd then
				break
			end
		end
		s.lonely = near < kCrowd
	end
	local kept = {}
	for _, l in ipairs(leaves) do
		if not l.spray.lonely then
			kept[#kept + 1] = l
		end
	end
	leaves = kept
end

-- Palette: the species' four tones, turned for this individual (hue, saturation and value from its
-- seed, wide enough to tell neighbours apart); a leaf takes the tone its light calls for, nudged a
-- little by its spray.
stream(5)
local tones
do
	local hue, sat, val = uniform(-1, 1) * hueSpread, uniform(1 - satSpread, 1 + satSpread), uniform(1 - valueSpread, 1 + valueSpread)
	tones = {
		color.tone(shadeTone, val, hue, sat),
		color.tone(baseTone, val, hue, sat),
		color.tone(lightTone, val, hue, sat),
		color.tone(hiTone, val, hue, sat),
	}
	barkTone = color.tone(barkTone, uniform(0.9, 1.1), 0, 1)
	if condition == "yellowing" then
		for i = 1, 4 do
			tones[i] = color.mixLinear(tones[i], yellowing, 0.3)
		end
	end
end
local kBands = { 0.36, 0.58, 0.82 }
local toneCache = {}
local function leafColor(t, jitter)
	local band = 4
	for i = 1, 3 do
		if t < kBands[i] then
			band = i
			break
		end
	end
	local j = floor((jitter + 1) * 2.5) -- six steps of spray jitter, so colors repeat and cache
	local key = band * 8 + j
	if not toneCache[key] then
		toneCache[key] = color.shade(tones[band], 0.93 + 0.035 * j)
	end
	return toneCache[key]
end

table.sort(leaves, function(a, b)
	return a.key < b.key
end)

-- A leaf that the leaves drawn over it already hide is left out: walk them top first, on a grid a
-- fifth of a leaf across, and drop a leaf whose outline lands only on cells a leaf above has filled.
do
	local cell = leafLen * 0.2
	local filled = {}
	local function at(l, a, b)
		local x = l.x + (l.dx * a - l.dy * b) * l.len
		local y = l.y + (l.dy * a + l.dx * b) * l.len
		return floor(x / cell) * 8192 + floor(y / cell)
	end
	local kProbe = { { 0.08, 0 }, { 0.5, 0 }, { 0.92, 0 }, { 0.45, -0.24 }, { 0.45, 0.24 } }
	local kFill = { { 0.25, 0 }, { 0.45, 0 }, { 0.65, 0 }, { 0.45, -0.1 }, { 0.45, 0.1 } }
	for pass = 1, 2 do
		local back = pass == 2
		for i = #leaves, 1, -1 do
			local l = leaves[i]
			if l.back == back then
				local open = 0
				for _, p in ipairs(kProbe) do
					if not filled[at(l, p[1], p[2])] then
						open = open + 1
					end
				end
				l.hidden, l.shows = open == 0, open / #kProbe
				if open > 0 then
					for _, p in ipairs(kFill) do
						filled[at(l, p[1], p[2])] = true
					end
				end
			end
		end
	end
end

-- ---- marks -------------------------------------------------------------------------------------
-- What the foliage draws: every leaf that shows, or, where a mark is coarser than a leaf, leaves merged.
-- Taken in drawing order, a leaf joins the nearest mark on its side of the wood whose first leaf lies
-- within mergeReach of it on screen, or starts one. A merged mark is the outline of its leaves (their
-- reach from its first leaf in kSides directions) in the mean colour of those that show, drawn at their
-- mean depth: the same silhouette and tone as its leaves, as finely as the density can show.

local marks = {}
if mergeReach == 0 then
	for _, l in ipairs(leaves) do
		if not l.hidden then
			marks[#marks + 1] = l
		end
	end
else
	-- the reach outline overshoots its leaves more the more of them it spans, so it is drawn smaller
	local kSides, kShrink = 7, 1 - 0.3 * mergeReach / (mergeReach + leafLen)
	local dirX, dirY = {}, {}
	for k = 1, kSides do
		dirX[k], dirY[k] = cos((k - 1) * 2 * pi / kSides), sin((k - 1) * 2 * pi / kSides)
	end
	local grid, merged = {}, {}
	for _, l in ipairs(leaves) do
		local lx0, ly0 = l.x + l.dx * l.len * 0.5, l.y + l.dy * l.len * 0.5
		local gx, gy = floor(lx0 / mergeReach), floor(ly0 / mergeReach)
		local m, best = nil, mergeReach * mergeReach
		for i = -1, 1 do
			for j = -1, 1 do
				for _, c in ipairs(grid[(gx + i) * 8192 + gy + j] or {}) do
					local d = (c.x - lx0) ^ 2 + (c.y - ly0) ^ 2
					if c.back == l.back and d < best then
						m, best = c, d
					end
				end
			end
		end
		if not m then
			m = { x = lx0, y = ly0, back = l.back, reach = {}, lin = { 0, 0, 0 }, shown = 0, key = 0, count = 0, first = l }
			for k = 1, kSides do
				m.reach[k] = 0
			end
			local cell = gx * 8192 + gy
			grid[cell] = grid[cell] or {}
			grid[cell][#grid[cell] + 1] = m
			merged[#merged + 1] = m
		end
		local len, nx, ny = l.len, -l.dy * l.len * l.wide * 0.28, l.dx * l.len * l.wide * 0.28
		local sx, sy = l.x + l.dx * len * 0.42, l.y + l.dy * len * 0.42
		local ptsX = { l.x, l.x + l.dx * len, sx + nx, sx - nx }
		local ptsY = { l.y, l.y + l.dy * len, sy + ny, sy - ny }
		for p = 1, 4 do
			local ox, oy = ptsX[p] - m.x, ptsY[p] - m.y
			for k = 1, kSides do
				m.reach[k] = max(m.reach[k], ox * dirX[k] + oy * dirY[k])
			end
		end
		m.key, m.count = m.key + l.key, m.count + 1
		if not l.hidden then
			local lin, w = color.toLinear(leafColor(l.tone, l.jitter)), l.shows * len * len
			m.lin[1], m.lin[2], m.lin[3] = m.lin[1] + lin[1] * w, m.lin[2] + lin[2] * w, m.lin[3] + lin[3] * w
			m.shown = m.shown + w
		end
	end
	for _, m in ipairs(merged) do
		if m.count == 1 then
			if not m.first.hidden then
				marks[#marks + 1] = m.first
			end
		elseif m.shown > 0 then
			local xs, ys = {}, {}
			for k = 1, kSides do
				local r = max(leafLen * 0.2, m.reach[k] * kShrink)
				xs[k], ys[k] = m.x + dirX[k] * r, m.y + dirY[k] * r
			end
			local s = 1 / m.shown
			marks[#marks + 1] = {
				back = m.back, key = m.key / m.count, polygon = shape.polygon(xs, ys),
				color = color.toSrgb({ m.lin[1] * s, m.lin[2] * s, m.lin[3] * s }),
			}
		end
	end
	table.sort(marks, function(a, b)
		return a.key < b.key
	end)
end

-- ---- drawing -----------------------------------------------------------------------------------

local function drawMarks(back)
	for _, m in ipairs(marks) do
		if m.back == back then
			if m.polygon then
				shape.emit(m.polygon, m.color)
			else
				shape.emit(shape.leaf(m.x, m.y, m.dx, m.dy, m.len, m.wide, pointed), leafColor(m.tone, m.jitter))
			end
		end
	end
end

-- 1. contact shadow, once it is a pixel across
stream(6)
if trunkWidth * 1.4 * ppm >= 1 then
	lib.foot.contactShadow(trunkWidth, shadowTone, 0.18)
end

-- 2. the leaves on the far side of their shoots
drawMarks(true)

-- 3. the wood: one bark-coloured shape traced around the skeleton. Each axis follows a shoot out
-- through its strongest child, drawn as a strip that tapers by the pipe rule (about twice real, so it
-- reads at game zoom) and starts inside the axis it grows from. In one colour with no outline the
-- strips read as one shape. The near leaves cover it, so limbs show below the crown and through its
-- gaps. A limb narrower than kWoodPx pixels is left out (the trunk always reaches the crown), and a
-- strip keeps a point only kSegPx pixels on from the last it kept.
local minWidth = max(0.014, kWoodPx / ppm)
local kSegPx = 2
local function widthOf(n)
	return trunkWidth * 0.5 * sqrt(n.area / max(1, stem.area))
end
local function strongest(n)
	local best = nil
	for _, k in ipairs(n.kids) do
		if k.area > 0 and (best == nil or k.area > best.area) then
			best = k
		end
	end
	return best
end
local function strip(px, py, pw)
	local step = kSegPx / ppm
	local kx, ky, kw = { px[1] }, { py[1] }, { pw[1] }
	for i = 2, #px do
		local last = #kx
		if i == #px or (px[i] - kx[last]) ^ 2 + (py[i] - ky[last]) ^ 2 >= step * step then
			kx[last + 1], ky[last + 1], kw[last + 1] = px[i], py[i], pw[i]
		end
	end
	px, py, pw = kx, ky, kw
	local count = #px
	local left, right = {}, {}
	for i = 1, count do
		local ax, ay = px[max(1, i - 1)], py[max(1, i - 1)]
		local bx, by = px[min(count, i + 1)], py[min(count, i + 1)]
		local dx, dy = bx - ax, by - ay
		local l = max(1e-6, sqrt(dx * dx + dy * dy))
		local w = pw[i] * 0.5
		left[i] = { px[i] - dy / l * w, py[i] + dx / l * w }
		right[i] = { px[i] + dy / l * w, py[i] - dx / l * w }
	end
	shape.emit(shape.stripPart(left, right, 1, count), barkTone)
end
do
	local flare = smooth(0.3, 1, age)
	local queue = { { base = root, child = stem } }
	while #queue > 0 do
		local axis = table.remove(queue, 1)
		local trunk = axis.base == root
		local px, py, pw = {}, {}, {}
		if trunk then
			-- drawn narrower than the collision rect's trunk, with a crook partway up
			stream(10)
			local _, topY = project(stem)
			px[1], py[1], pw[1] = 0, 0, trunkWidth * (0.7 + 0.1 * flare)
			px[2], py[2], pw[2] = 0, -trunkWidth * 0.4, trunkWidth * 0.6
			px[3], py[3], pw[3] = uniform(-0.07, 0.07) * topY, topY * uniform(0.5, 0.65), trunkWidth * 0.55
		else
			px[1], py[1] = project(axis.base)
			pw[1] = widthOf(axis.child)
		end
		local n = axis.child
		while n and (n == stem or widthOf(n) >= minWidth * 0.7) do
			px[#px + 1], py[#py + 1] = project(n)
			pw[#pw + 1] = widthOf(n)
			local next = strongest(n)
			for _, k in ipairs(n.kids) do
				if k ~= next and k.area > 0 and widthOf(k) >= minWidth then
					queue[#queue + 1] = { base = n, child = k }
				end
			end
			n = next
		end
		if not n then
			pw[#pw] = pw[#pw] * 0.4 -- a shoot ends in a point, not a cut; one left out for its width ends where it was
		end
		strip(px, py, pw)
	end
	-- root spurs: a few short buttresses where the trunk meets the ground, longer as the tree ages,
	-- once they are a pixel wide
	stream(9)
	local spurs = trunkWidth * 0.22 * ppm >= 1 and floor(uniform(2, 3.999)) or 0
	for i = 1, spurs do
		local side = (i % 2 == 0) and 1 or -1
		local reach = trunkWidth * (0.1 + 0.3 * flare) * uniform(0.7, 1.1)
		local fx = side * trunkWidth * uniform(0.16, 0.26)
		local drop = trunkWidth * uniform(0.02, 0.1)
		strip({ fx, fx + side * reach * 0.6, fx + side * reach }, { -trunkWidth * (0.35 + 0.3 * flare), -trunkWidth * 0.08, drop },
			{ trunkWidth * 0.22, trunkWidth * 0.12, trunkWidth * 0.03 })
	end
	-- dieback: a dying clump's limb runs on bare past its leaves, to its highest tip and beyond, where
	-- it is wide enough to show
	local bare = {}
	for _, c in ipairs(dying) do
		if widthOf(c.node) * ppm >= kWoodPx then
			bare[#bare + 1] = c
		end
	end
	for _, c in ipairs(bare) do
		-- folding clumps together can leave tips from another subtree in c.tips; the limb only runs
		-- through the clump's own
		local function under(t)
			while t and t ~= c.node do
				t = t.parent
			end
			return t ~= nil
		end
		local tip
		for _, t in ipairs(c.tips) do
			if under(t) and (not tip or select(2, project(t)) < select(2, project(tip))) then
				tip = t
			end
		end
		if not tip then
			tip = c.node
		end
		local chain = { addNode(tip, tip.dx, tip.dy, max(tip.dz, 0.3), rc * 1.3 * c.dieback, 0, tip.order, tip.az) }
		local m = tip
		while m do
			table.insert(chain, 1, m)
			if m == c.node then
				break
			end
			m = m.parent
		end
		local px, py, pw = {}, {}, {}
		local w0 = widthOf(c.node)
		for i, node in ipairs(chain) do
			px[i], py[i] = project(node)
			pw[i] = max(minWidth, w0 * (1 - 0.6 * (i - 1) / max(1, #chain - 1)))
		end
		table.insert(px, 1, (project(c.node.parent or stem)))
		table.insert(py, 1, select(2, project(c.node.parent or stem)))
		table.insert(pw, 1, w0)
		strip(px, py, pw)
	end
end

-- 4. the leaves on the near side of their shoots, dark and deep first, lit last
drawMarks(false)
