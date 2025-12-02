// AssetBatcher Implementation

#include "assets/AssetBatcher.h"

#include <cmath>

namespace engine::assets {

	void AssetBatcher::addInstances(const renderer::TessellatedMesh& templateMesh, const std::vector<SpawnedInstance>& instances) {
		for (const auto& instance : instances) {
			addTransformedInstance(templateMesh, instance);
		}
	}

	void AssetBatcher::addTransformedInstance(const renderer::TessellatedMesh& mesh, const SpawnedInstance& instance) {
		size_t vertsPerInstance = mesh.vertices.size();

		// Ensure we have a batch, or create new one if current is full
		if (m_batches.empty() || m_batches.back().vertices.size() + vertsPerInstance > kMaxVerticesPerBatch) {
			m_batches.emplace_back();
		}

		GeometryBatch& batch = m_batches.back();
		// Safe cast: kMaxVerticesPerBatch (60000) < uint16_t max (65535)
		auto baseIndex = static_cast<uint16_t>(batch.vertices.size());

		// Precompute transform
		float cosR = std::cos(instance.rotation);
		float sinR = std::sin(instance.rotation);

		// Check if mesh has per-vertex colors
		bool hasMeshColors = mesh.hasColors();

		// Transform and add vertices
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			const auto& v = mesh.vertices[i];

			// Scale
			float sx = v.x * instance.scale;
			float sy = v.y * instance.scale;

			// Rotate
			float rx = sx * cosR - sy * sinR;
			float ry = sx * sinR + sy * cosR;

			// Translate
			batch.vertices.push_back(Foundation::Vec2(rx + instance.position.x, ry + instance.position.y));

			// Use mesh color modulated by colorTint, or just colorTint if no mesh colors
			if (hasMeshColors) {
				const auto& meshColor = mesh.colors[i];
				// Modulate mesh color by instance colorTint
				batch.colors.push_back(Foundation::Color(
					meshColor.r * instance.colorTint.r,
					meshColor.g * instance.colorTint.g,
					meshColor.b * instance.colorTint.b,
					meshColor.a * instance.colorTint.a
				));
			} else {
				batch.colors.push_back(instance.colorTint);
			}
		}

		// Add indices with offset
		for (const auto& idx : mesh.indices) {
			batch.indices.push_back(baseIndex + idx);
		}

		m_instanceCount++;
	}

	void AssetBatcher::clear() {
		m_batches.clear();
		m_instanceCount = 0;
	}

	size_t AssetBatcher::totalVertices() const {
		size_t total = 0;
		for (const auto& batch : m_batches) {
			total += batch.vertices.size();
		}
		return total;
	}

	size_t AssetBatcher::totalIndices() const {
		size_t total = 0;
		for (const auto& batch : m_batches) {
			total += batch.indices.size();
		}
		return total;
	}

} // namespace engine::assets
