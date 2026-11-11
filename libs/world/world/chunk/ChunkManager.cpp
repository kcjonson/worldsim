#include "ChunkManager.h"

#include <utils/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace world {

	namespace {

		/// An inclusive rectangle of chunk coordinates; empty when min > max.
		struct ChunkSpan {
			int32_t minX = 0;
			int32_t minY = 0;
			int32_t maxX = -1;
			int32_t maxY = -1;

			bool operator==(const ChunkSpan&) const = default;
		};

		/// The chunks whose square overlaps the open square of half-side `range` around
		/// `p`. Open, so an anchor exactly `range` from a border doesn't reach across it
		/// and a keep range of half a chunk holds at most two chunks per axis.
		ChunkSpan chunksWithin(WorldPosition p, float range) {
			auto lo = [range](float v) { return static_cast<int32_t>(std::floor((v - range) / kChunkWorldSize)); };
			auto hi = [range](float v) { return static_cast<int32_t>(std::ceil((v + range) / kChunkWorldSize)) - 1; };
			return {lo(p.x), lo(p.y), hi(p.x), hi(p.y)};
		}

		void appendSpan(const ChunkSpan& span, std::vector<ChunkCoordinate>& out) {
			for (int32_t y = span.minY; y <= span.maxY; ++y) {
				for (int32_t x = span.minX; x <= span.maxX; ++x) {
					out.emplace_back(x, y);
				}
			}
		}

		bool coordLess(const ChunkCoordinate& a, const ChunkCoordinate& b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		}

		void sortUnique(std::vector<ChunkCoordinate>& coords) {
			std::sort(coords.begin(), coords.end(), coordLess);
			coords.erase(std::unique(coords.begin(), coords.end()), coords.end());
		}

	} // namespace

	ChunkManager::ChunkManager(std::unique_ptr<IWorldSampler> worldSampler)
		: sampler(std::move(worldSampler)) {}

	void ChunkManager::update(WorldPosition cameraCenter, std::span<const WorldPosition> anchors) {
		// Integrate any chunks whose generation worker finished
		pollGeneratedChunks();

		cameraChunk = worldToChunk(cameraCenter);
		for (int32_t dy = -cameraLoadRadius; dy <= cameraLoadRadius; ++dy) {
			for (int32_t dx = -cameraLoadRadius; dx <= cameraLoadRadius; ++dx) {
				ChunkCoordinate coord{cameraChunk.x + dx, cameraChunk.y + dy};
				if (chunks.find(coord) == chunks.end()) {
					loadChunk(coord);
				}
			}
		}

		gatherAnchorChunks(anchors);
		for (const ChunkCoordinate& coord : anchorLoadChunks) {
			if (chunks.find(coord) == chunks.end()) {
				loadChunk(coord);
			}
		}

		// Runs every update, not only when something moved: chunks are exempt while
		// their generation worker is in flight, so a sweep gated on movement could
		// strand finished chunks behind a camera and anchors that stopped.
		unloadUnheldChunks();
	}

	void ChunkManager::gatherAnchorChunks(std::span<const WorldPosition> anchors) {
		anchorLoadChunks.clear();
		anchorKeepChunks.clear();

		// Keep at least what loads, so a keep range set below the load range can't
		// unload a chunk the same update loaded.
		const float keepRange = std::max(anchorKeepRange, anchorLoadRange);

		// Movers cluster, so consecutive anchors mostly cover the same chunks; skipping
		// a repeat of the previous span keeps the sort below at the size of the
		// distinct chunks rather than the anchor count.
		ChunkSpan lastLoad;
		ChunkSpan lastKeep;
		for (const WorldPosition& anchor : anchors) {
			const ChunkSpan load = chunksWithin(anchor, anchorLoadRange);
			if (load != lastLoad) {
				appendSpan(load, anchorLoadChunks);
				lastLoad = load;
			}
			const ChunkSpan keep = chunksWithin(anchor, keepRange);
			if (keep != lastKeep) {
				appendSpan(keep, anchorKeepChunks);
				lastKeep = keep;
			}
		}
		sortUnique(anchorLoadChunks);
		sortUnique(anchorKeepChunks);
	}

	Chunk* ChunkManager::getChunk(ChunkCoordinate coord) {
		auto it = chunks.find(coord);
		if (it != chunks.end()) {
			it->second->touch();
			return it->second.get();
		}
		return nullptr;
	}

	const Chunk* ChunkManager::getChunk(ChunkCoordinate coord) const {
		auto it = chunks.find(coord);
		if (it != chunks.end()) {
			it->second->touch();
			return it->second.get();
		}
		return nullptr;
	}

	std::vector<Chunk*> ChunkManager::getLoadedChunks() {
		std::vector<Chunk*> result;
		result.reserve(chunks.size());
		for (auto& [coord, chunk] : chunks) {
			result.push_back(chunk.get());
		}
		return result;
	}

	std::vector<const Chunk*> ChunkManager::getLoadedChunks() const {
		std::vector<const Chunk*> result;
		result.reserve(chunks.size());
		for (const auto& [coord, chunk] : chunks) {
			result.push_back(chunk.get());
		}
		return result;
	}

	std::vector<const Chunk*> ChunkManager::getVisibleChunks(WorldPosition minCorner, WorldPosition maxCorner) const {
		std::vector<const Chunk*> result;

		ChunkCoordinate minChunk = worldToChunk(minCorner);
		ChunkCoordinate maxChunk = worldToChunk(maxCorner);

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
		ChunkSampleResult sampleResult = sampler->sampleChunk(coord);

		auto chunk = std::make_unique<Chunk>(coord, std::move(sampleResult), sampler->getWorldSeed());

		// Generate the 262k tiles on a worker thread: this takes tens of ms per
		// chunk and used to hitch the frame when scrolling crossed a chunk row.
		// Consumers gate on chunk->isReady(); pollGeneratedChunks() retires the
		// task once the worker finishes.
		Chunk* rawChunk = chunk.get();
		chunks[coord] = std::move(chunk);
		generating.push_back(
			{coord, std::async(std::launch::async, [rawChunk]() { rawChunk->generate(); }), std::chrono::steady_clock::now()}
		);

		LOG_DEBUG(Engine, "Loading chunk (%d, %d)", coord.x, coord.y);
	}

	void ChunkManager::pollGeneratedChunks() {
		for (auto it = generating.begin(); it != generating.end();) {
			if (it->task.valid() && it->task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
				it->task.get();
				LOG_DEBUG(
					Engine,
					"Loaded chunk (%d, %d) %.1f ms after its request",
					it->coord.x,
					it->coord.y,
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - it->requested).count()
				);
				it = generating.erase(it);
			} else {
				++it;
			}
		}
	}

	void ChunkManager::finishPendingGeneration() {
		for (Generation& generation : generating) {
			if (generation.task.valid()) {
				generation.task.get();
			}
		}
		generating.clear();
	}

	bool ChunkManager::isGenerating(ChunkCoordinate coord) const {
		for (const Generation& generation : generating) {
			if (generation.coord == coord) {
				return true;
			}
		}
		return false;
	}

	void ChunkManager::unloadUnheldChunks() {
		std::vector<ChunkCoordinate> toUnload;
		for (const auto& [coord, chunk] : chunks) {
			if (coord.chebyshevDistance(cameraChunk) <= cameraUnloadRadius) {
				continue;
			}
			if (std::binary_search(anchorKeepChunks.begin(), anchorKeepChunks.end(), coord, coordLess)) {
				continue;
			}
			// Never unload a chunk whose generation worker still references it
			if (isGenerating(coord)) {
				continue;
			}
			toUnload.push_back(coord);
		}

		for (const auto& coord : toUnload) {
			LOG_DEBUG(Engine, "Unloaded chunk (%d, %d)", coord.x, coord.y);
			chunks.erase(coord);
		}
	}

} // namespace world
