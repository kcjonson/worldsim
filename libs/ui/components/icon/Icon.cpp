#include "Icon.h"

#include "primitives/Primitives.h"
#include "vector/SVGLoader.h"
#include "vector/Tessellator.h"

#include <algorithm>

namespace UI {

	Icon::Icon(const Args& args)
		: svgPath(args.svgPath),
		  iconSize(args.size),
		  tint(args.tint) {
		position = args.position;
		size = {args.size, args.size};
		margin = args.margin;

		if (!svgPath.empty()) {
			rebuildMesh();
		}
	}

	void Icon::setSvgPath(const std::string& path) {
		if (path != svgPath) {
			svgPath = path;
			rebuildMesh();
		}
	}

	void Icon::setTint(Foundation::Color color) {
		tint = color;
	}

	void Icon::setIconSize(float newSize) {
		if (newSize != iconSize) {
			iconSize = newSize;
			size = {newSize, newSize};
			applyScaleToVertices();
		}
	}

	void Icon::setPosition(float x, float y) {
		position = {x, y};
	}

	void Icon::rebuildMesh() {
		originalVertices.clear();
		vertices.clear();
		indices.clear();

		if (svgPath.empty()) {
			return;
		}

		// Load SVG and flatten Bezier curves
		std::vector<renderer::LoadedSVGShape> shapes;
		if (!renderer::loadSVG(svgPath, 0.5F, shapes)) {
			// SVG load failed - leave mesh empty
			return;
		}

		if (shapes.empty()) {
			return;
		}

		// Store original dimensions for scaling
		originalWidth = shapes[0].width;
		originalHeight = shapes[0].height;

		// Tessellate each shape's paths and cache original vertices
		renderer::Tessellator tessellator;

		for (const auto& shape : shapes) {
			for (const auto& path : shape.paths) {
				renderer::TessellatedMesh mesh;
				if (tessellator.Tessellate(path, mesh)) {
					// Append vertices at original scale (for caching)
					size_t baseIndex = originalVertices.size();
					for (const auto& v : mesh.vertices) {
						originalVertices.push_back(v);
					}

					// Append indices with offset
					for (uint16_t idx : mesh.indices) {
						indices.push_back(static_cast<uint16_t>(baseIndex + idx));
					}
				}
			}
		}

		// Apply current scale to create render-ready vertices
		applyScaleToVertices();
	}

	void Icon::applyScaleToVertices() {
		if (originalVertices.empty() || (originalWidth == 0.0F && originalHeight == 0.0F)) {
			return;
		}

		// Calculate scale factor to fit icon into size × size square
		// Maintain aspect ratio, fit within bounds
		float scaleX = (originalWidth > 0.0F) ? iconSize / originalWidth : 1.0F;
		float scaleY = (originalHeight > 0.0F) ? iconSize / originalHeight : 1.0F;
		float scale = std::min(scaleX, scaleY);

		// Calculate centering offset
		float scaledWidth = originalWidth * scale;
		float scaledHeight = originalHeight * scale;
		float offsetX = (iconSize - scaledWidth) / 2.0F;
		float offsetY = (iconSize - scaledHeight) / 2.0F;

		// Transform cached original vertices to scaled vertices (no disk I/O!)
		vertices.clear();
		vertices.reserve(originalVertices.size());

		for (const auto& v : originalVertices) {
			vertices.push_back({v.x * scale + offsetX, v.y * scale + offsetY});
		}
	}

	void Icon::render() {
		if (!visible || vertices.empty() || indices.empty()) {
			return;
		}

		Foundation::Vec2 contentPos = getContentPosition();

		// Create translated vertices for rendering
		std::vector<Foundation::Vec2> translatedVerts;
		translatedVerts.reserve(vertices.size());
		for (const auto& v : vertices) {
			translatedVerts.push_back({v.x + contentPos.x, v.y + contentPos.y});
		}

		Renderer::Primitives::drawTriangles(
			Renderer::Primitives::TrianglesArgs{
				.vertices = translatedVerts.data(),
				.indices = indices.data(),
				.vertexCount = translatedVerts.size(),
				.indexCount = indices.size(),
				.color = tint,
				.zIndex = zIndex,
			}
		);
	}

} // namespace UI
