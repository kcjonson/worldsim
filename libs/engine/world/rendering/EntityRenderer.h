#pragma once

// EntityRenderer - Renders placed entities on top of chunk tiles.
// Uses AssetBatcher to batch entity instances for efficient rendering.
// Transforms world coordinates to screen space accounting for camera.

#include "assets/AssetBatcher.h"
#include "assets/placement/PlacementExecutor.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/Chunk.h"

#include <unordered_map>
#include <unordered_set>

namespace engine::world {

/// Renders entities placed by the PlacementExecutor.
/// Groups entities by asset type and batches them for efficient rendering.
class EntityRenderer {
  public:
	/// Create an entity renderer
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	explicit EntityRenderer(float pixelsPerMeter = 16.0F);

	/// Render entities from processed chunks
	/// @param executor PlacementExecutor containing entity data
	/// @param processedChunks Set of chunk coordinates that have been processed
	/// @param camera Camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const assets::PlacementExecutor& executor,
				const std::unordered_set<ChunkCoordinate>& processedChunks,
				const WorldCamera& camera,
				int viewportWidth,
				int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float pixelsPerMeter) { m_pixelsPerMeter = pixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return m_pixelsPerMeter; }

	/// Get number of entities rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastEntityCount() const { return m_lastEntityCount; }

  private:
	float m_pixelsPerMeter = 16.0F;
	assets::AssetBatcher m_batcher;
	uint32_t m_lastEntityCount = 0;

	// Cache for template meshes (keyed by defName)
	std::unordered_map<std::string, const renderer::TessellatedMesh*> m_templateCache;

	/// Get or cache a template mesh
	const renderer::TessellatedMesh* getTemplate(const std::string& defName);
};

}  // namespace engine::world
