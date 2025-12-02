#pragma once

// ChunkManager - Manages chunk loading, unloading, and caching.
// Loads chunks around the camera position and unloads distant chunks.
// Uses an LRU-like eviction strategy based on distance from camera.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/IWorldSampler.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine::world {

/// Manages chunk loading and caching based on camera position.
/// Chunks are loaded in a radius around the camera and unloaded when distant.
class ChunkManager {
  public:
	/// Create a chunk manager with the given world sampler
	explicit ChunkManager(std::unique_ptr<IWorldSampler> sampler);

	// Non-copyable
	ChunkManager(const ChunkManager&) = delete;
	ChunkManager& operator=(const ChunkManager&) = delete;

	// Movable
	ChunkManager(ChunkManager&&) = default;
	ChunkManager& operator=(ChunkManager&&) = default;

	~ChunkManager() = default;

	/// Update loaded chunks based on camera position.
	/// Loads new chunks within load radius, unloads chunks outside unload radius.
	/// @param cameraCenter World position of camera center
	void update(WorldPosition cameraCenter);

	/// Get a chunk by coordinate (returns nullptr if not loaded)
	[[nodiscard]] Chunk* getChunk(ChunkCoordinate coord);
	[[nodiscard]] const Chunk* getChunk(ChunkCoordinate coord) const;

	/// Get all currently loaded chunks
	[[nodiscard]] std::vector<Chunk*> getLoadedChunks();
	[[nodiscard]] std::vector<const Chunk*> getLoadedChunks() const;

	/// Get chunks visible within a world-space rectangle
	[[nodiscard]] std::vector<const Chunk*> getVisibleChunks(WorldPosition minCorner, WorldPosition maxCorner) const;

	/// Get number of loaded chunks
	[[nodiscard]] size_t loadedChunkCount() const { return m_chunks.size(); }

	/// Get the current center chunk (where camera is)
	[[nodiscard]] ChunkCoordinate centerChunk() const { return m_centerChunk; }

	/// Configuration
	void setLoadRadius(int32_t radius) { m_loadRadius = radius; }
	void setUnloadRadius(int32_t radius) { m_unloadRadius = radius; }
	[[nodiscard]] int32_t loadRadius() const { return m_loadRadius; }
	[[nodiscard]] int32_t unloadRadius() const { return m_unloadRadius; }

  private:
	std::unique_ptr<IWorldSampler> m_sampler;
	std::unordered_map<ChunkCoordinate, std::unique_ptr<Chunk>> m_chunks;
	ChunkCoordinate m_centerChunk{0, 0};

	// Load radius: chunks within this distance from center are loaded
	// Default: 2 chunks in each direction = 5×5 grid = 25 chunks
	int32_t m_loadRadius = 2;

	// Unload radius: chunks beyond this distance are unloaded
	// Default: 4 chunks = gives some hysteresis to prevent thrashing
	int32_t m_unloadRadius = 4;

	/// Load a single chunk
	void loadChunk(ChunkCoordinate coord);

	/// Unload chunks outside the unload radius
	void unloadDistantChunks(ChunkCoordinate center);
};

}  // namespace engine::world
