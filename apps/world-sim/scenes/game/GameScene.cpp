// Game Scene - Main gameplay with chunk-based world rendering

#include "GameWorldState.h"
#include "SceneTypes.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/selection/SelectionSystem.h"

#include <components/toast/Toast.h> // For ToastSeverity

#include <GL/glew.h>

#include <application/AppLauncher.h>
#include <chrono>
#include <cmath>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <input/InputManager.h>
#include <metrics/GPUTimer.h>
#include <metrics/MetricsCollector.h>
#include <metrics/PerformanceMetrics.h>
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
#include <ecs/components/Packaged.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <ecs/systems/AIDecisionSystem.h>
#include <ecs/systems/ActionSystem.h>
#include <ecs/systems/DynamicEntityRenderSystem.h>
#include <ecs/systems/MovementSystem.h>
#include <ecs/systems/NeedsDecaySystem.h>
#include <ecs/systems/PhysicsSystem.h>
#include <ecs/systems/TimeSystem.h>
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
				m_renderer->setTileResolution(1); // Render every tile

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

			// Create unified game UI (contains overlay and info panel)
			// Note: Callbacks for placement/selection are set up after systems are created below
			gameUI = std::make_unique<world_sim::GameUI>(world_sim::GameUI::Args{
				.onZoomIn = [this]() { m_camera->zoomIn(); },
				.onZoomOut = [this]() { m_camera->zoomOut(); },
				.onZoomReset = [this]() { m_camera->setZoomIndex(engine::world::kDefaultZoomIndex); },
				.onSelectionCleared = [this]() { m_selectionSystem->clearSelection(); },
				.onColonistSelected = [this](ecs::EntityID entityId) { m_selectionSystem->selectColonist(entityId); },
				.onColonistFollowed =
					[this](ecs::EntityID entityId) {
						// Center camera on colonist position
						if (auto* pos = ecsWorld->getComponent<ecs::Position>(entityId)) {
							m_camera->setPosition({pos->value.x, pos->value.y});
						}
					},
				.onBuildToggle = [this]() { m_placementSystem->toggleBuildMenu(); },
				.onBuildItemSelected = [this](const std::string& defName) { m_placementSystem->selectBuildItem(defName); },
				.onProductionSelected = [this](const std::string& defName) { m_placementSystem->selectBuildItem(defName); },
				.onQueueRecipe =
					[this](const std::string& recipeDefName, uint32_t quantity) { handleQueueRecipe(recipeDefName, quantity); },
				.onCancelJob = [this](const std::string& recipeDefName) { handleCancelJob(recipeDefName); },
				.onOpenCraftingDialog =
					[this](ecs::EntityID stationId, const std::string& defName) { gameUI->showCraftingDialog(stationId, defName); },
				.onOpenStorageConfig = [this](
										   ecs::EntityID containerId, const std::string& defName
									   ) { gameUI->showStorageConfigDialog(containerId, defName); },
				.onPlaceFurniture = [this]() { handlePlaceFurniture(); },
				.onPause =
					[this]() {
						auto& timeSystem = ecsWorld->getSystem<ecs::TimeSystem>();
						timeSystem.togglePause();
					},
				.onSpeedChange =
					[this](ecs::GameSpeed speed) {
						auto& timeSystem = ecsWorld->getSystem<ecs::TimeSystem>();
						timeSystem.setSpeed(speed);
					},
				.onMenuClick = [this]() { sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu)); },
				.queryResources = [this](const std::string& defName, Foundation::Vec2 position) -> std::optional<uint32_t> {
					auto coord = engine::world::worldToChunk({position.x, position.y});
					return m_placementExecutor->getResourceCount(coord, {position.x, position.y}, defName);
				}
			});

			// Populate Production dropdown with placeable stations (recipes where station="none")
			{
				auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
				auto  innateRecipes = recipeRegistry.getInnateRecipes();

				std::vector<std::pair<std::string, std::string>> productionItems;
				for (const auto* recipe : innateRecipes) {
					// Only include recipes that don't require a station (directly placeable)
					if (recipe->isStationless() && !recipe->outputs.empty()) {
						productionItems.emplace_back(recipe->outputs[0].defName, recipe->label);
					}
				}
				gameUI->setProductionItems(productionItems);
			}

			// Initial layout pass with consistent DPI scaling
			int viewportW = 0;
			int viewportH = 0;
			Renderer::Primitives::getLogicalViewport(viewportW, viewportH);
			gameUI->layout(Foundation::Rect{0, 0, static_cast<float>(viewportW), static_cast<float>(viewportH)});

			// Initialize ECS World
			initializeECS();

			// Initialize PlacementSystem (after ECS so we have the world)
			m_placementSystem = std::make_unique<world_sim::PlacementSystem>(world_sim::PlacementSystem::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.callbacks = {
					.onBuildMenuVisibility = [this](bool active) { gameUI->setBuildModeActive(active); },
					.onShowBuildMenu = [this](const std::vector<world_sim::BuildMenuItem>& items) { gameUI->showBuildMenu(items); },
					.onHideBuildMenu = [this]() { gameUI->hideBuildMenu(); },
					.onSelectionCleared = [this]() { m_selectionSystem->clearSelection(); }
				}
			});

			// Initialize SelectionSystem (after ECS and PlacementExecutor)
			m_selectionSystem = std::make_unique<world_sim::SelectionSystem>(world_sim::SelectionSystem::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.placementExecutor = m_placementExecutor.get(),
				.callbacks = {.onSelectionChanged = [](const world_sim::Selection&) {
					// Selection state is queried each frame - no action needed on change
				}}
			});

			// Enable GPU timing for performance monitoring
			m_gpuTimer.setEnabled(true);
		}

		/// Handle UI input events dispatched from Application.
		/// Forwards to gameUI and handles game-specific interactions (placement, selection).
		bool handleInput(UI::InputEvent& event) override {
			// Forward event to UI first
			bool consumed = gameUI->dispatchEvent(event);

			// Get viewport dimensions for coordinate transforms
			int logicalW = 0;
			int logicalH = 0;
			Renderer::Primitives::getLogicalViewport(logicalW, logicalH);

			// Handle placement mode interaction
			if (m_placementSystem->isActive()) {
				if (event.type == UI::InputEvent::Type::MouseMove) {
					m_placementSystem->handleMouseMove(event.position.x, event.position.y, logicalW, logicalH);
				} else if (!consumed && event.type == UI::InputEvent::Type::MouseUp) {
					if (m_placementSystem->handleClick()) {
						return true; // Consume click after successful placement
					}
				}
				return consumed;
			}

			// Handle entity selection on left click release (only if UI didn't consume it)
			if (!consumed && event.type == UI::InputEvent::Type::MouseUp) {
				m_selectionSystem->handleClick(event.position.x, event.position.y, logicalW, logicalH);
			}

			return consumed;
		}

		void update(float dt) override {
			auto& input = engine::InputManager::Get();

			// Handle Escape - cancel placement mode first, then exit to menu
			if (input.isKeyPressed(engine::Key::Escape)) {
				if (m_placementSystem->isActive()) {
					m_placementSystem->cancel();
				} else {
					sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
					return; // Don't process rest of update when switching scenes
				}
			}

			// Handle B key - toggle build mode
			if (input.isKeyPressed(engine::Key::B)) {
				m_placementSystem->toggleBuildMenu();
			}

			// Handle time controls
			auto& timeSystem = ecsWorld->getSystem<ecs::TimeSystem>();
			if (input.isKeyPressed(engine::Key::Space)) {
				timeSystem.togglePause();
			}
			if (input.isKeyPressed(engine::Key::Num1)) {
				timeSystem.setSpeed(ecs::GameSpeed::Normal);
			}
			if (input.isKeyPressed(engine::Key::Num2)) {
				timeSystem.setSpeed(ecs::GameSpeed::Fast);
			}
			if (input.isKeyPressed(engine::Key::Num3)) {
				timeSystem.setSpeed(ecs::GameSpeed::VeryFast);
			}

			// Zoom reset
			if (input.isKeyPressed(engine::Key::Home)) {
				m_camera->setZoomIndex(engine::world::kDefaultZoomIndex);
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
			// Skip scroll handling when a modal dialog is open (dialog scrolls instead)
			if (!gameUI->isCraftingDialogVisible() && !gameUI->isColonistDetailsVisible()) {
				// Accumulate scroll deltas to handle high-precision input devices (Magic Mouse, trackpad)
				// These devices generate many small fractional scroll events per gesture
				constexpr float kScrollThreshold = 1.0F; // Trigger zoom after ~1 "notch" of scrolling
				float			scrollDelta = input.consumeScrollDelta();
				m_scrollAccumulator += scrollDelta;

				// Trigger zoom steps when accumulated scroll crosses threshold
				while (m_scrollAccumulator >= kScrollThreshold) {
					m_camera->zoomIn();
					m_scrollAccumulator -= kScrollThreshold;
				}
				while (m_scrollAccumulator <= -kScrollThreshold) {
					m_camera->zoomOut();
					m_scrollAccumulator += kScrollThreshold;
				}
			}

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
			auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
			gameUI->update(dt, *m_camera, *m_chunkManager, *ecsWorld, assetRegistry, recipeRegistry, m_selectionSystem->current());

			m_lastUpdateMs = elapsedMs(updateStart, Clock::now());
		}

		void render() override {
			glClearColor(0.05F, 0.08F, 0.12F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Begin GPU timing (measures from here to end(), result from previous frame)
			m_gpuTimer.begin();

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
			m_selectionSystem->renderIndicator(w, h);

			// Render placement ghost preview (if in placing mode)
			m_placementSystem->render(w, h);

			// Render unified game UI (overlay + info panel)
			gameUI->render();

			// End GPU timing (query result will be available next frame)
			m_gpuTimer.end();

			// Report timing breakdown to metrics system
			auto* metrics = engine::AppLauncher::getMetrics();
			if (metrics != nullptr) {
				metrics->setTimingBreakdown(
					tileMs,
					entityMs,
					m_lastUpdateMs,
					m_renderer->lastTileCount(),
					m_entityRenderer->lastEntityCount(),
					m_renderer->lastChunkCount()
				);

				// Convert ECS system timings to Foundation format (reuse cache to avoid allocation)
				const auto& ecsTimings = ecsWorld->getSystemTimings();
				m_ecsTimingsCache.clear(); // Clear but keep capacity
				for (const auto& timing : ecsTimings) {
					m_ecsTimingsCache.push_back({timing.name, timing.durationMs});
				}
				metrics->setEcsSystemTimings(m_ecsTimingsCache);

				// GPU timing (from previous frame due to async query)
				metrics->setGpuRenderTime(m_gpuTimer.getTimeMs());
			}
		}

		void onExit() override {
			LOG_INFO(Game, "GameScene - Exiting");

			// Wait for all pending async tasks to complete before destroying executor
			if (m_asyncProcessor) {
				m_asyncProcessor->clear();
			}

			// Clean up subsystems (order matters - systems may reference ECS/Camera)
			m_placementSystem.reset();
			m_selectionSystem.reset();

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
			auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
			ecsWorld->registerSystem<ecs::TimeSystem>();									// Priority 10 - runs first
			ecsWorld->registerSystem<ecs::VisionSystem>();									// Priority 45
			ecsWorld->registerSystem<ecs::NeedsDecaySystem>();								// Priority 50
			ecsWorld->registerSystem<ecs::AIDecisionSystem>(assetRegistry, recipeRegistry); // Priority 60
			ecsWorld->registerSystem<ecs::MovementSystem>();								// Priority 100
			ecsWorld->registerSystem<ecs::PhysicsSystem>();									// Priority 200
			ecsWorld->registerSystem<ecs::ActionSystem>();									// Priority 350
			ecsWorld->registerSystem<ecs::DynamicEntityRenderSystem>();						// Priority 900

			// Wire up VisionSystem with placement data for entity queries
			auto& visionSystem = ecsWorld->getSystem<ecs::VisionSystem>();
			visionSystem.setPlacementData(m_placementExecutor.get(), &m_processedChunks);
			visionSystem.setChunkManager(m_chunkManager.get());

			// Wire up "Aha!" notification callback for recipe discoveries
			visionSystem.setRecipeDiscoveryCallback([this](const std::string& recipeLabel) {
				gameUI->pushNotification("Aha!", "Discovered: " + recipeLabel, UI::ToastSeverity::Info);
				LOG_INFO(Game, "Recipe discovered: %s", recipeLabel.c_str());
			});

			// Wire up AIDecisionSystem with chunk manager for toilet location queries
			auto& aiDecisionSystem = ecsWorld->getSystem<ecs::AIDecisionSystem>();
			aiDecisionSystem.setChunkManager(m_chunkManager.get());

			// Wire up ActionSystem for "item crafted" notifications
			auto& actionSystem = ecsWorld->getSystem<ecs::ActionSystem>();
			actionSystem.setItemCraftedCallback([this](const std::string& itemLabel) {
				gameUI->pushNotification("Crafted", itemLabel, UI::ToastSeverity::Info);
				LOG_INFO(Game, "Item crafted notification: %s", itemLabel.c_str());
			});

			// Wire up ActionSystem to drop non-backpackable items on the ground as packaged
			// Note: m_placementSystem is created after initializeECS, but callback is invoked at runtime
			// Offset from crafting station so items don't stack on top (2x typical station size)
			constexpr float kDropOffset = 2.0F;
			actionSystem.setDropItemCallback([this, kDropOffset](const std::string& defName, float x, float y) {
				auto entity = m_placementSystem->spawnEntity(defName, {x + kDropOffset, y});
				// Mark as packaged - player needs to place it via ghost preview
				ecsWorld->addComponent<ecs::Packaged>(entity, ecs::Packaged{});
				LOG_INFO(Game, "Spawned packaged '%s' - awaiting placement", defName.c_str());
			});

			// Wire up ActionSystem to remove harvested entities (destructive harvest)
			actionSystem.setRemoveEntityCallback([this](const std::string& defName, float x, float y) {
				auto coord = engine::world::worldToChunk({x, y});
				bool removed = m_placementExecutor->removeEntity(coord, {x, y}, defName);
				if (!removed) {
					LOG_WARNING(Game, "Failed to remove harvested entity %s at (%.1f, %.1f)", defName.c_str(), x, y);
				}
			});

			// Wire up ActionSystem to set cooldown on harvested entities (regrowth)
			actionSystem.setEntityCooldownCallback([this](const std::string& defName, float x, float y, float cooldownSeconds) {
				auto coord = engine::world::worldToChunk({x, y});
				m_placementExecutor->setEntityCooldown(coord, {x, y}, defName, cooldownSeconds);
			});

			// Wire up ActionSystem to decrement resource count for harvestable entities with resource pools
			actionSystem.setDecrementResourceCallback([this](const std::string& defName, float x, float y) -> bool {
				auto coord = engine::world::worldToChunk({x, y});
				return m_placementExecutor->decrementResourceCount(coord, {x, y}, defName);
			});

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
			ecsWorld->addComponent<ecs::Memory>(entity, ecs::Memory{.owner = entity});
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

		/// Handle furniture placement request from info panel.
		/// Enters placement mode to relocate the selected packaged furniture.
		void handlePlaceFurniture() {
			// Get currently selected furniture from selection system
			const auto& sel = m_selectionSystem->current();
			auto*		furnitureSel = std::get_if<world_sim::FurnitureSelection>(&sel);
			if (furnitureSel == nullptr || !furnitureSel->isPackaged) {
				LOG_WARNING(Game, "Cannot place furniture: no packaged furniture selected");
				return;
			}

			// Begin relocation via PlacementSystem
			m_placementSystem->beginRelocation(furnitureSel->entityId, furnitureSel->defName);
		}

		/// Handle recipe queue request from crafting station UI.
		/// Adds a crafting job to the selected station's WorkQueue.
		void handleQueueRecipe(const std::string& recipeDefName, uint32_t quantity) {
			// Get currently selected station from selection system
			const auto& sel = m_selectionSystem->current();
			auto*		stationSel = std::get_if<world_sim::CraftingStationSelection>(&sel);
			if (stationSel == nullptr) {
				LOG_WARNING(Game, "Cannot queue recipe: no station selected");
				return;
			}

			// Get the station's WorkQueue
			auto* workQueue = ecsWorld->getComponent<ecs::WorkQueue>(stationSel->entityId);
			if (workQueue == nullptr) {
				LOG_WARNING(Game, "Cannot queue recipe: station has no WorkQueue");
				return;
			}

			// Add the job with specified quantity
			workQueue->addJob(recipeDefName, quantity);
			LOG_INFO(Game, "Queued recipe '%s' x%u at station '%s'", recipeDefName.c_str(), quantity, stationSel->defName.c_str());
		}

		/// Handle cancel job request from crafting dialog.
		/// Removes a job from the crafting dialog's station WorkQueue.
		void handleCancelJob(const std::string& recipeDefName) {
			// The crafting dialog tracks which station it's open for
			if (!gameUI->isCraftingDialogVisible()) {
				LOG_WARNING(Game, "Cannot cancel job: crafting dialog not open");
				return;
			}

			// Get currently selected station from selection system (should match dialog)
			const auto& sel = m_selectionSystem->current();
			auto*		stationSel = std::get_if<world_sim::CraftingStationSelection>(&sel);
			if (stationSel == nullptr) {
				LOG_WARNING(Game, "Cannot cancel job: no station selected");
				return;
			}

			// Get the station's WorkQueue
			auto* workQueue = ecsWorld->getComponent<ecs::WorkQueue>(stationSel->entityId);
			if (workQueue == nullptr) {
				LOG_WARNING(Game, "Cannot cancel job: station has no WorkQueue");
				return;
			}

			// Remove the job
			workQueue->removeJob(recipeDefName);
			LOG_INFO(Game, "Canceled job '%s' at station '%s'", recipeDefName.c_str(), stationSel->defName.c_str());
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

		// Timing for metrics (persistent vectors to avoid per-frame heap allocation)
		float									 m_lastUpdateMs = 0.0F;
		Renderer::GPUTimer						 m_gpuTimer;		// GPU timing via OpenGL queries
		std::vector<Foundation::EcsSystemTiming> m_ecsTimingsCache; // Reused each frame

		// Scroll accumulator for smooth zoom on high-precision input devices (Magic Mouse, trackpad)
		// Accumulates fractional scroll deltas and triggers zoom only when threshold is crossed
		float m_scrollAccumulator = 0.0F;

		// World interaction subsystems (extracted from GameScene)
		std::unique_ptr<world_sim::PlacementSystem> m_placementSystem;
		std::unique_ptr<world_sim::SelectionSystem> m_selectionSystem;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
