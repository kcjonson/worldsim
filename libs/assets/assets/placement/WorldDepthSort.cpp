#include "WorldDepthSort.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/RenderContext.h"

#include <vector/Types.h>

#include <glm/common.hpp>

#include <algorithm>
#include <limits>

namespace assets {

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

	float anchorLocalY(const AssetDefinition* def, uint8_t ageClass, float meshMaxLocalY) {
		if (def == nullptr) {
			return meshMaxLocalY;
		}
		const CollisionShape& footprint = def->collisionFor(ageClass);
		switch (footprint.type) {
			case CollisionShapeType::Rect:
				return footprint.offsetMeters.y + footprint.halfExtentsMeters.y;
			case CollisionShapeType::Polygon:
				if (!footprint.pointsMeters.empty()) {
					float lowest = footprint.pointsMeters.front().y;
					for (const glm::vec2& p : footprint.pointsMeters) {
						lowest = std::max(lowest, p.y);
					}
					return lowest;
				}
				return meshMaxLocalY;
			case CollisionShapeType::None:
				return meshMaxLocalY;
		}
		return meshMaxLocalY;
	}

	void sortByAnchorY(std::vector<DepthSortItem>& items) {
		std::stable_sort(items.begin(), items.end(), [](const DepthSortItem& a, const DepthSortItem& b) {
			return a.anchorY < b.anchorY;
		});
	}

	const WorldDepthGather::ChunkStatics&
	WorldDepthGather::staticsOf(world::ChunkCoordinate coord, const SpatialIndex& index, bool tallOnly) {
		ChunkStatics& chunk = chunks[coord];
		chunk.usedFrame = frame;
		if (chunk.index == &index && chunk.version == index.version() && chunk.tallOnly == tallOnly) {
			return chunk;
		}
		chunk.index = &index;
		chunk.version = index.version();
		chunk.tallOnly = tallOnly;
		chunk.statics.clear();
		constexpr float kEverywhere = std::numeric_limits<float>::max();
		for (const PlacedEntity* entity : index.queryRect(-kEverywhere, -kEverywhere, kEverywhere, kEverywhere)) {
			if (!tallOnly || entity->isTallOccluder) {
				chunk.statics.push_back({entity->anchorY, entity->position, entity});
			}
		}
		std::stable_sort(chunk.statics.begin(), chunk.statics.end(), [](const Static& a, const Static& b) {
			return a.anchorY < b.anchorY;
		});
		chunk.min = chunk.max = chunk.statics.empty() ? glm::vec2{0.0F, 0.0F} : chunk.statics.front().position;
		for (const Static& s : chunk.statics) {
			chunk.min = glm::min(chunk.min, s.position);
			chunk.max = glm::max(chunk.max, s.position);
		}
		return chunk;
	}

	void WorldDepthGather::gather(const RenderContext& ctx, std::vector<DepthSortItem>& out, bool backgroundOnFastPath) {
		out.clear();
		++frame;
		// Padded by the farthest any template draws from its origin: statics, dynamic entities, and
		// the batched fallback's short flora are all culled by origin against this rect.
		const VisibleBounds vis = computeVisibleBounds(
			ctx.camera,
			ctx.viewportWidth,
			ctx.viewportHeight,
			ctx.pixelsPerMeter,
			entityCullMarginMeters(AssetRegistry::Get().maxReachMeters())
		);

		// Visible statics: each chunk's sorted list, culled by origin, so each chunk's stretch
		// of out is already in order. On the instanced fast path only tall occluders join the
		// sorted stream (short flora is baked, groundcover renders instanced); the batched
		// fallback has no separate background, so it keeps every static. Chunks go in (y, x)
		// order: processedChunks is an unordered_set whose order can shift on rehash, and a
		// fixed order keeps ties between chunks drawn the same way frame to frame.
		orderedChunks.assign(ctx.processedChunks.begin(), ctx.processedChunks.end());
		std::sort(orderedChunks.begin(), orderedChunks.end(), [](const world::ChunkCoordinate& a, const world::ChunkCoordinate& b) {
			return a.y != b.y ? a.y < b.y : a.x < b.x;
		});
		segmentEnds.clear();
		for (const auto& coord : orderedChunks) {
			const auto* index = ctx.executor.getChunkIndex(coord);
			if (index == nullptr) {
				continue;
			}
			const ChunkStatics& chunk = staticsOf(coord, *index, backgroundOnFastPath);
			if (chunk.statics.empty() || chunk.max.x < vis.minX || chunk.min.x > vis.maxX || chunk.max.y < vis.minY ||
				chunk.min.y > vis.maxY) {
				continue;
			}
			for (const Static& s : chunk.statics) {
				if (s.position.x >= vis.minX && s.position.x <= vis.maxX && s.position.y >= vis.minY && s.position.y <= vis.maxY) {
					out.push_back({s.anchorY, s.entity, false});
				}
			}
			segmentEnds.push_back(out.size());
		}
		std::erase_if(chunks, [this](const auto& entry) { return entry.second.usedFrame != frame; });

		// Dynamic ECS entities: anchorY already produced by DynamicEntityRenderSystem. Sorted
		// on their own and merged after every chunk, so they keep their place behind statics
		// of equal anchorY.
		const size_t staticCount = out.size();
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
		const auto dynamicBegin = out.begin() + static_cast<std::ptrdiff_t>(staticCount);
		const auto byAnchor = [](const DepthSortItem& a, const DepthSortItem& b) { return a.anchorY < b.anchorY; };
		std::stable_sort(dynamicBegin, out.end(), byAnchor);
		segmentEnds.push_back(out.size());

		// Merge neighbouring sorted stretches pairwise until one is left. inplace_merge keeps
		// the earlier stretch first among equal keys, which is what one stable sort would do.
		while (segmentEnds.size() > 1) {
			size_t kept = 0;
			size_t begin = 0;
			for (size_t i = 0; i < segmentEnds.size(); i += 2) {
				if (i + 1 == segmentEnds.size()) {
					segmentEnds[kept++] = segmentEnds[i];
					break;
				}
				const auto first = out.begin() + static_cast<std::ptrdiff_t>(begin);
				const auto middle = out.begin() + static_cast<std::ptrdiff_t>(segmentEnds[i]);
				const auto last = out.begin() + static_cast<std::ptrdiff_t>(segmentEnds[i + 1]);
				std::inplace_merge(first, middle, last, byAnchor);
				segmentEnds[kept++] = segmentEnds[i + 1];
				begin = segmentEnds[i + 1];
			}
			segmentEnds.resize(kept);
		}
	}

} // namespace assets
