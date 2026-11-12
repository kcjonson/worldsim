#pragma once

// ChunkManager - Manages chunk loading, unloading, and caching.
// Chunks load around the camera and around simulation anchors (every active
// mover), and unload only when neither the camera nor any anchor holds them.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/IWorldSampler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace world {

/// A simulation anchor: a mover and its own range in metres (its sight distance plus a buffer).
struct ChunkAnchor {
	WorldPosition position;
	float		  rangeMeters = 0.0F;
};

/// Manages chunk loading and caching around the camera and simulation anchors.
/// The camera loads chunks within loadRadius() chunks of its own and keeps them
/// within unloadRadius(). Each anchor loads every chunk within anchorLoadRange()
/// of its range and keeps every chunk within anchorKeepRange(). A chunk unloads
/// only when nothing holds it and its generation is not in flight.
class ChunkManager {
  public:
	/// Floors of an anchor's load and keep ranges: chunks hold the placement data read
	/// for remembered targets, so no anchor holds less than this.
	static constexpr float kMinAnchorLoadRangeMeters = 128.0F;
	static constexpr float kMinAnchorKeepRangeMeters = 256.0F;

	/// The metres an anchor of range R loads (the larger of the floor and R) and keeps
	/// (the larger of the floor and 2R), so keep is never below load.
	[[nodiscard]] static float anchorLoadRange(float rangeMeters) { return std::max(kMinAnchorLoadRangeMeters, rangeMeters); }
	[[nodiscard]] static float anchorKeepRange(float rangeMeters) { return std::max(kMinAnchorKeepRangeMeters, 2.0F * rangeMeters); }

	/// Create a chunk manager with the given world sampler; its chunks lay the forest floor by
	/// `canopyCover` (none by default).
	explicit ChunkManager(std::unique_ptr<IWorldSampler> worldSampler, grove::CanopyCover canopyCover = {});

	// Non-copyable
	ChunkManager(const ChunkManager&) = delete;
	ChunkManager& operator=(const ChunkManager&) = delete;

	// Movable
	ChunkManager(ChunkManager&&) = default;
	ChunkManager& operator=(ChunkManager&&) = default;

	~ChunkManager() = default;

	/// Load what the camera and the anchors reach, unload what none of them holds.
	/// Tile generation runs on worker threads; chunks become isReady() over the
	/// next few updates. Runs every frame; the anchors are reduced to the chunks
	/// they cover before any map lookup, so many anchors in one chunk cost one.
	/// @param cameraCenter World position of camera center
	/// @param anchors The simulation anchors (movers), each with its own range
	void update(WorldPosition cameraCenter, std::span<const ChunkAnchor> anchors = {});

	/// Block until all in-flight generation completes.
	/// For tests and loading flows; gameplay uses incremental polling in update().
	void finishPendingGeneration();

	/// Get a chunk by coordinate (returns nullptr if not loaded)
	[[nodiscard]] Chunk* getChunk(ChunkCoordinate coord);
	[[nodiscard]] const Chunk* getChunk(ChunkCoordinate coord) const;

	/// Get all currently loaded chunks
	[[nodiscard]] std::vector<Chunk*> getLoadedChunks();
	[[nodiscard]] std::vector<const Chunk*> getLoadedChunks() const;

	/// Get chunks visible within a world-space rectangle
	[[nodiscard]] std::vector<const Chunk*> getVisibleChunks(WorldPosition minCorner, WorldPosition maxCorner) const;

	/// Get number of loaded chunks
	[[nodiscard]] size_t loadedChunkCount() const { return chunks.size(); }

	/// The seed the world sampler generates from
	[[nodiscard]] uint64_t worldSeed() const { return sampler->getWorldSeed(); }

	/// The camera's chunk as of the last update
	[[nodiscard]] ChunkCoordinate centerChunk() const { return cameraChunk; }

	/// Camera radii, in chunks (Chebyshev). Anchor ranges are in world metres,
	/// measured per axis from the anchor to the chunk's square.
	void setLoadRadius(int32_t radius) { cameraLoadRadius = radius; }
	void setUnloadRadius(int32_t radius) { cameraUnloadRadius = radius; }
	[[nodiscard]] int32_t loadRadius() const { return cameraLoadRadius; }
	[[nodiscard]] int32_t unloadRadius() const { return cameraUnloadRadius; }

  private:
	std::unique_ptr<IWorldSampler>								sampler;
	grove::CanopyCover											canopyCover;
	std::unordered_map<ChunkCoordinate, std::unique_ptr<Chunk>> chunks;
	ChunkCoordinate												cameraChunk{0, 0};

	// Default 2 chunks in each direction = 5x5 grid
	int32_t cameraLoadRadius = 2;

	// Larger than the load radius so a camera pacing a chunk border doesn't thrash
	int32_t cameraUnloadRadius = 4;

	/// Chunks the anchors load and keep this update, sorted and unique. Members so
	/// the per-frame reduction reuses its storage.
	std::vector<ChunkCoordinate> anchorLoadChunks;
	std::vector<ChunkCoordinate> anchorKeepChunks;

	/// An in-flight tile generation task.
	struct Generation {
		ChunkCoordinate						  coord;
		std::future<void>					  task;
		std::chrono::steady_clock::time_point requested;
	};

	/// In-flight tile generation tasks. Chunks are inserted into chunks
	/// immediately but stay !isReady() until their worker finishes; consumers
	/// gate on isReady(). Chunks in this list must not be unloaded.
	std::vector<Generation> generating;

	/// Load a single chunk (tile generation runs on a worker thread)
	void loadChunk(ChunkCoordinate coord);

	/// Retire finished generation tasks
	void pollGeneratedChunks();

	/// Whether a chunk's generation task is still in flight
	[[nodiscard]] bool isGenerating(ChunkCoordinate coord) const;

	/// Reduce the anchors to anchorLoadChunks and anchorKeepChunks
	void gatherAnchorChunks(std::span<const ChunkAnchor> anchors);

	/// Unload chunks neither the camera nor an anchor holds
	void unloadUnheldChunks();
};

}  // namespace world
