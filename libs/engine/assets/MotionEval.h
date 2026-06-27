#pragma once

// Evaluate a motion clip at a phase into per-part transforms. Pure (no rendering): the renderer
// (game or asset-manager preview) applies the resulting PartTransforms to a mesh's part vertex
// ranges. Output pivots/translates are in the asset's meter (template) space; a caller drawing
// in a different space (e.g. a fitted preview) converts pivot/translate into that space.

#include "assets/MotionDef.h"

#include <string>
#include <unordered_map>

namespace engine::assets {

	/// Evaluate every driver of `clip` at `phase` (wrapped to [0,1)) and accumulate, per part id,
	/// a PartTransform. value = amp * wave(2*pi*(freq*phase + phaseOffset)); rotation accumulates
	/// into `rotation` (radians), posX/posY into `translate`, scaleX/scaleY into `scale` (around 1).
	void evaluateClip(const MotionClip& clip, float phase, std::unordered_map<std::string, PartTransform>& out);

} // namespace engine::assets
