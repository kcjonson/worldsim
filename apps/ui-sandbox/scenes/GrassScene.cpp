// Grass Scene - Tile-Based Asset Demo
// Demonstrates the asset system with tile-based grass spawning.
// Uses AssetRegistry for mesh template and placement rules from XML.
//
// Note: drawTriangles uses uint16_t indices (max 65535 vertices per draw call).
// With many blades, we split into batches.

#include <assets/AssetRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <world/Tile.h>

#include <GL/glew.h>
#include <cmath>
#include <random>
#include <vector>

namespace {

	// Tile grid configuration
	constexpr int32_t kTileGridWidth = 10; // 10x10 tiles
	constexpr int32_t kTileGridHeight = 10;
	constexpr float	  kTileSize = 64.0F; // Pixels per tile

	// Max vertices per batch (uint16_t indices can address 0-65535)
	constexpr size_t kMaxVerticesPerBatch = 60000;

	// Asset definition name
	const char* kGrassAssetName = "Flora_GrassBlade";

	// A single batch of geometry that can be rendered in one draw call
	struct GeometryBatch {
		std::vector<Foundation::Vec2>  vertices;
		std::vector<Foundation::Color> colors;
		std::vector<uint16_t>		   indices;
	};

	class GrassScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(UI, "Grass Scene - Tile-Based Asset Demo");

			// Get logical window dimensions
			m_windowWidth = Renderer::Primitives::PercentWidth(100.0F);
			m_windowHeight = Renderer::Primitives::PercentHeight(100.0F);

			if (m_windowWidth <= 0.0F || m_windowHeight <= 0.0F) {
				m_windowWidth = 672.0F;
				m_windowHeight = 420.0F;
			}

			// Get grass template from asset registry
			if (!loadGrassTemplate()) {
				LOG_ERROR(UI, "Failed to load grass template from asset registry");
				return;
			}

			// Create tile grid (all Grassland for now)
			createTileGrid();

			// Generate grass geometry based on tiles and placement rules
			generateBladeGeometryForTiles();

			// Log statistics
			size_t totalVerts = 0;
			size_t totalIndices = 0;
			for (const auto& batch : m_batches) {
				totalVerts += batch.vertices.size();
				totalIndices += batch.indices.size();
			}
			LOG_INFO(
				UI,
				"Generated %zu blade instances in %zu batches: %zu vertices, %zu indices",
				m_bladeCount,
				m_batches.size(),
				totalVerts,
				totalIndices
			);
		}

		void handleInput(float /*dt*/) override {}

		void update(float /*dt*/) override {}

		void render() override {
			// Clear background - grass tile base color
			glClearColor(0.25F, 0.45F, 0.2F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Draw tile grid (debug visualization)
			for (const auto& tile : m_tiles) {
				// Slight color variation per tile
				float shade = 0.02F * static_cast<float>((tile.gridX + tile.gridY) % 3);
				Renderer::Primitives::drawRect(
					{.bounds = Foundation::Rect(tile.worldPos.x, tile.worldPos.y, tile.width, tile.height),
					 .style = {.fill = Foundation::Color(0.2F + shade, 0.4F + shade, 0.15F + shade, 1.0F)}}
				);
			}

			// Draw grass blades in batches
			for (const auto& batch : m_batches) {
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
			for (const auto& batch : m_batches) {
				totalVerts += batch.vertices.size();
				totalIndices += batch.indices.size();
			}
			char buf[256];
			snprintf(
				buf,
				sizeof(buf),
				R"({"tiles": %zu, "blades": %zu, "batches": %zu, "vertices": %zu, "indices": %zu})",
				m_tiles.size(),
				m_bladeCount,
				m_batches.size(),
				totalVerts,
				totalIndices
			);
			return {buf};
		}

		const char* getName() const override { return "grass"; }

	  private:
		/// Load grass template mesh from AssetRegistry
		bool loadGrassTemplate() {
			// Get the definition to access placement rules
			m_grassDef = engine::assets::AssetRegistry::Get().getDefinition(kGrassAssetName);
			if (m_grassDef == nullptr) {
				LOG_ERROR(UI, "Asset definition not found: %s", kGrassAssetName);
				return false;
			}

			// Get the tessellated template mesh
			m_templateMesh = engine::assets::AssetRegistry::Get().getTemplate(kGrassAssetName);
			if (m_templateMesh == nullptr) {
				LOG_ERROR(UI, "Failed to get template mesh for: %s", kGrassAssetName);
				return false;
			}

			LOG_INFO(
				UI,
				"Loaded grass template via AssetRegistry: %zu vertices, %zu indices",
				m_templateMesh->vertices.size(),
				m_templateMesh->indices.size()
			);

			return true;
		}

		/// Create tile grid
		void createTileGrid() {
			m_tiles.clear();
			m_tiles.reserve(static_cast<size_t>(kTileGridWidth * kTileGridHeight));

			// Center the grid in the window
			float gridWidth = static_cast<float>(kTileGridWidth) * kTileSize;
			float gridHeight = static_cast<float>(kTileGridHeight) * kTileSize;
			float offsetX = (m_windowWidth - gridWidth) / 2.0F;
			float offsetY = (m_windowHeight - gridHeight) / 2.0F;

			for (int32_t y = 0; y < kTileGridHeight; ++y) {
				for (int32_t x = 0; x < kTileGridWidth; ++x) {
					engine::world::Tile tile;
					tile.gridX = x;
					tile.gridY = y;
					tile.worldPos = {offsetX + static_cast<float>(x) * kTileSize, offsetY + static_cast<float>(y) * kTileSize};
					tile.width = kTileSize;
					tile.height = kTileSize;
					tile.biome = engine::world::Biome::Grassland; // All Grassland for demo
					m_tiles.push_back(tile);
				}
			}

			LOG_INFO(UI, "Created %dx%d tile grid (%zu tiles)", kTileGridWidth, kTileGridHeight, m_tiles.size());
		}

		/// Check if a biome matches the asset's valid biomes
		bool biomeMatches(engine::world::Biome biome) const {
			if (m_grassDef == nullptr) {
				return false;
			}
			const std::string biomeName = engine::world::biomeToString(biome);
			for (const auto& validBiome : m_grassDef->placement.biomes) {
				if (validBiome == biomeName) {
					return true;
				}
			}
			return false;
		}

		/// Generate grass geometry based on tiles and placement rules from XML
		void generateBladeGeometryForTiles() {
			if (m_templateMesh == nullptr || m_grassDef == nullptr) {
				return;
			}

			std::mt19937 gen(42); // Fixed seed for reproducibility
			m_bladeCount = 0;
			m_batches.clear();
			m_batches.emplace_back(); // Start with one batch

			const auto& placement = m_grassDef->placement;

			// RNG distributions
			std::uniform_real_distribution<float> spawnDist(0.0F, 1.0F);
			std::uniform_real_distribution<float> rotationDist(-0.3F, 0.3F);
			std::uniform_real_distribution<float> scaleDist(0.8F, 1.5F);
			std::uniform_real_distribution<float> colorVariation(-0.08F, 0.08F);

			// Clumping distributions (based on placement rules)
			std::uniform_int_distribution<int32_t> clumpSizeDist(placement.clumping.clumpSizeMin, placement.clumping.clumpSizeMax);
			std::uniform_real_distribution<float>  clumpRadiusDist(
				 placement.clumping.clumpRadiusMin * kTileSize, placement.clumping.clumpRadiusMax * kTileSize
			 );

			size_t vertsPerBlade = m_templateMesh->vertices.size();

			for (const auto& tile : m_tiles) {
				// Check if this tile's biome supports grass
				if (!biomeMatches(tile.biome)) {
					continue;
				}

				// Check spawn chance
				if (spawnDist(gen) > placement.spawnChance) {
					continue;
				}

				// Generate clump for this tile
				if (placement.distribution == engine::assets::Distribution::Clumped) {
					// Place a clump of grass at a random position within the tile
					std::uniform_real_distribution<float> tileXDist(tile.worldPos.x, tile.worldPos.x + tile.width);
					std::uniform_real_distribution<float> tileYDist(tile.worldPos.y, tile.worldPos.y + tile.height);

					Foundation::Vec2 clumpCenter(tileXDist(gen), tileYDist(gen));
					int32_t			 clumpSize = clumpSizeDist(gen);
					float			 clumpRadius = clumpRadiusDist(gen);

					std::uniform_real_distribution<float> clumpOffsetDist(-clumpRadius, clumpRadius);

					for (int32_t i = 0; i < clumpSize; ++i) {
						// Check batch capacity
						if (m_batches.back().vertices.size() + vertsPerBlade > kMaxVerticesPerBatch) {
							m_batches.emplace_back();
						}

						Foundation::Vec2 position(clumpCenter.x + clumpOffsetDist(gen), clumpCenter.y + clumpOffsetDist(gen));
						float			 rotation = rotationDist(gen);
						float			 scale = scaleDist(gen);

						float			  greenVar = colorVariation(gen);
						Foundation::Color color(0.15F + greenVar, 0.35F + greenVar * 2.0F, 0.1F + greenVar * 0.5F, 1.0F);

						addTransformedBlade(m_batches.back(), position, rotation, scale, color);
						m_bladeCount++;
					}
				} else {
					// Uniform distribution - single blade per spawn point
					std::uniform_real_distribution<float> tileXDist(tile.worldPos.x, tile.worldPos.x + tile.width);
					std::uniform_real_distribution<float> tileYDist(tile.worldPos.y, tile.worldPos.y + tile.height);

					if (m_batches.back().vertices.size() + vertsPerBlade > kMaxVerticesPerBatch) {
						m_batches.emplace_back();
					}

					Foundation::Vec2 position(tileXDist(gen), tileYDist(gen));
					float			 rotation = rotationDist(gen);
					float			 scale = scaleDist(gen);

					float			  greenVar = colorVariation(gen);
					Foundation::Color color(0.15F + greenVar, 0.35F + greenVar * 2.0F, 0.1F + greenVar * 0.5F, 1.0F);

					addTransformedBlade(m_batches.back(), position, rotation, scale, color);
					m_bladeCount++;
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

			auto baseIndex = static_cast<uint16_t>(batch.vertices.size());

			for (const auto& v : m_templateMesh->vertices) {
				float sx = v.x * scale;
				float sy = v.y * scale;
				float rx = sx * cosR - sy * sinR;
				float ry = sx * sinR + sy * cosR;
				batch.vertices.push_back(Foundation::Vec2(rx + position.x, ry + position.y));
				batch.colors.push_back(color);
			}

			for (const auto& idx : m_templateMesh->indices) {
				batch.indices.push_back(baseIndex + idx);
			}
		}

		// Asset data
		const engine::assets::AssetDefinition* m_grassDef = nullptr;
		const renderer::TessellatedMesh*	   m_templateMesh = nullptr;

		// Tile grid
		std::vector<engine::world::Tile> m_tiles;

		// Window dimensions
		float m_windowWidth = 0.0F;
		float m_windowHeight = 0.0F;

		// Generated geometry
		std::vector<GeometryBatch> m_batches;
		size_t					   m_bladeCount = 0;
	};

	// Register scene with SceneManager
	[[maybe_unused]] bool g_registered = []() {
		engine::SceneManager::Get().registerScene("grass", []() { return std::make_unique<GrassScene>(); });
		return true;
	}();

} // anonymous namespace
