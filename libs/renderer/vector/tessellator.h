#pragma once

#include "types.h"

namespace renderer {

	// Tessellator options
	struct TessellatorOptions {
		// Fill rule: true = non-zero, false = even-odd
		bool useNonZeroFillRule = true;

		// Tolerance for curve flattening (smaller = more vertices)
		// Not used in Phase 0 (no curves yet), but planned for future
		float curveFlatteningTolerance = 0.5F;
	};

	// Tessellator class - converts VectorPath to TessellatedMesh
	// Currently implements simplified monotone decomposition for Phase 0
	// Based on Lyon's approach but simplified for simple polygons
	class Tessellator {
	  public:
		Tessellator();
		~Tessellator() = default;

		Tessellator(const Tessellator&) = default;
		Tessellator& operator=(const Tessellator&) = default;
		Tessellator(Tessellator&&) = default;
		Tessellator& operator=(Tessellator&&) = default;

		// Tessellate a path into triangles
		// Returns true on success, false on error
		bool Tessellate(const VectorPath& path, TessellatedMesh& outMesh, const TessellatorOptions& options = {});

	  private:
		// Helper: Determine vertex type (start, end, split, merge, regular)
		enum class VertexType : std::uint8_t // NOLINT(performance-enum-size) { Start, End, Split, Merge, Regular };

		// Internal structures for sweep line algorithm
		struct Vertex {
			Foundation::Vec2 position;
			size_t			 originalIndex{};
			bool			 isEar{false};
			bool			 isProcessed{false};
		};

		struct Edge {
			size_t startIndex{0};
			size_t endIndex{0};
		};

		struct Event {
			Foundation::Vec2 position;
			size_t			 vertexIndex{0};
			VertexType		 type{};

			bool operator<(const Event& other) const {
				if (std::abs(position.y - other.position.y) < 1e-6F) {
					return position.x < other.position.x;
				}
				return position.y < other.position.y;
			}
		};

		// Phase 1: Build events from path vertices
		void BuildEvents(const VectorPath& path);

		// Phase 2: Process events with sweep line
		void ProcessEvents(TessellatedMesh& outMesh);

		VertexType ClassifyVertex(size_t vertexIndex) const;

		// Helper: Compare vertices by Y coordinate (primary), then X (secondary)
		static bool CompareVertices(const Foundation::Vec2& a, const Foundation::Vec2& b);

		// Working data (cleared between tessellations)
		std::vector<Event>	m_events;
		std::vector<Vertex> m_vertices;
		std::vector<Edge>	m_edges;
	};

} // namespace renderer
