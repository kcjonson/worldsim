// Vector Performance Scene - 10,000 Stars Stress Test
// Phase 1 validation: Prove real-time tessellation at scale
// Press 'C' to toggle clipping for performance comparison

#include <graphics/clip_types.h>
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
		Foundation::Vec2		  position{};
		float					  outerRadius{};
		float					  innerRadius{};
		Foundation::Color		  color; // No {} - Color has default constructor
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

		void HandleInput(float /*dt*/) override {
			// Toggle clipping with 'C' key
			static bool lastKeyState = false;
			bool		currentKeyState = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_C) == GLFW_PRESS;

			// Detect key press (transition from not-pressed to pressed)
			if (currentKeyState && !lastKeyState) {
				m_clippingEnabled = !m_clippingEnabled;
				LOG_INFO(UI, "Clipping %s", m_clippingEnabled ? "ENABLED" : "DISABLED");
			}
			lastKeyState = currentKeyState;
		}

		void Update(float dt) override {
			// Update FPS counter
			m_frameCount++;
			m_frameDeltaAccumulator += dt;

			if (m_frameDeltaAccumulator >= 1.0F) {
				m_fps = static_cast<float>(m_frameCount) / m_frameDeltaAccumulator;
				m_frameCount = 0;
				m_frameDeltaAccumulator = 0.0F;
			}
		}

		void Render() override {
			using namespace Foundation;

			// Clear background
			glClearColor(0.05F, 0.05F, 0.1F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Get logical window dimensions (not physical pixels) for UI layout
			float windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			float windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			// Measure rendering time
			auto renderStart = std::chrono::high_resolution_clock::now();

			// Calculate clip region (100px margin on all sides)
			const float margin = 100.0F;
			float		clipWidth = windowWidth - (2.0F * margin);
			float		clipHeight = windowHeight - (2.0F * margin);

			// Apply clipping if enabled
			if (m_clippingEnabled) {
				ClipSettings clipSettings;
				clipSettings.shape = ClipRect{.bounds = Rect{margin, margin, clipWidth, clipHeight}};
				clipSettings.mode = ClipMode::Inside;
				Renderer::Primitives::PushClip(clipSettings);
			}

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

			// Pop clipping if it was enabled
			if (m_clippingEnabled) {
				Renderer::Primitives::PopClip();
			}

			auto  renderEnd = std::chrono::high_resolution_clock::now();
			float renderMs = 0.0F;
			renderMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

			// Draw FPS counter (top-left corner)
			char fpsText[64];
			snprintf(fpsText, sizeof(fpsText), "FPS: %.1F", m_fps);

			Renderer::Primitives::DrawRect({.bounds = {10, 10, 200, 30}, .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.7F)}});

			// Draw performance stats (top-left, below FPS)
			char statsText[256];
			snprintf(
				statsText,
				sizeof(statsText),
				"Stars: %zu | Tris: %zu | Clip: %s",
				m_stars.size(),
				CalculateTotalTriangles(),
				m_clippingEnabled ? "ON" : "OFF"
			);

			Renderer::Primitives::DrawRect({.bounds = {10, 50, 350, 50}, .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.7F)}});

			// Draw instruction hint
			Renderer::Primitives::DrawRect({.bounds = {10, 110, 200, 25}, .style = {.fill = Color(0.0F, 0.0F, 0.5F, 0.5F)}});

			// Draw clip boundary indicator when clipping is enabled
			if (m_clippingEnabled) {
				Renderer::Primitives::DrawRect(
					{.bounds = {margin, margin, clipWidth, clipHeight},
					 .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.0F), .border = BorderStyle{.color = Color::Cyan(), .width = 2.0F}}}
				);
			}

			// Update render time tracking
			m_lastRenderTime = renderMs;
		}

		void OnExit() override {
			LOG_INFO(UI, "Exiting Vector Performance Scene");
			LOG_INFO(UI, "Final stats: %zu stars, %.1F FPS, %.2fms render time", m_stars.size(), m_fps, m_lastRenderTime);
		}

		std::string ExportState() override {
			char buf[256];
			snprintf(buf, sizeof(buf), R"({"stars": %zu, "fps": %.1F, "renderMs": %.2F})", m_stars.size(), m_fps, m_lastRenderTime);
			return {buf};
		}

		const char* GetName() const override { return "vector-perf"; }

	  private:
		void GenerateStars(size_t count) { // NOLINT(readability-convert-member-functions-to-static)
			// Get logical window dimensions for star placement
			float windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			float windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			// Fall back to reasonable defaults if coordinate system not yet initialized
			if (windowWidth <= 0.0F || windowHeight <= 0.0F) {
				windowWidth = 672.0F;	// Default logical window width (1344/2 for Retina)
				windowHeight = 420.0F;	// Default logical window height (840/2 for Retina)
			}

			// Random number generator - spread stars across entire window
			std::random_device					  rd;
			std::mt19937						  gen(rd());
			std::uniform_real_distribution<float> posXDist(0.0F, windowWidth);
			std::uniform_real_distribution<float> posYDist(0.0F, windowHeight);
			std::uniform_real_distribution<float> outerRadiusDist(8.0F, 25.0F); // Star size variation
			std::uniform_real_distribution<float> colorDist(0.0F, 1.0F);

			LOG_INFO(UI, "Generating %zu stars...", count);
			auto genStart = std::chrono::high_resolution_clock::now();

			renderer::Tessellator tessellator;

			for (size_t i = 0; i < count; ++i) {
				Star star;
				star.position = Foundation::Vec2(posXDist(gen), posYDist(gen));
				star.outerRadius = outerRadiusDist(gen);
				star.innerRadius = star.outerRadius * 0.4F; // Inner radius is 40% of outer

				// Random color
				float hue = colorDist(gen);
				star.color = Foundation::Color(hue, (1.0F - (hue * 0.5F)), (0.3F + (hue * 0.4F)), 1.0F);

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

			const auto	kGenEnd = std::chrono::high_resolution_clock::now();
			const float kGenMs = std::chrono::duration<float, std::milli>(kGenEnd - genStart).count();

			// Log generation results
			const float kMsPerStar = m_stars.empty() ? 0.0F : (kGenMs / static_cast<float>(m_stars.size()));
			LOG_INFO(UI, "Generated and tessellated %zu stars in %.2F ms (%.3F ms per star)", m_stars.size(), kGenMs, kMsPerStar);
		}

		static renderer::VectorPath CreateStarPath(const Foundation::Vec2& center, float outerRadius, float innerRadius) {
			renderer::VectorPath path;
			const int			 kNumPoints = 5;

			for (int i = 0; i < kNumPoints * 2; ++i) {
				float angle = 0.0F;
				angle = (static_cast<float>(i) * std::numbers::pi_v<float> / static_cast<float>(kNumPoints)) -
						std::numbers::pi_v<float> / 2.0F; // Start at top
				float radius = (i % 2 == 0) ? outerRadius : innerRadius;

				float x = 0.0F;
				x = center.x + radius * std::cos(angle);
				float y = 0.0F;
				y = center.y + radius * std::sin(angle);

				path.vertices.emplace_back(x, y);
			}

			path.isClosed = true;
			return path;
		}

		size_t CalculateTotalTriangles() const { // NOLINT(readability-convert-member-functions-to-static)
												 // NOLINT(readability-convert-member-functions-to-static)
			size_t total = 0;
			for (const auto& star : m_stars) {
				total += star.mesh.GetTriangleCount();
			}
			return total;
		}

		size_t CalculateTotalVertices() const { // NOLINT(readability-convert-member-functions-to-static)
												// NOLINT(readability-convert-member-functions-to-static)
			size_t total = 0;
			for (const auto& star : m_stars) {
				total += star.mesh.GetVertexCount();
			}
			return total;
		}

		std::vector<Star> m_stars;
		float			  m_fps = 0.0F;
		int				  m_frameCount = 0;
		float			  m_frameDeltaAccumulator = 0.0F;
		float			  m_lastRenderTime = 0.0F;
		bool			  m_clippingEnabled = true; // Toggle with 'C' key (starts enabled)
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().RegisterScene("vector-perf", []() { return std::make_unique<VectorPerfScene>(); });
		return true;
	}();

} // anonymous namespace
