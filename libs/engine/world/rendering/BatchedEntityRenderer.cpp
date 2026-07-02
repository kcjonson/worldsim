#include "BatchedEntityRenderer.h"

#include "world/chunk/ChunkCoordinate.h"

#include <cmath>
#include <primitives/Primitives.h>
#include <vector/Tessellator.h>

namespace engine::world {

	void BatchedEntityRenderer::appendEntityTriangles(
		const assets::PlacedEntity&		 entity,
		const renderer::TessellatedMesh* mesh,
		float camX, float camY, float scale, float halfViewW, float halfViewH,
		uint32_t& vertexIndex
	) {
		const float entityScale = entity.scale;
		const float posX = entity.position.x;
		const float posY = entity.position.y;
		const bool	hasMeshColors = mesh->hasColors();

		constexpr float kRotationEpsilon = 0.0001F;
		const bool		noRotation = std::abs(entity.rotation) < kRotationEpsilon;

		const float cosR = noRotation ? 1.0F : std::cos(entity.rotation);
		const float sinR = noRotation ? 0.0F : std::sin(entity.rotation);

		for (size_t i = 0; i < mesh->vertices.size(); ++i) {
			const auto& v = mesh->vertices[i];
			const float sx = v.x * entityScale;
			const float sy = v.y * entityScale;
			// Rotate + translate to world (cos/sin are identity when unrotated), then to screen.
			const float worldX = sx * cosR - sy * sinR + posX;
			const float worldY = sx * sinR + sy * cosR + posY;
			m_vertices.emplace_back((worldX - camX) * scale + halfViewW, (worldY - camY) * scale + halfViewH);

			if (hasMeshColors) {
				const auto& meshColor = mesh->colors[i];
				m_colors.emplace_back(
					meshColor.r * entity.colorTint.r,
					meshColor.g * entity.colorTint.g,
					meshColor.b * entity.colorTint.b,
					meshColor.a * entity.colorTint.a
				);
			} else {
				m_colors.emplace_back(entity.colorTint.r, entity.colorTint.g, entity.colorTint.b, entity.colorTint.a);
			}
		}

		for (const auto& idx : mesh->indices) {
			m_indices.push_back(static_cast<uint16_t>(vertexIndex + idx));
		}
		vertexIndex += static_cast<uint32_t>(mesh->vertices.size());
	}

	void BatchedEntityRenderer::render(const RenderContext& ctx, InstancingUniforms& /*uniforms*/, RenderStats& stats) {
		stats.entities = 0;
		stats.drawCalls = 0;
		stats.triangles = 0;

		// Clear per-frame buffers (keep capacity for reuse)
		m_vertices.clear();
		m_colors.clear();
		m_indices.clear();

		// Reserve capacity based on typical entity counts to avoid reallocations
		// ~10k entities × ~15 vertices each = ~150k vertices typical
		constexpr size_t kExpectedVertices = 150000;
		constexpr size_t kExpectedIndices = 200000;
		if (m_vertices.capacity() < kExpectedVertices) {
			m_vertices.reserve(kExpectedVertices);
			m_colors.reserve(kExpectedVertices);
			m_indices.reserve(kExpectedIndices);
		}

		const float halfViewW = static_cast<float>(ctx.viewportWidth) * 0.5F;
		const float halfViewH = static_cast<float>(ctx.viewportHeight) * 0.5F;
		const float zoom = ctx.camera.zoom();
		const float scale = ctx.pixelsPerMeter * zoom;
		const float camX = ctx.camera.position().x;
		const float camY = ctx.camera.position().y;

		// Merge every visible static + dynamic entity and sort by ground-contact
		// anchorY, so this fallback matches the instanced path's 2.5D ordering. There
		// is no baked/instanced background here, so all statics are included.
		m_gather.gather(ctx, m_sorted, /*backgroundOnFastPath=*/false);

		uint32_t vertexIndex = 0;
		for (const auto& item : m_sorted) {
			const auto* templateMesh = m_templateCache.get(item.entity->defName);
			if (templateMesh == nullptr) {
				continue;
			}
			appendEntityTriangles(*item.entity, templateMesh, camX, camY, scale, halfViewW, halfViewH, vertexIndex);
			stats.entities++;
		}

		// Single draw call for all entities
		if (!m_indices.empty()) {
			Renderer::Primitives::drawTriangles(
				Renderer::Primitives::TrianglesArgs{
					.vertices = m_vertices.data(),
					.indices = m_indices.data(),
					.vertexCount = m_vertices.size(),
					.indexCount = m_indices.size(),
					.colors = m_colors.data()
				}
			);
		}
	}

} // namespace engine::world
