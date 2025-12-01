#include "Tessellator.h"
#include "utils/Log.h"
#include <algorithm>
#include <cmath>

namespace renderer {

	// Internal vertex structure for tessellation
	struct Tessellator::Vertex {
		Foundation::Vec2 position;
		size_t			 originalIndex;
		bool			 isEar = false; // For ear clipping
		bool			 isProcessed = false;
	};

	// Internal edge structure
	struct Tessellator::Edge {
		size_t startIndex{};
		size_t endIndex{};
	};

	// Internal event structure for sweep line
	struct Tessellator::Event {
		Foundation::Vec2 position;
		size_t			 vertexIndex{};
		VertexType		 type{};

		// Sort events by Y (top to bottom), then X (left to right)
		bool operator<(const Event& other) const {
			if (std::abs(position.y - other.position.y) < 1e-6F) {
				return position.x < other.position.x;
			}
			return position.y < other.position.y;
		}
	};

	Tessellator::Tessellator() = default; // NOLINT(cppcoreguidelines-pro-type-member-init)

	Tessellator::~Tessellator() = default; // NOLINT(performance-trivially-destructible)

	bool Tessellator::Tessellate( // NOLINT(readability-convert-member-functions-to-static)
		const VectorPath&		  path,
		TessellatedMesh&		  outMesh,
		const TessellatorOptions& options
	) {
		// Clear output
		outMesh.clear();

		// Validate input
		if (path.vertices.size() < 3) {
			LOG_ERROR(Renderer, "Tessellator: Path must have at least 3 vertices");
			return false;
		}

		if (!path.isClosed) {
			LOG_WARNING(Renderer, "Tessellator: Path is not closed, closing it automatically");
		}

		// For Phase 0: Use simple ear clipping algorithm
		// This is O(n²) but simple and works for all simple polygons
		// We'll replace with monotone decomposition for Phase 1

		// Compute signed area using the shoelace formula to determine winding order
		// Positive = CCW, Negative = CW
		float signedArea = 0.0F;
		for (size_t i = 0; i < path.vertices.size(); ++i) {
			size_t j = (i + 1) % path.vertices.size();
			signedArea +=
				(path.vertices[i].x * path.vertices[j].y) - (path.vertices[j].x * path.vertices[i].y);
		}
		signedArea *= 0.5F;
		bool isCCW = (signedArea > 0);

		// Copy vertices to mesh (tessellation doesn't add new vertices for simple polygons)
		// If winding is CW, reverse the order to make it CCW for consistent ear clipping
		if (isCCW) {
			outMesh.vertices = path.vertices;
		} else {
			outMesh.vertices.reserve(path.vertices.size());
			for (auto it = path.vertices.rbegin(); it != path.vertices.rend(); ++it) {
				outMesh.vertices.push_back(*it);
			}
		}

		// Build internal vertex list from the (possibly reversed) mesh vertices
		tessVertices.clear();
		tessVertices.reserve(outMesh.vertices.size());
		for (size_t i = 0; i < outMesh.vertices.size(); ++i) {
			Vertex v;
			v.position = outMesh.vertices[i];
			v.originalIndex = i;
			v.isProcessed = false;
			tessVertices.push_back(v);
		}

		// Ear clipping algorithm
		std::vector<size_t> remainingVertices;
		for (size_t i = 0; i < tessVertices.size(); ++i) {
			remainingVertices.push_back(i);
		}

		// Reserve space for indices (n-2 triangles, 3 indices each)
		outMesh.indices.reserve((tessVertices.size() - 2) * 3);

		// Keep clipping ears until we have a triangle
		while (remainingVertices.size() > 3) {
			bool earFound = false;

			for (size_t i = 0; i < remainingVertices.size(); ++i) {
				size_t prevIdx = (i == 0) ? remainingVertices.size() - 1 : i - 1;
				size_t nextIdx = (i + 1) % remainingVertices.size();

				size_t v0 = remainingVertices[prevIdx];
				size_t v1 = remainingVertices[i];
				size_t v2 = remainingVertices[nextIdx];

				Foundation::Vec2 p0 = tessVertices[v0].position;
				Foundation::Vec2 p1 = tessVertices[v1].position;
				Foundation::Vec2 p2 = tessVertices[v2].position;

				// Check if this forms a valid ear
				// 1. Must be a convex vertex (interior angle < 180°)
				Foundation::Vec2 edge1 = p1 - p0;
				Foundation::Vec2 edge2 = p2 - p1;
				float			 cross = (edge1.x * edge2.y) - (edge1.y * edge2.x);

				if (cross <= 0) {
					// Concave vertex, skip
					continue;
				}

				// 2. No other vertices should be inside this triangle
				bool hasInteriorVertex = false;
				for (size_t j = 0; j < remainingVertices.size(); ++j) {
					if (j == prevIdx || j == i || j == nextIdx) {
						continue;
					}

					Foundation::Vec2 p = tessVertices[remainingVertices[j]].position;

					// Point-in-triangle test using barycentric coordinates
					float denom = (((p1.y - p2.y) * (p0.x - p2.x)) + ((p2.x - p1.x) * (p0.y - p2.y)));
					if (std::abs(denom) < 1e-6F) {
						continue; // Degenerate triangle
					}

					float a = ((p1.y - p2.y) * (p.x - p2.x) + (p2.x - p1.x) * (p.y - p2.y)) / denom;
					float b = ((p2.y - p0.y) * (p.x - p2.x) + (p0.x - p2.x) * (p.y - p2.y)) / denom;
					float c = 1.0F - a - b;

					if (a >= 0 && b >= 0 && c >= 0) {
						hasInteriorVertex = true;
						break;
					}
				}

				if (!hasInteriorVertex) {
					// Found an ear! Add triangle
					outMesh.indices.push_back(static_cast<uint16_t>(v0));
					outMesh.indices.push_back(static_cast<uint16_t>(v1));
					outMesh.indices.push_back(static_cast<uint16_t>(v2));

					// Remove this vertex from the remaining list
					remainingVertices.erase(remainingVertices.begin() + i);
					earFound = true;
					break;
				}
			}

			if (!earFound) {
				LOG_ERROR(Renderer, "Tessellator: Failed to find ear (possible degenerate or self-intersecting polygon)");
				return false;
			}
		}

		// Add the final triangle
		if (remainingVertices.size() == 3) {
			outMesh.indices.push_back(static_cast<uint16_t>(remainingVertices[0]));
			outMesh.indices.push_back(static_cast<uint16_t>(remainingVertices[1]));
			outMesh.indices.push_back(static_cast<uint16_t>(remainingVertices[2]));
		}

		LOG_DEBUG(Renderer, "Tessellated polygon: %zu vertices → %zu triangles", path.vertices.size(), outMesh.getTriangleCount());
		return true;
	}

	void Tessellator::buildEvents(const VectorPath& path) {
		// Placeholder for monotone decomposition (Phase 1+)
		// Will classify vertices and build sorted event queue
	}

	void Tessellator::processEvents(TessellatedMesh& outMesh) {
		// Placeholder for monotone decomposition (Phase 1+)
		// Will process events with sweep line algorithm
	}

	Tessellator::VertexType Tessellator::ClassifyVertex( // NOLINT(readability-convert-member-functions-to-static)
		size_t vertexIndex
	) const {
		// Placeholder for monotone decomposition (Phase 1+)
		// Will classify vertex as Start, End, Split, Merge, or Regular
		return VertexType::Regular;
	}

	bool Tessellator::compareVertices(const Foundation::Vec2& a, const Foundation::Vec2& b) {
		// Sort by Y (top to bottom), then X (left to right)
		if (std::abs(a.y - b.y) < 1e-6F) {
			return a.x < b.x;
		}
		return a.y < b.y;
	}

} // namespace renderer
