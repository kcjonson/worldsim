#include "MeshBounds.h"

#include <algorithm>
#include <limits>

namespace renderer {

	Foundation::Rect computeBounds(const TessellatedMesh& mesh) {
		if (mesh.vertices.empty()) {
			return {0.0F, 0.0F, 0.0F, 0.0F};
		}

		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float maxX = std::numeric_limits<float>::lowest();
		float maxY = std::numeric_limits<float>::lowest();

		for (const auto& v : mesh.vertices) {
			minX = std::min(minX, v.x);
			minY = std::min(minY, v.y);
			maxX = std::max(maxX, v.x);
			maxY = std::max(maxY, v.y);
		}

		return {minX, minY, maxX - minX, maxY - minY};
	}

	void fitToRect(TessellatedMesh& mesh, const Foundation::Rect& src, const Foundation::Rect& dst) {
		if (src.width <= 0.0F || src.height <= 0.0F) {
			return;
		}

		const float scale = std::min(dst.width / src.width, dst.height / src.height);

		const float srcCenterX = src.x + (src.width * 0.5F);
		const float srcCenterY = src.y + (src.height * 0.5F);
		const float dstCenterX = dst.x + (dst.width * 0.5F);
		const float dstCenterY = dst.y + (dst.height * 0.5F);

		for (auto& v : mesh.vertices) {
			v.x = dstCenterX + ((v.x - srcCenterX) * scale);
			v.y = dstCenterY + ((v.y - srcCenterY) * scale);
		}
	}

} // namespace renderer
