// NANOSVG_IMPLEMENTATION must be defined in exactly ONE .cpp file before including the header
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#include "SVGLoader.h"
#include "Bezier.h"
#include "Tessellator.h"
#include "utils/Log.h"
#include <cmath>
#include <memory>

namespace renderer {

// RAII wrapper for NanoSVG image resources
struct NSVGImageDeleter {
	void operator()(NSVGimage* img) const {
		if (img != nullptr) {
			nsvgDelete(img);
		}
	}
};
using NSVGImagePtr = std::unique_ptr<NSVGimage, NSVGImageDeleter>;

namespace {

/// Convert NanoSVG ABGR color to Foundation::Color
Foundation::Color convertColor(unsigned int nsvgColor, float opacity) {
	// NanoSVG stores colors as 32-bit ABGR: alpha in bits 24-31, blue in bits 16-23, green in bits 8-15, red in bits 0-7
	const float kColorScale = 1.0F / 255.0F;
	float		red = static_cast<float>((nsvgColor >> 0) & 0xFF) * kColorScale;
	float		green = static_cast<float>((nsvgColor >> 8) & 0xFF) * kColorScale;
	float		blue = static_cast<float>((nsvgColor >> 16) & 0xFF) * kColorScale;
	float		alpha = static_cast<float>((nsvgColor >> 24) & 0xFF) * kColorScale * opacity;
	return Foundation::Color(red, green, blue, alpha);
}

/// Convert a NanoSVG gradient to our GradientFill (stops converted to Foundation::Color).
/// NanoSVG precomputes grad->xform as the shape-space -> gradient-space transform, so we keep
/// it verbatim and evaluate per vertex later.
GradientFill convertGradient(char paintType, const NSVGgradient* grad, float opacity) {
	GradientFill out;
	if (grad == nullptr) {
		return out;
	}
	out.type = (paintType == NSVG_PAINT_RADIAL_GRADIENT) ? GradientFill::Type::Radial : GradientFill::Type::Linear;
	out.spread = grad->spread;
	for (int i = 0; i < 6; ++i) {
		out.xform[i] = grad->xform[i];
	}
	out.stops.reserve(static_cast<size_t>(grad->nstops));
	for (int i = 0; i < grad->nstops; ++i) {
		GradientStop stop;
		stop.offset = grad->stops[i].offset;
		stop.color = convertColor(grad->stops[i].color, opacity);
		out.stops.push_back(stop);
	}
	return out;
}

/// Process a single NSVGpath into a VectorPath
void processPath(NSVGpath* nsvgPath, float tolerance, VectorPath& outPath) {
	outPath.isClosed = (nsvgPath->closed != 0);
	outPath.vertices.clear();

	// NanoSVG stores paths as sequences of cubic Bezier curves:
	// First point: pts[0], pts[1]
	// Each subsequent segment: 6 floats for control points and endpoint
	// Format: [x0,y0, (cp1x,cp1y,cp2x,cp2y,x1,y1), ...]

	if (nsvgPath->npts < 4) {
		return; // Need at least 4 points for one cubic Bezier segment
	}

	// Add the first point
	outPath.vertices.push_back(Foundation::Vec2{nsvgPath->pts[0], nsvgPath->pts[1]});

	// Process each cubic Bezier segment
	// Each segment uses 4 points: segment 0 uses points 0-3, segment 1 uses points 3-6, etc.
	// NanoSVG stores: start point, then groups of (cp1, cp2, end) = 3 points per segment
	int numSegments = (nsvgPath->npts - 1) / 3;
	for (int seg = 0; seg < numSegments; seg++) {
		// Each segment uses points seg*3 to seg*3+3: start, cp1, cp2, end
		float* p = &nsvgPath->pts[seg * 3 * 2];

		CubicBezier curve{
			.p0 = {p[0], p[1]}, // Current point (start of this segment)
			.p1 = {p[2], p[3]}, // Control point 1
			.p2 = {p[4], p[5]}, // Control point 2
			.p3 = {p[6], p[7]}	// End point
		};

		// Flatten the Bezier curve using existing function
		// Note: flattenCubicBezier appends points but does NOT include p0
		flattenCubicBezier(curve, tolerance, outPath.vertices);
	}

	// For closed paths, NanoSVG includes the closing segment back to the first point,
	// which means the last vertex equals the first. The tessellator expects a polygon
	// WITHOUT the duplicate closing vertex (it implicitly closes the polygon).
	// Remove the duplicate if present.
	if (outPath.isClosed && outPath.vertices.size() >= 2) {
		const auto& first = outPath.vertices.front();
		const auto& last = outPath.vertices.back();
		constexpr float kEpsilon = 1e-2F; // Use 0.01 pixels for robust duplicate detection
		if (std::abs(first.x - last.x) < kEpsilon && std::abs(first.y - last.y) < kEpsilon) {
			outPath.vertices.pop_back();
		}
	}
}

constexpr size_t kMaxMeshVertices = 65535; // indices are 16-bit

/// Append a centered stroke band of `width` along a polyline, with bevel joins. Closed polylines
/// stroke all the way around; open ones get butt ends. The stroke color is baked per vertex. The
/// join's outer wedge is picked from the local turn direction, so this is winding-independent.
void appendStrokeBand(const std::vector<Foundation::Vec2>& pts, bool closed, float width,
					  const Foundation::Color& color, TessellatedMesh& outMesh) {
	const size_t n = pts.size();
	if (n < 2 || width <= 0.0F) {
		return;
	}
	const float hw = width * 0.5F;

	auto addVert = [&](float x, float y) -> uint16_t {
		const auto idx = static_cast<uint16_t>(outMesh.vertices.size());
		outMesh.vertices.push_back(Foundation::Vec2{x, y});
		outMesh.colors.push_back(color);
		return idx;
	};
	auto addTri = [&](uint16_t a, uint16_t b, uint16_t c) {
		outMesh.indices.push_back(a);
		outMesh.indices.push_back(b);
		outMesh.indices.push_back(c);
	};

	const size_t segCount = closed ? n : n - 1;
	for (size_t i = 0; i < segCount; ++i) {
		if (outMesh.vertices.size() + 4 > kMaxMeshVertices) {
			return;
		}
		const Foundation::Vec2& a = pts[i];
		const Foundation::Vec2& b = pts[(i + 1) % n];
		const float				dx = b.x - a.x;
		const float				dy = b.y - a.y;
		const float				len = std::sqrt((dx * dx) + (dy * dy));
		if (len < 1e-6F) {
			continue;
		}
		const float	   nx = (-dy / len) * hw; // left normal * half-width
		const float	   ny = (dx / len) * hw;
		const uint16_t a0 = addVert(a.x + nx, a.y + ny);
		const uint16_t a1 = addVert(a.x - nx, a.y - ny);
		const uint16_t b0 = addVert(b.x + nx, b.y + ny);
		const uint16_t b1 = addVert(b.x - nx, b.y - ny);
		addTri(a0, b0, b1);
		addTri(a0, b1, a1);
	}

	const size_t firstJoin = closed ? 0 : 1;
	const size_t lastJoin = closed ? n : n - 1;
	for (size_t k = firstJoin; k < lastJoin; ++k) {
		if (outMesh.vertices.size() + 3 > kMaxMeshVertices) {
			return;
		}
		const Foundation::Vec2& prev = pts[(k + n - 1) % n];
		const Foundation::Vec2& cur = pts[k];
		const Foundation::Vec2& next = pts[(k + 1) % n];
		float					d0x = cur.x - prev.x;
		float					d0y = cur.y - prev.y;
		float					d1x = next.x - cur.x;
		float					d1y = next.y - cur.y;
		const float				l0 = std::sqrt((d0x * d0x) + (d0y * d0y));
		const float				l1 = std::sqrt((d1x * d1x) + (d1y * d1y));
		if (l0 < 1e-6F || l1 < 1e-6F) {
			continue;
		}
		d0x /= l0;
		d0y /= l0;
		d1x /= l1;
		d1y /= l1;
		const float cross = (d0x * d1y) - (d0y * d1x);
		if (std::abs(cross) < 1e-4F) {
			continue; // nearly straight; the segment quads already meet
		}
		const float	   n0x = -d0y * hw;
		const float	   n0y = d0x * hw;
		const float	   n1x = -d1y * hw;
		const float	   n1y = d1x * hw;
		const float	   s = (cross > 0.0F) ? -1.0F : 1.0F; // outer wedge side
		const uint16_t c = addVert(cur.x, cur.y);
		const uint16_t p0 = addVert(cur.x + (s * n0x), cur.y + (s * n0y));
		const uint16_t p1 = addVert(cur.x + (s * n1x), cur.y + (s * n1y));
		addTri(c, p0, p1);
	}
}

} // anonymous namespace

bool loadSVG(const std::string& filepath, float curveTolerance, std::vector<LoadedSVGShape>& outShapes) {
	outShapes.clear();

	// Parse the SVG file with RAII wrapper for automatic cleanup
	// Units: "px" = pixels, DPI: 96 is standard for screen rendering
	NSVGImagePtr image(nsvgParseFromFile(filepath.c_str(), "px", 96.0F));
	if (image == nullptr) {
		LOG_ERROR(Renderer, "Failed to load SVG: %s", filepath.c_str());
		return false;
	}

	LOG_DEBUG(Renderer, "Loading SVG: %s (%.1fx%.1f)", filepath.c_str(), image->width, image->height);

	// Process each shape in the SVG
	for (NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
		// Skip invisible shapes
		if ((shape->flags & NSVG_FLAGS_VISIBLE) == 0) {
			continue;
		}

		// Skip only shapes that have neither a fill nor a stroke.
		const bool fillNone = (shape->fill.type == NSVG_PAINT_NONE);
		const bool strokeNone = (shape->stroke.type == NSVG_PAINT_NONE) || (shape->strokeWidth <= 0.0F);
		if (fillNone && strokeNone) {
			continue;
		}

		LoadedSVGShape loadedShape;
		loadedShape.width = image->width;
		loadedShape.height = image->height;
		loadedShape.hasFill = !fillNone;

		// Extract fill: solid color, or a linear/radial gradient (evaluated per vertex downstream)
		if (shape->fill.type == NSVG_PAINT_COLOR) {
			loadedShape.fillColor = convertColor(shape->fill.color, shape->opacity);
		} else if (shape->fill.type == NSVG_PAINT_LINEAR_GRADIENT || shape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) {
			loadedShape.gradient = convertGradient(shape->fill.type, shape->fill.gradient, shape->opacity);
			// Solid fallback (first stop) for any consumer that ignores the gradient.
			loadedShape.fillColor =
				loadedShape.gradient.stops.empty() ? Foundation::Color::white() : loadedShape.gradient.stops.front().color;
		} else if (!fillNone) {
			loadedShape.fillColor = Foundation::Color::white();
		}

		// Extract stroke (solid color; gradient strokes fall back to their first stop).
		if (!strokeNone) {
			loadedShape.hasStroke = true;
			loadedShape.strokeWidth = shape->strokeWidth;
			if (shape->stroke.type == NSVG_PAINT_COLOR) {
				loadedShape.strokeColor = convertColor(shape->stroke.color, shape->opacity);
			} else if (shape->stroke.type == NSVG_PAINT_LINEAR_GRADIENT || shape->stroke.type == NSVG_PAINT_RADIAL_GRADIENT) {
				const GradientFill sg = convertGradient(shape->stroke.type, shape->stroke.gradient, shape->opacity);
				loadedShape.strokeColor = sg.stops.empty() ? Foundation::Color::white() : sg.stops.front().color;
			} else {
				loadedShape.strokeColor = Foundation::Color::white();
			}
		}

		// Process each path in the shape
		for (NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
			VectorPath vectorPath;
			processPath(path, curveTolerance, vectorPath);

			if (vectorPath.vertices.size() >= 3) {
				loadedShape.paths.push_back(std::move(vectorPath));
			}
		}

		if (!loadedShape.paths.empty()) {
			outShapes.push_back(std::move(loadedShape));
		}
	}

	// NSVGImagePtr automatically cleans up when going out of scope
	LOG_INFO(Renderer, "Loaded SVG: %s (%zu shapes)", filepath.c_str(), outShapes.size());
	return !outShapes.empty();
}

void appendShapeMesh(const LoadedSVGShape& shape, TessellatedMesh& outMesh) {
	// --- Fill ---
	if (shape.hasFill) {
		Tessellator tessellator;
		const bool	hasGradient = (shape.gradient.type != GradientFill::Type::None);

		TessellatorOptions options;
		// Only honored for a single subpath (where a convex fan may apply); multi-subpath shapes go
		// through the sweep, which ignores it. Setting it there would be an accepted-but-dropped option.
		options.fanFromCentroid = hasGradient && shape.paths.size() == 1;

		// Tessellate all of a shape's subpaths together so the nonzero fill rule carves holes where
		// subpaths overlap (the SVG hole convention), instead of filling each subpath solid.
		TessellatedMesh shapeMesh;
		bool			ok = false;
		if (shape.paths.size() == 1) {
			if (shape.paths[0].vertices.size() >= 3) {
				ok = tessellator.Tessellate(shape.paths[0], shapeMesh, options);
			}
		} else {
			std::vector<VectorPath> contours;
			contours.reserve(shape.paths.size());
			for (const auto& path : shape.paths) {
				if (path.vertices.size() >= 3) {
					contours.push_back(path);
				}
			}
			if (!contours.empty()) {
				ok = tessellator.Tessellate(contours, shapeMesh, options);
			}
		}

		if (!ok) {
			LOG_WARNING(Renderer, "appendShapeMesh: failed to tessellate fill (%zu subpaths)", shape.paths.size());
		} else if (outMesh.vertices.size() + shapeMesh.vertices.size() > kMaxMeshVertices) {
			LOG_WARNING(Renderer, "appendShapeMesh: mesh would exceed %zu vertices; dropping fill", kMaxMeshVertices);
		} else {
			const auto baseIndex = static_cast<uint16_t>(outMesh.vertices.size());
			for (const auto& v : shapeMesh.vertices) {
				outMesh.vertices.push_back(v);
				outMesh.colors.push_back(hasGradient ? shape.gradient.colorAt(v) : shape.fillColor);
			}
			for (const auto idx : shapeMesh.indices) {
				outMesh.indices.push_back(static_cast<uint16_t>(baseIndex + idx));
			}
		}
	}

	// --- Stroke (on top of the fill, matching SVG paint order) ---
	if (shape.hasStroke && shape.strokeWidth > 0.0F) {
		for (const auto& sub : shape.paths) {
			appendStrokeBand(sub.vertices, sub.isClosed, shape.strokeWidth, shape.strokeColor, outMesh);
		}
	}
}

} // namespace renderer
