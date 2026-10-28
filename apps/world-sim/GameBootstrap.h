#pragma once

namespace world_sim {

	/// From main after App::create, before the first scene: builds the terrain-surface tile atlas and starts the asset
	/// library load, synchronously when a --scene override jumps straight to a scene.
	void initializeResources(bool hasSceneArg);

	/// From main after App::shutdown: frees the tile atlas GL texture while the context is still alive.
	void shutdownResources();

} // namespace world_sim
