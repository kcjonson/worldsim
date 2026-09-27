#include "ChunkManager.h"

#include <utils/Log.h>

#include <chrono>

namespace engine::world {

	ChunkManager::ChunkManager(std::unique_ptr<IWorldSampler> sampler)
		: m_sampler(std::move(sampler)) {}

	void ChunkManager::update(WorldPosition cameraCenter) {
		// Integrate any chunks whose generation worker finished
		pollGeneratedChunks();

		// Convert camera position to chunk coordinate
		ChunkCoordinate newCenter = worldToChunk(cameraCenter);

		// Load chunks in radius around camera
		for (int32_t dy = -m_loadRadius; dy <= m_loadRadius; ++dy) {
			for (int32_t dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
				ChunkCoordinate coord{newCenter.x + dx, newCenter.y + dy};
				if (m_chunks.find(coord) == m_chunks.end()) {
					loadChunk(coord);
				}
			}
		}

		// Unload distant chunks. Runs every update (not just on center change):
		// chunks are exempt from unloading while their generation worker is in
		// flight, so a center-change-only sweep could strand finished chunks
		// behind a camera that stopped moving.
		unloadDistantChunks(newCenter);
		m_centerChunk = newCenter;
	}

	Chunk* ChunkManager::getChunk(ChunkCoordinate coord) {
		auto it = m_chunks.find(coord);
		if (it != m_chunks.end()) {
			it->second->touch();
			return it->second.get();
		}
		return nullptr;
	}

	const Chunk* ChunkManager::getChunk(ChunkCoordinate coord) const {
		auto it = m_chunks.find(coord);
		if (it != m_chunks.end()) {
			it->second->touch();
			return it->second.get();
		}
		return nullptr;
	}

	std::vector<Chunk*> ChunkManager::getLoadedChunks() {
		std::vector<Chunk*> result;
		result.reserve(m_chunks.size());
		for (auto& [coord, chunk] : m_chunks) {
			result.push_back(chunk.get());
		}
		return result;
	}

	std::vector<const Chunk*> ChunkManager::getLoadedChunks() const {
		std::vector<const Chunk*> result;
		result.reserve(m_chunks.size());
		for (const auto& [coord, chunk] : m_chunks) {
			result.push_back(chunk.get());
		}
		return result;
	}

	std::vector<const Chunk*> ChunkManager::getVisibleChunks(WorldPosition minCorner, WorldPosition maxCorner) const {
		std::vector<const Chunk*> result;

		// Convert corners to chunk coordinates
		ChunkCoordinate minChunk = worldToChunk(minCorner);
		ChunkCoordinate maxChunk = worldToChunk(maxCorner);

		// Iterate over all potentially visible chunks
		for (int32_t cy = minChunk.y; cy <= maxChunk.y; ++cy) {
			for (int32_t cx = minChunk.x; cx <= maxChunk.x; ++cx) {
				const Chunk* chunk = getChunk({cx, cy});
				if (chunk != nullptr) {
					result.push_back(chunk);
				}
			}
		}

		return result;
	}

	void ChunkManager::loadChunk(ChunkCoordinate coord) {
		// Sample world data for this chunk (cheap; stays on the main thread)
		ChunkSampleResult sampleResult = m_sampler->sampleChunk(coord);

		// Create chunk with sampled data
		auto chunk = std::make_unique<Chunk>(coord, std::move(sampleResult), m_sampler->getWorldSeed());

		// Generate the 262k tiles on a worker thread: this takes tens of ms per
		// chunk and used to hitch the frame when scrolling crossed a chunk row.
		// Consumers gate on chunk->isReady(); pollGeneratedChunks() retires the
		// task once the worker finishes.
		Chunk* rawChunk = chunk.get();
		m_chunks[coord] = std::move(chunk);
		m_generating.emplace_back(coord, std::async(std::launch::async, [rawChunk]() { rawChunk->generate(); }));

		LOG_DEBUG(Engine, "Loading chunk (%d, %d)", coord.x, coord.y);
	}

	void ChunkManager::pollGeneratedChunks() {
		for (auto it = m_generating.begin(); it != m_generating.end();) {
			auto& [coord, future] = *it;
			if (future.valid() && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				future.get();
				LOG_DEBUG(Engine, "Loaded chunk (%d, %d)", coord.x, coord.y);
				it = m_generating.erase(it);
			} else {
				++it;
			}
		}
	}

	void ChunkManager::finishPendingGeneration() {
		for (auto& [coord, future] : m_generating) {
			if (future.valid()) {
				future.get();
			}
		}
		m_generating.clear();
	}

	bool ChunkManager::isGenerating(ChunkCoordinate coord) const {
		for (const auto& [generatingCoord, future] : m_generating) {
			if (generatingCoord == coord) {
				return true;
			}
		}
		return false;
	}

	void ChunkManager::unloadDistantChunks(ChunkCoordinate center) {
		// Collect chunks to unload
		std::vector<ChunkCoordinate> toUnload;

		for (const auto& [coord, chunk] : m_chunks) {
			// Never unload a chunk whose generation worker still references it
			if (coord.chebyshevDistance(center) > m_unloadRadius && !isGenerating(coord)) {
				toUnload.push_back(coord);
			}
		}

		// Unload them
		for (const auto& coord : toUnload) {
			LOG_DEBUG(Engine, "Unloaded chunk (%d, %d)", coord.x, coord.y);
			m_chunks.erase(coord);
		}
	}

} // namespace engine::world
