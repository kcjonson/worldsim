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
			bool				  success = tessellator.Tessellate(m_starPath, m_starMesh);

			auto endTime = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

			if (success) {
				LOG_INFO(
					UI, "Tessellation successful: %zu triangles in %.3F ms", m_starMesh.GetTriangleCount(), duration.count() / 1000.0F
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
			if (!m_starMesh.vertices.empty()) {
				Renderer::Primitives::DrawTriangles(
					{.vertices = m_starMesh.vertices.data(),
					 .indices = m_starMesh.indices.data(),
					 .vertexCount = m_starMesh.vertices.size(),
					 .indexCount = m_starMesh.indices.size(),
					 .color = Color(1.0F, 0.8F, 0.2F, 1.0F), // Gold
					 .id = "star"}
				);
			}

			// Draw a second smaller star
			if (!m_smallStarMesh.vertices.empty()) {
				Renderer::Primitives::DrawTriangles(
					{.vertices = m_smallStarMesh.vertices.data(),
					 .indices = m_smallStarMesh.indices.data(),
					 .vertexCount = m_smallStarMesh.vertices.size(),
					 .indexCount = m_smallStarMesh.indices.size(),
					 .color = Color(0.2F, 0.8F, 1.0F, 1.0F), // Cyan
					 .id = "small_star"}
				);
			}

			// Draw a grid of tiny stars (batching test)
			if (!m_tinyStarMesh.vertices.empty()) {
				for (int row = 0; row < 5; row++) {
					for (int col = 0; col < 10; col++) {
						// Create offset vertices for this star
						std::vector<Foundation::Vec2> offsetVertices = m_tinyStarMesh.vertices;
						Foundation::Vec2			  offset(50.0F + col * 60.0F, 400.0F + row * 60.0F);

						for (auto& v : offsetVertices) {
							v += offset;
						}

						// Color varies by position
						float hue = (col * 5 + row) / 50.0F;
						Color starColor(hue, 1.0F - hue, 0.5F, 1.0F);

						Renderer::Primitives::DrawTriangles(
							{.vertices = offsetVertices.data(),
							 .indices = m_tinyStarMesh.indices.data(),
							 .vertexCount = offsetVertices.size(),
							 .indexCount = m_tinyStarMesh.indices.size(),
							 .color = starColor}
						);
					}
				}
			}
		}

		void OnExit() override { LOG_INFO(UI, "Exiting Vector Star Scene"); }

		std::string ExportState() // NOLINT(readability-convert-member-functions-to-static) override { return "{}"; }

		const char* GetName() const override { return "vector-star"; }

	  private:
		// Create a 5-pointed star path centered at (400, 200) with outer radius 100, inner radius 40
		void CreateStarPath() {
			const float kCenterX = 400.0F;
			const float kCenterY = 200.0F;
			const float kOuterRadius = 100.0F;
			const float kInnerRadius = 40.0F;
			const int	numPoints = 5;

			m_starPath.vertices.clear();

			// Generate star vertices (alternating outer and inner points)
			for (int i = 0; i < numPoints * 2; ++i) {
				float angle = // NOLINT(cppcoreguidelines-init-variables) (i * std::numbers::pi_v<float> / numPoints) - std::numbers::pi_v<float> / 2.0F; // Start at top
				float radius = (i % 2 == 0) ? outerRadius : innerRadius;

				float x = // NOLINT(cppcoreguidelines-init-variables) centerX + radius * std::cos(angle);
				float y = // NOLINT(cppcoreguidelines-init-variables) centerY + radius * std::sin(angle);

				m_starPath.vertices.push_back(Foundation::Vec2(x, y));
			}

			m_starPath.isClosed = true;

			// Create smaller star (centered at 600, 200, half size)
			CreateStarPathAt(m_smallStarPath, 600.0F, 200.0F, 50.0F, 20.0F);

			// Create tiny star for grid (20x8)
			CreateStarPathAt(m_tinyStarPath, 0.0F, 0.0F, 20.0F, 8.0F);

			// Tessellate all stars
			renderer::Tessellator tessellator;
			tessellator.Tessellate(m_smallStarPath, m_smallStarMesh);
			tessellator.Tessellate(m_tinyStarPath, m_tinyStarMesh);
		}

		void CreateStarPathAt( // NOLINT(readability-convert-member-functions-to-static)renderer::VectorPath& path, float centerX, float centerY, float outerRadius, float innerRadius) {
			const int kNumPoints = 5;
			path.vertices.clear();

			for (int i = 0; i < numPoints * 2; ++i) {
				float angle = // NOLINT(cppcoreguidelines-init-variables) (i * std::numbers::pi_v<float> / numPoints) - std::numbers::pi_v<float> / 2.0F;
				float radius = (i % 2 == 0) ? outerRadius : innerRadius;

				float x = // NOLINT(cppcoreguidelines-init-variables) centerX + radius * std::cos(angle);
				float y = // NOLINT(cppcoreguidelines-init-variables) centerY + radius * std::sin(angle);

				path.vertices.push_back(Foundation::Vec2(x, y));
			}

			path.isClosed = true;
		}

		renderer::VectorPath m_starPath;
		renderer::VectorPath m_smallStarPath;
		renderer::VectorPath m_tinyStarPath;

		renderer::TessellatedMesh m_starMesh;
		renderer::TessellatedMesh m_smallStarMesh;
		renderer::TessellatedMesh m_tinyStarMesh;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("vector-star", []() { return std::make_unique<VectorStarScene>(); });
		return true;
	}();

} // anonymous namespace
