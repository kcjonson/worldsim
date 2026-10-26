#pragma once

namespace world_sim {

	/// AppConfig::initializeResources hook: builds the terrain-surface tile atlas and starts the asset library load.
	void initializeResources(bool hasSceneArg);

	/// AppConfig::shutdownResources hook: frees the tile atlas GL texture while the context is still alive.
	void shutdownResources();

} // namespace world_sim
