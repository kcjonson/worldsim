#pragma once

// Shared per-frame inputs for the entity render paths. The orchestrator
// (EntityRenderer::renderInstanced) builds one RenderContext after bumping the
// frame counter and threads it through every sub-renderer, so they all see the
// same camera, viewport, and frame. Mutable inout state (uniforms, stats) stays
// in separate by-reference params; this struct is read-only.

#include "assets/placement/PlacementExecutor.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/ChunkCoordinate.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace engine::world {

/// Read-only per-frame render inputs shared by all entity render paths.
struct RenderContext {
	const assets::PlacementExecutor&		   executor;
	const std::unordered_set<ChunkCoordinate>& processedChunks;
	const std::vector<assets::PlacedEntity>*   dynamicEntities; // may be null
	const WorldCamera&						   camera;
	int										   viewportWidth = 0;
	int										   viewportHeight = 0;
	float									   pixelsPerMeter = 16.0F;
	uint64_t								   frameCounter = 0;
};

/// Visible world bounds (meters) for sub-chunk / entity culling.
struct VisibleBounds {
	float minX = 0, maxX = 0, minY = 0, maxY = 0;
};

/// Compute the visible world rect from camera + viewport, padded by `margin`
/// meters so entities straddling the screen edge aren't culled. Identical math
/// across all render paths (was copy-pasted; now one source of truth).
[[nodiscard]] inline VisibleBounds
computeVisibleBounds(const WorldCamera& camera, int vpW, int vpH, float pixelsPerMeter, float margin = 2.0F) {
	const float camX = camera.position().x;
	const float camY = camera.position().y;
	const float scale = pixelsPerMeter * camera.zoom();
	const float viewWorldW = static_cast<float>(vpW) / scale;
	const float viewWorldH = static_cast<float>(vpH) / scale;
	return {
		camX - (viewWorldW * 0.5F) - margin,
		camX + (viewWorldW * 0.5F) + margin,
		camY - (viewWorldH * 0.5F) - margin,
		camY + (viewWorldH * 0.5F) + margin
	};
}

}  // namespace engine::world
