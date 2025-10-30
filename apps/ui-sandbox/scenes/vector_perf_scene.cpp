// Vector Performance Scene - 10,000 Stars Stress Test
// Phase 1 validation: Prove real-time tessellation at scale

#include <graphics/color.h>
#include <primitives/primitives.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>
#include <utils/log.h>
#include <vector/tessellator.h>
#include <vector/types.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace {

	// Star instance data
	struct Star {
		Foundation::Vec2		  position;
		float					  outerRadius;
		float					  innerRadius;
		Foundation::Color		  color;
		renderer::TessellatedMesh mesh;
	};

	class VectorPerfScene : public engine::IScene {
	  public:
		void OnEnter() override {
			LOG_INFO(UI, "Vector Performance Scene - 10,000 Stars Stress Test");

			// Generate 10,000 stars
			GenerateStars(10000);

			LOG_INFO(UI, "Generated %zu stars", m_stars.size());
			LOG_INFO(UI, "Total triangles: %zu", CalculateTotalTriangles());
			LOG_INFO(UI, "Total vertices: %zu", CalculateTotalVertices());
		}

		void HandleInput(float dt) override {
			// No input handling needed - performance test scene
		}

		void Update(float dt) override {
			// Update FPS counter
			m_frameCount++;
			m_frameDeltaAccumulator += dt;

			if (m_frameDeltaAccumulator >= 1.0f) {
				m_fps = m_frameCount / m_frameDeltaAccumulator;
				m_frameCount = 0;
				m_frameDeltaAccumulator = 0.0f;
			}
		}

		void Render() override {
			using namespace Foundation;

			// Clear background
			glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);

			// Measure rendering time
			auto renderStart = std::chrono::high_resolution_clock::now();

			// Draw all 10,000 stars
			for (const auto& star : m_stars) {
				if (!star.mesh.vertices.empty()) {
					Renderer::Primitives::DrawTriangles(
						{.vertices = star.mesh.vertices.data(),
						 .indices = star.mesh.indices.data(),
						 .vertexCount = star.mesh.vertices.size(),
						 .indexCount = star.mesh.indices.size(),
						 .color = star.color}
					);
				}
			}

			auto  renderEnd = std::chrono::high_resolution_clock::now();
			float renderMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

			// Draw FPS counter (top-left corner)
			char fpsText[64];
			snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", m_fps);

			Renderer::Primitives::DrawRect({.bounds = {10, 10, 150, 30}, .style = {.fill = Color(0.0f, 0.0f, 0.0f, 0.7f)}});

			// Draw performance stats (top-left, below FPS)
			char statsText[256];
			snprintf(
				statsText,
				sizeof(statsText),
				"Stars: %zu | Triangles: %zu\nRender: %.2fms",
				m_stars.size(),
				CalculateTotalTriangles(),
				renderMs
			);

			Renderer::Primitives::DrawRect({.bounds = {10, 50, 300, 50}, .style = {.fill = Color(0.0f, 0.0f, 0.0f, 0.7f)}});

			// Update render time tracking
			m_lastRenderTime = renderMs;
		}

		void OnExit() override {
			LOG_INFO(UI, "Exiting Vector Performance Scene");
			LOG_INFO(UI, "Final stats: %zu stars, %.1f FPS, %.2fms render time", m_stars.size(), m_fps, m_lastRenderTime);
		}

		std::string ExportState() override {
			char buf[256];
			snprintf(buf, sizeof(buf), R"({"stars": %zu, "fps": %.1f, "renderMs": %.2f})", m_stars.size(), m_fps, m_lastRenderTime);
			return std::string(buf);
		}

		const char* GetName() const override { return "vector-perf"; }

	  private:
		void GenerateStars(size_t count) {
			// Random number generator
			std::random_device					  rd;
			std::mt19937						  gen(rd());
			std::uniform_real_distribution<float> posXDist(50.0f, 2510.0f);		// Screen width
			std::uniform_real_distribution<float> posYDist(50.0f, 1390.0f);		// Screen height
			std::uniform_real_distribution<float> outerRadiusDist(8.0f, 25.0f); // Star size variation
			std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

			LOG_INFO(UI, "Generating %zu stars...", count);
			auto genStart = std::chrono::high_resolution_clock::now();

			renderer::Tessellator tessellator;

			for (size_t i = 0; i < count; ++i) {
				Star star;
				star.position = Foundation::Vec2(posXDist(gen), posYDist(gen));
				star.outerRadius = outerRadiusDist(gen);
				star.innerRadius = star.outerRadius * 0.4f; // Inner radius is 40% of outer

				// Random color
				float hue = colorDist(gen);
				star.color = Foundation::Color(hue, 1.0f - hue * 0.5f, 0.3f + hue * 0.4f, 1.0f);

				// Create star path
				renderer::VectorPath path = CreateStarPath(star.position, star.outerRadius, star.innerRadius);

				// Tessellate
				bool success = tessellator.Tessellate(path, star.mesh);
				if (!success) {
					LOG_WARNING(UI, "Failed to tessellate star %zu", i);
					continue;
				}

				m_stars.push_back(std::move(star));
			}

			auto  genEnd = std::chrono::high_resolution_clock::now();
			float genMs = std::chrono::duration<float, std::milli>(genEnd - genStart).count();

			if (m_stars.size() > 0) {
				LOG_INFO(
					UI, "Generated and tessellated %zu stars in %.2f ms (%.3f ms per star)", m_stars.size(), genMs, genMs / m_stars.size()
				);
			} else {
				LOG_INFO(UI, "Generated and tessellated 0 stars in %.2f ms (N/A ms per star)", genMs);
			}
		}

		renderer::VectorPath CreateStarPath(const Foundation::Vec2& center, float outerRadius, float innerRadius) {
			renderer::VectorPath path;
			const int			 numPoints = 5;

			for (int i = 0; i < numPoints * 2; ++i) {
				float angle = (i * std::numbers::pi_v<float> / numPoints) - std::numbers::pi_v<float> / 2.0f; // Start at top
				float radius = (i % 2 == 0) ? outerRadius : innerRadius;

				float x = center.x + radius * std::cos(angle);
				float y = center.y + radius * std::sin(angle);

				path.vertices.push_back(Foundation::Vec2(x, y));
			}

			path.isClosed = true;
			return path;
		}

		size_t CalculateTotalTriangles() const {
			size_t total = 0;
			for (const auto& star : m_stars) {
				total += star.mesh.GetTriangleCount();
			}
			return total;
		}

		size_t CalculateTotalVertices() const {
			size_t total = 0;
			for (const auto& star : m_stars) {
				total += star.mesh.GetVertexCount();
			}
			return total;
		}

		std::vector<Star> m_stars;
		float			  m_fps = 0.0f;
		int				  m_frameCount = 0;
		float			  m_frameDeltaAccumulator = 0.0f;
		float			  m_lastRenderTime = 0.0f;
	};

	// Register scene with SceneManager
	static bool s_registered = []() {
		engine::SceneManager::Get().RegisterScene("vector-perf", []() { return std::make_unique<VectorPerfScene>(); });
		return true;
	}();

} // anonymous namespace
