// NANOSVG_IMPLEMENTATION must be defined in exactly ONE .cpp file before including the header
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#include "SVGLoader.h"
#include "Bezier.h"
#include "utils/Log.h"
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

		// Only handle filled shapes for now (strokes would require different handling)
		if (shape->fill.type == NSVG_PAINT_NONE) {
			continue;
		}

		LoadedSVGShape loadedShape;
		loadedShape.width = image->width;
		loadedShape.height = image->height;

		// Extract fill color
		if (shape->fill.type == NSVG_PAINT_COLOR) {
			loadedShape.fillColor = convertColor(shape->fill.color, shape->opacity);
		} else {
			// Gradients not supported yet - use white as fallback
			loadedShape.fillColor = Foundation::Color::white();
			LOG_DEBUG(Renderer, "SVG shape uses gradient fill (not supported), using white");
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

} // namespace renderer
