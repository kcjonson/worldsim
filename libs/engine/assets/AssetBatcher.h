#pragma once

// AssetBatcher - Batches spawned asset instances into renderable geometry.
// Transforms template meshes using instance data, manages batch size limits.
// Produces GeometryBatch data ready for drawTriangles().

#include "assets/AssetSpawner.h"

#include <graphics/Color.h>
#include <math/Types.h>
#include <vector/Tessellator.h>

#include <vector>

namespace engine::assets {

/// A batch of geometry that fits in one draw call (uint16_t indices max ~65535)
struct GeometryBatch {
	std::vector<Foundation::Vec2>  vertices;
	std::vector<Foundation::Color> colors;
	std::vector<uint16_t>		   indices;

	void clear() {
		vertices.clear();
		colors.clear();
		indices.clear();
	}

	[[nodiscard]] bool empty() const { return vertices.empty(); }
};

/// Batches spawned instances into renderable geometry.
class AssetBatcher {
  public:
	/// Add instances using a mesh template.
	/// Transforms each instance and adds to batches.
	void addInstances(
		const renderer::TessellatedMesh&	 templateMesh,
		const std::vector<SpawnedInstance>&	 instances
	);

	/// Get all batches for rendering
	[[nodiscard]] const std::vector<GeometryBatch>& batches() const { return m_batches; }

	/// Clear all batches
	void clear();

	/// Statistics
	[[nodiscard]] size_t totalVertices() const;
	[[nodiscard]] size_t totalIndices() const;
	[[nodiscard]] size_t instanceCount() const { return m_instanceCount; }

  private:
	static constexpr size_t kMaxVerticesPerBatch = 60000;  // uint16_t index limit safety margin

	std::vector<GeometryBatch> m_batches;
	size_t					   m_instanceCount = 0;

	/// Add a single transformed instance to current batch
	void addTransformedInstance(
		const renderer::TessellatedMesh& mesh,
		const SpawnedInstance&			 instance
	);
};

}  // namespace engine::assets
