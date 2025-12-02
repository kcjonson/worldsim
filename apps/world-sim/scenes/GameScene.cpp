// Game Scene - Main gameplay with chunk-based world rendering

#include "../components/GameOverlay.h"
#include "SceneTypes.h"

#include <GL/glew.h>

#include <graphics/Rect.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/MockWorldSampler.h>
#include <world/rendering/ChunkRenderer.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>

#include <memory>
#include <sstream>
#include <unordered_set>

namespace {

	constexpr const char* kSceneName = "game";
	constexpr uint64_t	  kDefaultWorldSeed = 12345;
	constexpr float		  kPixelsPerMeter = 8.0F;

	/// Convert GroundCover enum to string for placement rules
	std::string groundCoverToString(engine::world::GroundCover cover) {
		switch (cover) {
			case engine::world::GroundCover::Grass:
				return "Grass";
			case engine::world::GroundCover::Dirt:
				return "Dirt";
			case engine::world::GroundCover::Sand:
				return "Sand";
			case engine::world::GroundCover::Rock:
				return "Rock";
			case engine::world::GroundCover::Water:
				return "Water";
			case engine::world::GroundCover::Snow:
				return "Snow";
		}
		return "Unknown";
	}

	class GameScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "GameScene - Entering");

			// Create world systems
			auto sampler = std::make_unique<engine::world::MockWorldSampler>(kDefaultWorldSeed);
			m_chunkManager = std::make_unique<engine::world::ChunkManager>(std::move(sampler));

			m_camera = std::make_unique<engine::world::WorldCamera>();
			m_camera->setPanSpeed(200.0F);

			m_renderer = std::make_unique<engine::world::ChunkRenderer>(kPixelsPerMeter);
			m_renderer->setTileResolution(1); // Render every tile (no skipping)

			// Initialize entity placement system
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			m_placementExecutor = std::make_unique<engine::assets::PlacementExecutor>(assetRegistry);
			m_placementExecutor->initialize();
			LOG_INFO(Game, "PlacementExecutor initialized with %zu entity types", m_placementExecutor->getSpawnOrder().size());

			// Initial chunk load
			m_chunkManager->update(m_camera->position());

			// Process initial chunks for entity placement
			processNewChunks();

			// Create overlay with zoom callbacks
			m_overlay = std::make_unique<world_sim::GameOverlay>(
				world_sim::GameOverlay::Args{.onZoomIn = [this]() { m_camera->zoomIn(); }, .onZoomOut = [this]() { m_camera->zoomOut(); }}
			);

			// Initial layout pass
			int viewportW = 0;
			int viewportH = 0;
			Renderer::Primitives::getViewport(viewportW, viewportH);
			m_overlay->layout(Foundation::Rect{0, 0, static_cast<float>(viewportW), static_cast<float>(viewportH)});

			LOG_INFO(Game, "World initialized with seed %llu", kDefaultWorldSeed);
		}

		void handleInput(float dt) override {
			auto& input = engine::InputManager::Get();

			if (input.isKeyPressed(engine::Key::Escape)) {
				sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
				return;
			}

			// Camera movement
			float dx = 0.0F;
			float dy = 0.0F;

			if (input.isKeyDown(engine::Key::W) || input.isKeyDown(engine::Key::Up)) {
				dy -= 1.0F;
			}
			if (input.isKeyDown(engine::Key::S) || input.isKeyDown(engine::Key::Down)) {
				dy += 1.0F;
			}
			if (input.isKeyDown(engine::Key::A) || input.isKeyDown(engine::Key::Left)) {
				dx -= 1.0F;
			}
			if (input.isKeyDown(engine::Key::D) || input.isKeyDown(engine::Key::Right)) {
				dx += 1.0F;
			}

			if (dx != 0.0F && dy != 0.0F) {
				constexpr float kDiagonalNormalizer = 0.7071F; // 1/sqrt(2), normalizes diagonal movement to unit length
				dx *= kDiagonalNormalizer;
				dy *= kDiagonalNormalizer;
			}

			m_camera->move(dx, dy, dt);

			// Zoom with scroll wheel (snaps to discrete levels)
			float scrollDelta = input.consumeScrollDelta();
			if (scrollDelta > 0.0F) {
				m_camera->zoomIn();
			} else if (scrollDelta < 0.0F) {
				m_camera->zoomOut();
			}

			// Handle overlay input (zoom buttons)
			m_overlay->handleInput();
		}

		void update(float dt) override {
			m_camera->update(dt);
			m_chunkManager->update(m_camera->position());

			// Process newly loaded chunks for entity placement
			processNewChunks();

			// Unload placement data for chunks that were unloaded
			cleanupUnloadedChunks();

			m_overlay->update(*m_camera, *m_chunkManager);
		}

		void render() override {
			glClearColor(0.05F, 0.08F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			int w = 0;
			int h = 0;
			Renderer::Primitives::getViewport(w, h);
			m_renderer->render(*m_chunkManager, *m_camera, w, h);

			m_overlay->render();
		}

		void onExit() override {
			LOG_INFO(Game, "GameScene - Exiting");
			m_overlay.reset();
			m_placementExecutor.reset();
			m_chunkManager.reset();
			m_camera.reset();
			m_renderer.reset();
		}

		std::string exportState() override {
			std::ostringstream oss;
			oss << R"({"scene":"game","chunks":)" << m_chunkManager->loadedChunkCount() << "}";
			return oss.str();
		}

		const char* getName() const override { return kSceneName; }

	  private:
		/// Process newly loaded chunks for entity placement.
		/// Iterates loaded chunks and places entities in any that haven't been processed yet.
		void processNewChunks() {
			for (auto* chunk : m_chunkManager->getLoadedChunks()) {
				auto coord = chunk->coordinate();

				// Skip if already processed
				if (m_placementExecutor->getChunkIndex(coord) != nullptr) {
					continue;
				}

				// Create placement context with callbacks to chunk data
				engine::assets::ChunkPlacementContext ctx;
				ctx.coord = coord;
				ctx.worldSeed = kDefaultWorldSeed;
				ctx.getBiome = [chunk](uint16_t x, uint16_t y) {
					return chunk->getTile(x, y).biome.primary();
				};
				ctx.getGroundCover = [chunk](uint16_t x, uint16_t y) {
					return groundCoverToString(chunk->getTile(x, y).groundCover);
				};

				// Process chunk (PlacementExecutor serves as its own adjacent chunk provider)
				auto result = m_placementExecutor->processChunk(ctx, m_placementExecutor.get());

				// Track this chunk as processed
				m_processedChunks.insert(coord);

				if (result.entitiesPlaced > 0) {
					LOG_DEBUG(Game, "Placed %zu entities in chunk (%d, %d)", result.entitiesPlaced, coord.x, coord.y);
				}

				// TODO: Store result.entities for rendering
				// For now, entities are stored in the PlacementExecutor's spatial indices
			}
		}

		/// Unload placement data for chunks that are no longer loaded.
		/// Tracks which chunks exist in PlacementExecutor but not in ChunkManager.
		void cleanupUnloadedChunks() {
			// Get set of loaded chunk coordinates
			std::unordered_set<engine::world::ChunkCoordinate> loadedChunks;
			for (const auto* chunk : m_chunkManager->getLoadedChunks()) {
				loadedChunks.insert(chunk->coordinate());
			}

			// Find chunks in PlacementExecutor that are no longer loaded
			std::vector<engine::world::ChunkCoordinate> toUnload;
			for (const auto& coord : m_processedChunks) {
				if (loadedChunks.find(coord) == loadedChunks.end()) {
					toUnload.push_back(coord);
				}
			}

			// Unload them from PlacementExecutor
			for (const auto& coord : toUnload) {
				m_placementExecutor->unloadChunk(coord);
				m_processedChunks.erase(coord);
				LOG_DEBUG(Game, "Unloaded placement data for chunk (%d, %d)", coord.x, coord.y);
			}
		}

		std::unique_ptr<engine::world::ChunkManager>	   m_chunkManager;
		std::unique_ptr<engine::world::WorldCamera>		   m_camera;
		std::unique_ptr<engine::world::ChunkRenderer>	   m_renderer;
		std::unique_ptr<engine::assets::PlacementExecutor> m_placementExecutor;
		std::unique_ptr<world_sim::GameOverlay>			   m_overlay;

		// Track processed chunk coordinates for cleanup detection
		std::unordered_set<engine::world::ChunkCoordinate> m_processedChunks;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
