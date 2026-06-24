#pragma once

#include "math/Types.h"
#include <graphics/Color.h>
#include <cmath>
#include <cstddef>
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

	// A single gradient color stop (offset in [0,1]).
	struct GradientStop {
		float			  offset{0.0F};
		Foundation::Color color;
	};

	// Gradient fill parsed from an SVG. `xform` is NanoSVG's transform mapping shape-space
	// coordinates into gradient space (the same convention nanosvgrast uses), so a color can be
	// evaluated per vertex: linear gradients use the y' axis, radial use distance from the origin.
	struct GradientFill {
		enum class Type { None, Linear, Radial };

		Type					  type{Type::None};
		float					  xform[6]{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
		int						  spread{0}; // 0=pad, 1=reflect, 2=repeat (matches NSVGspread)
		std::vector<GradientStop> stops;

		// Evaluate the gradient color at a point expressed in xform-compatible shape space.
		Foundation::Color colorAt(Foundation::Vec2 p) const {
			if (stops.empty()) {
				return Foundation::Color::white();
			}

			const float gy = (p.x * xform[1]) + (p.y * xform[3]) + xform[5];
			float		t = gy;
			if (type == Type::Radial) {
				const float gx = (p.x * xform[0]) + (p.y * xform[2]) + xform[4];
				t = std::sqrt((gx * gx) + (gy * gy));
			}

			if (spread == 1) { // reflect
				float whole = 0.0F;
				float frac = std::modf(std::fabs(t), &whole);
				if ((static_cast<int>(whole) & 1) != 0) {
					frac = 1.0F - frac;
				}
				t = frac;
			} else if (spread == 2) { // repeat
				t = t - std::floor(t);
			} else { // pad
				t = (t < 0.0F) ? 0.0F : ((t > 1.0F) ? 1.0F : t);
			}

			if (t <= stops.front().offset) {
				return stops.front().color;
			}
			if (t >= stops.back().offset) {
				return stops.back().color;
			}
			for (std::size_t i = 1; i < stops.size(); ++i) {
				if (t <= stops[i].offset) {
					const GradientStop& lo = stops[i - 1];
					const GradientStop& hi = stops[i];
					const float			span = hi.offset - lo.offset;
					const float			f = (span > 1e-6F) ? (t - lo.offset) / span : 0.0F;
					return Foundation::Color(
						lo.color.r + ((hi.color.r - lo.color.r) * f),
						lo.color.g + ((hi.color.g - lo.color.g) * f),
						lo.color.b + ((hi.color.b - lo.color.b) * f),
						lo.color.a + ((hi.color.a - lo.color.a) * f)
					);
				}
			}
			return stops.back().color;
		}
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
