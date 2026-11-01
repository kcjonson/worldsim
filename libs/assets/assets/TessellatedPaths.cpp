#include "assets/TessellatedPaths.h"

#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <chrono>

namespace assets {

	void TessellatedPaths::tessellate(const GeneratedAsset& asset, float maxPixelsPerMeter) {
		const auto start = std::chrono::steady_clock::now();
		vertices.clear();
		colors.clear();
		indices.clear();
		paths.clear();

		renderer::Tessellator	  tessellator;
		renderer::VectorPath	  vectorPath;
		renderer::TessellatedMesh pathMesh;
		for (const GeneratedPath& path : asset.paths) {
			if (path.vertices.size() < 3 || path.minPixelsPerMeter > maxPixelsPerMeter) {
				continue;
			}
			vectorPath.vertices = path.vertices;
			vectorPath.isClosed = path.isClosed;
			pathMesh.clear();
			if (!tessellator.Tessellate(vectorPath, pathMesh)) {
				LOG_WARNING(Engine, "Failed to tessellate path with %zu vertices", path.vertices.size());
				continue;
			}
			if (pathMesh.vertices.size() > kMaxMeshVertices) {
				LOG_WARNING(Engine, "Path tessellates to %zu vertices, more than one mesh can index; dropping it", pathMesh.vertices.size());
				continue;
			}
			paths.push_back({
				.firstVertex = static_cast<uint32_t>(vertices.size()),
				.vertexCount = static_cast<uint32_t>(pathMesh.vertices.size()),
				.firstIndex = static_cast<uint32_t>(indices.size()),
				.indexCount = static_cast<uint32_t>(pathMesh.indices.size()),
				.minPixelsPerMeter = path.minPixelsPerMeter,
			});
			vertices.insert(vertices.end(), pathMesh.vertices.begin(), pathMesh.vertices.end());
			colors.insert(colors.end(), pathMesh.vertices.size(), path.fillColor);
			indices.insert(indices.end(), pathMesh.indices.begin(), pathMesh.indices.end());
		}
		tessellateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	bool TessellatedPaths::meshesAt(float pixelsPerMeter, std::vector<renderer::TessellatedMesh>& outMeshes) const {
		outMeshes.clear();
		for (const Path& path : paths) {
			if (path.minPixelsPerMeter > pixelsPerMeter) {
				continue;
			}
			// Start the next mesh when this path would push the current one past the 16-bit index range
			if (outMeshes.empty() || outMeshes.back().vertices.size() + path.vertexCount > kMaxMeshVertices) {
				outMeshes.emplace_back();
			}
			renderer::TessellatedMesh& mesh = outMeshes.back();
			const auto				   base = static_cast<uint16_t>(mesh.vertices.size());
			const auto				   firstVertex = static_cast<std::ptrdiff_t>(path.firstVertex);
			const auto				   lastVertex = firstVertex + static_cast<std::ptrdiff_t>(path.vertexCount);
			mesh.vertices.insert(mesh.vertices.end(), vertices.begin() + firstVertex, vertices.begin() + lastVertex);
			mesh.colors.insert(mesh.colors.end(), colors.begin() + firstVertex, colors.begin() + lastVertex);
			for (uint32_t i = 0; i < path.indexCount; ++i) {
				mesh.indices.push_back(static_cast<uint16_t>(base + indices[path.firstIndex + i]));
			}
		}
		return !outMeshes.empty();
	}

} // namespace assets
