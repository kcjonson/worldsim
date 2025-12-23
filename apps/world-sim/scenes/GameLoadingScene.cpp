// Game Loading Scene - Pre-loads world chunks and entities with progress bar
// Prevents asset "pop-in" by ensuring all initial content is ready before gameplay

#include "../GameWorldState.h"
#include "SceneTypes.h"

#include <GL/glew.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/AsyncChunkProcessor.h>
#include <assets/placement/PlacementExecutor.h>
#include <graphics/Color.h>
#include <input/InputManager.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>
#include <scene/SceneManager.h>
#include <shapes/Shapes.h>
#include <utils/Log.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/MockWorldSampler.h>
#include <world/rendering/ChunkRenderer.h>
#include <world/rendering/EntityRenderer.h>

#include <memory>
#include <sstream>
#include <vector>

namespace {

	constexpr const char* kSceneName = "gameloading";
	constexpr uint64_t	  kDefaultWorldSeed = 12345;
	constexpr float		  kPixelsPerMeter = 8.0F;
	constexpr int		  kTargetChunks = 9; // 3×3 grid (center + 8 adjacent)

	/// Loading phases
	enum class LoadingPhase { Initializing, LoadingChunks, PlacingEntities, Complete, Cancelling };

	class GameLoadingScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "GameLoadingScene - Entering");

			m_phase = LoadingPhase::Initializing;
			m_progress = 0.0F;
			m_chunksLoaded = 0;
			m_chunksProcessed = 0;
			m_asyncProcessor.reset();
			m_needsLayout = true; // Defer position update until first render (viewport not ready in onEnter)

			// Create the world state that will be transferred to GameScene
			m_worldState = std::make_unique<world_sim::GameWorldState>();
			m_worldState->worldSeed = kDefaultWorldSeed;

			// Create UI elements once with initial positions (will be updated in layoutUI)
			m_title = std::make_unique<UI::Text>(UI::Text::Args{
				.position = {0.0F, 0.0F},
				.text = "Loading World",
				.style =
					{
						.color = Foundation::Color::white(),
						.fontSize = 48.0F,
						.hAlign = Foundation::HorizontalAlign::Center,
						.vAlign = Foundation::VerticalAlign::Middle,
					},
				.id = "loading_title"
			});

			m_statusText = std::make_unique<UI::Text>(UI::Text::Args{
				.position = {0.0F, 0.0F},
				.text = "Initializing...",
				.style =
					{
						.color = Foundation::Color(0.7F, 0.7F, 0.7F, 1.0F),
						.fontSize = 18.0F,
						.hAlign = Foundation::HorizontalAlign::Center,
						.vAlign = Foundation::VerticalAlign::Middle,
					},
				.id = "loading_status"
			});
		}

		/// Update UI element positions based on current viewport size
		void layoutUI() {
			// Use percentage-based positioning (same pattern as SplashScene)
			float centerX = Renderer::Primitives::PercentWidth(50.0F);
			float centerY = Renderer::Primitives::PercentHeight(50.0F);

			// Check if viewport is ready (values will be 0 if not)
			if (centerX < 1.0F || centerY < 1.0F) {
				return; // Viewport not ready yet
			}

			// Update positions of existing UI elements
			m_title->position = {centerX, centerY - 80.0F};
			m_statusText->position = {centerX, centerY + 60.0F};

			// Progress bar dimensions
			m_barWidth = 400.0F;
			m_barHeight = 24.0F;
			m_barX = centerX - (m_barWidth / 2.0F);
			m_barY = centerY;

			m_needsLayout = false;
		}

		void update(float /*dt*/) override {
			// Check for ESC to cancel loading
			auto& input = engine::InputManager::Get();
			if (input.isKeyPressed(engine::Key::Escape) && m_phase != LoadingPhase::Cancelling && m_phase != LoadingPhase::Complete) {
				LOG_INFO(Game, "GameLoadingScene - Cancel requested");
				m_phase = LoadingPhase::Cancelling;
				updateStatusText("Cancelling...");
			}

			switch (m_phase) {
				case LoadingPhase::Initializing:
					initializeWorldSystems();
					break;

				case LoadingPhase::LoadingChunks:
					loadChunks();
					break;

				case LoadingPhase::PlacingEntities:
					placeEntities();
					break;

				case LoadingPhase::Complete:
					transitionToGame();
					break;

				case LoadingPhase::Cancelling:
					cancelLoading();
					break;
			}
		}

		void render() override {
			// Deferred layout - viewport is only valid during render
			if (m_needsLayout) {
				layoutUI();
			}

			// Dark background
			glClearColor(0.05F, 0.08F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render title
			if (m_title) {
				m_title->render();
			}

			// Render progress bar background
			Renderer::Primitives::drawRect({
				.bounds = {m_barX, m_barY, m_barWidth, m_barHeight},
				.style = {.fill = Foundation::Color(0.15F, 0.15F, 0.2F, 1.0F)},
			});

			// Render progress bar fill
			float fillWidth = m_barWidth * m_progress;
			if (fillWidth > 0.0F) {
				Renderer::Primitives::drawRect({
					.bounds = {m_barX, m_barY, fillWidth, m_barHeight},
					.style = {.fill = Foundation::Color(0.2F, 0.6F, 0.3F, 1.0F)},
				});
			}

			// Render progress bar border
			Renderer::Primitives::drawRect({
				.bounds = {m_barX, m_barY, m_barWidth, m_barHeight},
				.style = {
					.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), // Transparent fill
					.border = Foundation::BorderStyle{
						.color = Foundation::Color(0.4F, 0.4F, 0.5F, 1.0F),
						.width = 2.0F,
					},
				},
			});

			// Render status text
			if (m_statusText) {
				m_statusText->render();
			}
		}

		void onExit() override {
			LOG_INFO(Game, "GameLoadingScene - Exiting");
			m_asyncProcessor.reset();
			m_title.reset();
			m_statusText.reset();
			// Note: m_worldState is moved to GameWorldState::SetPending() before exit
		}

		std::string exportState() override {
			std::ostringstream oss;
			oss << R"({"scene":"gameloading","progress":)" << m_progress << "}";
			return oss.str();
		}

		const char* getName() const override { return kSceneName; }

	  private:
		/// Phase 1: Initialize world systems
		void initializeWorldSystems() {
			LOG_INFO(Game, "GameLoadingScene - Initializing world systems");

			// Create world sampler and chunk manager
			auto sampler = std::make_unique<engine::world::MockWorldSampler>(kDefaultWorldSeed);
			m_worldState->chunkManager = std::make_unique<engine::world::ChunkManager>(std::move(sampler));

			// Only load 3×3 grid (center + 8 adjacent) - chunks are large!
			m_worldState->chunkManager->setLoadRadius(1);
			m_worldState->chunkManager->setUnloadRadius(2);

			// Create camera at origin
			m_worldState->camera = std::make_unique<engine::world::WorldCamera>();
			m_worldState->camera->setPanSpeed(200.0F);

			// Create renderers
			m_worldState->renderer = std::make_unique<engine::world::ChunkRenderer>(kPixelsPerMeter);
			m_worldState->renderer->setTileResolution(1);
			m_worldState->entityRenderer = std::make_unique<engine::world::EntityRenderer>(kPixelsPerMeter);

			// Initialize placement executor
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			m_worldState->placementExecutor = std::make_unique<engine::assets::PlacementExecutor>(assetRegistry);
			m_worldState->placementExecutor->initialize();

			LOG_INFO(Game, "PlacementExecutor initialized with %zu entity types", m_worldState->placementExecutor->getSpawnOrder().size());

			// Move to next phase
			m_phase = LoadingPhase::LoadingChunks;
			updateStatusText("Generating terrain...");
		}

		/// Phase 2: Load chunks (ChunkManager loads all needed chunks in one call)
		void loadChunks() {
			// ChunkManager::update() loads the 5×5 grid around the camera position
			m_worldState->chunkManager->update(m_worldState->camera->position());
			m_chunksLoaded = static_cast<int>(m_worldState->chunkManager->loadedChunkCount());

			// Calculate progress (0-50% for chunk loading)
			m_progress = static_cast<float>(m_chunksLoaded) / static_cast<float>(kTargetChunks * 2);

			if (m_chunksLoaded >= kTargetChunks) {
				LOG_INFO(Game, "GameLoadingScene - %d chunks loaded", m_chunksLoaded);

				// Create async processor for entity placement
				m_asyncProcessor = std::make_unique<engine::assets::AsyncChunkProcessor>(
					*m_worldState->placementExecutor, m_worldState->worldSeed, m_worldState->processedChunks
				);

				// Launch all async tasks at once
				for (auto* chunk : m_worldState->chunkManager->getLoadedChunks()) {
					m_asyncProcessor->launchTask(chunk);
				}

				LOG_INFO(Game, "GameLoadingScene - Launched %zu async placement tasks", m_asyncProcessor->pendingCount());

				m_phase = LoadingPhase::PlacingEntities;
				updateStatusText("Placing entities...");
			}
		}

		/// Phase 3: Place entities asynchronously for responsive UI
		void placeEntities() {
			// Poll for completed futures (non-blocking)
			size_t completed = m_asyncProcessor->pollCompleted();
			m_chunksProcessed += static_cast<int>(completed);

			// Update progress (50-100% for entity placement)
			m_progress = 0.5F + (static_cast<float>(m_chunksProcessed) / static_cast<float>(kTargetChunks * 2));

			// Update status with progress
			int			percent = static_cast<int>(m_progress * 100.0F);
			std::string status = "Placing entities... " + std::to_string(percent) + "%";
			updateStatusText(status);

			// Check if all tasks are complete
			if (!m_asyncProcessor->hasPending()) {
				LOG_INFO(Game, "placeEntities: All %d chunks completed!", m_chunksProcessed);
				m_phase = LoadingPhase::Complete;
				m_progress = 1.0F;
				updateStatusText("Ready!");
			}
		}

		/// Transition to GameScene with fully loaded state
		void transitionToGame() {
			LOG_INFO(Game, "GameLoadingScene - Complete! %d chunks loaded, %d processed", m_chunksLoaded, m_chunksProcessed);

			// Transfer state to pending holder
			world_sim::GameWorldState::SetPending(std::move(m_worldState));

			// Switch to game scene
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::Game));
		}

		/// Cancel loading - waits for async tasks to complete with UI feedback
		void cancelLoading() {
			// Poll for completed tasks (non-blocking)
			if (m_asyncProcessor) {
				m_asyncProcessor->pollCompleted();

				// Still have pending tasks - wait for them
				if (m_asyncProcessor->hasPending()) {
					return; // Keep polling each frame
				}
			}

			// All tasks done, safe to transition
			LOG_INFO(Game, "GameLoadingScene - Cancelled, returning to main menu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}

		/// Update the status text content (not the element itself)
		void updateStatusText(const std::string& text) {
			if (m_statusText) {
				m_statusText->text = text;
			}
		}

		// Loading state
		LoadingPhase m_phase = LoadingPhase::Initializing;
		float		 m_progress = 0.0F;
		int			 m_chunksLoaded = 0;
		int			 m_chunksProcessed = 0;

		// Async chunk processor (shared implementation)
		std::unique_ptr<engine::assets::AsyncChunkProcessor> m_asyncProcessor;

		// World state being built (transferred to GameScene when complete)
		std::unique_ptr<world_sim::GameWorldState> m_worldState;

		// UI elements
		std::unique_ptr<UI::Text> m_title;
		std::unique_ptr<UI::Text> m_statusText;
		bool					  m_needsLayout = false;

		// Progress bar layout
		float m_barX = 0.0F;
		float m_barY = 0.0F;
		float m_barWidth = 400.0F;
		float m_barHeight = 24.0F;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo GameLoading = {kSceneName, []() { return std::make_unique<GameLoadingScene>(); }};
} // namespace world_sim::scenes
