#include "WorldDepthSort.h"

#include "world/rendering/RenderContext.h"

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

	void WorldDepthGather::gather(const RenderContext& ctx, std::vector<DepthSortItem>& out, bool backgroundOnFastPath) {
		out.clear();
		const VisibleBounds vis = computeVisibleBounds(ctx.camera, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter);

		// Visible static occluders, gathered live from the placement index (same
		// per-chunk queryRect the batched path uses). anchorY + isTallOccluder were
		// computed once at placement time, so this walks the query results with no
		// per-entity string/mesh lookups. On the instanced fast path only tall
		// occluders join the sorted stream (short flora is baked, groundcover
		// renders instanced); the batched fallback has no separate background, so it
		// keeps every static.
		// Iterate visible chunks in a deterministic (y, x) order. processedChunks is an
		// unordered_set whose iteration order can shift on rehash; fixing the order keeps
		// stable_sort's tie behavior (equal anchorY across chunks) identical frame to frame.
		std::vector<ChunkCoordinate> orderedChunks(ctx.processedChunks.begin(), ctx.processedChunks.end());
		std::sort(orderedChunks.begin(), orderedChunks.end(), [](const ChunkCoordinate& a, const ChunkCoordinate& b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		});
		for (const auto& coord : orderedChunks) {
			const auto* index = ctx.executor.getChunkIndex(coord);
			if (index == nullptr) {
				continue;
			}
			const auto visible = index->queryRect(vis.minX, vis.minY, vis.maxX, vis.maxY);
			for (const auto* entity : visible) {
				if (backgroundOnFastPath && !entity->isTallOccluder) {
					continue;
				}
				out.push_back({entity->anchorY, entity, false});
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
