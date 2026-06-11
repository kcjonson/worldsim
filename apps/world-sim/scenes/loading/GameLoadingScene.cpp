// Game Loading Scene - Pre-loads world chunks and entities with progress bar
// Prevents asset "pop-in" by ensuring all initial content is ready before gameplay.
//
// Always plays on a generated planet: the pending GameStartConfig either carries
// a world from the creator flow, or (Quick Start / direct scene jump) the scene
// loads the cached quickstart planet, generating and caching it on first run.

#include "GameStartConfig.h"
#include "GameWorldState.h"
#include "SceneTypes.h"

#include <GL/glew.h>

#include <assets/ActionTypeRegistry.h>
#include <assets/AssetRegistry.h>
#include <assets/ConfigValidator.h>
#include <assets/PriorityConfig.h>
#include <assets/TaskChainRegistry.h>
#include <assets/WorkTypeRegistry.h>
#include <assets/placement/AsyncChunkProcessor.h>
#include <assets/placement/PlacementExecutor.h>
#include <ecs/GlobalTaskRegistry.h>
#include <ecs/components/Memory.h>
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
#include <world/chunk/GeneratedWorldSampler.h>
#include <world/rendering/ChunkRenderer.h>
#include <world/rendering/EntityRenderer.h>

#include <worldgen/data/PlanetParams.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/pipeline/PlanetGenerator.h>
#include <worldgen/sampling/LandingSite.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <vector>

namespace {

	constexpr const char* kSceneName = "gameloading";
	constexpr float		  kPixelsPerMeter = 8.0F;
	constexpr int		  kTargetChunks = 9; // 3×3 grid (center + 8 adjacent)

	// Quickstart planet: fixed params so the cache file stays valid across runs.
	constexpr uint64_t	  kQuickstartSeed = 424242;
	constexpr uint32_t	  kQuickstartSubdivision = 256;
	constexpr const char* kQuickstartPlanetPath = "planets/quickstart.wsplanet";

	/// Loading phases
	enum class LoadingPhase { PreparingPlanet, Initializing, ConfigError, LoadingChunks, PlacingEntities, Complete, Cancelling };

	class GameLoadingScene : public engine::IScene {
	  public:
		void onEnter() override {
			LOG_INFO(Game, "GameLoadingScene - Entering");

			phase = LoadingPhase::PreparingPlanet;
			progress = 0.0F;
			chunksLoaded = 0;
			chunksProcessed = 0;
			configErrorLogged = false;
			asyncProcessor.reset();
			planetGenerator.reset();
			needsLayout = true; // Defer position update until first render (viewport not ready in onEnter)

			// Direct scene jumps (debug API, --scene=game) have no pending
			// config; treat them as Quick Start.
			startConfig = world_sim::GameStartConfig::Take();
			if (!startConfig) {
				startConfig = std::make_unique<world_sim::GameStartConfig>();
				startConfig->source = world_sim::GameStartConfig::Source::QuickStart;
			}

			// Create the world state that will be transferred to GameScene
			worldState = std::make_unique<world_sim::GameWorldState>();

			// Create UI elements once with initial positions (will be updated in layoutUI)
			title = std::make_unique<UI::Text>(UI::Text::Args{
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

			statusText = std::make_unique<UI::Text>(UI::Text::Args{
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
			title->position = {centerX, centerY - 80.0F};
			statusText->position = {centerX, centerY + 60.0F};

			// Progress bar dimensions
			barWidth = 400.0F;
			barHeight = 24.0F;
			barX = centerX - (barWidth / 2.0F);
			barY = centerY;

			needsLayout = false;
		}

		void update(float /*dt*/) override {
			// Check for ESC to cancel loading or return from error
			auto& input = engine::InputManager::Get();
			if (input.isKeyPressed(engine::Key::Escape)) {
				if (phase == LoadingPhase::ConfigError) {
					// Config error - return to menu immediately
					LOG_INFO(Game, "GameLoadingScene - Returning to menu from config error");
					sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
					return;
				}
				if (phase != LoadingPhase::Cancelling && phase != LoadingPhase::Complete) {
					LOG_INFO(Game, "GameLoadingScene - Cancel requested");
					phase = LoadingPhase::Cancelling;
					updateStatusText("Cancelling...");
				}
			}

			switch (phase) {
				case LoadingPhase::PreparingPlanet:
					preparePlanet();
					break;

				case LoadingPhase::Initializing:
					initializeWorldSystems();
					break;

				case LoadingPhase::ConfigError:
					handleConfigError();
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
			if (needsLayout) {
				layoutUI();
			}

			// Dark background
			glClearColor(0.05F, 0.08F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render title
			if (title) {
				title->render();
			}

			// Render progress bar background
			Renderer::Primitives::drawRect({
				.bounds = {barX, barY, barWidth, barHeight},
				.style = {.fill = Foundation::Color(0.15F, 0.15F, 0.2F, 1.0F)},
			});

			// Render progress bar fill
			float fillWidth = barWidth * progress;
			if (fillWidth > 0.0F) {
				Renderer::Primitives::drawRect({
					.bounds = {barX, barY, fillWidth, barHeight},
					.style = {.fill = Foundation::Color(0.2F, 0.6F, 0.3F, 1.0F)},
				});
			}

			// Render progress bar border
			Renderer::Primitives::drawRect({
				.bounds = {barX, barY, barWidth, barHeight},
				.style = {
					.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), // Transparent fill
					.border = Foundation::BorderStyle{
						.color = Foundation::Color(0.4F, 0.4F, 0.5F, 1.0F),
						.width = 2.0F,
					},
				},
			});

			// Render status text
			if (statusText) {
				statusText->render();
			}
		}

		void onExit() override {
			LOG_INFO(Game, "GameLoadingScene - Exiting");
			asyncProcessor.reset();
			planetGenerator.reset();
			title.reset();
			statusText.reset();
			// Note: worldState is moved to GameWorldState::SetPending() before exit
		}

		std::string exportState() override {
			std::ostringstream oss;
			oss << R"({"scene":"gameloading","progress":)" << progress << "}";
			return oss.str();
		}

		const char* getName() const override { return kSceneName; }

	  private:
		/// Phase 0: ensure we have a planet to land on.
		/// Creator flow already provides one; Quick Start loads the cached
		/// planet or generates it once (progress 0-30%) and caches it.
		void preparePlanet() {
			if (startConfig->world) {
				phase = LoadingPhase::Initializing;
				progress = 0.3F;
				return;
			}

			if (!planetGenerator) {
				// First try the cache
				if (auto cached = worldgen::loadPlanet(kQuickstartPlanetPath)) {
					LOG_INFO(Game, "GameLoadingScene - Loaded quickstart planet from cache");
					adoptQuickstartPlanet(std::move(cached));
					return;
				}

				LOG_INFO(Game, "GameLoadingScene - No cached quickstart planet, generating (n=%u seed=%llu)",
				         kQuickstartSubdivision,
				         static_cast<unsigned long long>(kQuickstartSeed));
				updateStatusText("Generating planet (first run)...");
				worldgen::PlanetParams params = worldgen::PlanetParams::preset(worldgen::Preset::EarthLike);
				params.gridSubdivision = kQuickstartSubdivision;
				params.seed = kQuickstartSeed;
				planetGenerator = std::make_unique<worldgen::PlanetGenerator>();
				planetGenerator->start(params);
				return;
			}

			auto prog = planetGenerator->progress();
			progress = prog.totalFraction * 0.3F;

			if (prog.state == worldgen::GenerationProgress::State::Complete) {
				auto result = planetGenerator->takeResult();
				planetGenerator.reset();
				if (!result) {
					LOG_ERROR(Game, "GameLoadingScene - Planet generation returned no result");
					phase = LoadingPhase::ConfigError;
					return;
				}
				if (worldgen::savePlanet(*result, kQuickstartPlanetPath)) {
					LOG_INFO(Game, "GameLoadingScene - Cached quickstart planet to %s", kQuickstartPlanetPath);
				}
				adoptQuickstartPlanet(std::move(result));
			} else if (prog.state == worldgen::GenerationProgress::State::Failed ||
			           prog.state == worldgen::GenerationProgress::State::Cancelled) {
				LOG_ERROR(Game, "GameLoadingScene - Planet generation failed/cancelled");
				planetGenerator.reset();
				phase = LoadingPhase::ConfigError;
			} else {
				int percent = static_cast<int>(prog.totalFraction * 100.0F);
				updateStatusText("Generating planet (first run)... " + std::to_string(percent) + "%");
			}
		}

		void adoptQuickstartPlanet(std::shared_ptr<const worldgen::GeneratedWorld> planet) {
			auto site = worldgen::findDefaultLandingSite(*planet);
			startConfig->world = std::move(planet);
			startConfig->landingLatDeg = site.latDeg;
			startConfig->landingLonDeg = site.lonDeg;
			LOG_INFO(Game, "GameLoadingScene - Quickstart landing site lat=%.2f lon=%.2f",
			         site.latDeg, site.lonDeg);
			phase = LoadingPhase::Initializing;
			progress = 0.3F;
		}

		/// Phase 1: Initialize world systems
		void initializeWorldSystems() {
			LOG_INFO(Game, "GameLoadingScene - Initializing world systems");

			// Load work configuration first
			updateStatusText("Loading configuration...");
			if (!loadWorkConfigs()) {
				phase = LoadingPhase::ConfigError;
				return;
			}

			// Create world sampler and chunk manager from the generated planet;
			// the landing site maps to the 2D world origin
			auto sampler = std::make_unique<engine::world::GeneratedWorldSampler>(
				startConfig->world, startConfig->landingLatDeg, startConfig->landingLonDeg);
			worldState->worldSeed = sampler->getWorldSeed();
			worldState->chunkManager = std::make_unique<engine::world::ChunkManager>(std::move(sampler));

			// Only load 3×3 grid (center + 8 adjacent) - chunks are large!
			worldState->chunkManager->setLoadRadius(1);
			worldState->chunkManager->setUnloadRadius(2);

			// Create camera at origin
			worldState->camera = std::make_unique<engine::world::WorldCamera>();
			worldState->camera->setPanSpeed(200.0F);

			// Create renderers
			worldState->renderer = std::make_unique<engine::world::ChunkRenderer>(kPixelsPerMeter);
			worldState->entityRenderer = std::make_unique<engine::world::EntityRenderer>(kPixelsPerMeter);

			// Initialize placement executor
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			worldState->placementExecutor = std::make_unique<engine::assets::PlacementExecutor>(assetRegistry);
			worldState->placementExecutor->initialize();

			LOG_INFO(Game, "PlacementExecutor initialized with %zu entity types", worldState->placementExecutor->getSpawnOrder().size());

			// Move to next phase
			phase = LoadingPhase::LoadingChunks;
			updateStatusText("Generating terrain...");
		}

		/// Phase 2: Load chunks (generation runs on workers; keep polling until ready)
		void loadChunks() {
			// ChunkManager::update() starts loads for the grid around the camera
			// and integrates finished generation workers each call
			worldState->chunkManager->update(worldState->camera->position());

			// Count chunks whose tile generation has completed
			chunksLoaded = 0;
			for (const auto* chunk : worldState->chunkManager->getLoadedChunks()) {
				if (chunk->isReady()) {
					chunksLoaded++;
				}
			}

			// Chunk loading covers 30-65% of the bar (planet was 0-30%)
			progress = 0.3F + 0.35F * (static_cast<float>(chunksLoaded) / static_cast<float>(kTargetChunks));

			if (chunksLoaded >= kTargetChunks) {
				LOG_INFO(Game, "GameLoadingScene - %d chunks loaded", chunksLoaded);

				// Create async processor for entity placement
				asyncProcessor = std::make_unique<engine::assets::AsyncChunkProcessor>(
					*worldState->placementExecutor, worldState->worldSeed, worldState->processedChunks
				);

				// Launch all async tasks at once
				for (auto* chunk : worldState->chunkManager->getLoadedChunks()) {
					asyncProcessor->launchTask(chunk);
				}

				LOG_INFO(Game, "GameLoadingScene - Launched %zu async placement tasks", asyncProcessor->pendingCount());

				phase = LoadingPhase::PlacingEntities;
				updateStatusText("Placing entities...");
			}
		}

		/// Phase 3: Place entities asynchronously for responsive UI
		void placeEntities() {
			// Chunks that finished generating after phase 2 still need placement
			worldState->chunkManager->update(worldState->camera->position());
			for (auto* chunk : worldState->chunkManager->getLoadedChunks()) {
				asyncProcessor->launchTask(chunk);
			}

			// Poll for completed futures (non-blocking)
			size_t completed = asyncProcessor->pollCompleted();
			chunksProcessed += static_cast<int>(completed);

			// Upload worker-baked entity meshes so gameplay starts pre-baked
			for (auto& [coord, bake] : asyncProcessor->takeReadyBakes()) {
				worldState->entityRenderer->uploadBakedChunk(coord, std::move(bake));
			}

			// Entity placement covers 65-100%
			progress = 0.65F + 0.35F * (static_cast<float>(chunksProcessed) / static_cast<float>(kTargetChunks));
			progress = std::min(progress, 1.0F);

			// Update status with progress
			int			percent = static_cast<int>(progress * 100.0F);
			std::string status = "Placing entities... " + std::to_string(percent) + "%";
			updateStatusText(status);

			// Check if all tasks are complete (every loaded chunk processed and
			// nothing in flight; chunks still generating haven't launched yet)
			if (!asyncProcessor->hasPending() &&
				worldState->processedChunks.size() >= worldState->chunkManager->loadedChunkCount()) {
				LOG_INFO(Game, "placeEntities: All %d chunks completed!", chunksProcessed);
				phase = LoadingPhase::Complete;
				progress = 1.0F;
				updateStatusText("Ready!");
			}
		}

		/// Transition to GameScene with fully loaded state
		void transitionToGame() {
			LOG_INFO(Game, "GameLoadingScene - Complete! %d chunks loaded, %d processed", chunksLoaded, chunksProcessed);

			// Transfer state to pending holder
			world_sim::GameWorldState::SetPending(std::move(worldState));

			// Switch to game scene
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::Game));
		}

		/// Cancel loading - waits for async tasks to complete with UI feedback
		void cancelLoading() {
			// Stop planet generation if it's still running
			if (planetGenerator) {
				planetGenerator->cancel();
				auto prog = planetGenerator->progress();
				if (prog.state == worldgen::GenerationProgress::State::Running) {
					return; // Keep polling until the worker observes the cancel
				}
				planetGenerator.reset();
			}

			// Poll for completed tasks (non-blocking)
			if (asyncProcessor) {
				asyncProcessor->pollCompleted();

				// Still have pending tasks - wait for them
				if (asyncProcessor->hasPending()) {
					return; // Keep polling each frame
				}
			}

			// All tasks done, safe to transition
			LOG_INFO(Game, "GameLoadingScene - Cancelled, returning to main menu");
			sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
		}

		/// Update the status text content (not the element itself)
		void updateStatusText(const std::string& text) {
			if (statusText) {
				statusText->text = text;
			}
		}

		/// Load work configuration files (actions, chains, work types, priority tuning)
		/// Returns false if any config fails to load or validate
		bool loadWorkConfigs() {
			using namespace engine::assets;

			// Clear any previous state (supports menu → new game cycle)
			ActionTypeRegistry::Get().clear();
			TaskChainRegistry::Get().clear();
			WorkTypeRegistry::Get().clear();
			PriorityConfig::Get().clear();
			ConfigValidator::clearErrors();
			ecs::GlobalTaskRegistry::Get().clear();

			// Set up Memory eviction callback to notify GlobalTaskRegistry when colonists forget entities
			ecs::Memory::setEvictionCallback([](ecs::EntityID colonist, uint64_t worldEntityKey) {
				ecs::GlobalTaskRegistry::Get().onEntityForgotten(colonist, worldEntityKey);
			});

			// Load in dependency order
			std::string basePath = "assets/config/";

			if (!ActionTypeRegistry::Get().loadFromFile(basePath + "actions/action-types.xml")) {
				LOG_ERROR(Game, "Failed to load action-types.xml");
				return false;
			}

			if (!TaskChainRegistry::Get().loadFromFile(basePath + "work/task-chains.xml")) {
				LOG_ERROR(Game, "Failed to load task-chains.xml");
				return false;
			}

			if (!WorkTypeRegistry::Get().loadFromFile(basePath + "work/work-types.xml")) {
				LOG_ERROR(Game, "Failed to load work-types.xml");
				return false;
			}

			if (!PriorityConfig::Get().loadFromFile(basePath + "work/priority-tuning.xml")) {
				LOG_ERROR(Game, "Failed to load priority-tuning.xml");
				return false;
			}

			// Validate cross-references between configs
			if (!ConfigValidator::validateAll()) {
				LOG_ERROR(Game, "Config validation failed");
				return false;
			}

			LOG_INFO(Game, "Work configuration loaded successfully");
			return true;
		}

		/// Handle config error state - show error and wait for ESC
		void handleConfigError() {
			// Only update UI once (errors are already logged by ConfigValidator::validateAll)
			if (!configErrorLogged) {
				configErrorLogged = true;
				updateStatusText("Loading failed - Press ESC to return to menu");
			}
			// ESC handling is done in update() before the switch
		}

		// Loading state
		LoadingPhase phase = LoadingPhase::Initializing;
		float		 progress = 0.0F;
		int			 chunksLoaded = 0;
		int			 chunksProcessed = 0;
		bool		 configErrorLogged = false;

		// Async chunk processor (shared implementation)
		std::unique_ptr<engine::assets::AsyncChunkProcessor> asyncProcessor;

		// World state being built (transferred to GameScene when complete)
		std::unique_ptr<world_sim::GameWorldState> worldState;

		// How this game starts (planet + landing site)
		std::unique_ptr<world_sim::GameStartConfig> startConfig;

		// Quickstart planet generation (only on cache miss)
		std::unique_ptr<worldgen::PlanetGenerator> planetGenerator;

		// UI elements
		std::unique_ptr<UI::Text> title;
		std::unique_ptr<UI::Text> statusText;
		bool					  needsLayout = false;

		// Progress bar layout
		float barX = 0.0F;
		float barY = 0.0F;
		float barWidth = 400.0F;
		float barHeight = 24.0F;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo GameLoading = {kSceneName, []() { return std::make_unique<GameLoadingScene>(); }};
} // namespace world_sim::scenes
