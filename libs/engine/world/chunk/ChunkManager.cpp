#include "ChunkManager.h"

#include <utils/Log.h>

#include <algorithm>
#include <chrono>

#include "world/chunk/TileAdjacency.h"

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
		// Sample world data for this chunk (cheap; stays on the main thread)
		ChunkSampleResult sampleResult = m_sampler->sampleChunk(coord);

		// Create chunk with sampled data
		auto chunk = std::make_unique<Chunk>(coord, std::move(sampleResult), m_sampler->getWorldSeed());

		// Generate the 262k tiles on a worker thread: this takes tens of ms per
		// chunk and used to hitch the frame when scrolling crossed a chunk row.
		// Consumers gate on chunk->isReady(); adjacency refresh happens in
		// pollGeneratedChunks() once the worker finishes.
		Chunk* rawChunk = chunk.get();
		m_chunks[coord] = std::move(chunk);
		m_generating.emplace_back(coord, std::async(std::launch::async, [rawChunk]() { rawChunk->generate(); }));

		LOG_DEBUG(Engine, "Loading chunk (%d, %d)", coord.x, coord.y);
	}

	void ChunkManager::pollGeneratedChunks() {
		// Integrate at most one chunk per update: border stitching costs a few
		// ms per chunk, and several workers finishing at once (crossing a chunk
		// row loads five) would otherwise spike a single frame
		for (auto it = m_generating.begin(); it != m_generating.end(); ++it) {
			auto& [coord, future] = *it;
			if (future.valid() && future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				future.get();
				// Stitch borders with any ready neighbors (and theirs with this chunk)
				refreshAdjacencyAround(coord);
				LOG_DEBUG(Engine, "Loaded chunk (%d, %d)", coord.x, coord.y);
				m_generating.erase(it);
				return;
			}
		}
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

	void ChunkManager::refreshAdjacencyForChunkBoundary(ChunkCoordinate coord) {
		Chunk* chunk = getChunk(coord);
		if (chunk == nullptr || !chunk->isReady()) {
			return;
		}

		// Cache the 3x3 neighborhood once: sampleSurface runs 8 times per border
		// tile (~16k calls per refresh) and per-call getChunk map lookups dominated
		std::array<const Chunk*, 9> neighborhood{};
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const Chunk* candidate = getChunk({coord.x + dx, coord.y + dy});
				if (candidate != nullptr && candidate->isReady()) {
					neighborhood[(dy + 1) * 3 + (dx + 1)] = candidate;
				}
			}
		}

		auto sampleSurface = [&](int localX, int localY) -> uint8_t {
			int cx = 1;
			int cy = 1;
			int tx = localX;
			int ty = localY;

			if (tx < 0) {
				cx = 0;
				tx += kChunkSize;
			} else if (tx >= kChunkSize) {
				cx = 2;
				tx -= kChunkSize;
			}

			if (ty < 0) {
				cy = 0;
				ty += kChunkSize;
			} else if (ty >= kChunkSize) {
				cy = 2;
				ty -= kChunkSize;
			}

			const Chunk* neighbor = neighborhood[cy * 3 + cx];
			if (neighbor == nullptr) {
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
