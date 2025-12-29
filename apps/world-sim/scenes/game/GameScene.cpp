// Game Scene - Main gameplay with chunk-based world rendering

#include "GameWorldState.h"
#include "SceneTypes.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/ui/components/Selection.h"
#include "scenes/game/world/GhostRenderer.h"
#include "scenes/game/world/PlacementMode.h"

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

			// Initialize placement mode with callback to spawn/relocate entities
			placementMode = world_sim::PlacementMode{
				world_sim::PlacementMode::Args{.onPlace = [this](const std::string& defName, Foundation::Vec2 worldPos) {
					if (m_relocatingFurnitureId != ecs::EntityID{0}) {
						// Relocating existing furniture - move it and remove Packaged component
						if (auto* pos = ecsWorld->getComponent<ecs::Position>(m_relocatingFurnitureId)) {
							pos->value = {worldPos.x, worldPos.y};
							ecsWorld->removeComponent<ecs::Packaged>(m_relocatingFurnitureId);
							LOG_INFO(Game, "Placed furniture at (%.1f, %.1f)", worldPos.x, worldPos.y);
						}
						// Clear selection after placing
						selection = world_sim::NoSelection{};
						m_relocatingFurnitureId = ecs::EntityID{0};
					} else {
						// Spawning new entity
						spawnPlacedEntity(defName, worldPos);
					}
				}}
			};

			// Create unified game UI (contains overlay and info panel)
			gameUI = std::make_unique<world_sim::GameUI>(world_sim::GameUI::Args{
				.onZoomIn = [this]() { m_camera->zoomIn(); },
				.onZoomOut = [this]() { m_camera->zoomOut(); },
				.onZoomReset = [this]() { m_camera->setZoomIndex(engine::world::kDefaultZoomIndex); },
				.onSelectionCleared = [this]() { selection = world_sim::NoSelection{}; },
				.onColonistSelected = [this](ecs::EntityID entityId) { selection = world_sim::ColonistSelection{entityId}; },
				.onColonistFollowed =
					[this](ecs::EntityID entityId) {
						// Center camera on colonist position
						if (auto* pos = ecsWorld->getComponent<ecs::Position>(entityId)) {
							m_camera->setPosition({pos->value.x, pos->value.y});
						}
					},
				.onBuildToggle = [this]() { handleBuildToggle(); },
				.onBuildItemSelected = [this](const std::string& defName) { handleBuildItemSelected(defName); },
				.onProductionSelected = [this](const std::string& defName) { handleBuildItemSelected(defName); },
				.onQueueRecipe = [this](const std::string& recipeDefName) { handleQueueRecipe(recipeDefName); },
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
				.onMenuClick = [this]() { sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu)); }
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

			// Enable GPU timing for performance monitoring
			m_gpuTimer.setEnabled(true);
		}

		/// Handle UI input events dispatched from Application.
		/// Forwards to gameUI and handles game-specific interactions (placement, selection).
		bool handleInput(UI::InputEvent& event) override {
			// Forward event to UI first
			bool consumed = gameUI->dispatchEvent(event);

			// Handle placement mode interaction
			if (placementMode.state() == world_sim::PlacementState::Placing) {
				if (event.type == UI::InputEvent::Type::MouseMove) {
					// Update ghost position from mouse
					int logicalW = 0;
					int logicalH = 0;
					Renderer::Primitives::getLogicalViewport(logicalW, logicalH);
					auto worldPos = m_camera->screenToWorld(event.position.x, event.position.y, logicalW, logicalH, kPixelsPerMeter);
					placementMode.updateGhostPosition({worldPos.x, worldPos.y});
				} else if (!consumed && event.type == UI::InputEvent::Type::MouseUp) {
					// Try to place on click (if not over UI)
					if (placementMode.tryPlace()) {
						// Successfully placed - update UI state
						gameUI->setBuildModeActive(false);
						gameUI->hideBuildMenu();
					}
					return true; // Consume click in placement mode
				}
				return consumed;
			}

			// Handle entity selection on left click release (only if UI didn't consume it)
			if (!consumed && event.type == UI::InputEvent::Type::MouseUp) {
				handleEntitySelection({event.position.x, event.position.y});
			}

			return consumed;
		}

		void update(float dt) override {
			auto& input = engine::InputManager::Get();

			// Handle Escape - cancel placement mode first, then exit to menu
			if (input.isKeyPressed(engine::Key::Escape)) {
				if (placementMode.isActive()) {
					placementMode.cancel();
					gameUI->setBuildModeActive(false);
					gameUI->hideBuildMenu();
				} else {
					sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
					return; // Don't process rest of update when switching scenes
				}
			}

			// Handle B key - toggle build mode
			if (input.isKeyPressed(engine::Key::B)) {
				handleBuildToggle();
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
			float scrollDelta = input.consumeScrollDelta();
			if (scrollDelta > 0.0F) {
				m_camera->zoomIn();
			} else if (scrollDelta < 0.0F) {
				m_camera->zoomOut();
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
			gameUI->update(dt, *m_camera, *m_chunkManager, *ecsWorld, assetRegistry, recipeRegistry, selection);

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
			renderSelectionIndicator(w, h);

			// Render placement ghost preview (if in placing mode)
			if (placementMode.state() == world_sim::PlacementState::Placing) {
				ghostRenderer.render(
					placementMode.selectedDefName(), placementMode.ghostPosition(), *m_camera, w, h, placementMode.isValidPlacement()
				);
			}

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
			// Offset from crafting station so items don't stack on top (2x typical station size)
			constexpr float kDropOffset = 2.0F;
			actionSystem.setDropItemCallback([this](const std::string& defName, float x, float y) {
				auto entity = spawnPlacedEntity(defName, {x + kDropOffset, y});
				// Mark as packaged - player needs to place it via ghost preview
				ecsWorld->addComponent<ecs::Packaged>(entity, ecs::Packaged{});
				LOG_INFO(Game, "Spawned packaged '%s' - awaiting placement", defName.c_str());
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
			// Get world position from selection (colonists, stations, and furniture have ECS positions)
			glm::vec2 worldPos{0.0F, 0.0F};
			bool	  hasPosition = false;

			if (auto* colonistSel = std::get_if<world_sim::ColonistSelection>(&selection)) {
				if (auto* pos = ecsWorld->getComponent<ecs::Position>(colonistSel->entityId)) {
					worldPos = pos->value;
					hasPosition = true;
				}
			} else if (auto* stationSel = std::get_if<world_sim::CraftingStationSelection>(&selection)) {
				if (auto* pos = ecsWorld->getComponent<ecs::Position>(stationSel->entityId)) {
					worldPos = pos->value;
					hasPosition = true;
				}
			} else if (auto* furnitureSel = std::get_if<world_sim::FurnitureSelection>(&selection)) {
				if (auto* pos = ecsWorld->getComponent<ecs::Position>(furnitureSel->entityId)) {
					worldPos = pos->value;
					hasPosition = true;
				}
			}

			if (!hasPosition) {
				return;
			}

			// Convert world position to screen position (viewport is already in logical coordinates)
			auto screenPos = m_camera->worldToScreen(worldPos.x, worldPos.y, viewportWidth, viewportHeight, kPixelsPerMeter);

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
		/// Selection priority: 1) ECS colonists, 2) ECS stations, 3) World entities with capabilities
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

			// Priority 1.5: Check ECS stations (entities with WorkQueue - player-placed crafting stations)
			float		  closestStationDist = kSelectionRadius;
			ecs::EntityID closestStation = 0;

			for (auto [entity, pos, appearance, workQueue] : ecsWorld->view<ecs::Position, ecs::Appearance, ecs::WorkQueue>()) {
				float dx = pos.value.x - worldPos.x;
				float dy = pos.value.y - worldPos.y;
				float dist = std::sqrt(dx * dx + dy * dy);

				if (dist < closestStationDist) {
					closestStationDist = dist;
					closestStation = entity;
				}
			}

			if (closestStation != 0) {
				auto* pos = ecsWorld->getComponent<ecs::Position>(closestStation);
				auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestStation);
				if (pos != nullptr && appearance != nullptr) {
					selection = world_sim::CraftingStationSelection{
						closestStation, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}
					};
					LOG_INFO(Game, "Selected station: %s at (%.1f, %.1f)", appearance->defName.c_str(), pos->value.x, pos->value.y);
				}
				return;
			}

			// Priority 1.6: Check ECS storage containers (entities with Inventory but no WorkQueue)
			float		  closestStorageDist = kSelectionRadius;
			ecs::EntityID closestStorage = 0;

			for (auto [entity, pos, appearance, inventory] : ecsWorld->view<ecs::Position, ecs::Appearance, ecs::Inventory>()) {
				// Skip entities that also have WorkQueue (those are crafting stations)
				if (ecsWorld->getComponent<ecs::WorkQueue>(entity) != nullptr) {
					continue;
				}
				// Skip colonists (they have Inventory for carrying items)
				if (ecsWorld->getComponent<ecs::Colonist>(entity) != nullptr) {
					continue;
				}

				float dx = pos.value.x - worldPos.x;
				float dy = pos.value.y - worldPos.y;
				float dist = std::sqrt(dx * dx + dy * dy);

				if (dist < closestStorageDist) {
					closestStorageDist = dist;
					closestStorage = entity;
				}
			}

			if (closestStorage != 0) {
				auto* pos = ecsWorld->getComponent<ecs::Position>(closestStorage);
				auto* appearance = ecsWorld->getComponent<ecs::Appearance>(closestStorage);
				if (pos != nullptr && appearance != nullptr) {
					bool isPackaged = ecsWorld->getComponent<ecs::Packaged>(closestStorage) != nullptr;
					selection = world_sim::FurnitureSelection{
						closestStorage, appearance->defName, Foundation::Vec2{pos->value.x, pos->value.y}, isPackaged
					};
					LOG_INFO(
						Game,
						"Selected storage: %s at (%.1f, %.1f)%s",
						appearance->defName.c_str(),
						pos->value.x,
						pos->value.y,
						isPackaged ? " (packaged)" : ""
					);
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
					auto  innateRecipes = recipeRegistry.getInnateRecipes();

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

		/// Handle furniture placement request from info panel.
		/// Enters placement mode to relocate the selected packaged furniture.
		void handlePlaceFurniture() {
			// Get currently selected furniture
			auto* furnitureSel = std::get_if<world_sim::FurnitureSelection>(&selection);
			if (furnitureSel == nullptr || !furnitureSel->isPackaged) {
				LOG_WARNING(Game, "Cannot place furniture: no packaged furniture selected");
				return;
			}

			// Store the entity ID we're relocating
			m_relocatingFurnitureId = furnitureSel->entityId;

			// Enter placement mode with the furniture's def name
			placementMode.selectItem(furnitureSel->defName);
			LOG_INFO(
				Game, "Placing furniture '%s' (entity %u)", furnitureSel->defName.c_str(), static_cast<uint32_t>(furnitureSel->entityId)
			);
		}

		/// Handle recipe queue request from crafting station UI.
		/// Adds a crafting job to the selected station's WorkQueue.
		void handleQueueRecipe(const std::string& recipeDefName) {
			// Get currently selected station
			auto* stationSel = std::get_if<world_sim::CraftingStationSelection>(&selection);
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

			// Add the job
			workQueue->addJob(recipeDefName, 1);
			LOG_INFO(Game, "Queued recipe '%s' at station '%s'", recipeDefName.c_str(), stationSel->defName.c_str());
		}

		/// Spawn a placed entity in the world.
		/// Called when placement mode successfully places an item.
		/// Returns the entity ID so callers can add additional components.
		ecs::EntityID spawnPlacedEntity(const std::string& defName, Foundation::Vec2 worldPos) {
			// Create ECS entity with components needed for rendering:
			// - Position: world location
			// - Rotation: required by DynamicEntityRenderSystem
			// - Appearance: defName for asset lookup
			auto entity = ecsWorld->createEntity();

			ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{{worldPos.x, worldPos.y}});
			ecsWorld->addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
			ecsWorld->addComponent<ecs::Appearance>(entity, ecs::Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});

			// Check if this is a crafting station (has craftable capability)
			// If so, add WorkQueue component for job management
			auto&		assetRegistry = engine::assets::AssetRegistry::Get();
			const auto* assetDef = assetRegistry.getDefinition(defName);
			if (assetDef != nullptr && assetDef->capabilities.craftable.has_value()) {
				ecsWorld->addComponent<ecs::WorkQueue>(entity, ecs::WorkQueue{});
				LOG_INFO(Game, "Spawned station '%s' at (%.1f, %.1f) with WorkQueue", defName.c_str(), worldPos.x, worldPos.y);
			} else if (assetDef != nullptr && assetDef->capabilities.storage.has_value()) {
				// Storage container - add Inventory configured from StorageCapability
				const auto&	   storageCap = assetDef->capabilities.storage.value();
				ecs::Inventory inventory{};
				inventory.maxCapacity = storageCap.maxCapacity;
				inventory.maxStackSize = storageCap.maxStackSize;
				ecsWorld->addComponent<ecs::Inventory>(entity, inventory);
				LOG_INFO(
					Game,
					"Spawned storage '%s' at (%.1f, %.1f) with Inventory (capacity=%u)",
					defName.c_str(),
					worldPos.x,
					worldPos.y,
					storageCap.maxCapacity
				);
			} else {
				LOG_INFO(Game, "Spawned '%s' at (%.1f, %.1f)", defName.c_str(), worldPos.x, worldPos.y);
			}

			return entity;
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

		// Current selection for info panel (NoSelection = panel hidden)
		world_sim::Selection selection = world_sim::NoSelection{};

		// Furniture entity being relocated (0 = spawning new entity, non-zero = relocating existing)
		ecs::EntityID m_relocatingFurnitureId{0};

		// Placement mode state machine
		world_sim::PlacementMode placementMode;

		// Ghost renderer for placement preview
		world_sim::GhostRenderer ghostRenderer;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
