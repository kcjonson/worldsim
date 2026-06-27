#include "GroundcoverRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>

namespace engine::world {

// --- Groundcover (grass) GPU-instanced path ---

GroundcoverRenderer::~GroundcoverRenderer() {
	auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
	if (batchRenderer != nullptr) {
		for (auto& [defName, handles] : m_groundcoverHandles) {
			for (auto& handle : handles) {
				if (handle.isValid()) {
					batchRenderer->releaseInstancedMesh(handle);
				}
			}
		}
	}
	m_groundcoverHandles.clear();
}

void GroundcoverRenderer::evictLRU(const std::unordered_set<ChunkCoordinate>& processedChunks) {
	if (m_groundcoverChunkCache.size() <= kMaxCachedChunks) {
		return;
	}
	std::vector<std::pair<ChunkCoordinate, uint64_t>> byAge;
	byAge.reserve(m_groundcoverChunkCache.size());
	for (const auto& [coord, cache] : m_groundcoverChunkCache) {
		if (processedChunks.find(coord) == processedChunks.end()) { // never evict visible chunks
			byAge.emplace_back(coord, cache.lastAccessFrame);
		}
	}
	std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
	const size_t toEvict = std::min(byAge.size(), kEvictionBatchSize);
	for (size_t i = 0; i < toEvict; ++i) {
		m_groundcoverChunkCache.erase(byAge[i].first);
	}
}

namespace {
	// Tunable groundcover constants.
	constexpr float	   kGroundcoverHeightM = 0.5F;	  // blade height used for the zoom LOD
	constexpr float	   kGroundcoverLodCutoffPx = 6.0F; // fade out below this on-screen height (px)
	constexpr float	   kGroundcoverReachM = 0.55F;	  // max tuft reach (deform; flat at rest)
	constexpr uint32_t kGroundcoverMaxInstancesPerVariant = 100000;
} // namespace

const std::vector<Renderer::InstancedMeshHandle>& GroundcoverRenderer::ensureGroundcoverVariants(const std::string& defName) {
	auto it = m_groundcoverHandles.find(defName);
	if (it != m_groundcoverHandles.end()) {
		return it->second;
	}

	// Generate the asset's variant tufts via the asset system (one buildMesh per seed) so the
	// look lives entirely in the asset (grass.lua + params), not here.
	std::vector<Renderer::InstancedMeshHandle> handles;
	auto*									   batchRenderer = Renderer::Primitives::getBatchRenderer();
	auto&									   registry = assets::AssetRegistry::Get();
	const auto*								   def = registry.getDefinition(defName);
	if (batchRenderer != nullptr && def != nullptr) {
		const uint32_t variantCount = std::max<uint32_t>(1, def->variantCount);
		handles.reserve(variantCount);
		for (uint32_t seed = 0; seed < variantCount; ++seed) {
			renderer::TessellatedMesh mesh;
			if (registry.buildMesh(defName, seed, mesh) && !mesh.vertices.empty()) {
				handles.push_back(batchRenderer->uploadInstancedMesh(mesh, kGroundcoverMaxInstancesPerVariant));
			} else {
				handles.emplace_back(); // invalid placeholder keeps variant indices aligned
			}
		}
	}
	auto [insIt, _] = m_groundcoverHandles.emplace(defName, std::move(handles));
	return insIt->second;
}

void GroundcoverRenderer::buildGroundcoverChunk(
	const assets::PlacementExecutor& executor, const ChunkCoordinate& coord, GroundcoverChunkCache& cache
) {
	cache.byDef.clear();
	cache.built = true;

	WorldPosition	chunkOrigin = coord.origin();
	constexpr float kChunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
	cache.minX = chunkOrigin.x;
	cache.minY = chunkOrigin.y;
	cache.maxX = chunkOrigin.x + kChunkWorldSize;
	cache.maxY = chunkOrigin.y + kChunkWorldSize;

	const auto* index = executor.getChunkIndex(coord);
	if (index == nullptr) {
		return;
	}

	auto& registry = assets::AssetRegistry::Get();
	auto  entities = index->queryRect(cache.minX, cache.minY, cache.maxX, cache.maxY);
	for (const auto* e : entities) {
		const auto* def = registry.getDefinition(e->defName);
		if (def == nullptr || def->role != assets::AssetRole::Groundcover) {
			continue;
		}
		const uint32_t variantCount = std::max<uint32_t>(1, def->variantCount);
		auto&		   buckets = cache.byDef[e->defName];
		if (buckets.size() != variantCount) {
			buckets.resize(variantCount);
		}
		// Deterministic variant from quantized world position (chunk re-builds identically).
		auto	 hx = static_cast<uint32_t>(static_cast<int32_t>(std::floor(e->position.x * 4.0F)));
		auto	 hy = static_cast<uint32_t>(static_cast<int32_t>(std::floor(e->position.y * 4.0F)));
		uint32_t v = ((hx * 73856093u) ^ (hy * 19349663u)) % variantCount;
		buckets[v].emplace_back(
			Foundation::Vec2(e->position.x, e->position.y), e->rotation, e->scale, Foundation::Color(e->colorTint)
		);
	}
}

void GroundcoverRenderer::render(
	const assets::PlacementExecutor&		   executor,
	const std::unordered_set<ChunkCoordinate>& processedChunks,
	const WorldCamera&						   camera,
	int										   viewportWidth,
	int										   viewportHeight,
	float									   pixelsPerMeter,
	uint64_t								   frameCounter,
	InstancingUniforms&						   uniforms,
	RenderStats&							   stats
) {
	auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
	if (batchRenderer == nullptr) {
		return;
	}

	const float zoom = camera.zoom();
	const float ppm = pixelsPerMeter;

	// Zoom LOD: a tuft is kGroundcoverHeightM tall; below kGroundcoverLodCutoffPx on screen
	// we skip the geometry entirely and let the grass tile texture carry it. Tunable.
	const float bladeScreenPx = kGroundcoverHeightM * ppm * zoom;
	const float lodAlpha = std::clamp((bladeScreenPx - kGroundcoverLodCutoffPx) / kGroundcoverLodCutoffPx, 0.0F, 1.0F);
	if (lodAlpha <= 0.0F) {
		return;
	}

	const float camX = camera.position().x;
	const float camY = camera.position().y;
	const float scale = ppm * zoom;
	const float viewWorldW = static_cast<float>(viewportWidth) / scale;
	const float viewWorldH = static_cast<float>(viewportHeight) / scale;
	constexpr float kMargin = 2.0F;
	const float		visMinX = camX - (viewWorldW * 0.5F) - kMargin;
	const float		visMaxX = camX + (viewWorldW * 0.5F) + kMargin;
	const float		visMinY = camY - (viewWorldH * 0.5F) - kMargin;
	const float		visMaxY = camY + (viewWorldH * 0.5F) + kMargin;

	GLuint shaderProgram = batchRenderer->getShaderProgram();
	glUseProgram(shaderProgram);
	uniforms.init(shaderProgram);
	if (uniforms.groundcoverMode >= 0) {
		glUniform1i(uniforms.groundcoverMode, 1);
	}
	if (uniforms.groundcoverOpenness >= 0) {
		glUniform1f(uniforms.groundcoverOpenness, 1.0F);
	}
	if (uniforms.groundcoverReach >= 0) {
		glUniform1f(uniforms.groundcoverReach, kGroundcoverReachM);
	}
	if (uniforms.cursorRadius >= 0) {
		glUniform1f(uniforms.cursorRadius, 0.0F); // no interaction deform yet
	}
	if (uniforms.bakedAlpha >= 0) {
		glUniform1f(uniforms.bakedAlpha, lodAlpha);
	}

	const Foundation::Vec2 cameraPos(camX, camY);
	for (const auto& coord : processedChunks) {
		auto& cache = m_groundcoverChunkCache[coord];
		cache.lastAccessFrame = frameCounter;
		if (!cache.built) {
			buildGroundcoverChunk(executor, coord, cache);
		}
		if (cache.maxX < visMinX || cache.minX > visMaxX || cache.maxY < visMinY || cache.minY > visMaxY) {
			continue;
		}
		for (auto& [defName, buckets] : cache.byDef) {
			const auto&	 handles = ensureGroundcoverVariants(defName);
			const size_t count = std::min(buckets.size(), handles.size());
			for (size_t v = 0; v < count; ++v) {
				auto& inst = buckets[v];
				if (inst.empty() || !handles[v].isValid()) {
					continue;
				}
				batchRenderer->drawInstanced(
					handles[v], inst.data(), static_cast<uint32_t>(inst.size()), cameraPos, zoom, ppm
				);
				stats.entities += static_cast<uint32_t>(inst.size());
			}
		}
	}

	// Restore normal (non-grass) instancing state for the rest of the frame.
	if (uniforms.groundcoverMode >= 0) {
		glUniform1i(uniforms.groundcoverMode, 0);
	}
	if (uniforms.bakedAlpha >= 0) {
		glUniform1f(uniforms.bakedAlpha, 1.0F);
	}
}

} // namespace engine::world
