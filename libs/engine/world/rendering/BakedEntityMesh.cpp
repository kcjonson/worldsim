#include "BakedEntityMesh.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace engine::world {

	BakedChunkCPUData bakeChunkEntities(
		const std::vector<const assets::PlacedEntity*>& entities,
		ChunkCoordinate coord,
		const TemplateLookup& getTemplate
	) {
		BakedChunkCPUData data;
		WorldPosition	  chunkOrigin = coord.origin();

		// Sub-chunk bounds (set even for empty sub-chunks so culling data is valid)
		for (int subY = 0; subY < kSubChunkGridSize; ++subY) {
			for (int subX = 0; subX < kSubChunkGridSize; ++subX) {
				auto& subChunk = data.subChunks[subY * kSubChunkGridSize + subX];
				subChunk.minX = chunkOrigin.x + static_cast<float>(subX) * kSubChunkWorldSize;
				subChunk.minY = chunkOrigin.y + static_cast<float>(subY) * kSubChunkWorldSize;
				subChunk.maxX = subChunk.minX + kSubChunkWorldSize;
				subChunk.maxY = subChunk.minY + kSubChunkWorldSize;
			}
		}

		// Bin entities by sub-chunk
		std::array<std::vector<const assets::PlacedEntity*>, kSubChunkCount> bins;
		for (const auto* entity : entities) {
			int subX = static_cast<int>((entity->position.x - chunkOrigin.x) / kSubChunkWorldSize);
			int subY = static_cast<int>((entity->position.y - chunkOrigin.y) / kSubChunkWorldSize);
			subX = std::clamp(subX, 0, kSubChunkGridSize - 1);
			subY = std::clamp(subY, 0, kSubChunkGridSize - 1);
			bins[subY * kSubChunkGridSize + subX].push_back(entity);
		}

		// Template mesh height cache (template Y extent, before entity scale)
		std::unordered_map<const renderer::TessellatedMesh*, float> heightCache;
		auto templateHeight = [&heightCache](const renderer::TessellatedMesh* mesh) {
			auto it = heightCache.find(mesh);
			if (it != heightCache.end()) {
				return it->second;
			}
			float minY = 0.0F;
			float maxY = 0.0F;
			if (!mesh->vertices.empty()) {
				minY = maxY = mesh->vertices[0].y;
				for (const auto& v : mesh->vertices) {
					minY = std::min(minY, v.y);
					maxY = std::max(maxY, v.y);
				}
			}
			float height = maxY - minY;
			heightCache.emplace(mesh, height);
			return height;
		};

		// Transform each bin into world-space vertices, split by height bucket
		for (int subIndex = 0; subIndex < kSubChunkCount; ++subIndex) {
			const auto& bin = bins[subIndex];
			if (bin.empty()) {
				continue;
			}

			auto&	 subChunk = data.subChunks[subIndex];
			uint32_t vertexOffset = 0;

			for (const auto* entity : bin) {
				const auto* templateMesh = getTemplate(entity->defName);
				if (templateMesh == nullptr) {
					continue;
				}

				float entityScale = entity->scale;
				float worldHeight = templateHeight(templateMesh) * entityScale;
				// Tall occluders draw live in the Y-sorted upright stream, not baked.
				if (worldHeight >= kShortFloraMaxHeight) {
					continue;
				}
				auto& bucket = subChunk.floraMesh;
				bucket.maxEntityHeight = std::max(bucket.maxEntityHeight, worldHeight);

				float posX = entity->position.x;
				float posY = entity->position.y;
				bool  hasMeshColors = templateMesh->hasColors();

				constexpr float kRotationEpsilon = 0.0001F;
				bool			noRotation = std::abs(entity->rotation) < kRotationEpsilon;

				float cosR = 1.0F;
				float sinR = 0.0F;
				if (!noRotation) {
					cosR = std::cos(entity->rotation);
					sinR = std::sin(entity->rotation);
				}

				for (size_t i = 0; i < templateMesh->vertices.size(); ++i) {
					const auto& v = templateMesh->vertices[i];
					BakedVertex baked;

					float sx = v.x * entityScale;
					float sy = v.y * entityScale;

					if (noRotation) {
						baked.position.x = sx + posX;
						baked.position.y = sy + posY;
					} else {
						baked.position.x = sx * cosR - sy * sinR + posX;
						baked.position.y = sx * sinR + sy * cosR + posY;
					}

					if (hasMeshColors) {
						const auto& meshColor = templateMesh->colors[i];
						baked.color = Foundation::Color(
							meshColor.r * entity->colorTint.r,
							meshColor.g * entity->colorTint.g,
							meshColor.b * entity->colorTint.b,
							meshColor.a * entity->colorTint.a
						);
					} else {
						baked.color = Foundation::Color(entity->colorTint);
					}

					bucket.vertices.push_back(baked);
				}

				for (const auto& idx : templateMesh->indices) {
					bucket.indices.push_back(vertexOffset + idx);
				}

				vertexOffset += static_cast<uint32_t>(templateMesh->vertices.size());
				bucket.entityCount++;
			}

			data.totalEntityCount += subChunk.floraMesh.entityCount;
		}

		return data;
	}

} // namespace engine::world
