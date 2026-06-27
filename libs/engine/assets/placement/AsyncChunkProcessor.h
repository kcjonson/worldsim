#pragma once

// AsyncChunkProcessor - Manages async entity placement tasks
// Shared between GameLoadingScene (bulk initial loading) and GameScene (runtime streaming)
//
// Each worker task computes entity placement AND bakes the entities into
// world-space vertex arrays (the expensive part of static entity rendering).
// The render thread only uploads the finished arrays to the GPU, so chunks
// scrolling into view never stall the frame on a synchronous bake.

#include "PlacementExecutor.h"

#include <assets/AssetRegistry.h>

#include <utils/Log.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/rendering/BakedEntityMesh.h>

#include <chrono>
#include <future>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::assets {

	/// Snapshot of chunk tile data for thread-safe async processing.
	/// Captures biome/surface data so async tasks don't access Chunk directly.
	/// Surfaces stay as enums; the string conversion placement rules expect
	/// happens lazily on the worker (a per-tile string here cost ~10ms of main
	/// thread per chunk launch).
	struct ChunkDataSnapshot {
		world::ChunkCoordinate		coord;
		std::vector<world::Biome>	biomes;
		std::vector<world::Surface> surfaces;
	};

	/// Capture chunk tile data for thread-safe async processing.
	/// Creates a snapshot to avoid concurrent access to Chunk objects.
	inline ChunkDataSnapshot captureChunkData(const world::Chunk* chunk) {
		ChunkDataSnapshot snapshot;
		snapshot.coord = chunk->coordinate();

		const size_t tileCount = world::kChunkSize * world::kChunkSize;
		snapshot.biomes.reserve(tileCount);
		snapshot.surfaces.reserve(tileCount);

		for (uint16_t y = 0; y < world::kChunkSize; ++y) {
			for (uint16_t x = 0; x < world::kChunkSize; ++x) {
				const auto& tile = chunk->getTile(x, y);
				snapshot.biomes.push_back(tile.primaryBiome);
				snapshot.surfaces.push_back(tile.surface);
			}
		}

		return snapshot;
	}

	/// Result of one worker task: placement plus the CPU-side entity mesh bake.
	struct ChunkTaskResult {
		AsyncChunkPlacementResult placement;
		world::BakedChunkCPUData  bakedMesh;
	};

	/// Manages async entity placement tasks for chunk processing.
	/// Handles launching, polling, and integrating async computation results.
	class AsyncChunkProcessor {
	  public:
		/// Create processor with references to placement system
		/// @param executor PlacementExecutor for entity computation
		/// @param worldSeed World seed for deterministic placement
		/// @param processedChunks Set to track which chunks have been processed
		AsyncChunkProcessor(PlacementExecutor& executor, uint64_t worldSeed, std::unordered_set<world::ChunkCoordinate>& processedChunks)
			: m_executor(executor),
			  m_worldSeed(worldSeed),
			  m_processedChunks(processedChunks) {}

		/// Launch an async task for a single chunk
		/// @param chunk The chunk to process
		void launchTask(const world::Chunk* chunk) {
			// Tile generation may still be running on a worker; try again next frame
			if (!chunk->isReady()) {
				return;
			}

			auto coord = chunk->coordinate();

			// Skip if already processed or in progress
			if (m_executor.getChunkIndex(coord) != nullptr) {
				return;
			}
			if (m_chunksInProgress.find(coord) != m_chunksInProgress.end()) {
				return;
			}

			m_chunksInProgress.insert(coord);

			// Capture chunk data for thread safety
			auto chunkData = captureChunkData(chunk);

			// Capture by value for the async lambda
			auto*	 executor = &m_executor;
			uint64_t seed = m_worldSeed;

			auto future = std::async(std::launch::async, [executor, seed, chunkData = std::move(chunkData)]() {
				ChunkPlacementContext ctx;
				ctx.coord = chunkData.coord;
				ctx.worldSeed = seed;
				ctx.getBiome = [&chunkData](uint16_t x, uint16_t y) {
					return chunkData.biomes[y * world::kChunkSize + x];
				};
				ctx.getSurface = [&chunkData](uint16_t x, uint16_t y) {
					return world::surfaceToString(chunkData.surfaces[y * world::kChunkSize + x]);
				};

				ChunkTaskResult result;
				result.placement = executor->computeChunkEntities(ctx, executor);

				// Bake the entity meshes here on the worker: the entity list is
				// task-local and AssetRegistry::getTemplate is thread-safe, so the
				// render thread only has to upload the finished arrays.
				std::vector<const PlacedEntity*> entityPtrs;
				entityPtrs.reserve(result.placement.entities.size());
				for (const auto& entity : result.placement.entities) {
					entityPtrs.push_back(&entity);
				}
				result.bakedMesh = world::bakeChunkEntities(
					entityPtrs, chunkData.coord,
					[](const std::string& defName) -> const renderer::TessellatedMesh* {
						if (world::isGroundcoverDef(defName)) {
							return nullptr; // groundcover renders via the instanced path, not baking
						}
						return AssetRegistry::Get().getTemplate(defName);
					}
				);

				return result;
			});

			m_pendingFutures.emplace_back(coord, std::move(future));
		}

		/// Launch async tasks for multiple chunks
		/// @param chunks List of chunks to process
		void launchTasks(const std::vector<const world::Chunk*>& chunks) {
			for (const auto* chunk : chunks) {
				launchTask(chunk);
			}
		}

		/// Poll for completed async tasks and integrate results (non-blocking)
		/// @return Number of tasks completed this call
		size_t pollCompleted() {
			size_t completed = 0;

			for (auto it = m_pendingFutures.begin(); it != m_pendingFutures.end();) {
				auto& [coord, future] = *it;

				// Skip invalid futures (should not happen, but defensive)
				if (!future.valid()) {
					m_chunksInProgress.erase(coord);
					it = m_pendingFutures.erase(it);
					continue;
				}

				// Non-blocking check if future is ready
				if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
					// Get result and store on main thread
					auto result = future.get();
					m_executor.storeChunkResult(std::move(result.placement));
					m_readyBakes.emplace_back(coord, std::move(result.bakedMesh));
					m_processedChunks.insert(coord);
					m_chunksInProgress.erase(coord);
					completed++;

					it = m_pendingFutures.erase(it);
				} else {
					++it;
				}
			}

			return completed;
		}

		/// Wait for all pending tasks to complete (blocking)
		void waitAll() {
			for (auto& [coord, future] : m_pendingFutures) {
				if (future.valid()) {
					auto result = future.get();
					m_executor.storeChunkResult(std::move(result.placement));
					m_readyBakes.emplace_back(coord, std::move(result.bakedMesh));
					m_processedChunks.insert(coord);
					m_chunksInProgress.erase(coord);
				}
			}
			m_pendingFutures.clear();
		}

		/// Take baked entity meshes ready for GPU upload (call on render thread,
		/// hand each to EntityRenderer::uploadBakedChunk)
		[[nodiscard]] std::vector<std::pair<world::ChunkCoordinate, world::BakedChunkCPUData>> takeReadyBakes() {
			return std::exchange(m_readyBakes, {});
		}

		/// Clear all pending tasks (waits for completion to avoid dangling references)
		void clear() {
			waitAll();
			m_chunksInProgress.clear();
		}

		/// Get number of tasks currently pending
		[[nodiscard]] size_t pendingCount() const { return m_pendingFutures.size(); }

		/// Check if there are any pending tasks
		[[nodiscard]] bool hasPending() const { return !m_pendingFutures.empty(); }

		/// Check if a chunk is currently being processed
		[[nodiscard]] bool isProcessing(world::ChunkCoordinate coord) const {
			return m_chunksInProgress.find(coord) != m_chunksInProgress.end();
		}

	  private:
		PlacementExecutor&							m_executor;
		uint64_t									m_worldSeed;
		std::unordered_set<world::ChunkCoordinate>& m_processedChunks;

		// Async state
		std::unordered_set<world::ChunkCoordinate>									 m_chunksInProgress;
		std::vector<std::pair<world::ChunkCoordinate, std::future<ChunkTaskResult>>> m_pendingFutures;
		std::vector<std::pair<world::ChunkCoordinate, world::BakedChunkCPUData>>	 m_readyBakes;
	};

} // namespace engine::assets
