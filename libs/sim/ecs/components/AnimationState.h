#pragma once

namespace ecs {

	/// Per-entity animation clock. `phase` is the position in the current motion clip's cycle,
	/// wrapped to [0,1). Advanced from movement speed each frame by DynamicEntityRenderSystem,
	/// which evaluates the entity's motion clip at this phase into per-part transforms.
	struct AnimationState {
		float phase = 0.0F;
	};

} // namespace ecs
