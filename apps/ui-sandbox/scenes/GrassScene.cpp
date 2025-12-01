// Grass Scene - Asset System Integration Demo
// Demonstrates the data-driven asset system with procedural grass generation.
// Uses AssetRegistry to load XML definitions and GrassBladeGenerator for procedural shapes.

#include <assets/AssetRegistry.h>
#include <assets/generators/GrassBladeGenerator.h>
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

	// Wind animation constants
	constexpr float kWindSpeed = 2.0F;		  // Base wind wave speed
	constexpr float kWindStrength = 0.4F;	  // How much wind affects bend (0-1)
	constexpr float kWindWaveLength = 100.0F; // Spatial wavelength of wind pattern
	constexpr float kWindTurbulence = 0.3F;	  // Secondary turbulence amount

	// Stored parameters for each grass blade (needed for per-frame regeneration)
	struct BladeParams {
		Foundation::Vec2  position;
		float			  rotation;
		float			  scale;
		float			  heightMult;
		float			  widthMult;
		float			  baseBendAmount; // Static bend before wind
		Foundation::Color color;
		float			  phaseOffset; // For wind variation between blades
	};

	class GrassScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Grass Scene - Asset System Integration Demo");

			// Register generators (required before loading definitions that use them)
			engine::assets::registerGrassBladeGenerator();

			// Load asset definitions from XML
			auto& registry = engine::assets::AssetRegistry::Get();
			if (!registry.loadDefinitions("assets/definitions/flora/grass.xml")) {
				LOG_ERROR(UI, "Failed to load grass asset definitions!");
			} else {
				// Get the template mesh from the asset system
				m_bladeTemplate = registry.getTemplate("Flora_GrassBlade");
				if (m_bladeTemplate == nullptr) {
					LOG_ERROR(UI, "Failed to get blade template!");
				}
			}

			// Get logical window dimensions
			m_windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			m_windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			if (m_windowWidth <= 0.0F || m_windowHeight <= 0.0F) {
				m_windowWidth = 672.0F;
				m_windowHeight = 420.0F;
			}

			// Generate blade parameters (not geometry - that happens per frame)
			generateBladeParams(kDefaultBladeCount);
		}

		void handleInput(float /*dt*/) override {
			// No input handling needed for this demo
		}

		void update(float dt) override {
			// Update animation time
			m_time += dt;

			// Regenerate all grass blade geometry with animated wind
			auto tessStart = std::chrono::high_resolution_clock::now();
			regenerateAnimatedGeometry();
			auto tessEnd = std::chrono::high_resolution_clock::now();
			m_lastTessTimeMs = std::chrono::duration<float, std::milli>(tessEnd - tessStart).count();

			// Track frame time stats
			m_frameCount++;
			m_frameDeltaAccumulator += dt;

			float frameTimeMs = dt * 1000.0F;
			m_minFrameTimeMs = std::min(m_minFrameTimeMs, frameTimeMs);
			m_maxFrameTimeMs = std::max(m_maxFrameTimeMs, frameTimeMs);
			m_frameTimeSumMs += frameTimeMs;
			m_tessTimeSumMs += m_lastTessTimeMs;
			m_frameTimeSamples++;

			if (m_frameDeltaAccumulator >= 1.0F) {
				m_fps = static_cast<float>(m_frameCount) / m_frameDeltaAccumulator;
				m_avgFrameTimeMs = m_frameTimeSumMs / static_cast<float>(m_frameTimeSamples);
				m_avgTessTimeMs = m_tessTimeSumMs / static_cast<float>(m_frameTimeSamples);

				// Reset for next second
				m_frameCount = 0;
				m_frameDeltaAccumulator = 0.0F;
				m_frameTimeSumMs = 0.0F;
				m_tessTimeSumMs = 0.0F;
				m_frameTimeSamples = 0;
				m_minFrameTimeMs = 1000.0F;
				m_maxFrameTimeMs = 0.0F;
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
			// Performance stats available via exportState() for programmatic access
		}

		std::string exportState() override {
			char buf[512];
			snprintf(
				buf,
				sizeof(buf),
				R"({"blades": %zu, "animated": true, "fps": %.1f, )"
				R"("frameTimeMs": {"avg": %.2f, "min": %.2f, "max": %.2f}, )"
				R"("tessTimeMs": %.2f, "targetMet": %s})",
				m_bladeParams.size(),
				m_fps,
				m_avgFrameTimeMs,
				m_minFrameTimeMs,
				m_maxFrameTimeMs,
				m_avgTessTimeMs,
				m_fps >= 55.0F ? "true" : "false"
			);
			return {buf};
		}

		const char* getName() const override { return "grass"; }

	  private:
		/// Calculate wind bend at a given position and time
		float calculateWindBend(const Foundation::Vec2& position, float time, float phaseOffset) {
			// Primary wind wave (large-scale movement)
			float primaryWave = std::sin((position.x / kWindWaveLength + time * kWindSpeed) + phaseOffset);

			// Secondary turbulence (higher frequency, smaller amplitude)
			float turbulence =
				std::sin((position.x / (kWindWaveLength * 0.3F) + time * kWindSpeed * 2.5F) * 1.7F + phaseOffset * 2.0F) * kWindTurbulence;

			// Y-axis variation (wind gusts moving across field)
			float gustVariation = std::sin((position.y / (kWindWaveLength * 0.5F) + time * kWindSpeed * 0.5F)) * 0.3F;

			return (primaryWave + turbulence + gustVariation) * kWindStrength;
		}

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

		/// Generate blade parameters (called once on scene enter)
		void generateBladeParams(size_t count) {
			std::mt19937 gen(42);

			// Clump center positions - distribute evenly across the ENTIRE window
			size_t								  numClumps = count / kBladesPerClump;
			std::uniform_real_distribution<float> xPosDist(0.0F, m_windowWidth);
			std::uniform_real_distribution<float> yPosDist(0.0F, m_windowHeight);

			// Per-blade variation within clump
			std::uniform_real_distribution<float> clumpOffsetDist(-kClumpRadius, kClumpRadius);
			std::uniform_real_distribution<float> rotationDist(-0.2F, 0.2F);
			std::uniform_real_distribution<float> scaleDist(0.6F, 1.4F);
			std::uniform_real_distribution<float> heightMultDist(0.7F, 1.3F);
			std::uniform_real_distribution<float> widthMultDist(0.8F, 1.2F);
			std::uniform_real_distribution<float> bendDist(-0.3F, 0.3F); // Reduced base bend (wind adds more)
			std::uniform_real_distribution<float> colorVariation(-0.08F, 0.08F);
			std::uniform_real_distribution<float> phaseDist(0.0F, 6.28318F); // Random phase offset per blade

			m_bladeParams.reserve(count);

			for (size_t clump = 0; clump < numClumps; ++clump) {
				Foundation::Vec2 clumpCenter(xPosDist(gen), yPosDist(gen));

				for (size_t blade = 0; blade < kBladesPerClump; ++blade) {
					BladeParams params;
					params.position = Foundation::Vec2(clumpCenter.x + clumpOffsetDist(gen), clumpCenter.y + clumpOffsetDist(gen));
					params.rotation = rotationDist(gen);
					params.scale = scaleDist(gen);
					params.heightMult = heightMultDist(gen);
					params.widthMult = widthMultDist(gen);
					params.baseBendAmount = bendDist(gen);
					params.phaseOffset = phaseDist(gen);

					float greenVar = colorVariation(gen);
					params.color = Foundation::Color(0.15F + greenVar, 0.35F + greenVar * 2.0F, 0.1F + greenVar * 0.5F, 1.0F);

					m_bladeParams.push_back(params);
				}
			}
		}

		/// Regenerate all geometry with current wind animation (called every frame)
		void regenerateAnimatedGeometry() {
			// Clear previous frame's geometry
			m_combinedVertices.clear();
			m_combinedColors.clear();
			m_combinedIndices.clear();

			// Pre-allocate (reuse capacity from previous frames)
			m_combinedVertices.reserve(m_bladeParams.size() * 5);
			m_combinedColors.reserve(m_bladeParams.size() * 5);
			m_combinedIndices.reserve(m_bladeParams.size() * 9);

			renderer::Tessellator tessellator;
			size_t				  failedBlades = 0;

			for (const auto& params : m_bladeParams) {
				// Calculate animated bend amount (base + wind)
				float windBend = calculateWindBend(params.position, m_time, params.phaseOffset);
				float totalBend = params.baseBendAmount + windBend;

				// Flatten Bezier curves with animated bend
				std::vector<Foundation::Vec2> vertices =
					createGrassBladeVertices(params.scale, params.heightMult, params.widthMult, totalBend);

				// Apply transformation
				transformVertices(vertices, params.position, params.rotation);

				// Tessellate
				renderer::VectorPath path;
				path.vertices = std::move(vertices);
				path.isClosed = true;

				renderer::TessellatedMesh mesh;
				bool					  success = tessellator.Tessellate(path, mesh);

				if (!success) {
					failedBlades++;
					continue;
				}

				// Append to combined buffers with index offset
				uint16_t baseIndex = static_cast<uint16_t>(m_combinedVertices.size());

				for (const auto& v : mesh.vertices) {
					m_combinedVertices.push_back(v);
					m_combinedColors.push_back(params.color);
				}

				for (const auto& idx : mesh.indices) {
					m_combinedIndices.push_back(baseIndex + idx);
				}
			}
		}

		// Blade parameters (generated once, used for per-frame regeneration)
		std::vector<BladeParams> m_bladeParams;

		// Asset system template (loaded once, cached)
		const renderer::TessellatedMesh* m_bladeTemplate = nullptr;

		// Window dimensions
		float m_windowWidth = 0.0F;
		float m_windowHeight = 0.0F;

		// Animation state
		float m_time = 0.0F;

		// Combined geometry buffers (regenerated every frame)
		std::vector<Foundation::Vec2>  m_combinedVertices;
		std::vector<Foundation::Color> m_combinedColors;
		std::vector<uint16_t>		   m_combinedIndices;

		// Performance tracking
		float m_fps = 0.0F;
		int	  m_frameCount = 0;
		float m_frameDeltaAccumulator = 0.0F;
		float m_lastTessTimeMs = 0.0F;
		float m_avgTessTimeMs = 0.0F;
		float m_tessTimeSumMs = 0.0F;

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
