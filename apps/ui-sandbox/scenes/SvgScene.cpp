// SVG Scene - Demonstrates loading and rendering SVG files
// Uses NanoSVG for parsing, then our own tessellation and rendering pipeline

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>
#include <vector/SVGLoader.h>
#include <vector/Tessellator.h>
#include <vector/Types.h>

#include <GL/glew.h>
#include <chrono>

namespace {

	/// Holds a tessellated SVG shape ready for rendering
	struct TessellatedShape {
		renderer::TessellatedMesh mesh;
		Foundation::Color		  color;
	};

	class SvgScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "SVG Scene - SVG File Loading Demo");

			loadAndTessellate();
		}

		void handleInput(float dt) override {
			// No input handling needed - static scene
		}

		void update(float dt) override {
			// No update logic needed - static display
		}

		void render() override {
			using namespace Foundation;

			// Clear background to dark gray
			glClearColor(0.15F, 0.15F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw each tessellated shape centered on screen
			constexpr float kScale = 1.5F;
			constexpr float kCenterX = 400.0F;
			constexpr float kCenterY = 300.0F;

			for (const auto& shape : m_shapes) {
				if (shape.mesh.vertices.empty()) {
					continue;
				}

				// Transform vertices: scale and center
				std::vector<Vec2> transformedVerts = shape.mesh.vertices;
				for (auto& v : transformedVerts) {
					v.x = v.x * kScale + kCenterX;
					v.y = v.y * kScale + kCenterY;
				}

				Renderer::Primitives::drawTriangles(
					{.vertices = transformedVerts.data(),
					 .indices = shape.mesh.indices.data(),
					 .vertexCount = transformedVerts.size(),
					 .indexCount = shape.mesh.indices.size(),
					 .color = shape.color,
					 .id = "svg_shape"}
				);
			}
		}

		void onExit() override { LOG_INFO(UI, "Exiting SVG Scene"); }

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return "{}";
		}

		const char* getName() const override { return "svg"; }

	  private:
		void loadAndTessellate() {
			// Path to test SVG file - use findResourceString for portable paths
			const std::string kRelativePath = "assets/svg/test_shape.svg";
			const float		  kCurveTolerance = 0.5F; // Half-pixel tolerance for smooth curves

			// Use findResource to handle different working directories (IDE vs terminal)
			std::string svgPath = Foundation::findResourceString(kRelativePath);
			if (svgPath.empty()) {
				LOG_ERROR(UI, "Could not find SVG: %s", kRelativePath.c_str());
				return;
			}

			LOG_INFO(UI, "Loading SVG: %s", svgPath.c_str());

			auto startTime = std::chrono::high_resolution_clock::now();

			// Load the SVG file
			std::vector<renderer::LoadedSVGShape> loadedShapes;
			if (!renderer::loadSVG(svgPath, kCurveTolerance, loadedShapes)) {
				LOG_ERROR(UI, "Failed to load SVG file: %s", svgPath.c_str());
				return;
			}

			auto loadEndTime = std::chrono::high_resolution_clock::now();
			auto loadDuration = std::chrono::duration_cast<std::chrono::microseconds>(loadEndTime - startTime);

			LOG_INFO(UI, "SVG loaded: %zu shapes in %.3f ms", loadedShapes.size(), loadDuration.count() / 1000.0F);

			// Tessellate each shape
			renderer::Tessellator tessellator;
			size_t				  totalTriangles = 0;

			for (const auto& loadedShape : loadedShapes) {
				// Tessellate each path in the shape
				for (const auto& path : loadedShape.paths) {
					TessellatedShape shape;
					shape.color = loadedShape.fillColor;

					if (tessellator.Tessellate(path, shape.mesh)) {
						totalTriangles += shape.mesh.getTriangleCount();
						m_shapes.push_back(std::move(shape));
						LOG_DEBUG(
							UI, "Tessellated path: %zu vertices -> %zu triangles", path.vertices.size(), shape.mesh.getTriangleCount()
						);
					} else {
						LOG_WARNING(UI, "Failed to tessellate path with %zu vertices", path.vertices.size());
					}
				}
			}

			auto tessEndTime = std::chrono::high_resolution_clock::now();
			auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(tessEndTime - startTime);

			LOG_INFO(
				UI,
				"SVG processing complete: %zu shapes, %zu triangles in %.3f ms",
				m_shapes.size(),
				totalTriangles,
				totalDuration.count() / 1000.0F
			);
		}

		std::vector<TessellatedShape> m_shapes;
	};

	// Register scene with SceneManager
	[[maybe_unused]] bool g_registered = []() {
		engine::SceneManager::Get().registerScene("svg", []() { return std::make_unique<SvgScene>(); });
		return true;
	}();

} // anonymous namespace
