#include "ChunkManager.h"

#include <utils/Log.h>

#include <algorithm>

#include "world/chunk/TileAdjacency.h"

namespace engine::world {

	ChunkManager::ChunkManager(std::unique_ptr<IWorldSampler> sampler)
		: m_sampler(std::move(sampler)) {}

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
		// Sample world data for this chunk
		ChunkSampleResult sampleResult = m_sampler->sampleChunk(coord);

		// Create chunk with sampled data
		auto chunk = std::make_unique<Chunk>(coord, std::move(sampleResult), m_sampler->getWorldSeed());

		// Pre-compute all tiles (fills the flat array)
		chunk->generate();

		LOG_DEBUG(Engine, "Loaded chunk (%d, %d)", coord.x, coord.y);

		m_chunks[coord] = std::move(chunk);

		// Now that the chunk exists, refresh adjacency for it and its neighbors so edges cross chunk boundaries
		refreshAdjacencyAround(coord);
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

	void ChunkManager::refreshAdjacencyForChunkBoundary(ChunkCoordinate coord) {
		Chunk* chunk = getChunk(coord);
		if (chunk == nullptr || !chunk->isReady()) {
			return;
		}

		auto sampleSurface = [&](int localX, int localY) -> uint8_t {
			ChunkCoordinate target = coord;
			int tx = localX;
			int ty = localY;

			if (tx < 0) {
				target.x -= 1;
				tx += kChunkSize;
			} else if (tx >= kChunkSize) {
				target.x += 1;
				tx -= kChunkSize;
			}

			if (ty < 0) {
				target.y -= 1;
				ty += kChunkSize;
			} else if (ty >= kChunkSize) {
				target.y += 1;
				ty -= kChunkSize;
			}

			const Chunk* neighbor = getChunk(target);
			if (neighbor == nullptr || !neighbor->isReady()) {
				// Fallback: use the current chunk's edge tile to avoid fake edge strokes
				tx = std::clamp(localX, 0, kChunkSize - 1);
				ty = std::clamp(localY, 0, kChunkSize - 1);
				return static_cast<uint8_t>(chunk->getTile(static_cast<uint16_t>(tx), static_cast<uint16_t>(ty)).surface);
			}

			return static_cast<uint8_t>(neighbor->getTile(static_cast<uint16_t>(tx), static_cast<uint16_t>(ty)).surface);
		};

		auto recomputeTileAdjacency = [&](int x, int y) {
			uint64_t adj = 0;
			TileAdjacency::setNeighbor(adj, TileAdjacency::NW, sampleSurface(x - 1, y - 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::W, sampleSurface(x - 1, y));
			TileAdjacency::setNeighbor(adj, TileAdjacency::SW, sampleSurface(x - 1, y + 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::S, sampleSurface(x, y + 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::SE, sampleSurface(x + 1, y + 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::E, sampleSurface(x + 1, y));
			TileAdjacency::setNeighbor(adj, TileAdjacency::NE, sampleSurface(x + 1, y - 1));
			TileAdjacency::setNeighbor(adj, TileAdjacency::N, sampleSurface(x, y - 1));

			chunk->setAdjacency(static_cast<uint16_t>(x), static_cast<uint16_t>(y), adj);
		};

		// Iterate only boundary tiles directly (more efficient than checking every tile)
		// Top row (y=0)
		for (int x = 0; x < kChunkSize; ++x) {
			recomputeTileAdjacency(x, 0);
		}
		// Bottom row (y=kChunkSize-1)
		for (int x = 0; x < kChunkSize; ++x) {
			recomputeTileAdjacency(x, kChunkSize - 1);
		}
		// Left column (excluding corners already handled)
		for (int y = 1; y < kChunkSize - 1; ++y) {
			recomputeTileAdjacency(0, y);
		}
		// Right column (excluding corners already handled)
		for (int y = 1; y < kChunkSize - 1; ++y) {
			recomputeTileAdjacency(kChunkSize - 1, y);
		}
	}

	void ChunkManager::refreshAdjacencyAround(ChunkCoordinate coord) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				refreshAdjacencyForChunkBoundary({coord.x + dx, coord.y + dy});
			}
		}
	}

} // namespace engine::world
