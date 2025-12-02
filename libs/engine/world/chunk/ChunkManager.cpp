#include "ChunkManager.h"

#include <utils/Log.h>

#include <algorithm>

namespace engine::world {

ChunkManager::ChunkManager(std::unique_ptr<IWorldSampler> sampler) : m_sampler(std::move(sampler)) {}

void ChunkManager::update(WorldPosition cameraCenter) {
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

	// Unload distant chunks
	if (newCenter != m_centerChunk) {
		unloadDistantChunks(newCenter);
		m_centerChunk = newCenter;
	}
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
	// Sample world data for this chunk
	ChunkSampleResult sampleResult = m_sampler->sampleChunk(coord);

	// Create chunk with sampled data
	auto chunk = std::make_unique<Chunk>(coord, std::move(sampleResult), m_sampler->getWorldSeed());

	LOG_DEBUG(Engine, "Loaded chunk (%d, %d) - %s", coord.x, coord.y, chunk->isPure() ? "pure" : "boundary");

	m_chunks[coord] = std::move(chunk);
}

void ChunkManager::unloadDistantChunks(ChunkCoordinate center) {
	// Collect chunks to unload
	std::vector<ChunkCoordinate> toUnload;

	for (const auto& [coord, chunk] : m_chunks) {
		if (coord.chebyshevDistance(center) > m_unloadRadius) {
			toUnload.push_back(coord);
		}
	}

	// Unload them
	for (const auto& coord : toUnload) {
		LOG_DEBUG(Engine, "Unloaded chunk (%d, %d)", coord.x, coord.y);
		m_chunks.erase(coord);
	}
}

}  // namespace engine::world
