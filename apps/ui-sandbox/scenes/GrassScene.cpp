// Grass Scene - Bezier Curve Tessellation Validation
// Phase 4 validation: Prove Bezier curve flattening and tessellation works
// 10,000 static grass blades in clumps for top-down tile decoration - single draw call

#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <vector/Bezier.h>
#include <vector/Tessellator.h>
#include <vector/Types.h>

#include <GL/glew.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

namespace {

	// Constants for TOP-DOWN grass blade shape (small decorations on grass tiles)
	constexpr float	 kBladeBaseWidth = 2.0F;	 // Thin blades
	constexpr float	 kBladeHeight = 12.0F;		 // Short blades for top-down view
	constexpr float	 kCurveTolerance = 1.0F;	 // Pixels - flatness tolerance
	constexpr size_t kDefaultBladeCount = 10000; // Target: 10,000 grass blades
	constexpr size_t kBladesPerClump = 5;		 // Blades grouped in clumps
	constexpr float	 kClumpRadius = 6.0F;		 // How spread out blades are within a clump

	class GrassScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Grass Scene - 10,000 Static Grass Blades (Single Draw Call)");

			// Generate 10,000 grass blades combined into a single batch
			generateGrassBlades(kDefaultBladeCount);

			LOG_INFO(UI, "Generated %zu grass blades", m_bladeCount);
			LOG_INFO(UI, "Total triangles: %zu", m_combinedIndices.size() / 3);
			LOG_INFO(UI, "Total vertices: %zu", m_combinedVertices.size());
			LOG_INFO(UI, "Curve flattening time: %.3f ms", m_flatteningTimeMs);
			LOG_INFO(UI, "Tessellation time: %.3f ms", m_tessellationTimeMs);
			LOG_INFO(UI, "Total generation time: %.3f ms", m_flatteningTimeMs + m_tessellationTimeMs);
		}

		void handleInput(float /*dt*/) override {
			// No input handling needed for this demo
		}

		void update(float dt) override {
			// Update FPS counter and frame time stats
			m_frameCount++;
			m_frameDeltaAccumulator += dt;

			// Track frame time (dt is in seconds)
			float frameTimeMs = dt * 1000.0F;
			m_minFrameTimeMs = std::min(m_minFrameTimeMs, frameTimeMs);
			m_maxFrameTimeMs = std::max(m_maxFrameTimeMs, frameTimeMs);
			m_frameTimeSumMs += frameTimeMs;
			m_frameTimeSamples++;

			if (m_frameDeltaAccumulator >= 1.0F) {
				m_fps = static_cast<float>(m_frameCount) / m_frameDeltaAccumulator;
				m_avgFrameTimeMs = m_frameTimeSumMs / static_cast<float>(m_frameTimeSamples);

				// Log performance stats once per second
				LOG_INFO(
					UI,
					"[PERF] FPS: %.1f | Frame time: avg=%.2fms min=%.2fms max=%.2fms",
					m_fps,
					m_avgFrameTimeMs,
					m_minFrameTimeMs,
					m_maxFrameTimeMs
				);

				// Reset for next second
				m_frameCount = 0;
				m_frameDeltaAccumulator = 0.0F;
			}
		}

		void render() override {
			using namespace Foundation;

			// Clear background - grass tile base color (light green)
			glClearColor(0.25F, 0.45F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw ALL grass blades in a SINGLE draw call using per-vertex colors
			if (!m_combinedVertices.empty()) {
				Renderer::Primitives::drawTriangles(
					{.vertices = m_combinedVertices.data(),
					 .indices = m_combinedIndices.data(),
					 .vertexCount = m_combinedVertices.size(),
					 .indexCount = m_combinedIndices.size(),
					 .color = Color(0.3F, 0.6F, 0.2F, 1.0F), // Fallback color (unused)
					 .colors = m_combinedColors.data()}		 // Per-vertex colors!
				);
			}
		}

		void onExit() override {
			LOG_INFO(UI, "Exiting Grass Scene");
			LOG_INFO(
				UI,
				"Final stats: %zu blades, %.1f FPS, frame time: avg=%.2fms min=%.2fms max=%.2fms",
				m_bladeCount,
				m_fps,
				m_avgFrameTimeMs,
				m_minFrameTimeMs,
				m_maxFrameTimeMs
			);
			LOG_INFO(
				UI,
				"Generation stats: flatten=%.1f ms, tessellate=%.1f ms, total=%.1f ms",
				m_flatteningTimeMs,
				m_tessellationTimeMs,
				m_flatteningTimeMs + m_tessellationTimeMs
			);
			LOG_INFO(UI, "Triangle count: %zu, Vertex count: %zu", m_combinedIndices.size() / 3, m_combinedVertices.size());

			// Performance target check
			bool targetMet = m_avgFrameTimeMs < 5.0F;
			LOG_INFO(UI, "[PERFORMANCE TARGET] <5ms frame time: %s (actual: %.2fms)", targetMet ? "PASSED" : "FAILED", m_avgFrameTimeMs);
		}

		std::string exportState() override {
			char buf[512];
			snprintf(
				buf,
				sizeof(buf),
				R"({"blades": %zu, "triangles": %zu, "vertices": %zu, "fps": %.1f, )"
				R"("frameTimeMs": {"avg": %.2f, "min": %.2f, "max": %.2f}, )"
				R"("generationMs": {"flatten": %.1f, "tessellate": %.1f, "total": %.1f}, )"
				R"("targetMet": %s})",
				m_bladeCount,
				m_combinedIndices.size() / 3,
				m_combinedVertices.size(),
				m_fps,
				m_avgFrameTimeMs,
				m_minFrameTimeMs,
				m_maxFrameTimeMs,
				m_flatteningTimeMs,
				m_tessellationTimeMs,
				m_flatteningTimeMs + m_tessellationTimeMs,
				m_avgFrameTimeMs < 5.0F ? "true" : "false"
			);
			return {buf};
		}

		const char* getName() const override { return "grass"; }

	  private:
		/// Create the Bezier curves that define a single grass blade shape.
		/// Returns flattened vertices ready for tessellation.
		std::vector<Foundation::Vec2> createGrassBladeVertices(float scale, float heightMult, float widthMult, float bendAmount) {
			using namespace Foundation;

			std::vector<Vec2> vertices;

			// Apply procedural variation to base dimensions
			float bladeWidth = kBladeBaseWidth * widthMult * scale;
			float bladeHeight = kBladeHeight * heightMult * scale;
			float bladeTipX = bladeWidth / 2.0F;

			// Bend offset - shifts control points horizontally based on bendAmount
			float bendOffset = bendAmount * 15.0F * scale;

			// Left edge curve
			renderer::CubicBezier leftEdge = {
				.p0 = {0.0F, 0.0F},
				.p1 = {-2.0F * scale + bendOffset * 0.3F, -bladeHeight * 0.33F},
				.p2 = {bladeTipX - 2.0F * scale + bendOffset * 0.7F, -bladeHeight * 0.83F},
				.p3 = {bladeTipX + bendOffset, -bladeHeight}
			};

			// Right edge curve
			renderer::CubicBezier rightEdge = {
				.p0 = {bladeTipX + bendOffset, -bladeHeight},
				.p1 = {bladeTipX + 2.0F * scale + bendOffset * 0.7F, -bladeHeight * 0.83F},
				.p2 = {bladeWidth + 2.0F * scale + bendOffset * 0.3F, -bladeHeight * 0.33F},
				.p3 = {bladeWidth, 0.0F}
			};

			vertices.push_back(leftEdge.p0);
			renderer::flattenCubicBezier(leftEdge, kCurveTolerance, vertices);
			renderer::flattenCubicBezier(rightEdge, kCurveTolerance, vertices);

			return vertices;
		}

		/// Apply rotation and translation to vertices
		void transformVertices(std::vector<Foundation::Vec2>& vertices, const Foundation::Vec2& position, float rotation) {
			float cosR = std::cos(rotation);
			float sinR = std::sin(rotation);

			for (auto& v : vertices) {
				float rx = v.x * cosR - v.y * sinR;
				float ry = v.x * sinR + v.y * cosR;
				v.x = rx + position.x;
				v.y = ry + position.y;
			}
		}

		void generateGrassBlades(size_t count) {
			// Get logical window dimensions
			float windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			float windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			if (windowWidth <= 0.0F || windowHeight <= 0.0F) {
				windowWidth = 672.0F;
				windowHeight = 420.0F;
			}

			// Pre-allocate combined buffers (estimate ~5 vertices per blade)
			m_combinedVertices.reserve(count * 5);
			m_combinedColors.reserve(count * 5);
			m_combinedIndices.reserve(count * 9); // ~3 triangles per blade

			std::mt19937 gen(42);

			// Clump center positions - distribute evenly across the ENTIRE window
			size_t								  numClumps = count / kBladesPerClump;
			std::uniform_real_distribution<float> xPosDist(0.0F, windowWidth);
			std::uniform_real_distribution<float> yPosDist(0.0F, windowHeight); // Full window coverage

			// Per-blade variation within clump
			std::uniform_real_distribution<float> clumpOffsetDist(-kClumpRadius, kClumpRadius);
			std::uniform_real_distribution<float> rotationDist(-0.2F, 0.2F); // Subtle rotation variation
			std::uniform_real_distribution<float> scaleDist(0.6F, 1.4F);
			std::uniform_real_distribution<float> heightMultDist(0.7F, 1.3F);
			std::uniform_real_distribution<float> widthMultDist(0.8F, 1.2F);
			std::uniform_real_distribution<float> bendDist(-0.6F, 0.6F);
			std::uniform_real_distribution<float> colorVariation(-0.08F, 0.08F); // Subtle color variation

			LOG_INFO(UI, "Generating %zu grass blades in %zu clumps (batched)...", count, numClumps);

			renderer::Tessellator tessellator;

			float  totalFlattenTime = 0.0F;
			float  totalTessTime = 0.0F;
			size_t failedBlades = 0;

			for (size_t clump = 0; clump < numClumps; ++clump) {
				// Clump center position
				Foundation::Vec2 clumpCenter(xPosDist(gen), yPosDist(gen));

				// Generate blades within this clump
				for (size_t blade = 0; blade < kBladesPerClump; ++blade) {
					// Position offset from clump center
					Foundation::Vec2 position(clumpCenter.x + clumpOffsetDist(gen), clumpCenter.y + clumpOffsetDist(gen));

					float rotation = rotationDist(gen);
					float scale = scaleDist(gen);
					float heightMult = heightMultDist(gen);
					float widthMult = widthMultDist(gen);
					float bendAmount = bendDist(gen);

					// Darker green color with subtle variation (for top-down grass decoration)
					float			  greenVar = colorVariation(gen);
					Foundation::Color bladeColor(
						0.15F + greenVar,		 // R: dark
						0.35F + greenVar * 2.0F, // G: dominant green, slightly darker than background
						0.1F + greenVar * 0.5F,	 // B: low
						1.0F
					);

					// Flatten Bezier curves
					auto						  flattenStart = std::chrono::high_resolution_clock::now();
					std::vector<Foundation::Vec2> vertices = createGrassBladeVertices(scale, heightMult, widthMult, bendAmount);
					auto						  flattenEnd = std::chrono::high_resolution_clock::now();
					totalFlattenTime += std::chrono::duration<float, std::milli>(flattenEnd - flattenStart).count();

					// Apply transformation
					transformVertices(vertices, position, rotation);

					// Tessellate
					renderer::VectorPath path;
					path.vertices = std::move(vertices);
					path.isClosed = true;

					auto					  tessStart = std::chrono::high_resolution_clock::now();
					renderer::TessellatedMesh mesh;
					bool					  success = tessellator.Tessellate(path, mesh);
					auto					  tessEnd = std::chrono::high_resolution_clock::now();
					totalTessTime += std::chrono::duration<float, std::milli>(tessEnd - tessStart).count();

					if (!success) {
						failedBlades++;
						continue;
					}

					// Append to combined buffers with index offset
					uint16_t baseIndex = static_cast<uint16_t>(m_combinedVertices.size());

					// Add vertices
					for (const auto& v : mesh.vertices) {
						m_combinedVertices.push_back(v);
						m_combinedColors.push_back(bladeColor);
					}

					// Add indices (offset by baseIndex)
					for (const auto& idx : mesh.indices) {
						m_combinedIndices.push_back(baseIndex + idx);
					}

					m_bladeCount++;
				}
			}

			m_flatteningTimeMs = totalFlattenTime;
			m_tessellationTimeMs = totalTessTime;

			LOG_INFO(
				UI,
				"Generated %zu blades (%zu failed): verts=%zu, tris=%zu, flatten=%.1fms, tess=%.1fms",
				m_bladeCount,
				failedBlades,
				m_combinedVertices.size(),
				m_combinedIndices.size() / 3,
				m_flatteningTimeMs,
				m_tessellationTimeMs
			);
		}

		// Combined geometry buffers for single draw call
		std::vector<Foundation::Vec2>  m_combinedVertices;
		std::vector<Foundation::Color> m_combinedColors;
		std::vector<uint16_t>		   m_combinedIndices;
		size_t						   m_bladeCount = 0;

		// Performance tracking
		float m_fps = 0.0F;
		int	  m_frameCount = 0;
		float m_frameDeltaAccumulator = 0.0F;
		float m_flatteningTimeMs = 0.0F;
		float m_tessellationTimeMs = 0.0F;

		// Frame time tracking
		float m_minFrameTimeMs = 1000.0F;
		float m_maxFrameTimeMs = 0.0F;
		float m_avgFrameTimeMs = 0.0F;
		float m_frameTimeSumMs = 0.0F;
		int	  m_frameTimeSamples = 0;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("grass", []() { return std::make_unique<GrassScene>(); });
		return true;
	}();

} // anonymous namespace
