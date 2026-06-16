#include "design-system/Icon.h"

#include "theme/IconGlyphs.h"
#include "graphics/PrimitiveStyles.h"
#include "primitives/Primitives.h"
#include "vector/Tessellator.h"
#include "vector/Types.h"

namespace UI::DS {

	namespace {
		constexpr float kViewBox = 24.0F; // icon authoring space
	}

	Icon::Icon(const Args& args)
		: position(args.position),
		  color(args.color) {
		const Icons::GlyphDef* def = Icons::find(args.glyph);
		if (def == nullptr) {
			return;
		}

		const float scale = args.size / kViewBox;
		filled = def->filled;
		strokeWidth = args.strokeWidth * scale;

		if (filled) {
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
					const auto base = static_cast<std::uint16_t>(meshVertices.size());
					for (const Foundation::Vec2& v : mesh.vertices) {
						meshVertices.push_back(v);
					}
					for (std::uint16_t idx : mesh.indices) {
						meshIndices.push_back(static_cast<std::uint16_t>(base + idx));
					}
				}
			}
		} else {
			strokes.reserve(static_cast<size_t>(def->subCount));
			for (int s = 0; s < def->subCount; ++s) {
				const Icons::SubPath& sp = def->subs[s];
				Stroke stroke;
				stroke.closed = sp.closed;
				stroke.points.reserve(static_cast<size_t>(sp.count));
				for (int i = 0; i < sp.count; ++i) {
					stroke.points.push_back({sp.pts[i].x * scale, sp.pts[i].y * scale});
				}
				strokes.push_back(std::move(stroke));
			}
		}
	}

	void Icon::render() const {
		using Renderer::Primitives::drawCircle;
		using Renderer::Primitives::drawLine;
		using Renderer::Primitives::drawTriangles;

		if (filled) {
			if (meshVertices.empty() || meshIndices.empty()) {
				return;
			}
			std::vector<Foundation::Vec2> verts;
			verts.reserve(meshVertices.size());
			for (const Foundation::Vec2& v : meshVertices) {
				verts.push_back({v.x + position.x, v.y + position.y});
			}
			drawTriangles({.vertices = verts.data(),
						   .indices = meshIndices.data(),
						   .vertexCount = verts.size(),
						   .indexCount = meshIndices.size(),
						   .color = color,
						   .id = "ds_icon"});
			return;
		}

		// Stroked: a thick line per segment, a round cap/join dot at every vertex.
		const float dotR = strokeWidth * 0.5F;
		for (const Stroke& stroke : strokes) {
			const auto& pts = stroke.points;
			const size_t n = pts.size();
			for (size_t i = 0; i + 1 < n; ++i) {
				drawLine({.start = {pts[i].x + position.x, pts[i].y + position.y},
						  .end = {pts[i + 1].x + position.x, pts[i + 1].y + position.y},
						  .style = {.color = color, .width = strokeWidth}});
			}
			if (stroke.closed && n > 2) {
				drawLine({.start = {pts[n - 1].x + position.x, pts[n - 1].y + position.y},
						  .end = {pts[0].x + position.x, pts[0].y + position.y},
						  .style = {.color = color, .width = strokeWidth}});
			}
			for (const Foundation::Vec2& p : pts) {
				drawCircle({.center = {p.x + position.x, p.y + position.y}, .radius = dotR, .style = {.fill = color}});
			}
		}
	}

} // namespace UI::DS
