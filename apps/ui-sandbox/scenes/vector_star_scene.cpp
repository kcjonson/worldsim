// Vector Star Scene - Vector Graphics Tessellation Demo
// Demonstrates vector graphics tessellation with a 5-pointed star

#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <primitives/primitives.h>
#include <vector/types.h>
#include <vector/tessellator.h>
#include <graphics/color.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <cmath>
#include <chrono>
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
		bool success = tessellator.Tessellate(m_starPath, m_starMesh);

		auto endTime = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

		if (success) {
			LOG_INFO(UI, "Tessellation successful: %zu triangles in %.3f ms", m_starMesh.GetTriangleCount(),
					 duration.count() / 1000.0f);
		} else {
			LOG_ERROR(UI, "Tessellation failed!");
		}
	}

	void Update(float dt) override {
		// No update logic needed - static star
	}

	void Render() override {
		using namespace Foundation;

		// Clear background
		glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);


		// Draw the tessellated star
		if (!m_starMesh.vertices.empty()) {
			Renderer::Primitives::DrawTriangles({.vertices = m_starMesh.vertices.data(),
												 .indices = m_starMesh.indices.data(),
												 .vertexCount = m_starMesh.vertices.size(),
												 .indexCount = m_starMesh.indices.size(),
												 .color = Color(1.0f, 0.8f, 0.2f, 1.0f), // Gold
												 .id = "star"});
		}

		// Draw a second smaller star
		if (!m_smallStarMesh.vertices.empty()) {
			Renderer::Primitives::DrawTriangles({.vertices = m_smallStarMesh.vertices.data(),
												 .indices = m_smallStarMesh.indices.data(),
												 .vertexCount = m_smallStarMesh.vertices.size(),
												 .indexCount = m_smallStarMesh.indices.size(),
												 .color = Color(0.2f, 0.8f, 1.0f, 1.0f), // Cyan
												 .id = "small_star"});
		}

		// Draw a grid of tiny stars (batching test)
		if (!m_tinyStarMesh.vertices.empty()) {
			for (int row = 0; row < 5; row++) {
				for (int col = 0; col < 10; col++) {
					// Create offset vertices for this star
					std::vector<Foundation::Vec2> offsetVertices = m_tinyStarMesh.vertices;
					Foundation::Vec2 offset(50.0f + col * 60.0f, 400.0f + row * 60.0f);

					for (auto& v : offsetVertices) {
						v += offset;
					}

					// Color varies by position
					float hue = (col * 5 + row) / 50.0f;
					Color starColor(hue, 1.0f - hue, 0.5f, 1.0f);

					Renderer::Primitives::DrawTriangles({.vertices = offsetVertices.data(),
														 .indices = m_tinyStarMesh.indices.data(),
														 .vertexCount = offsetVertices.size(),
														 .indexCount = m_tinyStarMesh.indices.size(),
														 .color = starColor});
				}
			}
		}

	}

	void OnExit() override {
		LOG_INFO(UI, "Exiting Vector Star Scene");
	}

	std::string ExportState() override { return "{}"; }

	const char* GetName() const override { return "vector-star"; }

  private:
	// Create a 5-pointed star path centered at (400, 200) with outer radius 100, inner radius 40
	void CreateStarPath() {
		const float centerX = 400.0f;
		const float centerY = 200.0f;
		const float outerRadius = 100.0f;
		const float innerRadius = 40.0f;
		const int numPoints = 5;

		m_starPath.vertices.clear();

		// Generate star vertices (alternating outer and inner points)
		for (int i = 0; i < numPoints * 2; ++i) {
			float angle = (i * std::numbers::pi_v<float> / numPoints) - std::numbers::pi_v<float> / 2.0f; // Start at top
			float radius = (i % 2 == 0) ? outerRadius : innerRadius;

			float x = centerX + radius * std::cos(angle);
			float y = centerY + radius * std::sin(angle);

			m_starPath.vertices.push_back(Foundation::Vec2(x, y));
		}

		m_starPath.isClosed = true;

		// Create smaller star (centered at 600, 200, half size)
		CreateStarPathAt(m_smallStarPath, 600.0f, 200.0f, 50.0f, 20.0f);

		// Create tiny star for grid (20x8)
		CreateStarPathAt(m_tinyStarPath, 0.0f, 0.0f, 20.0f, 8.0f);

		// Tessellate all stars
		renderer::Tessellator tessellator;
		tessellator.Tessellate(m_smallStarPath, m_smallStarMesh);
		tessellator.Tessellate(m_tinyStarPath, m_tinyStarMesh);
	}

	void CreateStarPathAt(renderer::VectorPath& path, float centerX, float centerY, float outerRadius,
						  float innerRadius) {
		const int numPoints = 5;
		path.vertices.clear();

		for (int i = 0; i < numPoints * 2; ++i) {
			float angle = (i * std::numbers::pi_v<float> / numPoints) - std::numbers::pi_v<float> / 2.0f;
			float radius = (i % 2 == 0) ? outerRadius : innerRadius;

			float x = centerX + radius * std::cos(angle);
			float y = centerY + radius * std::sin(angle);

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
static bool s_registered = []() {
	engine::SceneManager::Get().RegisterScene("vector-star", []() { return std::make_unique<VectorStarScene>(); });
	return true;
}();

} // anonymous namespace
