// Grass Scene - Tile-Based Asset Demo
// Demonstrates the asset system with tile-based grass spawning.
// Uses TileGrid and AssetBatcher for rendering with inline clumping logic.

#include "SceneTypes.h"
#include <assets/AssetBatcher.h>
#include <assets/AssetRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <world/TileGrid.h>

#include <GL/glew.h>
#include <random>

namespace {

	constexpr const char* kSceneName = "grass";

	// Grid configuration
	constexpr int32_t kTileGridWidth = 10;
	constexpr int32_t kTileGridHeight = 10;
	constexpr float	  kTileSize = 64.0F;

	// Spawning configuration
	constexpr float	  kSpawnChance = 0.3F;
	constexpr int32_t kClumpSizeMin = 8;
	constexpr int32_t kClumpSizeMax = 20;
	constexpr float	  kClumpRadiusMin = 0.3F;
	constexpr float	  kClumpRadiusMax = 0.6F;

	// Asset to spawn
	const char* kGrassAssetName = "Flora_GrassBlade";

	class GrassScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Grass Scene - Tile-Based Asset Demo");

			// Get logical window dimensions
			float windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			float windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			if (windowWidth <= 0.0F || windowHeight <= 0.0F) {
				windowWidth = 672.0F;
				windowHeight = 420.0F;
			}

			// Center the grid in the window
			float			 gridWidth = static_cast<float>(kTileGridWidth) * kTileSize;
			float			 gridHeight = static_cast<float>(kTileGridHeight) * kTileSize;
			Foundation::Vec2 gridOrigin((windowWidth - gridWidth) / 2.0F, (windowHeight - gridHeight) / 2.0F);

			// Create tile grid
			engine::world::TileGridConfig gridConfig{
				.width = kTileGridWidth, .height = kTileGridHeight, .tileSize = kTileSize, .origin = gridOrigin
			};
			m_grid = engine::world::TileGrid(gridConfig);
			m_grid.setAllBiomes(engine::world::Biome::Grassland);

			LOG_INFO(UI, "Created %dx%d tile grid (%zu tiles)", kTileGridWidth, kTileGridHeight, m_grid.tileCount());

			// Get template mesh
			m_templateMesh = engine::assets::AssetRegistry::Get().getTemplate(kGrassAssetName);
			if (m_templateMesh == nullptr) {
				LOG_ERROR(UI, "Failed to get template mesh for: %s", kGrassAssetName);
				return;
			}

			LOG_INFO(
				UI, "Loaded grass template: %zu vertices, %zu indices", m_templateMesh->vertices.size(), m_templateMesh->indices.size()
			);

			// Generate clumped grass instances
			auto instances = generateClumpedInstances();

			// Batch geometry for rendering
			m_batcher.addInstances(*m_templateMesh, instances);

			LOG_INFO(
				UI,
				"Generated %zu grass instances in %zu batches: %zu vertices, %zu indices",
				m_batcher.instanceCount(),
				m_batcher.batches().size(),
				m_batcher.totalVertices(),
				m_batcher.totalIndices()
			);
		}

		void update(float /*dt*/) override {}

		void render() override {
			// Clear background - grass tile base color
			glClearColor(0.25F, 0.45F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw tile grid (debug visualization)
			for (const auto& tile : m_grid.tiles()) {
				float shade = 0.02F * static_cast<float>((tile.gridX + tile.gridY) % 3);
				Renderer::Primitives::drawRect(
					{.bounds = Foundation::Rect(tile.worldPos.x, tile.worldPos.y, tile.width, tile.height),
					 .style = {.fill = Foundation::Color(0.2F + shade, 0.4F + shade, 0.15F + shade, 1.0F)}}
				);
			}

			// Draw grass batches
			for (const auto& batch : m_batcher.batches()) {
				if (!batch.empty()) {
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
			char buf[256];
			snprintf(
				buf,
				sizeof(buf),
				R"({"tiles": %zu, "instances": %zu, "batches": %zu, "vertices": %zu, "indices": %zu})",
				m_grid.tileCount(),
				m_batcher.instanceCount(),
				m_batcher.batches().size(),
				m_batcher.totalVertices(),
				m_batcher.totalIndices()
			);
			return {buf};
		}

		const char* getName() const override { return kSceneName; }

	  private:
		/// Generate grass instances with clumping behavior
		std::vector<engine::assets::SpawnedInstance> generateClumpedInstances() {
			std::vector<engine::assets::SpawnedInstance> instances;

			std::mt19937						  rng(42); // Fixed seed for reproducibility
			std::uniform_real_distribution<float> chanceDist(0.0F, 1.0F);
			std::uniform_real_distribution<float> rotationDist(-0.3F, 0.3F);
			std::uniform_real_distribution<float> scaleDist(0.8F, 1.5F);
			std::uniform_real_distribution<float> colorDist(-0.08F, 0.08F);

			for (const auto& tile : m_grid.tiles()) {
				// Roll for spawn
				if (chanceDist(rng) > kSpawnChance) {
					continue;
				}

				// Generate clump center randomly within tile
				std::uniform_real_distribution<float> posXDist(tile.worldPos.x, tile.worldPos.x + tile.width);
				std::uniform_real_distribution<float> posYDist(tile.worldPos.y, tile.worldPos.y + tile.height);

				Foundation::Vec2 clumpCenter{posXDist(rng), posYDist(rng)};

				// Clump parameters
				std::uniform_int_distribution<int32_t> clumpSizeDist(kClumpSizeMin, kClumpSizeMax);
				int32_t								   clumpSize = clumpSizeDist(rng);

				std::uniform_real_distribution<float> clumpRadiusDist(kClumpRadiusMin * tile.width, kClumpRadiusMax * tile.width);
				float								  clumpRadius = clumpRadiusDist(rng);

				std::uniform_real_distribution<float> clumpOffsetDist(-clumpRadius, clumpRadius);

				// Spawn instances in clump
				for (int32_t i = 0; i < clumpSize; ++i) {
					engine::assets::SpawnedInstance inst;
					inst.position = {clumpCenter.x + clumpOffsetDist(rng), clumpCenter.y + clumpOffsetDist(rng)};
					inst.rotation = rotationDist(rng);
					inst.scale = scaleDist(rng);

					// Green color with variation
					float greenVar = colorDist(rng);
					inst.colorTint = Foundation::Color(0.15F + greenVar, 0.35F + greenVar * 2.0F, 0.1F + greenVar * 0.5F, 1.0F);

					instances.push_back(inst);
				}
			}

			return instances;
		}

		engine::world::TileGrid			 m_grid;
		engine::assets::AssetBatcher	 m_batcher;
		const renderer::TessellatedMesh* m_templateMesh = nullptr;
	};

} // anonymous namespace

// Export scene info for registry
namespace ui_sandbox::scenes {
	extern const ui_sandbox::SceneInfo Grass = {kSceneName, []() { return std::make_unique<GrassScene>(); }};
} // namespace ui_sandbox::scenes
