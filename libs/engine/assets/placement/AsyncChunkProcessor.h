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

#include <core/Vec2i64.h>
#include <utils/Log.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/RenderTiles.h>
#include <world/chunk/SurfaceField.h>
#include <world/chunk/TerrainPolygonQuery.h>
#include <world/rendering/BakedEntityMesh.h>

#include <chrono>
#include <future>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace engine::assets {

	/// Snapshot of chunk data for thread-safe async processing, so async tasks
	/// don't access the Chunk (it may unload first): the seed of the world the
	/// chunk belongs to, which placement is rolled from; biomes by tile; the render
	/// tiles and parameters the land field reads (SurfaceField, D16), the
	/// parameters as the game thread's tunables hold them; the terrain polygons
	/// that say where water is (D1), shared rather than copied.
	struct ChunkDataSnapshot {
		world::ChunkCoordinate							   coord;
		uint64_t										   worldSeed = 0;
		std::vector<world::Biome>						   biomes;
		std::vector<world::TileRenderData>				   renderTiles;
		world::SurfaceFieldParams						   landParams;
		std::shared_ptr<const world::ChunkTerrainPolygons> terrainPolygons;
	};

	/// Capture chunk data for thread-safe async processing (game thread).
	inline ChunkDataSnapshot captureChunkData(const world::Chunk* chunk) {
		ChunkDataSnapshot snapshot;
		snapshot.coord = chunk->coordinate();
		snapshot.worldSeed = chunk->worldSeed();

		const size_t tileCount = world::kChunkSize * world::kChunkSize;
		snapshot.biomes.reserve(tileCount);

		for (uint16_t y = 0; y < world::kChunkSize; ++y) {
			for (uint16_t x = 0; x < world::kChunkSize; ++x) {
				snapshot.biomes.push_back(chunk->getTile(x, y).primaryBiome);
			}
		}

		const world::TileRenderData* renderTiles = chunk->renderData();
		snapshot.renderTiles.assign(renderTiles, renderTiles + static_cast<size_t>(world::kRenderTilesSide) * world::kRenderTilesSide);
		snapshot.landParams = world::SurfaceFieldTunables::get().params(snapshot.worldSeed);
		snapshot.terrainPolygons = chunk->sharedTerrainPolygons();
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
		/// Create processor with references to placement system. Placement is rolled
		/// from each chunk's own world seed (Chunk::worldSeed), so a chunk is laid out
		/// the same whichever scene, and whichever load, places it.
		/// @param executor PlacementExecutor for entity computation
		/// @param processedChunks Set to track which chunks have been processed
		AsyncChunkProcessor(PlacementExecutor& executor, std::unordered_set<world::ChunkCoordinate>& processedChunks)
			: m_executor(executor),
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
			auto* executor = &m_executor;

			auto future = std::async(std::launch::async, [executor, chunkData = std::move(chunkData)]() {
				const world::TerrainPolygonQuery water(*chunkData.terrainPolygons);
				const world::RenderTileView		 land = world::chunkRenderTiles(chunkData.coord, chunkData.renderTiles);

				ChunkPlacementContext ctx;
				ctx.coord = chunkData.coord;
				ctx.worldSeed = chunkData.worldSeed;
				ctx.getBiome = [&chunkData](uint16_t x, uint16_t y) {
					return chunkData.biomes[y * world::kChunkSize + x];
				};
				ctx.isWater = [&water](glm::vec2 pos) { return water.isInsideWater(geometry::quantize(pos)); };
				ctx.landSurface = [&land, &chunkData](glm::vec2 pos) {
					return world::evaluateSurfaceField(land, world::tilePointOf(pos.x, pos.y), chunkData.landParams).surface;
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
		std::unordered_set<world::ChunkCoordinate>& m_processedChunks;

		// Async state
		std::unordered_set<world::ChunkCoordinate>									 m_chunksInProgress;
		std::vector<std::pair<world::ChunkCoordinate, std::future<ChunkTaskResult>>> m_pendingFutures;
		std::vector<std::pair<world::ChunkCoordinate, world::BakedChunkCPUData>>	 m_readyBakes;
	};

} // namespace engine::assets
