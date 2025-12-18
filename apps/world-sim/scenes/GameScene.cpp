// Game Scene - Main gameplay with chunk-based world rendering

#include "../GameWorldState.h"
#include "../components/GameUI.h"
#include "../components/GhostRenderer.h"
#include "../components/PlacementMode.h"
#include "../components/Selection.h"
#include "SceneTypes.h"

#include <GL/glew.h>

#include <application/AppLauncher.h>
#include <chrono>
#include <cmath>
#include <graphics/Rect.h>
#include <input/InputManager.h>
#include <metrics/MetricsCollector.h>
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
#include <assets/RecipeRegistry.h>
#include <assets/placement/AsyncChunkProcessor.h>
#include <assets/placement/PlacementExecutor.h>

// ECS
#include <ecs/World.h>
#include <ecs/components/Action.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Knowledge.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/AIDecisionSystem.h>
#include <ecs/systems/ActionSystem.h>
#include <ecs/systems/DynamicEntityRenderSystem.h>
#include <ecs/systems/MovementSystem.h>
#include <ecs/systems/NeedsDecaySystem.h>
#include <ecs/systems/PhysicsSystem.h>
#include <ecs/systems/VisionSystem.h>

#include <memory>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {
	// Helper for timing measurements
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = std::chrono::time_point<Clock>;

	inline float elapsedMs(TimePoint start, TimePoint end) {
		return std::chrono::duration<float, std::milli>(end - start).count();
	}

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
				m_chunkManager = std::move(preloadedState->chunkManager);
				m_camera = std::move(preloadedState->camera);
				m_renderer = std::move(preloadedState->renderer);
				m_entityRenderer = std::move(preloadedState->entityRenderer);
				m_placementExecutor = std::move(preloadedState->placementExecutor);
				m_processedChunks = std::move(preloadedState->processedChunks);

				LOG_INFO(Game, "Pre-loaded state: %zu chunks, %zu processed", m_chunkManager->loadedChunkCount(), m_processedChunks.size());
			} else {
				// Initialize fresh (for debugging or direct GameScene access)
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
			m_asyncProcessor =
				std::make_unique<engine::assets::AsyncChunkProcessor>(*m_placementExecutor, kDefaultWorldSeed, m_processedChunks);

			// Initialize placement mode with callback to spawn entities
			placementMode = world_sim::PlacementMode{world_sim::PlacementMode::Args{
				.onPlace = [this](const std::string& defName, Foundation::Vec2 worldPos) {
					spawnPlacedEntity(defName, worldPos);
				}
			}};

			// Create unified game UI (contains overlay and info panel)
			gameUI = std::make_unique<world_sim::GameUI>(world_sim::GameUI::Args{
				.onZoomIn = [this]() { m_camera->zoomIn(); },
				.onZoomOut = [this]() { m_camera->zoomOut(); },
				.onSelectionCleared = [this]() { selection = world_sim::NoSelection{}; },
				.onColonistSelected = [this](ecs::EntityID entityId) { selection = world_sim::ColonistSelection{entityId}; },
				.onBuildToggle = [this]() { handleBuildToggle(); },
				.onBuildItemSelected = [this](const std::string& defName) { handleBuildItemSelected(defName); }
			});

			// Initial layout pass with consistent DPI scaling
			int viewportW = 0;
			int viewportH = 0;
			Renderer::Primitives::getLogicalViewport(viewportW, viewportH);
			gameUI->layout(Foundation::Rect{0, 0, static_cast<float>(viewportW), static_cast<float>(viewportH)});

			// Initialize ECS World
			initializeECS();
		}

		void handleInput(float dt) override {
			auto& input = engine::InputManager::Get();

			// Handle Escape - cancel placement mode first, then exit to menu
			if (input.isKeyPressed(engine::Key::Escape)) {
				if (placementMode.isActive()) {
					placementMode.cancel();
					gameUI->setBuildModeActive(false);
					gameUI->hideBuildMenu();
					return;
				}
				sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
				return;
			}

			// Handle B key - toggle build mode
			if (input.isKeyPressed(engine::Key::B)) {
				handleBuildToggle();
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

			// Handle UI input first - returns true if UI consumed the click
			bool uiConsumedInput = gameUI->handleInput();

			// Handle placement mode interaction
			if (placementMode.state() == world_sim::PlacementState::Placing) {
				auto mousePos = input.getMousePosition();
				int logicalW = 0;
				int logicalH = 0;
				Renderer::Primitives::getLogicalViewport(logicalW, logicalH);

				// Update ghost position from mouse
				auto worldPos = m_camera->screenToWorld(mousePos.x, mousePos.y, logicalW, logicalH, kPixelsPerMeter);
				placementMode.updateGhostPosition({worldPos.x, worldPos.y});

				// Try to place on click (if not over UI)
				if (!uiConsumedInput && input.isMouseButtonReleased(engine::MouseButton::Left)) {
					if (placementMode.tryPlace()) {
						// Successfully placed - update UI state
						gameUI->setBuildModeActive(false);
						gameUI->hideBuildMenu();
					}
				}
				return;
			}

			// Handle entity selection on left click release (only if UI didn't consume it and not in placement mode)
			// Note: Use isMouseButtonReleased (not Pressed) to avoid timing issues
			// with the input state machine's Pressed→Down transition
			if (!uiConsumedInput && input.isMouseButtonReleased(engine::MouseButton::Left)) {
				auto mousePos = input.getMousePosition();
				handleEntitySelection(mousePos);
			}
		}

		void update(float dt) override {
			auto updateStart = Clock::now();

			m_camera->update(dt);
			m_chunkManager->update(m_camera->position());

			// Process newly loaded chunks for entity placement
			processNewChunks();

			// Unload placement data for chunks that were unloaded
			cleanupUnloadedChunks();

			// Update ECS world (movement, physics, render system)
			ecsWorld->update(dt);

			// Update unified game UI (overlay + info panel)
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			gameUI->update(*m_camera, *m_chunkManager, *ecsWorld, assetRegistry, selection);

			m_lastUpdateMs = elapsedMs(updateStart, Clock::now());
		}

		void render() override {
			glClearColor(0.05F, 0.08F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			int w = 0;
			int h = 0;
			// Use logical viewport (DPI-independent) for consistent world-to-screen transforms
			// Physical viewport is 2x on Retina displays, which causes coordinate mismatches
			Renderer::Primitives::getLogicalViewport(w, h);

			// Time tile rendering
			auto tileStart = Clock::now();
			m_renderer->render(*m_chunkManager, *m_camera, w, h);
			float tileMs = elapsedMs(tileStart, Clock::now());

			// Time entity rendering (includes dynamic ECS entities)
			auto		entityStart = Clock::now();
			auto&		renderSystem = ecsWorld->getSystem<ecs::DynamicEntityRenderSystem>();
			const auto& dynamicEntities = renderSystem.getRenderData();
			m_entityRenderer->render(*m_placementExecutor, m_processedChunks, dynamicEntities, *m_camera, w, h);
			float entityMs = elapsedMs(entityStart, Clock::now());

			// Render selection indicator in world-space (after entities, before UI)
			renderSelectionIndicator(w, h);

			// Render placement ghost preview (if in placing mode)
			if (placementMode.state() == world_sim::PlacementState::Placing) {
				ghostRenderer.render(
					placementMode.selectedDefName(),
					placementMode.ghostPosition(),
					*m_camera,
					w,
					h,
					placementMode.isValidPlacement()
				);
			}

			// Render unified game UI (overlay + info panel)
			gameUI->render();

			// Report timing breakdown to metrics system
			auto* metrics = engine::AppLauncher::getMetrics();
			if (metrics != nullptr) {
				metrics->setTimingBreakdown(
					tileMs, entityMs, m_lastUpdateMs, m_renderer->lastTileCount(), m_entityRenderer->lastEntityCount()
				);
			}
		}

		void onExit() override {
			LOG_INFO(Game, "GameScene - Exiting");

			// Wait for all pending async tasks to complete before destroying executor
			if (m_asyncProcessor) {
				m_asyncProcessor->clear();
			}

			m_asyncProcessor.reset();
			gameUI.reset();
			ecsWorld.reset();
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
		/// Initialize ECS world with systems and spawn initial entities.
		void initializeECS() {
			LOG_INFO(Game, "Initializing ECS World");

			ecsWorld = std::make_unique<ecs::World>();

			// Register systems in priority order (lower = runs first)
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			ecsWorld->registerSystem<ecs::VisionSystem>();					// Priority 45
			ecsWorld->registerSystem<ecs::NeedsDecaySystem>();				// Priority 50
			ecsWorld->registerSystem<ecs::AIDecisionSystem>(assetRegistry); // Priority 60
			ecsWorld->registerSystem<ecs::MovementSystem>();				// Priority 100
			ecsWorld->registerSystem<ecs::PhysicsSystem>();					// Priority 200
			ecsWorld->registerSystem<ecs::ActionSystem>();					// Priority 350
			ecsWorld->registerSystem<ecs::DynamicEntityRenderSystem>();		// Priority 900

			// Wire up VisionSystem with placement data for entity queries
			auto& visionSystem = ecsWorld->getSystem<ecs::VisionSystem>();
			visionSystem.setPlacementData(m_placementExecutor.get(), &m_processedChunks);
			visionSystem.setChunkManager(m_chunkManager.get());

			// Wire up AIDecisionSystem with chunk manager for toilet location queries
			auto& aiDecisionSystem = ecsWorld->getSystem<ecs::AIDecisionSystem>();
			aiDecisionSystem.setChunkManager(m_chunkManager.get());

			// Spawn initial colonist at map center (0, 0)
			spawnColonist({0.0F, 0.0F}, "Bob");

			LOG_INFO(Game, "ECS initialized with 1 colonist");
		}

		/// Spawn a new colonist entity at the given position.
		ecs::EntityID spawnColonist(glm::vec2 newPosition, const std::string& newName) {
			auto entity = ecsWorld->createEntity();

			ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{newPosition});
			ecsWorld->addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
			ecsWorld->addComponent<ecs::Velocity>(entity, ecs::Velocity{{0.0F, 0.0F}});
			ecsWorld->addComponent<ecs::MovementTarget>(entity, ecs::MovementTarget{{0.0F, 0.0F}, 2.0F, false});
			ecsWorld->addComponent<ecs::FacingDirection>(entity, ecs::FacingDirection{}); // Default: Down
			ecsWorld->addComponent<ecs::Appearance>(entity, ecs::Appearance{"Colonist", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			ecsWorld->addComponent<ecs::Colonist>(entity, ecs::Colonist{newName});
			ecsWorld->addComponent<ecs::NeedsComponent>(entity, ecs::NeedsComponent::createDefault());
			ecsWorld->addComponent<ecs::Inventory>(entity, ecs::Inventory::createForColonist());
			ecsWorld->addComponent<ecs::Knowledge>(entity, ecs::Knowledge{});
			ecsWorld->addComponent<ecs::Memory>(entity, ecs::Memory{});
			ecsWorld->addComponent<ecs::Task>(entity, ecs::Task{});
			ecsWorld->addComponent<ecs::DecisionTrace>(entity, ecs::DecisionTrace{});
			ecsWorld->addComponent<ecs::Action>(entity, ecs::Action{});

			LOG_INFO(Game, "Spawned colonist '%s' at (%.1f, %.1f)", newName.c_str(), newPosition.x, newPosition.y);
			return entity;
		}

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

		/// Render selection indicator around selected colonist.
		/// Draws a circle outline in screen-space at the entity's position.
		/// @param viewportWidth Logical viewport width in pixels
		/// @param viewportHeight Logical viewport height in pixels
		void renderSelectionIndicator(int viewportWidth, int viewportHeight) {
			// Only render for colonist selections (world entities don't need in-world highlight)
			auto* colonistSel = std::get_if<world_sim::ColonistSelection>(&selection);
			if (colonistSel == nullptr) {
				return;
			}

			// Get entity position
			auto* pos = ecsWorld->getComponent<ecs::Position>(colonistSel->entityId);
			if (pos == nullptr) {
				return;
			}

			// Convert world position to screen position (viewport is already in logical coordinates)
			auto screenPos = m_camera->worldToScreen(pos->value.x, pos->value.y, viewportWidth, viewportHeight, kPixelsPerMeter);

			// Convert selection radius from world units to screen pixels
			constexpr float kSelectionRadiusWorld = 1.0F; // 1 meter radius
			float			screenRadius = m_camera->worldDistanceToScreen(kSelectionRadiusWorld, kPixelsPerMeter);

			// Draw selection circle with border-only style (transparent fill)
			Renderer::Primitives::drawCircle(
				Renderer::Primitives::CircleArgs{
					.center = Foundation::Vec2{screenPos.x, screenPos.y},
					.radius = screenRadius,
					.style =
						Foundation::CircleStyle{
							.fill = Foundation::Color(0.0F, 0.0F, 0.0F, 0.0F), // Transparent fill
							.border =
								Foundation::BorderStyle{
									.color = Foundation::Color(1.0F, 0.85F, 0.0F, 0.8F), // Gold color with slight transparency
									.width = 2.0F,
								},
						},
					.id = "selection-indicator",
					.zIndex = 100, // Above entities
				}
			);
		}

		/// Handle entity selection via mouse click.
		/// Selection priority: 1) ECS colonists, 2) World entities with capabilities
		/// @param screenPos Mouse position in screen coordinates (logical/window coordinates)
		void handleEntitySelection(glm::vec2 screenPos) {
			int logicalW = 0;
			int logicalH = 0;
			// Use logical viewport for consistent world-to-screen transforms
			// (mouse input is in logical/window coordinates)
			Renderer::Primitives::getLogicalViewport(logicalW, logicalH);

			// Convert screen position to world position
			auto worldPos = m_camera->screenToWorld(screenPos.x, screenPos.y, logicalW, logicalH, kPixelsPerMeter);

			LOG_DEBUG(Game, "Click at screen (%.1f, %.1f) -> world (%.2f, %.2f)", screenPos.x, screenPos.y, worldPos.x, worldPos.y);

			constexpr float kSelectionRadius = 2.0F; // meters

			// Priority 1: Check ECS colonists first (dynamic, moving entities)
			float		  closestColonistDist = kSelectionRadius;
			ecs::EntityID closestColonist = 0;

			for (auto [entity, pos, colonist] : ecsWorld->view<ecs::Position, ecs::Colonist>()) {
				float dx = pos.value.x - worldPos.x;
				float dy = pos.value.y - worldPos.y;
				float dist = std::sqrt(dx * dx + dy * dy);

				if (dist < closestColonistDist) {
					closestColonistDist = dist;
					closestColonist = entity;
				}
			}

			if (closestColonist != 0) {
				selection = world_sim::ColonistSelection{closestColonist};
				if (auto* colonist = ecsWorld->getComponent<ecs::Colonist>(closestColonist)) {
					LOG_INFO(Game, "Selected colonist: %s", colonist->name.c_str());
				}
				return;
			}

			// Priority 2: Check world entities (static placed assets)
			auto&						   assetRegistry = engine::assets::AssetRegistry::Get();
			engine::world::ChunkCoordinate chunkCoord = engine::world::worldToChunk(engine::world::WorldPosition{worldPos.x, worldPos.y});
			const auto*					   spatialIndex = m_placementExecutor->getChunkIndex(chunkCoord);
			if (spatialIndex == nullptr) {
				// Chunk not loaded, deselect
				selection = world_sim::NoSelection{};
				LOG_DEBUG(Game, "No selectable entity found (chunk not loaded), deselecting");
				return;
			}
			auto nearbyEntities = spatialIndex->queryRadius({worldPos.x, worldPos.y}, kSelectionRadius);

			float								closestEntityDist = kSelectionRadius;
			const engine::assets::PlacedEntity* closestWorldEntity = nullptr;

			for (const auto* placedEntity : nearbyEntities) {
				// Only select entities with capabilities (not grass/decorative)
				const auto* def = assetRegistry.getDefinition(placedEntity->defName);
				if (def == nullptr || !def->capabilities.hasAny()) {
					continue; // Skip entities without capabilities
				}

				float dx = placedEntity->position.x - worldPos.x;
				float dy = placedEntity->position.y - worldPos.y;
				float dist = std::sqrt(dx * dx + dy * dy);

				if (dist < closestEntityDist) {
					closestEntityDist = dist;
					closestWorldEntity = placedEntity;
				}
			}

			if (closestWorldEntity != nullptr) {
				selection = world_sim::WorldEntitySelection{closestWorldEntity->defName, closestWorldEntity->position};
				LOG_INFO(
					Game,
					"Selected world entity: %s at (%.1f, %.1f)",
					closestWorldEntity->defName.c_str(),
					closestWorldEntity->position.x,
					closestWorldEntity->position.y
				);
				return;
			}

			// Nothing found - deselect
			selection = world_sim::NoSelection{};
			LOG_DEBUG(Game, "No selectable entity found, deselecting");
		}

		/// Handle build button toggle / B key press.
		/// Opens build menu when in normal mode, cancels when in placement mode.
		void handleBuildToggle() {
			switch (placementMode.state()) {
				case world_sim::PlacementState::None: {
					// Open build menu
					placementMode.enterMenu();
					gameUI->setBuildModeActive(true);

					// Get innate recipes for the build menu
					auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
					auto innateRecipes = recipeRegistry.getInnateRecipes();

					std::vector<world_sim::BuildMenuItem> items;
					for (const auto* recipe : innateRecipes) {
						if (!recipe->outputs.empty()) {
							items.push_back({recipe->outputs[0].defName, recipe->label});
						}
					}

					gameUI->showBuildMenu(items);
					break;
				}

				case world_sim::PlacementState::MenuOpen:
				case world_sim::PlacementState::Placing:
					// Cancel placement
					placementMode.cancel();
					gameUI->setBuildModeActive(false);
					gameUI->hideBuildMenu();
					break;
			}
		}

		/// Handle item selection from build menu.
		/// Transitions to Placing state with selected item.
		void handleBuildItemSelected(const std::string& defName) {
			placementMode.selectItem(defName);
			gameUI->hideBuildMenu();
			LOG_INFO(Game, "Selected '%s' for placement", defName.c_str());
		}

		/// Spawn a placed entity in the world.
		/// Called when placement mode successfully places an item.
		void spawnPlacedEntity(const std::string& defName, Foundation::Vec2 worldPos) {
			// Create ECS entity with components needed for rendering:
			// - Position: world location
			// - Rotation: required by DynamicEntityRenderSystem
			// - Appearance: defName for asset lookup
			auto entity = ecsWorld->createEntity();

			ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{{worldPos.x, worldPos.y}});
			ecsWorld->addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
			ecsWorld->addComponent<ecs::Appearance>(entity, ecs::Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});

			LOG_INFO(Game, "Spawned '%s' at (%.1f, %.1f)", defName.c_str(), worldPos.x, worldPos.y);
		}

		std::unique_ptr<engine::world::ChunkManager>	   m_chunkManager;
		std::unique_ptr<engine::world::WorldCamera>		   m_camera;
		std::unique_ptr<engine::world::ChunkRenderer>	   m_renderer;
		std::unique_ptr<engine::world::EntityRenderer>	   m_entityRenderer;
		std::unique_ptr<engine::assets::PlacementExecutor> m_placementExecutor;
		std::unique_ptr<world_sim::GameUI>				   gameUI;

		// ECS World containing all dynamic entities
		std::unique_ptr<ecs::World> ecsWorld;

		// Async chunk processor (shared implementation with GameLoadingScene)
		std::unique_ptr<engine::assets::AsyncChunkProcessor> m_asyncProcessor;

		// Track processed chunk coordinates for cleanup detection
		std::unordered_set<engine::world::ChunkCoordinate> m_processedChunks;

		// Timing for metrics
		float m_lastUpdateMs = 0.0F;

		// Current selection for info panel (NoSelection = panel hidden)
		world_sim::Selection selection = world_sim::NoSelection{};

		// Placement mode state machine
		world_sim::PlacementMode placementMode;

		// Ghost renderer for placement preview
		world_sim::GhostRenderer ghostRenderer;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
