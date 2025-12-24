// Vector Performance Scene - 10,000 Stars Stress Test
// Phase 1 validation: Prove real-time tessellation at scale
// Press 'C' to toggle clipping for performance comparison

#include <graphics/ClipTypes.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include "SceneTypes.h"
#include <utils/Log.h>
#include <vector/Tessellator.h>
#include <vector/Types.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace {

constexpr const char* kSceneName = "vector-perf";

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
		void onEnter() override {
			LOG_INFO(UI, "Vector Performance Scene - 10,000 Stars Stress Test");

			// Generate 10,000 stars
			generateStars(10000);

			LOG_INFO(UI, "Generated %zu stars", stars.size());
			LOG_INFO(UI, "Total triangles: %zu", CalculateTotalTriangles());
			LOG_INFO(UI, "Total vertices: %zu", CalculateTotalVertices());
		}

		void update(float dt) override {
			// Toggle clipping with 'C' key
			static bool lastKeyState = false;
			bool		currentKeyState = glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_C) == GLFW_PRESS;

			// Detect key press (transition from not-pressed to pressed)
			if (currentKeyState && !lastKeyState) {
				clippingEnabled = !clippingEnabled;
				LOG_INFO(UI, "Clipping %s", clippingEnabled ? "ENABLED" : "DISABLED");
			}
			lastKeyState = currentKeyState;

			// Update FPS counter
			frameCount++;
			frameDeltaAccumulator += dt;

			if (frameDeltaAccumulator >= 1.0F) {
				fps = static_cast<float>(frameCount) / frameDeltaAccumulator;
				frameCount = 0;
				frameDeltaAccumulator = 0.0F;
			}
		}

		void render() override {
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
			if (clippingEnabled) {
				ClipSettings clipSettings;
				clipSettings.shape = ClipRect{.bounds = Rect{margin, margin, clipWidth, clipHeight}};
				clipSettings.mode = ClipMode::Inside;
				Renderer::Primitives::pushClip(clipSettings);
			}

			// Draw all 10,000 stars
			for (const auto& star : stars) {
				if (!star.mesh.vertices.empty()) {
					Renderer::Primitives::drawTriangles(
						{.vertices = star.mesh.vertices.data(),
						 .indices = star.mesh.indices.data(),
						 .vertexCount = star.mesh.vertices.size(),
						 .indexCount = star.mesh.indices.size(),
						 .color = star.color}
					);
				}
			}

			// Pop clipping if it was enabled
			if (clippingEnabled) {
				Renderer::Primitives::popClip();
			}

			auto  renderEnd = std::chrono::high_resolution_clock::now();
			float renderMs = 0.0F;
			renderMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

			// Draw FPS counter (top-left corner)
			char fpsText[64];
			snprintf(fpsText, sizeof(fpsText), "FPS: %.1F", fps);

			Renderer::Primitives::drawRect({.bounds = {10, 10, 200, 30}, .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.7F)}});

			// Draw performance stats (top-left, below FPS)
			char statsText[256];
			snprintf(
				statsText,
				sizeof(statsText),
				"Stars: %zu | Tris: %zu | Clip: %s",
				stars.size(),
				CalculateTotalTriangles(),
				clippingEnabled ? "ON" : "OFF"
			);

			Renderer::Primitives::drawRect({.bounds = {10, 50, 350, 50}, .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.7F)}});

			// Draw instruction hint
			Renderer::Primitives::drawRect({.bounds = {10, 110, 200, 25}, .style = {.fill = Color(0.0F, 0.0F, 0.5F, 0.5F)}});

			// Draw clip boundary indicator when clipping is enabled
			if (clippingEnabled) {
				Renderer::Primitives::drawRect(
					{.bounds = {margin, margin, clipWidth, clipHeight},
					 .style = {.fill = Color(0.0F, 0.0F, 0.0F, 0.0F), .border = BorderStyle{.color = Color::cyan(), .width = 2.0F}}}
				);
			}

			// Update render time tracking
			lastRenderTime = renderMs;
		}

		void onExit() override {
			LOG_INFO(UI, "Exiting Vector Performance Scene");
			LOG_INFO(UI, "Final stats: %zu stars, %.1F FPS, %.2fms render time", stars.size(), fps, lastRenderTime);
		}

		std::string exportState() override {
			char buf[256];
			snprintf(buf, sizeof(buf), R"({"stars": %zu, "fps": %.1F, "renderMs": %.2F})", stars.size(), fps, lastRenderTime);
			return {buf};
		}

		const char* getName() const override { return kSceneName; }

	  private:
		void generateStars(size_t count) { // NOLINT(readability-convert-member-functions-to-static)
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
				renderer::VectorPath path = createStarPath(star.position, star.outerRadius, star.innerRadius);

				// Tessellate
				bool success = tessellator.Tessellate(path, star.mesh);
				if (!success) {
					LOG_WARNING(UI, "Failed to tessellate star %zu", i);
					continue;
				}

				stars.push_back(std::move(star));
			}

			const auto	kGenEnd = std::chrono::high_resolution_clock::now();
			const float kGenMs = std::chrono::duration<float, std::milli>(kGenEnd - genStart).count();

			// Log generation results
			const float kMsPerStar = stars.empty() ? 0.0F : (kGenMs / static_cast<float>(stars.size()));
			LOG_INFO(UI, "Generated and tessellated %zu stars in %.2F ms (%.3F ms per star)", stars.size(), kGenMs, kMsPerStar);
		}

		static renderer::VectorPath createStarPath(const Foundation::Vec2& center, float outerRadius, float innerRadius) {
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
			for (const auto& star : stars) {
				total += star.mesh.getTriangleCount();
			}
			return total;
		}

		size_t CalculateTotalVertices() const { // NOLINT(readability-convert-member-functions-to-static)
												// NOLINT(readability-convert-member-functions-to-static)
			size_t total = 0;
			for (const auto& star : stars) {
				total += star.mesh.getVertexCount();
			}
			return total;
		}

		std::vector<Star> stars;
		float			  fps = 0.0F;
		int				  frameCount = 0;
		float			  frameDeltaAccumulator = 0.0F;
		float			  lastRenderTime = 0.0F;
		bool			  clippingEnabled = true; // Toggle with 'C' key (starts enabled)
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo VectorPerf = {kSceneName, []() { return std::make_unique<VectorPerfScene>(); }};
}
