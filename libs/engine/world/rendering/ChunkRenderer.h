#pragma once

// ChunkRenderer - Renders chunks as colored ground tiles.
// Batches all visible tiles into a single draw call per frame.
// Uses per-tile visibility culling for efficiency.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/camera/WorldCamera.h"

#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <math/Types.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace engine::world {

/// Renders chunks as colored ground tiles.
/// Batches visible tiles into single draw call for performance.
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

	/// Per-frame geometry buffers (reused each frame, cleared at start)
	std::vector<Foundation::Vec2> m_vertices;    // Screen-space positions
	std::vector<Foundation::Color> m_colors;     // Per-vertex colors
	std::vector<uint16_t> m_indices;             // Triangle indices

	/// Add visible tiles from a chunk to the frame buffers
	void addChunkTiles(const Chunk& chunk, const WorldCamera& camera, const Foundation::Rect& visibleRect,
					   int viewportWidth, int viewportHeight);

	/// Add pure chunk as single quad
	void addPureChunk(const Chunk& chunk, const WorldCamera& camera,
					  int viewportWidth, int viewportHeight);
};

}  // namespace engine::world
