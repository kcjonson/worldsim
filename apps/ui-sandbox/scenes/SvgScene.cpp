// SVG Scene - Demonstrates loading and rendering SVG files
// Uses NanoSVG for parsing, then our own tessellation and rendering pipeline

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
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

			// Draw each tessellated shape
			for (size_t i = 0; i < m_shapes.size(); ++i) {
				const auto& shape = m_shapes[i];
				if (!shape.mesh.vertices.empty()) {
					// Offset the shape to center it on screen
					std::vector<Foundation::Vec2> offsetVertices = shape.mesh.vertices;
					Foundation::Vec2			  offset(300.0F, 250.0F);

					for (auto& v : offsetVertices) {
						// Scale up for visibility (original SVG is 100x100)
						v.x *= 3.0F;
						v.y *= 3.0F;
						v += offset;
					}

					Renderer::Primitives::drawTriangles(
						{.vertices = offsetVertices.data(),
						 .indices = shape.mesh.indices.data(),
						 .vertexCount = offsetVertices.size(),
						 .indexCount = shape.mesh.indices.size(),
						 .color = shape.color,
						 .id = "svg_shape"}
					);
				}
			}

			// Draw info text position indicator (small marker at origin)
			if (!m_shapes.empty()) {
				// Draw a small indicator showing where the SVG was loaded
			}
		}

		void onExit() override { LOG_INFO(UI, "Exiting SVG Scene"); }

		std::string exportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return "{}";
		}

		const char* getName() const override { return "svg"; }

	  private:
		void loadAndTessellate() {
			// Path to test SVG file (copied to build directory by CMake)
			const std::string kSvgPath = "assets/svg/test_shape.svg";
			const float		  kCurveTolerance = 0.5F; // Half-pixel tolerance for smooth curves

			LOG_INFO(UI, "Loading SVG: %s", kSvgPath.c_str());

			auto startTime = std::chrono::high_resolution_clock::now();

			// Load the SVG file
			std::vector<renderer::LoadedSVGShape> loadedShapes;
			if (!renderer::loadSVG(kSvgPath, kCurveTolerance, loadedShapes)) {
				LOG_ERROR(UI, "Failed to load SVG file: %s", kSvgPath.c_str());
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
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("svg", []() { return std::make_unique<SvgScene>(); });
		return true;
	}();

} // anonymous namespace
