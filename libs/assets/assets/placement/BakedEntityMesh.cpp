#include "BakedEntityMesh.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace assets {

	namespace {

		/// Template Y extents (before entity scale), measured once per template over a bake
		class TemplateHeights {
		  public:
			float of(const renderer::TessellatedMesh* mesh) {
				auto it = m_heights.find(mesh);
				if (it != m_heights.end()) {
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
				const float height = maxY - minY;
				m_heights.emplace(mesh, height);
				return height;
			}

		  private:
			std::unordered_map<const renderer::TessellatedMesh*, float> m_heights;
		};

		/// A sub-region with its culling bounds set and nothing baked into it yet
		BakedSubChunkCPUData emptySubChunk(world::ChunkCoordinate coord, int subIndex) {
			const world::WorldPosition chunkOrigin = coord.origin();
			BakedSubChunkCPUData	   subChunk;
			subChunk.minX = chunkOrigin.x + static_cast<float>(subIndex % kSubChunkGridSize) * kSubChunkWorldSize;
			subChunk.minY = chunkOrigin.y + static_cast<float>(subIndex / kSubChunkGridSize) * kSubChunkWorldSize;
			subChunk.maxX = subChunk.minX + kSubChunkWorldSize;
			subChunk.maxY = subChunk.minY + kSubChunkWorldSize;
			return subChunk;
		}

		/// Transform one sub-region's entities into its world-space vertices
		void bakeInto(
			BakedMeshCPU&									bucket,
			const std::vector<const assets::PlacedEntity*>& entities,
			const TemplateLookup&							getTemplate,
			TemplateHeights&								heights
		) {
			uint32_t vertexOffset = 0;

			for (const auto* entity : entities) {
				// Tall occluders draw live in the Y-sorted upright stream, not baked.
				if (entity->isTallOccluder) {
					continue;
				}
				const auto* templateMesh = getTemplate(*entity);
				if (templateMesh == nullptr) {
					continue;
				}

				float entityScale = entity->scale;
				float worldHeight = heights.of(templateMesh) * entityScale;
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
		}

	} // namespace

	BakedChunkCPUData bakeChunkEntities(
		const std::vector<const assets::PlacedEntity*>& entities,
		world::ChunkCoordinate coord,
		const TemplateLookup& getTemplate
	) {
		BakedChunkCPUData data;

		// Sub-chunk bounds (set even for empty sub-chunks so culling data is valid)
		for (int subIndex = 0; subIndex < kSubChunkCount; ++subIndex) {
			data.subChunks[subIndex] = emptySubChunk(coord, subIndex);
		}

		// Bin entities by sub-chunk
		std::array<std::vector<const assets::PlacedEntity*>, kSubChunkCount> bins;
		for (const auto* entity : entities) {
			bins[subChunkIndexOf(coord, entity->position)].push_back(entity);
		}

		TemplateHeights heights;
		for (int subIndex = 0; subIndex < kSubChunkCount; ++subIndex) {
			auto& subChunk = data.subChunks[subIndex];
			bakeInto(subChunk.floraMesh, bins[subIndex], getTemplate, heights);
			data.totalEntityCount += subChunk.floraMesh.entityCount;
		}

		return data;
	}

	BakedSubChunkCPUData bakeSubChunkEntities(
		const assets::SpatialIndex& index,
		world::ChunkCoordinate coord,
		int subIndex,
		const TemplateLookup& getTemplate
	) {
		BakedSubChunkCPUData subChunk = emptySubChunk(coord, subIndex);

		// The rect query takes both edges; a static on a shared edge belongs to one sub-region only.
		auto entities = index.queryRect(subChunk.minX, subChunk.minY, subChunk.maxX, subChunk.maxY);
		std::erase_if(entities, [&](const assets::PlacedEntity* entity) { return subChunkIndexOf(coord, entity->position) != subIndex; });

		TemplateHeights heights;
		bakeInto(subChunk.floraMesh, entities, getTemplate, heights);
		return subChunk;
	}

} // namespace assets
