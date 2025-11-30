// Vector Star Scene - Vector Graphics Tessellation Demo
// Demonstrates vector graphics tessellation with a 5-pointed star

#include <graphics/color.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <utils/log.h>
#include <vector/tessellator.h>
#include <vector/types.h>

#include <GL/glew.h>
#include <chrono>
#include <cmath>
#include <numbers> // C++20 mathematical constants

namespace {

	class VectorStarScene : public engine::IScene {
	  public:
		void OnEnter() override {
			LOG_INFO(UI, "Vector Star Scene - Tessellation Demo");

			// Create a 5-pointed star path
			CreateStarPath();

			// Tessellate the star
			auto startTime = std::chrono::high_resolution_clock::now();

			renderer::Tessellator tessellator;
			bool				  success = tessellator.Tessellate(starPath, starMesh);

			auto endTime = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

			if (success) {
				LOG_INFO(
					UI, "Tessellation successful: %zu triangles in %.3F ms", starMesh.GetTriangleCount(), duration.count() / 1000.0F
				);
			} else {
				LOG_ERROR(UI, "Tessellation failed!");
			}
		}

		void HandleInput(float dt) override {
			// No input handling needed - static scene
		}

		void Update(float dt) override {
			// No update logic needed - static star
		}

		void Render() override {
			using namespace Foundation;

			// Clear background
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw the tessellated star
			if (!starMesh.vertices.empty()) {
				Renderer::Primitives::DrawTriangles(
					{.vertices = starMesh.vertices.data(),
					 .indices = starMesh.indices.data(),
					 .vertexCount = starMesh.vertices.size(),
					 .indexCount = starMesh.indices.size(),
					 .color = Color(1.0F, 0.8F, 0.2F, 1.0F), // Gold
					 .id = "star"}
				);
			}

			// Draw a second smaller star
			if (!smallStarMesh.vertices.empty()) {
				Renderer::Primitives::DrawTriangles(
					{.vertices = smallStarMesh.vertices.data(),
					 .indices = smallStarMesh.indices.data(),
					 .vertexCount = smallStarMesh.vertices.size(),
					 .indexCount = smallStarMesh.indices.size(),
					 .color = Color(0.2F, 0.8F, 1.0F, 1.0F), // Cyan
					 .id = "small_star"}
				);
			}

			// Draw a grid of tiny stars (batching test)
			if (!tinyStarMesh.vertices.empty()) {
				for (int row = 0; row < 5; row++) {
					for (int col = 0; col < 10; col++) {
						// Create offset vertices for this star
						std::vector<Foundation::Vec2> offsetVertices = tinyStarMesh.vertices;
						Foundation::Vec2 offset(50.0F + static_cast<float>(col) * 60.0F, 400.0F + static_cast<float>(row) * 60.0F);

						for (auto& v : offsetVertices) {
							v += offset;
						}

						// Color varies by position
						float hue = static_cast<float>((col * 5) + row) / 50.0F;
						Color starColor(hue, 1.0F - hue, 0.5F, 1.0F);

						Renderer::Primitives::DrawTriangles(
							{.vertices = offsetVertices.data(),
							 .indices = tinyStarMesh.indices.data(),
							 .vertexCount = offsetVertices.size(),
							 .indexCount = tinyStarMesh.indices.size(),
							 .color = starColor}
						);
					}
				}
			}
		}

		void OnExit() override { LOG_INFO(UI, "Exiting Vector Star Scene"); }

		std::string ExportState() override { // NOLINT(readability-convert-member-functions-to-static)
			return "{}";
		}

		const char* GetName() const override { return "vector-star"; }

	  private:
		// Create a 5-pointed star path centered at (400, 200) with outer radius 100, inner radius 40
		void CreateStarPath() {
			const float kCenterX = 400.0F;
			const float kCenterY = 200.0F;
			const float kOuterRadius = 100.0F;
			const float kInnerRadius = 40.0F;
			const int	kNumPoints = 5;

			starPath.vertices.clear();

			// Generate star vertices (alternating outer and inner points)
			for (int i = 0; i < kNumPoints * 2; ++i) {
				float angle = (static_cast<float>(i) * std::numbers::pi_v<float> / static_cast<float>(kNumPoints)) -
							  (std::numbers::pi_v<float> / 2.0F); // Start at top
				float radius = (i % 2 == 0) ? kOuterRadius : kInnerRadius;

				float x = kCenterX + (radius * std::cos(angle));
				float y = kCenterY + (radius * std::sin(angle));

				starPath.vertices.emplace_back(x, y);
			}

			starPath.isClosed = true;

			// Create smaller star (centered at 600, 200, half size)
			CreateStarPathAt(smallStarPath, 600.0F, 200.0F, 50.0F, 20.0F);

			// Create tiny star for grid (20x8)
			CreateStarPathAt(tinyStarPath, 0.0F, 0.0F, 20.0F, 8.0F);

			// Tessellate all stars
			renderer::Tessellator tessellator;
			tessellator.Tessellate(smallStarPath, smallStarMesh);
			tessellator.Tessellate(tinyStarPath, tinyStarMesh);
		}

		void CreateStarPathAt( // NOLINT(readability-convert-member-functions-to-static)
			renderer::VectorPath& path,
			float				  kCenterX,
			float				  kCenterY,
			float				  kOuterRadius,
			float				  kInnerRadius
		) { // NOLINT(readability-convert-member-functions-to-static)
			const int kNumPoints = 5;
			path.vertices.clear();

			for (int i = 0; i < kNumPoints * 2; ++i) {
				float angle = (static_cast<float>(i) * std::numbers::pi_v<float> / static_cast<float>(kNumPoints)) -
							  (std::numbers::pi_v<float> / 2.0F);
				float radius = (i % 2 == 0) ? kOuterRadius : kInnerRadius;

				float x = kCenterX + (radius * std::cos(angle));
				float y = kCenterY + (radius * std::sin(angle));

				path.vertices.emplace_back(x, y);
			}

			path.isClosed = true;
		}

		renderer::VectorPath starPath;
		renderer::VectorPath smallStarPath;
		renderer::VectorPath tinyStarPath;

		renderer::TessellatedMesh starMesh;
		renderer::TessellatedMesh smallStarMesh;
		renderer::TessellatedMesh tinyStarMesh;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("vector-star", []() { return std::make_unique<VectorStarScene>(); });
		return true;
	}();

} // anonymous namespace
