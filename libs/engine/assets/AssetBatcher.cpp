// AssetBatcher Implementation

#include "assets/AssetBatcher.h"

#include <cmath>

namespace engine::assets {

	void AssetBatcher::addInstances(const renderer::TessellatedMesh& templateMesh, const std::vector<SpawnedInstance>& instances) {
		if (instances.empty()) {
			return;
		}

		size_t vertsPerInstance = templateMesh.vertices.size();
		size_t indicesPerInstance = templateMesh.indices.size();
		size_t totalVerts = instances.size() * vertsPerInstance;

		// Pre-reserve capacity to avoid reallocations
		if (m_batches.empty()) {
			m_batches.emplace_back();
			auto& batch = m_batches.back();
			batch.vertices.reserve(std::min(totalVerts, kMaxVerticesPerBatch));
			batch.colors.reserve(std::min(totalVerts, kMaxVerticesPerBatch));
			batch.indices.reserve(std::min(instances.size() * indicesPerInstance, kMaxVerticesPerBatch * 3 / 2));
		}

		for (const auto& instance : instances) {
			addTransformedInstance(templateMesh, instance);
		}
	}

	void AssetBatcher::addTransformedInstance(const renderer::TessellatedMesh& mesh, const SpawnedInstance& instance) {
		size_t vertsPerInstance = mesh.vertices.size();

		// Ensure we have a batch, or create new one if current is full
		if (m_batches.empty() || m_batches.back().vertices.size() + vertsPerInstance > kMaxVerticesPerBatch) {
			m_batches.emplace_back();
			// Reserve capacity for new batch
			auto& batch = m_batches.back();
			batch.vertices.reserve(kMaxVerticesPerBatch);
			batch.colors.reserve(kMaxVerticesPerBatch);
			batch.indices.reserve(kMaxVerticesPerBatch * 3 / 2);
		}

		GeometryBatch& batch = m_batches.back();
		// Safe cast: kMaxVerticesPerBatch (60000) < uint16_t max (65535)
		auto baseIndex = static_cast<uint16_t>(batch.vertices.size());

		// Check if mesh has per-vertex colors
		bool hasMeshColors = mesh.hasColors();

		// Fast path: no rotation (common case for grass, trees, etc.)
		constexpr float kRotationEpsilon = 0.0001F;
		bool noRotation = std::abs(instance.rotation) < kRotationEpsilon;

		if (noRotation) {
			// Optimized path: skip rotation math
			float scale = instance.scale;
			float posX = instance.position.x;
			float posY = instance.position.y;

			for (size_t i = 0; i < mesh.vertices.size(); ++i) {
				const auto& v = mesh.vertices[i];
				// Scale + translate only
				batch.vertices.emplace_back(v.x * scale + posX, v.y * scale + posY);

				if (hasMeshColors) {
					const auto& meshColor = mesh.colors[i];
					batch.colors.emplace_back(
						meshColor.r * instance.colorTint.r,
						meshColor.g * instance.colorTint.g,
						meshColor.b * instance.colorTint.b,
						meshColor.a * instance.colorTint.a
					);
				} else {
					batch.colors.emplace_back(instance.colorTint);
				}
			}
		} else {
			// Full transform with rotation
			float cosR = std::cos(instance.rotation);
			float sinR = std::sin(instance.rotation);
			float scale = instance.scale;
			float posX = instance.position.x;
			float posY = instance.position.y;

			for (size_t i = 0; i < mesh.vertices.size(); ++i) {
				const auto& v = mesh.vertices[i];

				// Scale
				float sx = v.x * scale;
				float sy = v.y * scale;

				// Rotate + translate
				batch.vertices.emplace_back(sx * cosR - sy * sinR + posX, sx * sinR + sy * cosR + posY);

				if (hasMeshColors) {
					const auto& meshColor = mesh.colors[i];
					batch.colors.emplace_back(
						meshColor.r * instance.colorTint.r,
						meshColor.g * instance.colorTint.g,
						meshColor.b * instance.colorTint.b,
						meshColor.a * instance.colorTint.a
					);
				} else {
					batch.colors.emplace_back(instance.colorTint);
				}
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
