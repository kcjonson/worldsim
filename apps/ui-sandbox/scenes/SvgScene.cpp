// SVG Scene - Comprehensive vector/SVG capability showcase for the ui-sandbox.
// Renders assets/svg/showcase.svg (solid + gradient fills incl. transparency, shape primitives,
// opacity, document-order layering) through our NanoSVG -> tessellation -> gradient-baking ->
// render pipeline, plus a few Primitives drawRect examples for fills and borders. Consolidates
// the former "shapes" and "vector-star" scenes.

#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include "SceneTypes.h"
#include <utils/Log.h>
#include <utils/ResourcePath.h>
#include <vector/SVGLoader.h>
#include <vector/Types.h>

#include <GL/glew.h>

namespace {

	constexpr const char* kSceneName = "svg";

	class SvgScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "SVG Scene - vector/SVG capability showcase");
			loadAndTessellate();
		}

		void update(float /*dt*/) override {
			// Static display
		}

		void render() override {
			using namespace Foundation;

			glClearColor(0.15F, 0.15F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// SVG showcase mesh: gradients (incl. transparency), primitives, opacity, layering.
			if (!m_mesh.vertices.empty()) {
				Renderer::Primitives::drawTriangles(
					{.vertices = m_mesh.vertices.data(),
					 .indices = m_mesh.indices.data(),
					 .vertexCount = m_mesh.vertices.size(),
					 .indexCount = m_mesh.indices.size(),
					 .colors = m_mesh.colors.data(),
					 .id = "svg_showcase"}
				);
			}

			// Primitive rects with borders (folded in from the former "shapes" scene): fill,
			// border-only, and fill+border. SVG strokes are dropped by the loader, so borders are
			// drawn via Primitives. NOTE: drawRect bounds are in native-pixel window coordinates,
			// a different space from the local coords the SVG mesh is built in, so these are placed
			// to the right of the SVG grid in window space.
			Renderer::Primitives::drawRect(
				{.bounds = {1100, 150, 260, 150}, .style = {.fill = Color(0.31F, 0.46F, 0.21F, 1.0F)}, .id = "rect_fill"}
			);
			Renderer::Primitives::drawRect(
				{.bounds = {1100, 360, 260, 150},
				 .style = {.fill = Color::transparent(), .border = BorderStyle{.color = Color(0.88F, 0.64F, 0.24F, 1.0F), .width = 5.0F}},
				 .id = "rect_border"}
			);
			Renderer::Primitives::drawRect(
				{.bounds = {1100, 570, 260, 150},
				 .style = {.fill = Color(0.42F, 0.29F, 0.54F, 1.0F), .border = BorderStyle{.color = Color::white(), .width = 4.0F}},
				 .id = "rect_fill_border"}
			);
		}

		void onExit() override { LOG_INFO(UI, "Exiting SVG Scene"); }

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return "{}";
		}

		const char* getName() const override { return kSceneName; }

	  private:
		// The showcase SVG is authored centered on the origin, so origin maps to (kCenterX, kCenterY).
		static constexpr float kScale = 1.5F;
		static constexpr float kCenterX = 400.0F;
		static constexpr float kCenterY = 300.0F;

		void loadAndTessellate() {
			const std::string kRelativePath = "assets/svg/showcase.svg";
			const float		  kCurveTolerance = 0.5F;

			std::string svgPath = Foundation::findResourceString(kRelativePath);
			if (svgPath.empty()) {
				LOG_ERROR(UI, "Could not find SVG: %s", kRelativePath.c_str());
				return;
			}

			std::vector<renderer::LoadedSVGShape> loadedShapes;
			if (!renderer::loadSVG(svgPath, kCurveTolerance, loadedShapes)) {
				LOG_ERROR(UI, "Failed to load SVG file: %s", svgPath.c_str());
				return;
			}

			// Tessellate + bake per-vertex colors (solid or gradient) via the shared helper.
			m_mesh.clear();
			for (const auto& shape : loadedShapes) {
				renderer::appendShapeMesh(shape, m_mesh);
			}

			// Scale + center for display; gradient colors are already baked per vertex.
			for (auto& v : m_mesh.vertices) {
				v.x = (v.x * kScale) + kCenterX;
				v.y = (v.y * kScale) + kCenterY;
			}

			LOG_INFO(
				UI,
				"SVG showcase: %zu shapes -> %zu vertices, %zu triangles",
				loadedShapes.size(),
				m_mesh.getVertexCount(),
				m_mesh.getTriangleCount()
			);
		}

		renderer::TessellatedMesh m_mesh;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Svg = {kSceneName, []() { return std::make_unique<SvgScene>(); }};
}
