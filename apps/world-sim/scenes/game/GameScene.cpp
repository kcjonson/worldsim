// Game Scene - Main gameplay with chunk-based world rendering

#include "GameWorldState.h"
#include "SceneTypes.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/selection/SelectionSystem.h"

#include <assets/ConstructionRegistry.h>

#include <components/toast/Toast.h> // For ToastSeverity

#include <GL/glew.h>

#include <application/AppLauncher.h>
#include <debug/DebugServer.h>
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
#include <ecs/components/Room.h>
#include <ecs/components/Skills.h>
#include <ecs/components/Structure.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/StructureHealth.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <ecs/systems/AIDecisionSystem.h>
#include <ecs/systems/ActionSystem.h>
#include <ecs/systems/BuildGoalSystem.h>
#include <ecs/systems/ConstructionSystem.h>
#include <ecs/systems/CraftingGoalSystem.h>
#include <ecs/systems/DynamicEntityRenderSystem.h>
#include <ecs/systems/MovementSystem.h>
#include <ecs/systems/NeedsDecaySystem.h>
#include <ecs/systems/PhysicsSystem.h>
#include <ecs/systems/RoomDetectionSystem.h>
#include <ecs/systems/StorageGoalSystem.h>
#include <ecs/systems/TimeSystem.h>
#include <ecs/systems/VisionSystem.h>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
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
				.onBuildToggle =
					[this]() {
						// One tool owns world input: drop the foundation tool first.
						if (m_drawingSystem) {
							m_drawingSystem->deactivate();
						}
						m_placementSystem->toggleBuildMenu();
					},
				.onBuildItemSelected =
					[this](const std::string& defName) {
						if (m_drawingSystem) {
							m_drawingSystem->deactivate();
						}
						m_placementSystem->selectBuildItem(defName);
					},
				.onProductionSelected =
					[this](const std::string& defName) {
						if (m_drawingSystem) {
							m_drawingSystem->deactivate();
						}
						m_placementSystem->selectBuildItem(defName);
					},
				.onQueueRecipe =
					[this](const std::string& recipeDefName, uint32_t quantity) { handleQueueRecipe(recipeDefName, quantity); },
				.onCancelJob = [this](const std::string& recipeDefName) { handleCancelJob(recipeDefName); },
				.onOpenCraftingDialog =
					[this](ecs::EntityID stationId, const std::string& defName) { gameUI->showCraftingDialog(stationId, defName); },
				.onOpenStorageConfig = [this](
										   ecs::EntityID containerId, const std::string& defName
									   ) { gameUI->showStorageConfigDialog(containerId, defName); },
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
				.onPlaceFurniture = [this]() { handlePlaceFurniture(); },
				.onDemolishFoundation = [this]() { handleDemolishFoundation(); },
				.onDemolishWallSegment = [this]() { handleDemolishWallSegment(); },
				.queryResources = [this](const std::string& defName, Foundation::Vec2 position) -> std::optional<uint32_t> {
					auto coord = engine::world::worldToChunk({position.x, position.y});
					return m_placementExecutor->getResourceCount(coord, {position.x, position.y}, defName);
				},
				.onStructureSelected = [this](const std::string& structure) {
					if (!m_drawingSystem) {
						return;
					}
					// One tool owns world input: drop any active placement first.
					if (m_placementSystem) {
						m_placementSystem->cancel();
					}
					if (structure == "foundation") {
						m_drawingSystem->activateFoundationTool();
					} else if (structure == "wall") {
						m_drawingSystem->activateWallTool();
					} else if (structure == "door") {
						m_drawingSystem->activateOpeningTool("Door");
					} else if (structure == "window") {
						m_drawingSystem->activateOpeningTool("Window");
					}
				},
				.onConstructionMaterialSelected = [this](const std::string& material) {
					if (m_drawingSystem) {
						m_drawingSystem->setActiveMaterial(material);
						refreshThicknessPresets(material);
					}
				},
				.onConstructionThicknessSelected = [this](const std::string& preset) {
					if (m_drawingSystem) {
						m_drawingSystem->setActiveThicknessPreset(preset);
					}
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

			// Initialize DrawingSystem (foundation tool). Sibling of PlacementSystem;
			// owns the app's ConstructionWorld topology store. Created before the
			// SelectionSystem so the latter can hold a pointer to that store for
			// foundation hit-testing.
			m_drawingSystem = std::make_unique<world_sim::DrawingSystem>(world_sim::DrawingSystem::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.callbacks = {
					.onToolActive = [this](bool /*active*/) {
						// Visibility is driven by the per-frame status push in update().
					},
					.onToast = [this](const std::string& title, const std::string& message) {
						gameUI->pushNotification(title, message, UI::ToastSeverity::Warning);
					}
				}
			});

			// Now that DrawingSystem owns the ConstructionWorld, give ConstructionSystem a handle
			// to it and wire the structure-lifecycle callbacks. ConstructionSystem lives in
			// libs/engine and doesn't know ConstructionWorld topology state; these callbacks are
			// the cross-layer signal (same pattern as ActionSystem's other callbacks).
			{
				auto& constructionSystem = ecsWorld->getSystem<ecs::ConstructionSystem>();
				constructionSystem.setConstructionWorld(&m_drawingSystem->world());

				auto& actionSys = ecsWorld->getSystem<ecs::ActionSystem>();

				// Build complete: flip the structure to Built (bumps ConstructionWorld version,
				// so the render picks up the new style next frame) and toast the player. Walls
				// and foundations share this callback; the Structure kind selects which topology
				// mutator to call. The SAME callback is given to both ActionSystem (normal builds)
				// and ConstructionSystem (DEV free-build), so a free-built structure flips state
				// and renders identically to a normally-built one.
				auto structureCompleted = [this](ecs::EntityID blueprintEntity) {
					const auto* structure = ecsWorld->getComponent<ecs::Structure>(blueprintEntity);
					if (structure == nullptr || structure->graphId == 0) {
						LOG_WARNING(Game, "Structure-completed: entity %u has no topology link", static_cast<uint32_t>(blueprintEntity));
						return;
					}
					if (structure->kind == ecs::StructureKind::Wall) {
						m_drawingSystem->world().setSegmentState(structure->graphId, engine::construction::FoundationState::Built);
						gameUI->pushNotification("Construction complete", "Wall built", UI::ToastSeverity::Info);
						LOG_INFO(Game, "Wall segment #%llu built (entity %u)", static_cast<unsigned long long>(structure->graphId), static_cast<uint32_t>(blueprintEntity));
						return;
					}
					if (structure->kind == ecs::StructureKind::Opening) {
						m_drawingSystem->world().setOpeningState(structure->graphId, engine::construction::FoundationState::Built);
						gameUI->pushNotification("Construction complete", "Opening built", UI::ToastSeverity::Info);
						LOG_INFO(
							Game,
							"Opening #%llu built (entity %u)",
							static_cast<unsigned long long>(structure->graphId),
							static_cast<uint32_t>(blueprintEntity)
						);
						return;
					}
					m_drawingSystem->world().setState(structure->graphId, engine::construction::FoundationState::Built);
					gameUI->pushNotification("Construction complete", "Foundation built", UI::ToastSeverity::Info);
					LOG_INFO(Game, "Foundation #%llu built (entity %u)", static_cast<unsigned long long>(structure->graphId), static_cast<uint32_t>(blueprintEntity));
				};
				actionSys.setStructureCompletedCallback(structureCompleted);
				constructionSystem.setStructureCompletedCallback(structureCompleted);

				// Deconstruct complete: remove the foundation topology now, but DEFER the
				// entity destruction. This callback fires from inside ActionSystem::update's
				// live view iteration; destroying here swap-and-pops the shared component
				// pools and corrupts that iteration. The queue is drained after
				// ecsWorld->update() returns (see update()). Refund is a later slice.
				actionSys.setStructureDeconstructedCallback([this](ecs::EntityID blueprintEntity) {
					const auto* structure = ecsWorld->getComponent<ecs::Structure>(blueprintEntity);
					if (structure != nullptr && structure->graphId != 0) {
						// The Structure kind selects which topology record to drop (an
						// opening is attached to a segment, not a foundation, so it has its
						// own removal). Walls deconstruct through handleDemolishWallSegment,
						// not this callback.
						if (structure->kind == ecs::StructureKind::Opening) {
							m_drawingSystem->world().removeOpening(structure->graphId);
							LOG_INFO(
								Game,
								"Opening #%llu deconstructed (entity %u)",
								static_cast<unsigned long long>(structure->graphId),
								static_cast<uint32_t>(blueprintEntity)
							);
						} else {
							m_drawingSystem->world().removeFoundation(structure->graphId);
							LOG_INFO(
								Game,
								"Foundation #%llu deconstructed (entity %u)",
								static_cast<unsigned long long>(structure->graphId),
								static_cast<uint32_t>(blueprintEntity)
							);
						}
					}
					m_pendingEntityRemoval.push_back(blueprintEntity);
				});

				// Room detection watches the same ConstructionWorld: when a closed loop of
				// built walls forms, it spawns a room entity and toasts the player. Identity
				// (id/name) persists across edits inside the system; here we only feed it the
				// topology handle and the room-formed seam (engine lib can't touch UI).
				auto& roomSystem = ecsWorld->getSystem<ecs::RoomDetectionSystem>();
				roomSystem.setConstructionWorld(&m_drawingSystem->world());
				roomSystem.setRoomFormedCallback([this](ecs::EntityID room) {
					const auto* roomComp = ecsWorld->getComponent<ecs::Room>(room);
					gameUI->pushNotification("Room formed", roomComp != nullptr ? roomComp->name : "New room", UI::ToastSeverity::Info);
					LOG_INFO(
						Game, "Room formed: %s (entity %u)", roomComp != nullptr ? roomComp->name.c_str() : "?", static_cast<uint32_t>(room)
					);
				});
			}

			// Initialize SelectionSystem (after ECS, PlacementExecutor, and DrawingSystem)
			m_selectionSystem = std::make_unique<world_sim::SelectionSystem>(world_sim::SelectionSystem::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.placementExecutor = m_placementExecutor.get(),
				.constructionWorld = &m_drawingSystem->world(),
				.callbacks = {.onSelectionChanged = [](const world_sim::Selection&) {
					// Selection state is queried each frame - no action needed on change
				}}
			});

			// Populate the config strip's material cards from construction config.
			{
				std::vector<std::pair<std::string, float>> materials;
				for (const auto& [name, def] : engine::assets::ConstructionRegistry::Get().getAllMaterials()) {
					materials.emplace_back(name, def.costRatePerSquareMeter);
				}
				gameUI->setConstructionMaterials(materials);
			}

			// Populate the config strip's wall thickness-preset cards for the active
			// material (Wood). Refreshed in onConstructionMaterialSelected too.
			refreshThicknessPresets(m_drawingSystem->activeMaterial());

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

			// Drawing tool gets input before placement/selection (GameUI already had
			// first dibs above). It consumes clicks while active.
			if (m_drawingSystem->isActive()) {
				constexpr int kModAlt = 0x0004; // GLFW_MOD_ALT
				constexpr int kModCtrl = 0x0002; // GLFW_MOD_CONTROL
				const bool	  freeform = (event.modifiers & kModAlt) != 0;
				const bool	  ctrl = (event.modifiers & kModCtrl) != 0;

				if (!consumed && event.type == UI::InputEvent::Type::MouseMove) {
					// Skip when a UI panel consumed the move, otherwise the preview
					// tracks the snapped cursor under the panel to a spot you can't click.
					m_drawingSystem->handleMouseMove(event.position.x, event.position.y, logicalW, logicalH, freeform);
				} else if (!consumed && event.type == UI::InputEvent::Type::MouseUp) {
					if (event.button == engine::MouseButton::Right) {
						// Foundation: cancel the shape. Wall: end (commit) the chain.
						m_drawingSystem->cancel();
						return true;
					}
					if (event.button == engine::MouseButton::Left) {
						m_drawingSystem->handleClick(event.position.x, event.position.y, logicalW, logicalH, freeform, ctrl);
						return true;
					}
				}
				return consumed;
			}

			// Handle placement mode interaction
			if (m_placementSystem->isActive()) {
				if (!consumed && event.type == UI::InputEvent::Type::MouseMove) {
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

			// Handle Escape - cancel the drawing tool / placement first, then exit
			if (input.isKeyPressed(engine::Key::Escape)) {
				if (m_drawingSystem->isActive()) {
					m_drawingSystem->cancel();
				} else if (m_placementSystem->isActive()) {
					m_placementSystem->cancel();
				} else {
					sceneManager->switchTo(world_sim::toKey(world_sim::SceneType::MainMenu));
					return; // Don't process rest of update when switching scenes
				}
			}

			// Backspace removes the last placed point while drawing.
			if (input.isKeyPressed(engine::Key::Backspace) && m_drawingSystem->isActive()) {
				m_drawingSystem->removeLastPoint();
			}

			// Enter finishes (commits) a wall chain; the foundation tool ignores it
			// (it closes on origin-click instead).
			if (input.isKeyPressed(engine::Key::Enter) && m_drawingSystem->isActive()) {
				m_drawingSystem->finishChain();
			}

			// Handle B key - toggle build mode. Gated while the foundation tool owns
			// world input so the two tools can't be live at once (see tool-exclusivity
			// wiring in the placement/drawing activation callbacks).
			if (input.isKeyPressed(engine::Key::B) && !m_drawingSystem->isActive()) {
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

			// Remote camera control from the debug server (position/zoom set once,
			// pan persists like held movement keys until cleared)
			if (auto* debugServer = engine::AppLauncher::getDebugServer()) {
				Foundation::CameraCommand cmd;
				if (debugServer->consumeCameraCommand(cmd)) {
					if (cmd.hasPosition) {
						m_camera->setPosition({cmd.x, cmd.y});
					}
					if (cmd.hasZoom) {
						m_camera->setZoom(cmd.zoom);
					}
					if (cmd.hasPan) {
						m_debugPanX = cmd.panX;
						m_debugPanY = cmd.panY;
					}
				}

				// Drain DEV/TEST construction commands (/api/dev/...). DebugServer is
				// domain-agnostic and only queues generic DevCommands; GameScene owns the
				// construction context (ConstructionSystem, ConstructionWorld, ecsWorld) so
				// it interprets them here. Mirrors the camera/input consume path; runs only
				// in dev builds (the debug server is dev-only). Drained BEFORE ecsWorld->update()
				// so a freebuild/foundation command takes effect the same frame.
				std::vector<Foundation::DevCommand> devCommands;
				if (debugServer->consumeDevCommands(devCommands)) {
					for (const auto& devCmd : devCommands) {
						handleDevCommand(devCmd);
					}
				}
			}
			dx += m_debugPanX;
			dy += m_debugPanY;

			// Normalize combined input so diagonal movement (keys or debug pan) is unit speed
			float inputLength = std::sqrt(dx * dx + dy * dy);
			if (inputLength > 1.0F) {
				dx /= inputLength;
				dy /= inputLength;
			}

			m_camera->move(dx, dy, dt);

			// Zoom with scroll wheel (snaps to discrete levels)
			// Skip scroll handling when a modal dialog is open or task list expanded (they scroll instead)
			if (!gameUI->isCraftingDialogVisible() && !gameUI->isColonistDetailsVisible() && !gameUI->isGlobalTaskListExpanded()) {
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

			// Drain deferred entity removals AFTER the ECS update. Systems (e.g.
			// ActionSystem on a completed Deconstruct) and input handlers queue
			// destroys here instead of calling destroyEntity mid-iteration, which
			// would swap-and-pop the component pools out from under a live view.
			if (!m_pendingEntityRemoval.empty()) {
				for (ecs::EntityID entity : m_pendingEntityRemoval) {
					ecsWorld->destroyEntity(entity);
				}
				m_pendingEntityRemoval.clear();
			}

			// Update unified game UI (overlay + info panel)
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
			gameUI->update(
				dt, *m_camera, *m_chunkManager, *ecsWorld, assetRegistry, recipeRegistry, m_selectionSystem->current(),
				&m_drawingSystem->world()
			);

			// Push drawing-tool status to the config strip (drives its readouts and
			// visibility).
			gameUI->setConstructionStatus(m_drawingSystem->status());

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

			// Render committed foundations + in-progress drawing preview (interim;
			// C6 replaces committed-foundation rendering). Drawn after entities so
			// foundations sit above terrain and below the cursor ghost/UI.
			m_drawingSystem->render(w, h);

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
				metrics->setEntityRenderStats(m_entityRenderer->lastDrawCallCount(), m_entityRenderer->lastTriangleCount());

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
			m_drawingSystem.reset();

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
			ecsWorld->registerSystem<ecs::StorageGoalSystem>();								// Priority 55 - goals before AI
			ecsWorld->registerSystem<ecs::CraftingGoalSystem>();							// Priority 56 - goals before AI
			ecsWorld->registerSystem<ecs::BuildGoalSystem>();								// Priority 57 - goals before AI
			ecsWorld->registerSystem<ecs::ConstructionSystem>();							// Priority 58 - foundation lifecycle goals
			ecsWorld->registerSystem<ecs::RoomDetectionSystem>();							// Priority 59 - derive rooms from built walls
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

			// Wire ConstructionSystem with placement data for footprint-clearing queries. The
			// ConstructionWorld pointer and completion callbacks are wired after DrawingSystem
			// (which owns the ConstructionWorld) is created, back in initialize().
			auto& constructionSystem = ecsWorld->getSystem<ecs::ConstructionSystem>();
			constructionSystem.setPlacementData(m_placementExecutor.get(), &m_processedChunks);

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

			// Add skills with default starting values
			// Skills range 0-20 (see SkillLevels in Skills.h)
			ecs::Skills skills;
			skills.setLevel("Farming", 3.0F);	   // Novice farmer
			skills.setLevel("Crafting", 2.0F);	   // Novice crafter
			skills.setLevel("Construction", 1.0F); // Barely trained builder
			// Medicine: 0 (untrained) - omitted from map
			ecsWorld->addComponent<ecs::Skills>(entity, std::move(skills));

			LOG_INFO(Game, "Spawned colonist '%s' at (%.1f, %.1f)", newName.c_str(), newPosition.x, newPosition.y);
			return entity;
		}

		/// Launch async tasks for newly loaded chunks.
		/// Non-blocking: spawns background threads for entity placement computation.
		void processNewChunks() {
			// First, poll and integrate any completed async tasks
			m_asyncProcessor->pollCompleted();

			// Queue worker-baked entity meshes for budgeted GPU upload (the bake
			// itself ran on the placement worker)
			for (auto& [coord, bake] : m_asyncProcessor->takeReadyBakes()) {
				m_entityRenderer->queueBakedChunk(coord, std::move(bake));
			}

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
		/// Refresh the config strip's wall thickness-preset cards for `material`.
		/// A material with no wall presets clears the cards (the wall tool then
		/// rejects commits with a "no wall preset" toast).
		void refreshThicknessPresets(const std::string& material) {
			std::vector<world_sim::ConstructionConfigStrip::ThicknessPresetInfo> presets;
			if (const auto* mat = engine::assets::ConstructionRegistry::Get().getWallMaterial(material)) {
				for (const auto& p : mat->wallThicknesses) {
					presets.push_back({p.name, p.thicknessMeters});
				}
			}
			gameUI->setConstructionThicknessPresets(presets);
		}

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

		// =====================================================================
		// DEV/TEST HTTP commands (/api/dev/...). Dev-only: the debug server that
		// queues these runs only in dev builds. DebugServer stays domain-agnostic
		// and hands us a generic DevCommand; this is where the construction context
		// (ConstructionSystem, ConstructionWorld, ecsWorld) interprets the verb.
		// =====================================================================

		/// Interpret one queued DevCommand. Unknown verbs are logged and ignored.
		void handleDevCommand(const Foundation::DevCommand& cmd) {
			if (cmd.verb == "freebuild" || cmd.verb == "construction") {
				devFreeBuild(cmd);
			} else if (cmd.verb == "givewood") {
				devGiveWood(cmd);
			} else if (cmd.verb == "foundation") {
				devFoundation(cmd);
			} else if (cmd.verb == "walls") {
				devWalls(cmd);
			} else if (cmd.verb == "opening") {
				devOpening(cmd);
			} else {
				LOG_WARNING(Game, "[DevAPI] Unknown dev command verb '%s'", cmd.verb.c_str());
			}
		}

		/// /api/dev/freebuild?on=1|0  (also /api/dev/construction?freebuild=on|off).
		/// Toggles ConstructionSystem free-build: every blueprint is then driven
		/// straight to Built each tick without colonists. Inert when off.
		void devFreeBuild(const Foundation::DevCommand& cmd) {
			// Accept on= (freebuild verb) or freebuild= (construction verb); truthy on
			// 1/on/true/yes.
			std::string value = cmd.hasParam("on") ? cmd.param("on") : cmd.param("freebuild", "1");
			const bool	on = (value == "1" || value == "on" || value == "true" || value == "yes");
			ecsWorld->getSystem<ecs::ConstructionSystem>().setFreeBuild(on);
			LOG_INFO(Game, "[DevAPI] free-build %s", on ? "ON" : "OFF");
			gameUI->pushNotification("Dev", on ? "Free-build ON" : "Free-build OFF", UI::ToastSeverity::Info);
		}

		/// /api/dev/givewood?n=100[&where=site|loose]. Credits N Wood to active build
		/// sites' delivery inventories (where=site, the default and most direct), so the
		/// build proceeds without harvesting/hauling. where=loose is not wired here.
		void devGiveWood(const Foundation::DevCommand& cmd) {
			const long n = std::strtol(cmd.param("n", "100").c_str(), nullptr, 10);
			if (n <= 0) {
				LOG_WARNING(Game, "[DevAPI] givewood: n must be > 0");
				return;
			}
			const std::string where = cmd.param("where", "site");
			if (where != "site") {
				LOG_WARNING(Game, "[DevAPI] givewood where=%s not supported (only 'site'); ignoring", where.c_str());
				return;
			}
			const uint32_t credited =
				ecsWorld->getSystem<ecs::ConstructionSystem>().creditMaterialToSites("Wood", static_cast<uint32_t>(n));
			LOG_INFO(Game, "[DevAPI] givewood: credited %u Wood across build sites", credited);
			gameUI->pushNotification("Dev", "Credited " + std::to_string(credited) + " Wood to sites", UI::ToastSeverity::Info);
		}

		/// /api/dev/foundation?pts=x0,y0;x1,y1;...&material=Wood&built=1. Commits a
		/// foundation from world-meter coordinates straight into the ConstructionWorld
		/// (bypassing the draw tool), spawns its blueprint entity the same way the draw
		/// tool's commit does, and optionally drives it to Built via the free-build path
		/// (built=1). built=0 leaves a normal blueprint colonists will build.
		void devFoundation(const Foundation::DevCommand& cmd) {
			std::vector<Foundation::Vec2> pts = parsePointList(cmd.param("pts"));
			if (pts.size() < 3) {
				LOG_WARNING(Game, "[DevAPI] foundation: pts needs >= 3 'x,y' pairs (got %zu)", pts.size());
				return;
			}
			const std::string material = cmd.param("material", "Wood");

			auto&	   constructionWorld = m_drawingSystem->world();
			const auto commit = constructionWorld.commitFoundation(pts, material);
			if (!commit.ok()) {
				LOG_WARNING(Game, "[DevAPI] foundation: commit rejected (status %d)", static_cast<int>(commit.status));
				gameUI->pushNotification("Dev", "Foundation commit rejected", UI::ToastSeverity::Warning);
				return;
			}

			const ecs::EntityID entity = spawnFoundationBlueprintEntity(commit.id, pts, material);
			if (entity == ecs::kInvalidEntity) {
				LOG_WARNING(Game, "[DevAPI] foundation: spawn failed for #%llu", static_cast<unsigned long long>(commit.id));
				return;
			}

			const std::string builtStr = cmd.param("built", "0");
			const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");
			if (built) {
				// Route through the SAME instant-completion path free-build uses, so a
				// one-call built foundation is indistinguishable from a normally-built one.
				ecsWorld->getSystem<ecs::ConstructionSystem>().forceCompleteBlueprint(entity);
			}
			LOG_INFO(
				Game,
				"[DevAPI] foundation #%llu spawned (%zu pts, %s, built=%d, entity %u)",
				static_cast<unsigned long long>(commit.id),
				pts.size(),
				material.c_str(),
				built ? 1 : 0,
				static_cast<uint32_t>(entity)
			);
			gameUI->pushNotification("Dev", built ? "Built foundation placed" : "Foundation blueprint placed", UI::ToastSeverity::Info);
		}

		/// /api/dev/walls?pts=x0,y0;x1,y1;...&material=Wood&thickness=Standard&built=1&host=0&close=1.
		/// Commits a wall chain straight into the ConstructionWorld (bypassing the draw
		/// tool) and spawns segment entities. close=1 (default) appends the first point
		/// so a >=3-point chain encloses, which forms a room when built=1. host=0
		/// (default) makes the walls freestanding -- rooms need only built centerlines,
		/// not a host foundation. built=0 leaves blueprints for colonists to build.
		void devWalls(const Foundation::DevCommand& cmd) {
			std::vector<Foundation::Vec2> pts = parsePointList(cmd.param("pts"));
			if (pts.size() < 2) {
				LOG_WARNING(Game, "[DevAPI] walls: pts needs >= 2 'x,y' pairs (got %zu)", pts.size());
				return;
			}
			const std::string material = cmd.param("material", "Wood");
			const std::string thickness = cmd.param("thickness", "Standard");
			// strtoull, not strtoul: FoundationId is 64-bit, and unsigned long is only
			// 32-bit on Windows (LLP64), which would truncate large ids.
			const auto host = static_cast<engine::construction::FoundationId>(std::strtoull(cmd.param("host", "0").c_str(), nullptr, 10));

			const std::string builtStr = cmd.param("built", "1");
			const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");
			const std::string closeStr = cmd.param("close", "1");
			const bool		  close = (closeStr == "1" || closeStr == "on" || closeStr == "true" || closeStr == "yes");
			if (close && pts.size() >= 3) {
				pts.push_back(pts.front()); // close the loop so the chain encloses
			}

			const int n = m_drawingSystem->devCommitWalls(pts, material, thickness, host, built);
			LOG_INFO(
				Game,
				"[DevAPI] walls: committed %d segment(s) (%s/%s, built=%d, host=%llu)",
				n,
				material.c_str(),
				thickness.c_str(),
				built ? 1 : 0,
				static_cast<unsigned long long>(host)
			);
			gameUI->pushNotification("Dev", std::to_string(n) + (built ? " built wall(s)" : " wall blueprint(s)"), UI::ToastSeverity::Info);
		}

		/// /api/dev/opening?seg=<id>|pt=x,y&type=Door|Window&t=<0..1>&built=1.
		/// Places an opening on a built wall and (built=1, default) spawns it Complete in
		/// one call. The target segment is `seg` when given, else the nearest built wall
		/// to `pt` (world meters) via snapOpening. `t` overrides the centerline parameter
		/// (default: the snapped t for pt, or 0.5 for seg). built=0 leaves a blueprint
		/// colonists build once the host wall is up. Bypasses the draw tool + soft
		/// validator (the opening analogue of /api/dev/foundation).
		void devOpening(const Foundation::DevCommand& cmd) {
			const std::string type = cmd.param("type", "Door");
			const auto*		  typeDef = engine::assets::ConstructionRegistry::Get().getOpeningType(type);
			if (typeDef == nullptr) {
				LOG_WARNING(Game, "[DevAPI] opening: unknown type '%s'", type.c_str());
				gameUI->pushNotification("Dev", "Unknown opening type", UI::ToastSeverity::Warning);
				return;
			}

			auto& constructionWorld = m_drawingSystem->world();

			// Resolve the target segment + centerline t: seg= names it directly (t
			// defaults to center), pt= snaps to the nearest built wall (t from the snap).
			engine::construction::SegmentId segment = engine::construction::kInvalidSegment;
			float							t = 0.5F;
			if (cmd.hasParam("seg")) {
				segment = static_cast<engine::construction::SegmentId>(std::strtoull(cmd.param("seg").c_str(), nullptr, 10));
			} else if (cmd.hasParam("pt")) {
				const std::vector<Foundation::Vec2> pts = parsePointList(cmd.param("pt"));
				if (pts.empty()) {
					LOG_WARNING(Game, "[DevAPI] opening: pt must be 'x,y' world meters");
					return;
				}
				const auto&							   registry = engine::assets::ConstructionRegistry::Get();
				const engine::construction::SnapEngine snap(registry.snapping(), constructionWorld);
				const auto							   os = snap.snapOpening(pts.front(), typeDef->widthMeters);
				if (!os.valid) {
					LOG_WARNING(Game, "[DevAPI] opening: no built wall near pt");
					gameUI->pushNotification("Dev", "No built wall near point", UI::ToastSeverity::Warning);
					return;
				}
				segment = os.segment;
				t = os.t;
			} else {
				LOG_WARNING(Game, "[DevAPI] opening: needs seg=<id> or pt=x,y");
				return;
			}

			// Explicit t= overrides the resolved parameter (addOpening clamps to [0,1]).
			if (cmd.hasParam("t")) {
				t = std::strtof(cmd.param("t").c_str(), nullptr);
			}

			const std::string builtStr = cmd.param("built", "1");
			const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");

			const engine::construction::OpeningId id = m_drawingSystem->devCommitOpening(segment, t, type, built);
			if (id == engine::construction::kInvalidOpening) {
				LOG_WARNING(
					Game,
					"[DevAPI] opening: commit rejected (segment #%llu, type %s)",
					static_cast<unsigned long long>(segment),
					type.c_str()
				);
				gameUI->pushNotification("Dev", "Opening commit rejected", UI::ToastSeverity::Warning);
				return;
			}

			LOG_INFO(
				Game,
				"[DevAPI] opening #%llu spawned (%s on segment #%llu at t=%.2f, built=%d)",
				static_cast<unsigned long long>(id),
				type.c_str(),
				static_cast<unsigned long long>(segment),
				static_cast<double>(t),
				built ? 1 : 0
			);
			gameUI->pushNotification("Dev", built ? "Built opening placed" : "Opening blueprint placed", UI::ToastSeverity::Info);
		}

		/// Parse "x0,y0;x1,y1;..." (world meters) into a point list. Tolerant: skips
		/// malformed pairs. Used by /api/dev/foundation and /api/dev/walls.
		static std::vector<Foundation::Vec2> parsePointList(const std::string& spec) {
			std::vector<Foundation::Vec2> pts;
			std::stringstream			  ss(spec);
			std::string					  pair;
			while (std::getline(ss, pair, ';')) {
				const auto comma = pair.find(',');
				if (comma == std::string::npos) {
					continue;
				}
				const std::string xs = pair.substr(0, comma);
				const std::string ys = pair.substr(comma + 1);
				char*			  endX = nullptr;
				char*			  endY = nullptr;
				const float		  x = std::strtof(xs.c_str(), &endX);
				const float		  y = std::strtof(ys.c_str(), &endY);
				// Skip the pair unless both parses consumed at least one character;
				// strtof returns 0.0 on a non-numeric string, which would otherwise
				// silently inject a (0,0) vertex.
				if (endX == xs.c_str() || endY == ys.c_str()) {
					continue;
				}
				pts.emplace_back(x, y);
			}
			return pts;
		}

		/// Build the ECS blueprint entity for a programmatically committed foundation.
		/// Replicates DrawingSystem::spawnBlueprintEntity (which is private): same
		/// components, same material-driven manifest/work/HP, same ConstructionWorld
		/// entity link. Kept minimal and in sync with the draw-tool path.
		ecs::EntityID spawnFoundationBlueprintEntity(engine::construction::FoundationId id, const std::vector<Foundation::Vec2>& pts, const std::string& material) {
			auto& constructionWorld = m_drawingSystem->world();
			if (constructionWorld.get(id) == nullptr) {
				return ecs::kInvalidEntity;
			}

			const float area = constructionWorld.areaSquareMeters(id);

			const auto& registry = engine::assets::ConstructionRegistry::Get();
			const auto* mat = registry.getMaterial(material);
			float		costRate = 0.0F;
			float		workRate = 0.0F;
			float		hpRate = 0.0F;
			if (mat != nullptr) {
				costRate = mat->costRatePerSquareMeter;
				workRate = mat->workRatePerSquareMeter;
				hpRate = mat->hp;
			}

			auto entity = ecsWorld->createEntity();

			// Centroid (average of vertices) keeps the transform inside the footprint.
			Foundation::Vec2 centroid{0.0F, 0.0F};
			for (const auto& p : pts) {
				centroid += p;
			}
			centroid /= static_cast<float>(pts.size());
			ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{{centroid.x, centroid.y}});

			ecsWorld->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Foundation, id});

			ecs::StructureBlueprint blueprint;
			blueprint.phase = ecs::StructureBlueprint::BuildPhase::Clearing;
			const auto requiredQty = static_cast<uint32_t>(std::ceil(static_cast<double>(area) * static_cast<double>(costRate)));
			if (requiredQty > 0) {
				blueprint.required.emplace_back(material, requiredQty);
			}
			blueprint.workTotal = area * workRate;
			ecsWorld->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

			ecs::Inventory deliveryInv;
			deliveryInv.maxCapacity = 8;
			deliveryInv.maxStackSize = 100000;
			ecsWorld->addComponent<ecs::Inventory>(entity, std::move(deliveryInv));

			const float maxHp = area * hpRate;
			ecsWorld->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{maxHp, maxHp});

			constructionWorld.setEntity(id, entity);
			return entity;
		}

		/// Handle Demolish request from a foundation's info panel.
		/// Epic C scope: immediate whole-foundation removal. The work-driven Deconstruct
		/// action exists; only its material refund and the Demolish-building cascade are
		/// deferred polish.
		void handleDemolishFoundation() {
			const auto& sel = m_selectionSystem->current();
			auto*		foundationSel = std::get_if<world_sim::FoundationSelection>(&sel);
			if (foundationSel == nullptr) {
				LOG_WARNING(Game, "Cannot demolish: no foundation selected");
				return;
			}

			auto& constructionWorld = m_drawingSystem->world();
			const auto* foundation = constructionWorld.get(foundationSel->id);
			if (foundation == nullptr) {
				LOG_WARNING(Game, "Cannot demolish: foundation #%llu not found", static_cast<unsigned long long>(foundationSel->id));
				m_selectionSystem->clearSelection();
				return;
			}

			// Capture the ECS mirror handle before the topology record goes away, then
			// remove the topology record. Defer the entity destruction through the same
			// queue the deconstruct callback uses, so there's one removal path (drained
			// after ecsWorld->update() in update()).
			const ecs::EntityID entity = foundation->entity;
			constructionWorld.removeFoundation(foundationSel->id);
			if (entity != ecs::kInvalidEntity) {
				m_pendingEntityRemoval.push_back(entity);
			}

			LOG_INFO(Game, "Demolished foundation #%llu", static_cast<unsigned long long>(foundationSel->id));
			m_selectionSystem->clearSelection();
		}

		/// Handle Demolish request from a wall segment's info panel. The SEGMENT, not
		/// the chain, is the demolition unit (design D6): a multi-segment wall keeps its
		/// other segments. Mirrors handleDemolishFoundation: immediate topology removal
		/// plus a deferred ECS entity destroy through the same queue (so we never
		/// destroyEntity mid-update). removeSegment also prunes any vertex left orphaned,
		/// so a split T-junction can't leave a dangling vertex; the only entity to clean
		/// up is this segment's own mirror (a split already re-mapped the host's entity).
		void handleDemolishWallSegment() {
			const auto& sel = m_selectionSystem->current();
			auto*		wallSel = std::get_if<world_sim::WallSegmentSelection>(&sel);
			if (wallSel == nullptr) {
				LOG_WARNING(Game, "Cannot demolish: no wall segment selected");
				return;
			}

			auto&		constructionWorld = m_drawingSystem->world();
			const auto* segment = constructionWorld.getSegment(wallSel->id);
			if (segment == nullptr) {
				LOG_WARNING(Game, "Cannot demolish: wall segment #%llu not found", static_cast<unsigned long long>(wallSel->id));
				m_selectionSystem->clearSelection();
				return;
			}

			// Capture the ECS mirror handle before the topology record goes away, then
			// remove just this segment (vertices left orphaned are pruned inside).
			// Defer the entity destruction through the shared queue (drained after
			// ecsWorld->update() in update()).
			const ecs::EntityID entity = segment->entity;
			constructionWorld.removeSegment(wallSel->id);
			if (entity != ecs::kInvalidEntity) {
				m_pendingEntityRemoval.push_back(entity);
			}

			LOG_INFO(Game, "Demolished wall segment #%llu", static_cast<unsigned long long>(wallSel->id));
			gameUI->pushNotification("Demolished", "Wall segment removed", UI::ToastSeverity::Info);
			m_selectionSystem->clearSelection();
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

		// Persistent pan direction from debug server camera commands (held-key style)
		float m_debugPanX = 0.0F;
		float m_debugPanY = 0.0F;

		// World interaction subsystems (extracted from GameScene)
		std::unique_ptr<world_sim::PlacementSystem> m_placementSystem;
		std::unique_ptr<world_sim::SelectionSystem> m_selectionSystem;
		std::unique_ptr<world_sim::DrawingSystem>	m_drawingSystem;

		// Entities queued for destruction, drained after ecsWorld->update() so we
		// never destroyEntity mid-view-iteration (deconstruct callback, demolish).
		std::vector<ecs::EntityID> m_pendingEntityRemoval;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
