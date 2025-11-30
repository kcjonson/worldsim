#pragma once

#include "Types.h"

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
	class Tessellator { // NOLINT(cppcoreguidelines-special-member-functions)
	  public:
		Tessellator();
		~Tessellator(); // NOLINT(performance-trivially-destructible) - Defined in .cpp (needs complete types for vectors)

		// Delete copy/move operations (Rule of 5)
		Tessellator(const Tessellator&) = delete;
		Tessellator& operator=(const Tessellator&) = delete;
		Tessellator(Tessellator&&) = delete;
		Tessellator& operator=(Tessellator&&) = delete;

		// Tessellate a path into triangles
		// Returns true on success, false on error
		bool Tessellate(const VectorPath& path, TessellatedMesh& outMesh, const TessellatorOptions& options = {});

	  private:
		// Internal structures for sweep line algorithm
		struct Vertex;
		struct Edge;
		struct Event;

		// Phase 1: Build events from path vertices
		void buildEvents(const VectorPath& path);

		// Phase 2: Process events with sweep line
		void processEvents(TessellatedMesh& outMesh);

		// Helper: Determine vertex type (start, end, split, merge, regular)
		enum class VertexType : std::uint8_t { // NOLINT(performance-enum-size)
			Start,
			End,
			Split,
			Merge,
			Regular
		};
		VertexType ClassifyVertex(size_t vertexIndex) const; // NOLINT(readability-convert-member-functions-to-static)

		// Helper: Compare vertices by Y coordinate (primary), then X (secondary)
		static bool compareVertices(const Foundation::Vec2& a, const Foundation::Vec2& b);

		// Working data (cleared between tessellations)
		std::vector<Event>	events;
		std::vector<Vertex> tessVertices;
		std::vector<Edge>	edges;
	};

} // namespace renderer
