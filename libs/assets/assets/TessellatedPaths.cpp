#include "assets/TessellatedPaths.h"

#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <chrono>

namespace assets {

	namespace {

		// The paths drawn at pixelsPerMeter packed into meshes, each vertex colored colorOf(path, vertex).
		template <typename ColorOf>
		bool packMeshes(
			const TessellatedPaths& source, float pixelsPerMeter, std::vector<renderer::TessellatedMesh>& outMeshes, const ColorOf& colorOf
		) {
			outMeshes.clear();
			for (const TessellatedPaths::Path& path : source.paths) {
				if (path.minPixelsPerMeter > pixelsPerMeter) {
					continue;
				}
				// Start the next mesh when this path would push the current one past the 16-bit index range
				if (outMeshes.empty() || outMeshes.back().vertices.size() + path.vertexCount > TessellatedPaths::kMaxMeshVertices) {
					outMeshes.emplace_back();
				}
				renderer::TessellatedMesh& mesh = outMeshes.back();
				const auto				   base = static_cast<uint16_t>(mesh.vertices.size());
				const auto				   firstVertex = static_cast<std::ptrdiff_t>(path.firstVertex);
				const auto				   lastVertex = firstVertex + static_cast<std::ptrdiff_t>(path.vertexCount);
				mesh.vertices.insert(mesh.vertices.end(), source.vertices.begin() + firstVertex, source.vertices.begin() + lastVertex);
				for (uint32_t v = path.firstVertex; v < path.firstVertex + path.vertexCount; ++v) {
					mesh.colors.push_back(colorOf(path, v));
				}
				for (uint32_t i = 0; i < path.indexCount; ++i) {
					mesh.indices.push_back(static_cast<uint16_t>(base + source.indices[path.firstIndex + i]));
				}
			}
			return !outMeshes.empty();
		}

	} // namespace

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
				.height = path.height.value_or(0.0F),
			});
			vertices.insert(vertices.end(), pathMesh.vertices.begin(), pathMesh.vertices.end());
			colors.insert(colors.end(), pathMesh.vertices.size(), path.fillColor);
			indices.insert(indices.end(), pathMesh.indices.begin(), pathMesh.indices.end());
		}
		tessellateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	bool TessellatedPaths::meshesAt(float pixelsPerMeter, std::vector<renderer::TessellatedMesh>& outMeshes) const {
		return packMeshes(*this, pixelsPerMeter, outMeshes, [this](const Path& /*path*/, uint32_t v) { return colors[v]; });
	}

	bool TessellatedPaths::heightMeshesAt(float pixelsPerMeter, std::vector<renderer::TessellatedMesh>& outMeshes) const {
		return packMeshes(*this, pixelsPerMeter, outMeshes, [this](const Path& path, uint32_t v) {
			return Foundation::Color(path.height, path.height, path.height, colors[v].a);
		});
	}

} // namespace assets
