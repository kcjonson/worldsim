#include "WorldDepthSort.h"

#include "world/rendering/BakedEntityMesh.h" // isGroundcoverDef, kShortFloraMaxHeight

#include <vector/Types.h>

#include <algorithm>

namespace engine::world {

	MeshYExtent meshYExtent(const renderer::TessellatedMesh* mesh) {
		MeshYExtent ext;
		if (mesh == nullptr || mesh->vertices.empty()) {
			return ext;
		}
		ext.minY = ext.maxY = mesh->vertices[0].y;
		for (const auto& v : mesh->vertices) {
			ext.minY = std::min(ext.minY, v.y);
			ext.maxY = std::max(ext.maxY, v.y);
		}
		return ext;
	}

	void sortByAnchorY(std::vector<DepthSortItem>& items) {
		std::stable_sort(items.begin(), items.end(), [](const DepthSortItem& a, const DepthSortItem& b) {
			return a.anchorY < b.anchorY;
		});
	}

	MeshYExtent WorldDepthGather::cachedExtent(const renderer::TessellatedMesh* mesh) {
		auto it = m_extentCache.find(mesh);
		if (it != m_extentCache.end()) {
			return it->second;
		}
		const MeshYExtent ext = meshYExtent(mesh);
		m_extentCache.emplace(mesh, ext);
		return ext;
	}

	void WorldDepthGather::gather(const RenderContext& ctx, std::vector<DepthSortItem>& out, bool backgroundOnFastPath) {
		out.clear();
		const VisibleBounds vis = computeVisibleBounds(ctx.camera, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter);

		// Visible static occluders, gathered live from the placement index (same
		// per-chunk queryRect the batched path uses). Tall occluders are pulled off
		// the baked fast path so they can interleave with actors by anchorY.
		for (const auto& coord : ctx.processedChunks) {
			const auto* index = ctx.executor.getChunkIndex(coord);
			if (index == nullptr) {
				continue;
			}
			const auto visible = index->queryRect(vis.minX, vis.minY, vis.maxX, vis.maxY);
			for (const auto* entity : visible) {
				// On the instanced path groundcover is drawn by GroundcoverRenderer;
				// exclude it here so it isn't drawn twice. The batched fallback has no
				// such path, so it keeps groundcover in the sorted stream.
				if (backgroundOnFastPath && isGroundcoverDef(entity->defName)) {
					continue;
				}
				const auto* mesh = m_templateCache.get(entity->defName);
				if (mesh == nullptr) {
					continue;
				}
				const MeshYExtent ext = cachedExtent(mesh);
				const float		  worldHeight = (ext.maxY - ext.minY) * entity->scale;
				// Short flora stays baked as background on the instanced path.
				if (backgroundOnFastPath && worldHeight < kShortFloraMaxHeight) {
					continue;
				}
				out.push_back({computeAnchorY(entity->position.y, ext.maxY, entity->scale), entity, false});
			}
		}

		// Dynamic ECS entities: anchorY already produced by DynamicEntityRenderSystem.
		if (ctx.dynamicEntities != nullptr) {
			for (const auto& entity : *ctx.dynamicEntities) {
				if (entity.position.x < vis.minX || entity.position.x > vis.maxX || entity.position.y < vis.minY ||
					entity.position.y > vis.maxY) {
					continue;
				}
				const bool animated = entity.partTransforms != nullptr && !entity.partTransforms->empty();
				out.push_back({entity.anchorY, &entity, animated});
			}
		}

		sortByAnchorY(out);
	}

} // namespace engine::world
