// Grass Scene - SVG Asset Demo
// Demonstrates loading grass blade shapes from SVG and rendering 10,000 static blades.
// Uses SVGLoader to load the blade shape, tessellates once, then transforms mesh vertices.
//
// Note: drawTriangles uses uint16_t indices (max 65535 vertices per draw call).
// With 10,000 blades × 7 vertices = 70,000 vertices, we split into batches.

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
#include <cmath>
#include <random>
#include <vector>

namespace {

	// Grass blade configuration
	constexpr size_t kDefaultBladeCount = 10000; // Target: 10,000 grass blades
	constexpr size_t kBladesPerClump = 5;		 // Blades grouped in clumps
	constexpr float	 kClumpRadius = 6.0F;		 // How spread out blades are within a clump

	// Max vertices per batch (uint16_t indices can address 0-65535)
	constexpr size_t kMaxVerticesPerBatch = 60000; // Leave headroom below 65535

	// A single batch of geometry that can be rendered in one draw call
	struct GeometryBatch {
		std::vector<Foundation::Vec2>  vertices;
		std::vector<Foundation::Color> colors;
		std::vector<uint16_t>		   indices;
	};

	class GrassScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Grass Scene - SVG Asset Demo (Static)");

			// Load grass blade from SVG asset and tessellate it once
			if (!loadAndTessellateGrassBlade()) {
				LOG_ERROR(UI, "Failed to load grass blade - scene will be empty");
				return;
			}

			// Get logical window dimensions
			windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			if (windowWidth <= 0.0F || windowHeight <= 0.0F) {
				windowWidth = 672.0F;
				windowHeight = 420.0F;
			}

			// Generate all blade instances (geometry is created once, not per-frame)
			generateAllBladeGeometry(kDefaultBladeCount);

			size_t totalVerts = 0;
			size_t totalIndices = 0;
			for (const auto& batch : batches) {
				totalVerts += batch.vertices.size();
				totalIndices += batch.indices.size();
			}
			LOG_INFO(
				UI,
				"Generated %zu blade instances in %zu batches: %zu vertices, %zu indices",
				kDefaultBladeCount,
				batches.size(),
				totalVerts,
				totalIndices
			);
		}

		void handleInput(float /*dt*/) override {}

		void update(float /*dt*/) override {
			// Static scene - no animation
		}

		void render() override {
			// Clear background - grass tile base color (light green)
			glClearColor(0.25F, 0.45F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw grass blades in batches (each batch stays under 65535 vertices)
			for (const auto& batch : batches) {
				if (!batch.vertices.empty()) {
					Renderer::Primitives::drawTriangles(
						{.vertices = batch.vertices.data(),
						 .indices = batch.indices.data(),
						 .vertexCount = batch.vertices.size(),
						 .indexCount = batch.indices.size(),
						 .color = Foundation::Color(0.3F, 0.6F, 0.2F, 1.0F),
						 .colors = batch.colors.data()}
					);
				}
			}
		}

		void onExit() override {}

		std::string exportState() override {
			size_t totalVerts = 0;
			size_t totalIndices = 0;
			for (const auto& batch : batches) {
				totalVerts += batch.vertices.size();
				totalIndices += batch.indices.size();
			}
			char buf[256];
			snprintf(
				buf,
				sizeof(buf),
				R"({"blades": %zu, "batches": %zu, "vertices": %zu, "indices": %zu})",
				kDefaultBladeCount,
				batches.size(),
				totalVerts,
				totalIndices
			);
			return {buf};
		}

		const char* getName() const override { return "grass"; }

	  private:
		/// Load grass blade from SVG and tessellate it once
		bool loadAndTessellateGrassBlade() {
			const std::string kRelativePath = "assets/svg/grass_blade.svg";
			const float		  kCurveTolerance = 0.5F;

			// Use findResource to handle different working directories (IDE vs terminal)
			std::string svgPath = Foundation::findResourceString(kRelativePath);
			if (svgPath.empty()) {
				LOG_ERROR(UI, "Could not find grass blade SVG: %s", kRelativePath.c_str());
				return false;
			}

			LOG_INFO(UI, "Loading grass blade SVG: %s", svgPath.c_str());

			// Load the SVG file
			std::vector<renderer::LoadedSVGShape> loadedShapes;
			if (!renderer::loadSVG(svgPath, kCurveTolerance, loadedShapes)) {
				LOG_ERROR(UI, "Failed to load grass blade SVG: %s", svgPath.c_str());
				return false;
			}

			if (loadedShapes.empty() || loadedShapes[0].paths.empty()) {
				LOG_ERROR(UI, "Grass blade SVG has no paths!");
				return false;
			}

			// Tessellate the SVG path to get our template mesh
			renderer::VectorPath path;
			path.vertices = loadedShapes[0].paths[0].vertices;
			path.isClosed = true;

			renderer::Tessellator tessellator;
			if (!tessellator.Tessellate(path, templateMesh)) {
				LOG_ERROR(UI, "Failed to tessellate grass blade!");
				return false;
			}

			LOG_INFO(
				UI,
				"Loaded grass blade: %zu path vertices -> %zu mesh vertices, %zu indices",
				path.vertices.size(),
				templateMesh.vertices.size(),
				templateMesh.indices.size()
			);

			return true;
		}

		/// Generate all blade geometry at once (called once on scene enter)
		void generateAllBladeGeometry(size_t count) {
			std::mt19937 gen(42);

			// Clump center positions
			size_t								  numClumps = count / kBladesPerClump;
			std::uniform_real_distribution<float> xPosDist(0.0F, windowWidth);
			std::uniform_real_distribution<float> yPosDist(0.0F, windowHeight);

			// Per-blade variation
			std::uniform_real_distribution<float> clumpOffsetDist(-kClumpRadius, kClumpRadius);
			std::uniform_real_distribution<float> rotationDist(-0.3F, 0.3F);
			std::uniform_real_distribution<float> scaleDist(0.8F, 1.5F);
			std::uniform_real_distribution<float> colorVariation(-0.08F, 0.08F);

			size_t vertsPerBlade = templateMesh.vertices.size();

			// Start with one batch
			batches.emplace_back();

			for (size_t clump = 0; clump < numClumps; ++clump) {
				Foundation::Vec2 clumpCenter(xPosDist(gen), yPosDist(gen));

				for (size_t blade = 0; blade < kBladesPerClump; ++blade) {
					// Check if we need a new batch before adding this blade
					if (batches.back().vertices.size() + vertsPerBlade > kMaxVerticesPerBatch) {
						batches.emplace_back();
					}

					// Blade parameters
					Foundation::Vec2 position(clumpCenter.x + clumpOffsetDist(gen), clumpCenter.y + clumpOffsetDist(gen));
					float			 rotation = rotationDist(gen);
					float			 scale = scaleDist(gen);

					float			  greenVar = colorVariation(gen);
					Foundation::Color color(0.15F + greenVar, 0.35F + greenVar * 2.0F, 0.1F + greenVar * 0.5F, 1.0F);

					// Add transformed blade to current batch
					addTransformedBlade(batches.back(), position, rotation, scale, color);
				}
			}
		}

		/// Add a single transformed blade instance to a batch
		void addTransformedBlade(
			GeometryBatch&			 batch,
			const Foundation::Vec2&	 position,
			float					 rotation,
			float					 scale,
			const Foundation::Color& color
		) {
			float cosR = std::cos(rotation);
			float sinR = std::sin(rotation);

			// Record base index for this blade's vertices (relative to this batch)
			auto baseIndex = static_cast<uint16_t>(batch.vertices.size());

			// Transform and add each vertex from template
			for (const auto& v : templateMesh.vertices) {
				// Scale
				float sx = v.x * scale;
				float sy = v.y * scale;

				// Rotate
				float rx = sx * cosR - sy * sinR;
				float ry = sx * sinR + sy * cosR;

				// Translate
				batch.vertices.push_back(Foundation::Vec2(rx + position.x, ry + position.y));
				batch.colors.push_back(color);
			}

			// Add indices (offset by base index within this batch)
			for (const auto& idx : templateMesh.indices) {
				batch.indices.push_back(baseIndex + idx);
			}
		}

		// Template mesh (tessellated once from SVG)
		renderer::TessellatedMesh templateMesh;

		// Window dimensions
		float windowWidth = 0.0F;
		float windowHeight = 0.0F;

		// Geometry batches (each batch stays under 65535 vertices for uint16_t indices)
		std::vector<GeometryBatch> batches;
	};

	// Register scene with SceneManager
	bool g_registered = []() {
		engine::SceneManager::Get().registerScene("grass", []() { return std::make_unique<GrassScene>(); });
		return true;
	}();

} // anonymous namespace
