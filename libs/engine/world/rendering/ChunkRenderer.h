#pragma once

// ChunkRenderer - Renders chunks as colored ground tiles.
// Uses the Primitives API to batch-render visible tiles.
// For now, renders simple colored rectangles for each visible tile.
// Future: will spawn assets on tiles based on biome.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/camera/WorldCamera.h"

#include <graphics/Rect.h>

#include <algorithm>
#include <cstdint>

namespace engine::world {

/// Renders chunks as colored ground tiles.
/// Batches all visible tiles into efficient draw calls.
class ChunkRenderer {
  public:
	/// Create a chunk renderer
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	ChunkRenderer(float pixelsPerMeter = 16.0F);

	/// Render visible chunks
	/// @param chunkManager Chunk manager with loaded chunks
	/// @param camera Camera for visibility culling
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float pixelsPerMeter) { m_pixelsPerMeter = pixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return m_pixelsPerMeter; }

	/// Set tile resolution for rendering (how many tiles to skip when rendering)
	/// 1 = render every tile, 2 = skip every other tile, etc.
	/// Higher values = faster but lower quality
	void setTileResolution(int32_t resolution) { m_tileResolution = std::max(1, resolution); }
	[[nodiscard]] int32_t tileResolution() const { return m_tileResolution; }

	/// Get number of tiles rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastTileCount() const { return m_lastTileCount; }

  private:
	float m_pixelsPerMeter = 16.0F;
	int32_t m_tileResolution = 1;
	uint32_t m_lastTileCount = 0;

	/// Render a single chunk
	void renderChunk(const Chunk& chunk, const WorldCamera& camera, const Foundation::Rect& visibleRect,
					 int viewportWidth, int viewportHeight);
};

}  // namespace engine::world
