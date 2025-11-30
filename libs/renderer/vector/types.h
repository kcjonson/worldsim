#pragma once

#include "math/types.h"
#include <cstdint>
#include <vector>

namespace renderer {

	// Forward declarations
	struct VectorPath;
	struct TessellatedMesh;

	// VectorPath represents a 2D polygon path with vertices
	struct VectorPath {
		std::vector<Foundation::Vec2> vertices;
		bool						  isClosed{true};

		VectorPath() = default;

		// Create path from vertex list
		VectorPath(const std::vector<Foundation::Vec2>& verts, bool closed = true)
			: vertices(verts),
			  isClosed(closed) {}

		// Convenience: Add a vertex
		void addVertex(Foundation::Vec2 v) { vertices.push_back(v); }

		// Get vertex count
		size_t getVertexCount() const { return vertices.size(); }

		// Clear all vertices
		void clear() { vertices.clear(); }
	};

	// TessellatedMesh represents the triangulated output of tessellation
	struct TessellatedMesh {
		std::vector<Foundation::Vec2> vertices; // Position data (x, y)
		std::vector<uint16_t>		  indices;	// Triangle indices (3 per triangle)

		TessellatedMesh() = default;

		// Get triangle count
		size_t getTriangleCount() const { return indices.size() / 3; }

		// Get vertex count
		size_t getVertexCount() const { return vertices.size(); }

		// Clear all data
		void clear() {
			vertices.clear();
			indices.clear();
		}

		// Reserve memory for vertices and indices
		void reserve(size_t vertexCount, size_t indexCount) {
			vertices.reserve(vertexCount);
			indices.reserve(indexCount);
		}
	};

} // namespace renderer
