// Game Scene - Main gameplay with chunk-based world rendering

#include "../GameWorldState.h"
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
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/MockWorldSampler.h>
#include <world/rendering/ChunkRenderer.h>
#include <world/rendering/EntityRenderer.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/AsyncChunkProcessor.h>
#include <assets/placement/PlacementExecutor.h>

#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

	constexpr const char* kSceneName = "game";
	constexpr uint64_t	  kDefaultWorldSeed = 12345;
	constexpr float		  kPixelsPerMeter = 8.0F;

	class GameScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "GameScene - Entering");

			// Check for pre-loaded state from GameLoadingScene
			auto preloadedState = world_sim::GameWorldState::Take();
			if (preloadedState) {
				// Use pre-loaded state (no pop-in!)
				LOG_INFO(Game, "GameScene - Using pre-loaded world state");
				m_chunkManager = std::move(preloadedState->m_chunkManager);
				m_camera = std::move(preloadedState->m_camera);
				m_renderer = std::move(preloadedState->m_renderer);
				m_entityRenderer = std::move(preloadedState->m_entityRenderer);
				m_placementExecutor = std::move(preloadedState->m_placementExecutor);
				m_processedChunks = std::move(preloadedState->m_processedChunks);

				LOG_INFO(
					Game,
					"Pre-loaded state: %zu chunks, %zu processed",
					m_chunkManager->loadedChunkCount(),
					m_processedChunks.size()
				);
			} else {
				// Fall back to progressive loading (for --scene=game direct access)
				LOG_INFO(Game, "GameScene - No pre-loaded state, initializing fresh");

				// Create world systems
				auto sampler = std::make_unique<engine::world::MockWorldSampler>(kDefaultWorldSeed);
				m_chunkManager = std::make_unique<engine::world::ChunkManager>(std::move(sampler));

				m_camera = std::make_unique<engine::world::WorldCamera>();
				m_camera->setPanSpeed(200.0F);

				m_renderer = std::make_unique<engine::world::ChunkRenderer>(kPixelsPerMeter);
				m_renderer->setTileResolution(1); // Render every tile (no skipping)

				m_entityRenderer = std::make_unique<engine::world::EntityRenderer>(kPixelsPerMeter);

				// Initialize entity placement system
				auto& assetRegistry = engine::assets::AssetRegistry::Get();
				m_placementExecutor = std::make_unique<engine::assets::PlacementExecutor>(assetRegistry);
				m_placementExecutor->initialize();
				LOG_INFO(Game, "PlacementExecutor initialized with %zu entity types", m_placementExecutor->getSpawnOrder().size());

				// Initial chunk load (entity placement happens async in update())
				m_chunkManager->update(m_camera->position());

				LOG_INFO(Game, "World initialized with seed %llu", kDefaultWorldSeed);
			}

			// Create async processor for runtime chunk streaming
			m_asyncProcessor = std::make_unique<engine::assets::AsyncChunkProcessor>(
				*m_placementExecutor,
				kDefaultWorldSeed,
				m_processedChunks
			);

			// Create overlay with zoom callbacks
			m_overlay = std::make_unique<world_sim::GameOverlay>(
				world_sim::GameOverlay::Args{.onZoomIn = [this]() { m_camera->zoomIn(); }, .onZoomOut = [this]() { m_camera->zoomOut(); }}
			);

			// Initial layout pass
			int viewportW = 0;
			int viewportH = 0;
			Renderer::Primitives::getViewport(viewportW, viewportH);
			m_overlay->layout(Foundation::Rect{0, 0, static_cast<float>(viewportW), static_cast<float>(viewportH)});
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

			m_entityRenderer->render(*m_placementExecutor, m_processedChunks, *m_camera, w, h);

			m_overlay->render();
		}

		void onExit() override {
			LOG_INFO(Game, "GameScene - Exiting");

			// Wait for all pending async tasks to complete before destroying executor
			if (m_asyncProcessor) {
				m_asyncProcessor->clear();
			}

			m_asyncProcessor.reset();
			m_overlay.reset();
			m_placementExecutor.reset();
			m_chunkManager.reset();
			m_camera.reset();
			m_entityRenderer.reset();
			m_renderer.reset();
		}

		std::string exportState() override {
			std::ostringstream oss;
			oss << R"({"scene":"game","chunks":)" << m_chunkManager->loadedChunkCount() << "}";
			return oss.str();
		}

		const char* getName() const override { return kSceneName; }

	  private:
		/// Launch async tasks for newly loaded chunks.
		/// Non-blocking: spawns background threads for entity placement computation.
		void processNewChunks() {
			// First, poll and integrate any completed async tasks
			m_asyncProcessor->pollCompleted();

			// Then launch new async tasks for unprocessed chunks
			for (auto* chunk : m_chunkManager->getLoadedChunks()) {
				m_asyncProcessor->launchTask(chunk);
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
		std::unique_ptr<engine::world::EntityRenderer>	   m_entityRenderer;
		std::unique_ptr<engine::assets::PlacementExecutor> m_placementExecutor;
		std::unique_ptr<world_sim::GameOverlay>			   m_overlay;

		// Async chunk processor (shared implementation with GameLoadingScene)
		std::unique_ptr<engine::assets::AsyncChunkProcessor> m_asyncProcessor;

		// Track processed chunk coordinates for cleanup detection
		std::unordered_set<engine::world::ChunkCoordinate> m_processedChunks;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
