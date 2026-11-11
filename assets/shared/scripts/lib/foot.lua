-- Prelude: where a plant meets the ground, in lib.foot. Meters, +Y down, the foot at the origin.

lib = lib or {}
lib.foot = {}

-- A small lobed contact shadow under a trunk w wide: about 1.4 w across, nudged away from the upper-left
-- light, in color c at alpha (under the silhouette threshold, so it is drawn but not clicked).
function lib.foot.contactShadow(w, c, alpha)
	local p = lib.shape.lobed(w * 0.12, w * 0.04, w * 0.7, 7, 0.82, 0.12, math.random() * math.pi, 0.62)
	lib.shape.emit(p, c, alpha)
end
