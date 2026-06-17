#include "Icon.h"

#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"
#include "theme/IconGlyphs.h"
#include "vector/SVGLoader.h"
#include "vector/Tessellator.h"
#include "vector/Types.h"

#include <algorithm>

namespace UI {

	namespace {
		constexpr float kGlyphViewBox = 24.0F; // glyph authoring space
	}

	Icon::Icon(const Args& args)
		: svgPath(args.svgPath)
		, glyph(args.glyph)
		, iconSize(args.size)
		, tint(args.tint)
		, glyphStrokeBase(args.strokeWidth) {
		position = args.position;
		size = {args.size, args.size};
		margin = args.margin;

		if (!glyph.empty()) {
			glyphMode = true;
			buildGlyph();
		} else if (!svgPath.empty()) {
			rebuildMesh();
		}
	}

	void Icon::setSvgPath(const std::string& path) {
		if (path != svgPath) {
			svgPath = path;
			glyph.clear();
			glyphMode = false;
			rebuildMesh();
		}
	}

	void Icon::setTint(Foundation::Color color) { tint = color; }

	void Icon::setIconSize(float newSize) {
		if (newSize == iconSize) {
			return;
		}
		iconSize = newSize;
		size = {newSize, newSize};
		if (glyphMode) {
			buildGlyph();
		} else {
			applyScaleToVertices();
		}
	}

	void Icon::setPosition(float x, float y) { position = {x, y}; }

	// ----- Salvage glyph mode -----

	void Icon::buildGlyph() {
		glyphMeshVertices.clear();
		glyphMeshIndices.clear();
		glyphStrokes.clear();

		const Icons::GlyphDef* def = Icons::find(glyph);
		if (def == nullptr) {
			return;
		}

		const float scale = iconSize / kGlyphViewBox;
		glyphFilled = def->filled;
		glyphStrokeWidth = glyphStrokeBase * scale;

		if (glyphFilled) {
			renderer::Tessellator tess;
			for (int s = 0; s < def->subCount; ++s) {
				const Icons::SubPath& sp = def->subs[s];
				renderer::VectorPath path;
				path.isClosed = true;
				path.vertices.reserve(static_cast<size_t>(sp.count));
				for (int i = 0; i < sp.count; ++i) {
					path.vertices.push_back({sp.pts[i].x * scale, sp.pts[i].y * scale});
				}
				renderer::TessellatedMesh mesh;
				if (tess.Tessellate(path, mesh)) {
					const auto base = static_cast<uint16_t>(glyphMeshVertices.size());
					for (const Foundation::Vec2& v : mesh.vertices) {
						glyphMeshVertices.push_back(v);
					}
					for (uint16_t idx : mesh.indices) {
						glyphMeshIndices.push_back(static_cast<uint16_t>(base + idx));
					}
				}
			}
		} else {
			glyphStrokes.reserve(static_cast<size_t>(def->subCount));
			for (int s = 0; s < def->subCount; ++s) {
				const Icons::SubPath& sp = def->subs[s];
				Stroke stroke;
				stroke.closed = sp.closed;
				stroke.points.reserve(static_cast<size_t>(sp.count));
				for (int i = 0; i < sp.count; ++i) {
					stroke.points.push_back({sp.pts[i].x * scale, sp.pts[i].y * scale});
				}
				glyphStrokes.push_back(std::move(stroke));
			}
		}
	}

	// ----- SVG mode -----

	void Icon::rebuildMesh() {
		originalVertices.clear();
		vertices.clear();
		indices.clear();

		if (svgPath.empty()) {
			return;
		}

		std::vector<renderer::LoadedSVGShape> shapes;
		if (!renderer::loadSVG(svgPath, 0.5F, shapes)) {
			return;
		}
		if (shapes.empty()) {
			return;
		}

		originalWidth = shapes[0].width;
		originalHeight = shapes[0].height;

		renderer::Tessellator tessellator;
		for (const auto& shape : shapes) {
			for (const auto& path : shape.paths) {
				renderer::TessellatedMesh mesh;
				if (tessellator.Tessellate(path, mesh)) {
					size_t baseIndex = originalVertices.size();
					for (const auto& v : mesh.vertices) {
						originalVertices.push_back(v);
					}
					for (uint16_t idx : mesh.indices) {
						indices.push_back(static_cast<uint16_t>(baseIndex + idx));
					}
				}
			}
		}

		applyScaleToVertices();
	}

	void Icon::applyScaleToVertices() {
		if (originalVertices.empty() || (originalWidth == 0.0F && originalHeight == 0.0F)) {
			return;
		}

		float scaleX = (originalWidth > 0.0F) ? iconSize / originalWidth : 1.0F;
		float scaleY = (originalHeight > 0.0F) ? iconSize / originalHeight : 1.0F;
		float scale = std::min(scaleX, scaleY);

		float scaledWidth = originalWidth * scale;
		float scaledHeight = originalHeight * scale;
		float offsetX = (iconSize - scaledWidth) / 2.0F;
		float offsetY = (iconSize - scaledHeight) / 2.0F;

		vertices.clear();
		vertices.reserve(originalVertices.size());
		for (const auto& v : originalVertices) {
			vertices.push_back({v.x * scale + offsetX, v.y * scale + offsetY});
		}
	}

	void Icon::render() {
		if (!visible) {
			return;
		}

		const Foundation::Vec2 contentPos = getContentPosition();

		if (glyphMode) {
			using Renderer::Primitives::drawCircle;
			using Renderer::Primitives::drawLine;
			using Renderer::Primitives::drawTriangles;

			if (glyphFilled) {
				if (glyphMeshVertices.empty() || glyphMeshIndices.empty()) {
					return;
				}
				std::vector<Foundation::Vec2> verts;
				verts.reserve(glyphMeshVertices.size());
				for (const Foundation::Vec2& v : glyphMeshVertices) {
					verts.push_back({v.x + contentPos.x, v.y + contentPos.y});
				}
				drawTriangles({.vertices = verts.data(),
							   .indices = glyphMeshIndices.data(),
							   .vertexCount = verts.size(),
							   .indexCount = glyphMeshIndices.size(),
							   .color = tint,
							   .zIndex = zIndex});
				return;
			}

			// Stroked: a thick line per segment, a round cap/join dot at every vertex.
			const float dotR = glyphStrokeWidth * 0.5F;
			for (const Stroke& stroke : glyphStrokes) {
				const auto&	 pts = stroke.points;
				const size_t n = pts.size();
				for (size_t i = 0; i + 1 < n; ++i) {
					drawLine({.start = {pts[i].x + contentPos.x, pts[i].y + contentPos.y},
							  .end = {pts[i + 1].x + contentPos.x, pts[i + 1].y + contentPos.y},
							  .style = {.color = tint, .width = glyphStrokeWidth}});
				}
				if (stroke.closed && n > 2) {
					drawLine({.start = {pts[n - 1].x + contentPos.x, pts[n - 1].y + contentPos.y},
							  .end = {pts[0].x + contentPos.x, pts[0].y + contentPos.y},
							  .style = {.color = tint, .width = glyphStrokeWidth}});
				}
				for (const Foundation::Vec2& p : pts) {
					drawCircle({.center = {p.x + contentPos.x, p.y + contentPos.y}, .radius = dotR, .style = {.fill = tint}});
				}
			}
			return;
		}

		// SVG mode
		if (vertices.empty() || indices.empty()) {
			return;
		}
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
