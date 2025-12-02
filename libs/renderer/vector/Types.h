#pragma once

#include "math/Types.h"
#include <graphics/Color.h>
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
		std::vector<Foundation::Vec2>  vertices; // Position data (x, y)
		std::vector<uint16_t>		   indices;	 // Triangle indices (3 per triangle)
		std::vector<Foundation::Color> colors;	 // Per-vertex colors (parallel to vertices)

		TessellatedMesh() = default;

		// Get triangle count
		size_t getTriangleCount() const { return indices.size() / 3; }

		// Get vertex count
		size_t getVertexCount() const { return vertices.size(); }

		// Check if mesh has per-vertex colors
		bool hasColors() const { return !colors.empty() && colors.size() == vertices.size(); }

		// Clear all data
		void clear() {
			vertices.clear();
			indices.clear();
			colors.clear();
		}

		// Reserve memory for vertices, indices, and colors
		void reserve(size_t vertexCount, size_t indexCount) {
			vertices.reserve(vertexCount);
			indices.reserve(indexCount);
			colors.reserve(vertexCount);
		}
	};

} // namespace renderer
