// Game Scene - Main gameplay with chunk-based world rendering

#include "GameWorldState.h"
#include "SceneTypes.h"
#include "scenes/game/dev/DevCommandHandler.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/nav/NavOverlay.h"
#include "scenes/game/world/vision/VisionOverlay.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/rooms/RoomOverlay.h"
#include "scenes/game/world/selection/SelectionSystem.h"

#include <assets/ConstructionRegistry.h>

#include <components/toast/Toast.h> // For ToastSeverity

#include <GL/glew.h>

#include <application/AppLauncher.h>
#include <debug/DebugServer.h>
#include <chrono>
#include <cmath>
#include <limits>
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
#include <ecs/components/AnimationState.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Colony.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Knowledge.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Room.h>
#include <ecs/components/ResourceStack.h>
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
#include <ecs/systems/CollisionSystem.h>
#include <ecs/systems/ConstructionSystem.h>
#include <ecs/systems/CraftingGoalSystem.h>
#include <ecs/systems/DynamicEntityRenderSystem.h>
#include <ecs/systems/MovementSystem.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/NeedsDecaySystem.h>
#include <ecs/systems/PhysicsSystem.h>
#include <ecs/systems/RoomDetectionSystem.h>
#include <ecs/systems/StorageGoalSystem.h>
#include <ecs/systems/TimeSystem.h>
#include <ecs/systems/StaticRectCollisionSystem.h>
#include <ecs/systems/VisionSystem.h>
#include <ecs/systems/WallCollisionSystem.h>

#include <cstdlib>
#include <memory>
#include <random>
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
				m_spawnPosition = preloadedState->spawnPosition;
				m_colony = preloadedState->colony;
				m_planet = std::move(preloadedState->planet);
				m_landingLatDeg = preloadedState->landingLatDeg;
				m_landingLonDeg = preloadedState->landingLonDeg;

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
				.onDemolishBuilding = [this]() { handleDemolishBuilding(); },
				.onDemolishWallSegment = [this]() { handleDemolishWallSegment(); },
				.onDemolishOpening = [this]() { handleDemolishOpening(); },
				.queryResources = [this](const std::string& defName, Foundation::Vec2 position) -> std::optional<uint32_t> {
					auto coord = engine::world::worldToChunk({position.x, position.y});
					return m_placementExecutor->getResourceCount(coord, {position.x, position.y}, defName);
				},
				.onStructureSelected =
					[this](const std::string& structure) {
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
				.onConstructionMaterialSelected =
					[this](const std::string& material) {
						if (m_drawingSystem) {
							m_drawingSystem->setActiveMaterial(material);
							refreshThicknessPresets(material);
						}
					},
				.onConstructionThicknessSelected =
					[this](const std::string& preset) {
						if (m_drawingSystem) {
							m_drawingSystem->setActiveThicknessPreset(preset);
						}
					},
				.onRoomsToggle =
					[this]() {
						// Button and R hotkey share this one method so they never drift.
						if (m_roomOverlay) {
							setRoomOverlayActive(!m_roomOverlay->isActive());
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
				.placementExecutor = m_placementExecutor.get(),
				.navigation = &ecsWorld->getSystem<ecs::NavigationSystem>(),
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
				.navigation = &ecsWorld->getSystem<ecs::NavigationSystem>(),
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

				// NavigationSystem reads the same ConstructionWorld (walls/doors) to build
				// its navmesh input; wired here for the same reason ConstructionSystem is.
				ecsWorld->getSystem<ecs::NavigationSystem>().setConstructionWorld(&m_drawingSystem->world());

				// VisionSystem reads the same ConstructionWorld so walls occlude sight:
				// its GeometryIndex turns built walls into opaque occluders and gates
				// discovery by a per-observer visibility polygon.
				ecsWorld->getSystem<ecs::VisionSystem>().setConstructionWorld(&m_drawingSystem->world());

				// WallCollisionSystem reads the same store for its built-wall safety net.
				ecsWorld->getSystem<ecs::WallCollisionSystem>().setConstructionWorld(&m_drawingSystem->world());

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

				// Deconstruct complete: drop the topology record now (the Structure kind selects
				// which mutator; a wall surfaces its openings' entities too) and DEFER the entity
				// destruction. This callback fires from inside ActionSystem::update's live view
				// iteration; destroying here would swap-and-pop the shared component pools and corrupt
				// that iteration, so entities go on the queue drained after ecsWorld->update() (see
				// update()). This is the SINGLE removal path: the demolish buttons only mark
				// structures; the work-driven deconstruct lands here. The SAME lambda is wired to
				// ConstructionSystem for the no-work edge case (a marked blueprint with workDone <= 0
				// is removed immediately through here). Material return splits by path: a built teardown
				// salvages refundPercent% of its delivered[] HERE at completion (below); a no-work cancel
				// dumps 100% at order-time inside ConstructionSystem and clears delivered[] (so the salvage
				// below reads empty for it).
				auto structureDeconstructed = [this](ecs::EntityID blueprintEntity) {
					// Salvage refundPercent% of a BUILT structure's delivered[] manifest as loose piles HERE,
					// at tear-down completion (the structure is gone), not at the demolish-order tick when it
					// was still standing. The entity is still alive (destruction is deferred to the queue), so
					// delivered[] and Position read fine. A no-work cancel already dumped 100% and cleared
					// delivered[] at order-time in ConstructionSystem, so this reads empty for it and drops
					// nothing -- no double-dump.
					if (const auto* blueprint = ecsWorld->getComponent<ecs::StructureBlueprint>(blueprintEntity);
						blueprint != nullptr && !blueprint->delivered.empty()) {
						const float		refundPercent = engine::assets::ConstructionRegistry::Get().constraints().refundPercent;
						const auto*		position = ecsWorld->getComponent<ecs::Position>(blueprintEntity);
						const glm::vec2 dropPos = position != nullptr ? position->value : glm::vec2{0.0F, 0.0F};
						for (const auto& [defName, qty] : blueprint->delivered) {
							const auto salvageQty =
								static_cast<uint32_t>(std::floor(static_cast<float>(qty) * refundPercent / 100.0F));
							if (salvageQty > 0) {
								dropResourcePiles(defName, dropPos.x, dropPos.y, salvageQty);
							}
						}
					}

					const auto* structure = ecsWorld->getComponent<ecs::Structure>(blueprintEntity);
					if (structure != nullptr && structure->graphId != 0) {
						auto& constructionWorld = m_drawingSystem->world();
						switch (structure->kind) {
							case ecs::StructureKind::Opening:
								constructionWorld.removeOpening(structure->graphId);
								LOG_INFO(
									Game,
									"Opening #%llu deconstructed (entity %u)",
									static_cast<unsigned long long>(structure->graphId),
									static_cast<uint32_t>(blueprintEntity)
								);
								break;
							case ecs::StructureKind::Wall: {
								// removeSegment hands back any openings still on the wall; queue
								// their mirror entities too (the cascade should have torn them down
								// first, but this keeps removal leak-free if one survives).
								std::vector<ecs::EntityID> removedOpeningEntities;
								constructionWorld.removeSegment(structure->graphId, &removedOpeningEntities);
								for (const ecs::EntityID openingEntity : removedOpeningEntities) {
									m_pendingEntityRemoval.push_back(openingEntity);
								}
								LOG_INFO(
									Game,
									"Wall segment #%llu deconstructed (entity %u)",
									static_cast<unsigned long long>(structure->graphId),
									static_cast<uint32_t>(blueprintEntity)
								);
								break;
							}
							case ecs::StructureKind::Foundation:
							case ecs::StructureKind::Room:
								constructionWorld.removeFoundation(structure->graphId);
								LOG_INFO(
									Game,
									"Foundation #%llu deconstructed (entity %u)",
									static_cast<unsigned long long>(structure->graphId),
									static_cast<uint32_t>(blueprintEntity)
								);
								break;
						}
					}
					m_pendingEntityRemoval.push_back(blueprintEntity);
				};
				actionSys.setStructureDeconstructedCallback(structureDeconstructed);
				constructionSystem.setStructureDeconstructedCallback(structureDeconstructed);

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

			// Dev/test command + readback handler. All the systems it pokes now exist; it
			// borrows the colonist-spawn helper (also used at world init) via a callback.
			m_devHandler = std::make_unique<world_sim::DevCommandHandler>(world_sim::DevCommandContext{
				.world = ecsWorld.get(),
				.drawing = m_drawingSystem.get(),
				.placement = m_placementSystem.get(),
				.selection = m_selectionSystem.get(),
				.ui = gameUI.get(),
				.chunks = m_chunkManager.get(),
				.navigation = &ecsWorld->getSystem<ecs::NavigationSystem>(),
				.planet = m_planet,
				.landingLatDeg = m_landingLatDeg,
				.landingLonDeg = m_landingLonDeg,
				.spawnColonist = [this](glm::vec2 pos, const std::string& name) { return spawnColonist(pos, name); },
			});

			// Rooms overlay: scene-owned world-space layer that tints/outlines/labels
			// detected rooms when toggled on (R). Reads the same RoomDetectionSystem
			// records live; off by default.
			m_roomOverlay = std::make_unique<world_sim::RoomOverlay>(world_sim::RoomOverlay::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.roomDetection = &ecsWorld->getSystem<ecs::RoomDetectionSystem>(),
			});

			// Nav debug overlay: draws the cached navmesh wireframe and live agent
			// routes when toggled on (N). Reads the NavigationSystem mesh + per-entity
			// NavPath components live; off by default.
			m_navOverlay = std::make_unique<world_sim::NavOverlay>(world_sim::NavOverlay::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.navigation = &ecsWorld->getSystem<ecs::NavigationSystem>(),
			});

			// Vision debug overlay: draws each colonist's visibility polygon and the
			// occluder wall segments when toggled on (V). Off by default.
			m_visionOverlay = std::make_unique<world_sim::VisionOverlay>(world_sim::VisionOverlay::Args{
				.world = ecsWorld.get(),
				.camera = m_camera.get(),
				.vision = &ecsWorld->getSystem<ecs::VisionSystem>(),
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
				// While the room overlay is active it owns the LEFT click: hit a room ->
				// RoomSelection, miss -> deselect. It never falls through to structure
				// selection (rooms aren't in the SelectionSystem ladder). Routed through
				// setSelection so the result flows through the same sink (current()).
				if (m_roomOverlay->isActive() && event.button == engine::MouseButton::Left) {
					if (auto roomId = m_roomOverlay->handleClick(event.position.x, event.position.y, logicalW, logicalH)) {
						m_selectionSystem->setSelection(world_sim::RoomSelection{*roomId});
					} else {
						m_selectionSystem->clearSelection();
					}
				} else {
					m_selectionSystem->handleClick(event.position.x, event.position.y, logicalW, logicalH);
				}
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

			// R toggles the rooms overlay (tint/outline/label of detected rooms). Same
			// state the GameplayBar Rooms button flips, through the same method, so the
			// two never drift.
			if (input.isKeyPressed(engine::Key::R)) {
				setRoomOverlayActive(!m_roomOverlay->isActive());
			}

			// N toggles the nav debug overlay (navmesh wireframe + agent routes).
			if (input.isKeyPressed(engine::Key::N)) {
				setNavOverlayActive(!m_navOverlay->isActive());
			}

			// V toggles the vision debug overlay (visibility polygons + occluders).
			if (input.isKeyPressed(engine::Key::V)) {
				setVisionOverlayActive(!m_visionOverlay->isActive());
			}

			// T toggles the per-colonist decision inspector panel.
			if (input.isKeyPressed(engine::Key::T)) {
				gameUI->toggleDecisionInspector();
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

				// Drain DEV/TEST commands (/api/dev/...) and serve a pending /api/state
				// readback. DebugServer is domain-agnostic and only queues generic
				// DevCommands / state requests; DevCommandHandler owns the game context and
				// interprets them. Both run on the game thread (the HTTP thread must never
				// touch the ECS) and BEFORE ecsWorld->update() so a command takes effect the
				// same frame. Dev builds only (the debug server is dev-only).
				std::vector<Foundation::DevCommand> devCommands;
				if (debugServer->consumeDevCommands(devCommands)) {
					for (const auto& devCmd : devCommands) {
						m_devHandler->handle(devCmd);
					}
				}

				std::string stateQuery;
				if (debugServer->consumeStateRequest(stateQuery)) {
					debugServer->deliverState(m_devHandler->serializeState(stateQuery));
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

			// Feed the viewport rect to NavigationSystem as PLAIN DATA -- the scene no
			// longer drives the navmesh. NavigationSystem owns the sim area: it tracks the
			// colonists plus this viewport, clusters them into regions, and self-gates the
			// rebuilds off the render clock. The visible rect (expanded by kViewportMargin
			// for scroll/zoom headroom) is the camera's contribution; the system clamps the
			// half-extents and decides internally when a region needs (re)building.
			{
				int vpW = 0;
				int vpH = 0;
				Renderer::Primitives::getLogicalViewport(vpW, vpH);
				Foundation::Rect vis = m_camera->getVisibleRect(vpW, vpH, kPixelsPerMeter);
				const glm::vec2 halfExtentMeters{
					0.5F * vis.width * ecs::NavigationSystem::kViewportMargin,
					0.5F * vis.height * ecs::NavigationSystem::kViewportMargin};
				const engine::world::WorldPosition pos = m_camera->position();
				ecsWorld->getSystem<ecs::NavigationSystem>().setViewportRect(glm::vec2{pos.x, pos.y}, halfExtentMeters);
			}

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

			// Drain deferred drops AFTER the ECS update, for the same reason as the removals above:
			// spawnEntity grows the component pools and a synchronous spawn from inside a system's
			// view loop (a colonist freeing its hands, a fell's uncarried remainder) would reallocate
			// and dangle that view's bound refs. A packaged item spawns one entity with a Packaged
			// tag; a resource pile routes through dropResourcePiles (stack-capped split).
			if (!m_pendingSpawns.empty()) {
				for (const PendingSpawn& spawn : m_pendingSpawns) {
					if (spawn.packaged) {
						// Spawn AT the resolved target (when one is set) so the box never overlaps
						// the station: the colonist walks over and installs it via PlacePackaged.
						// targetPosition wired here -> BuildGoalSystem raises the place goal next
						// frame. nullopt -> spawn in place, await the player's [Place] click.
						const glm::vec2 spawnAt =
							spawn.targetPosition.has_value() ? *spawn.targetPosition : glm::vec2{spawn.x, spawn.y};
						auto entity = m_placementSystem->spawnEntity(spawn.defName, spawnAt);
						ecs::Packaged packaged;
						packaged.targetPosition = spawn.targetPosition;
						ecsWorld->addComponent<ecs::Packaged>(entity, packaged);
						if (spawn.targetPosition.has_value()) {
							LOG_INFO(Game, "Spawned packaged '%s' at (%.1f, %.1f) - auto-placing", spawn.defName.c_str(),
									 spawnAt.x, spawnAt.y);
						} else {
							LOG_INFO(Game, "Spawned packaged '%s' - awaiting placement", spawn.defName.c_str());
						}
					} else {
						dropResourcePiles(spawn.defName, spawn.x, spawn.y, spawn.quantity);
					}
				}
				m_pendingSpawns.clear();
			}

			// Feed the current selection's room id to the overlay so it can draw the
			// gold selected highlight; 0 (no room selected) clears it.
			{
				const auto&	  sel = m_selectionSystem->current();
				std::uint64_t selectedRoom = 0;
				if (const auto* roomSel = std::get_if<world_sim::RoomSelection>(&sel)) {
					selectedRoom = roomSel->roomId;
				}
				m_roomOverlay->setSelectedRoom(selectedRoom);
			}

			// Update unified game UI (overlay + info panel)
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
			gameUI->update(
				dt,
				*m_camera,
				*m_chunkManager,
				*ecsWorld,
				assetRegistry,
				recipeRegistry,
				m_selectionSystem->current(),
				&m_drawingSystem->world(),
				&ecsWorld->getSystem<ecs::RoomDetectionSystem>()
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

			// Rooms overlay (tint/outline/label) above foundation fills, below walls.
			// No-op unless toggled on (R).
			m_roomOverlay->render(w, h);

			// Nav debug overlay (navmesh wireframe + agent routes) above wall bands.
			// No-op unless toggled on (N).
			m_navOverlay->render(w, h);

			// Vision debug overlay (visibility polygons + occluders). No-op unless on (V).
			m_visionOverlay->render(w, h);

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
			ecsWorld->registerSystem<ecs::NavigationSystem>();								// Priority 51 - cached navmesh + path queries
			ecsWorld->registerSystem<ecs::AIDecisionSystem>(assetRegistry, recipeRegistry); // Priority 60
			ecsWorld->registerSystem<ecs::MovementSystem>();								// Priority 100
			ecsWorld->registerSystem<ecs::PhysicsSystem>();									// Priority 200
			ecsWorld->registerSystem<ecs::CollisionSystem>();								// Priority 250 - positional separation after physics
			ecsWorld->registerSystem<ecs::WallCollisionSystem>();							// Priority 260 - wall safety-net after agent separation
			ecsWorld->registerSystem<ecs::StaticRectCollisionSystem>();						// Priority 270 - flora-rect safety-net after wall collision
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

			// Wire NavigationSystem resources (same data VisionSystem consumes). The
			// ConstructionWorld pointer is wired after DrawingSystem is created, back in
			// initialize(), exactly like ConstructionSystem. The simulation area itself
			// is pushed per-frame in update() via setSimulationArea (GameScene owns the
			// camera and viewport dimensions needed to compute it).
			auto& navSystem = ecsWorld->getSystem<ecs::NavigationSystem>();
			navSystem.setChunkManager(m_chunkManager.get());
			navSystem.setPlacementData(m_placementExecutor.get(), &m_processedChunks);

			// Tier-3 flora-rect safety net gathers placed rects locally each frame.
			auto& staticRectCollisionSystem = ecsWorld->getSystem<ecs::StaticRectCollisionSystem>();
			staticRectCollisionSystem.setPlacementData(m_placementExecutor.get());

			// Wire up AIDecisionSystem with chunk manager for toilet location queries
			auto& aiDecisionSystem = ecsWorld->getSystem<ecs::AIDecisionSystem>();
			aiDecisionSystem.setChunkManager(m_chunkManager.get());

			// AI resolves a navmesh route for every move; with no route possible the colonist holds
			// (pre-mesh / off-area) or stops (believed wall), and an off-mesh colonist snaps to
			// valid ground. There is no straight-line beeline in the running game.
			aiDecisionSystem.setNavigationSystem(&navSystem);

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

			// Every ground drop -- a felled tree's uncarried remainder, craft overflow, a stowed
			// tool, a cancelled build site's salvage -- snaps to a walkable nav point before it
			// lands, so a drop whose raw origin sits in a river / off-mesh (a tree felled on a
			// bank) ends up on solid ground instead of in the water. findValidPositionNear is the
			// one primitive for this; the snap lives here, at the two drop funnels (loose pile,
			// packaged item), so no upstream drop site needs its own nav check (One-Path). It is
			// deterministic (no RNG, canonical +X bias) and returns nullopt only when no mesh
			// covers the origin -- then we keep the raw origin so a drop never hard-fails.
			auto snapDropToGround = [&navSystem](float x, float y) -> glm::vec2 {
				const std::optional<glm::vec2> onGround = navSystem.findValidPositionNear({x, y}, 1.0F);
				return onGround.value_or(glm::vec2{x, y});
			};

			// Wire up ActionSystem to drop non-backpackable items on the ground as packaged.
			// These callbacks fire from inside the ActionSystem view loop, so they only ENQUEUE;
			// the real spawnEntity runs at the pending-spawn drain after ecsWorld->update() (see
			// m_pendingSpawns) to avoid reallocating component pools out from under the live view.
			actionSystem.setDropItemCallback([this, snapDropToGround](const std::string& defName, float x, float y) {
				const glm::vec2 at = snapDropToGround(x, y);
				m_pendingSpawns.push_back({defName, at.x, at.y, 0, /*packaged=*/true, std::nullopt});
			});

			// Crafted FURNITURE comes out PACKAGED and auto-installs a short distance off the
			// station via the existing PlacePackaged flow (it must not seat in the colonist's
			// hands or stack on the station). Resolve a walkable spot >= 1 m away; the aim rotates
			// by the golden angle each spawn so a queued run of boxes FANS OUT instead of stacking
			// on one target. If no mesh covers the station (findValidPositionNear -> nullopt), fall
			// back to the manual packaged drop (spawn beside the station, await a [Place] click) so
			// crafting never hard-fails. ENQUEUE only -- fires from inside the ActionSystem view loop.
			actionSystem.setSpawnPackagedAtCallback([this, &navSystem](const std::string& defName, glm::vec2 stationPos) {
				constexpr float kGoldenAngle = 2.39996323F; // ~137.5 deg, spreads successive aims evenly
				const float		angle		 = static_cast<float>(m_packagedSpawnSeq++) * kGoldenAngle;
				const glm::vec2 aim{std::cos(angle), std::sin(angle)};
				const std::optional<glm::vec2> target = navSystem.findValidPositionNear(stationPos, 1.0F, aim);
				if (target.has_value()) {
					m_pendingSpawns.push_back({defName, target->x, target->y, 0, /*packaged=*/true, target});
				} else {
					// No mesh at the station: fall back to the player-driven packaged drop, nudged
					// ~2 m off the station (no mesh to snap to) so the box doesn't stack on it.
					m_pendingSpawns.push_back({defName, stationPos.x + 2.0F, stationPos.y, 0, /*packaged=*/true, std::nullopt});
				}
			});

			// Drop a loose, haulable resource pile (felling remainder, or a cancelled build site's
			// staged materials). Unlike the crafting drop above, this pile is NOT packaged: a
			// per-entity ResourceStack holds the count and it is immediately haulable. Both the
			// ActionSystem fell-remainder drop and the ConstructionSystem cancelled-site drop fire
			// from inside their system's view loop, so both ENQUEUE; the drain runs dropResourcePiles
			// (the single pile-split spawn path) after ecsWorld->update().
			auto enqueueResourceDrop = [this, snapDropToGround](const std::string& defName, float x, float y, uint32_t quantity) {
				const glm::vec2 at = snapDropToGround(x, y);
				m_pendingSpawns.push_back({defName, at.x, at.y, quantity, /*packaged=*/false});
			};
			actionSystem.setDropResourceCallback(enqueueResourceDrop);
			constructionSystem.setDropResourceCallback(enqueueResourceDrop);

			// Wire up ActionSystem to remove harvested entities (destructive harvest)
			actionSystem.setRemoveEntityCallback([this](const std::string& defName, float x, float y) {
				auto coord = engine::world::worldToChunk({x, y});
				bool removedFromIndex = m_placementExecutor->removeEntity(coord, {x, y}, defName);

				// World-gen flora live only in the placement index (above). Runtime-spawned
				// harvestables (dev spawns, future saplings) are ECS entities that VisionSystem
				// Pass 2 sees by Appearance; the index removal misses them, so a depleted one
				// would linger and get re-discovered forever. Find the matching ECS entity and
				// queue it for the deferred drain (never destroy mid-view-iteration).
				bool			queuedEcs = false;
				constexpr float kMatchEps = 0.25F;
				for (auto [ent, entPos, appearance] : ecsWorld->view<ecs::Position, ecs::Appearance>()) {
					if (appearance.defName != defName) {
						continue;
					}
					const float ddx = entPos.value.x - x;
					const float ddy = entPos.value.y - y;
					if (ddx * ddx + ddy * ddy <= kMatchEps * kMatchEps) {
						m_pendingEntityRemoval.push_back(ent);
						queuedEcs = true;
						break;
					}
				}

				if (!removedFromIndex && !queuedEcs) {
					LOG_WARNING(Game, "Failed to remove harvested entity %s at (%.1f, %.1f)", defName.c_str(), x, y);
				}
			});

			// Wire up ActionSystem to remove a specific entity by id (a loose pile drained to
			// zero). The system already holds the exact entity, so there is no position scan to
			// alias between two same-material piles. Queue for the deferred drain like the others.
			actionSystem.setRemoveEntityByIdCallback([this](ecs::EntityID entity) {
				m_pendingEntityRemoval.push_back(entity);
			});

			// Wire up ActionSystem to set cooldown on harvested entities (regrowth)
			actionSystem.setEntityCooldownCallback([this](const std::string& defName, float x, float y, float cooldownSeconds) {
				auto coord = engine::world::worldToChunk({x, y});
				m_placementExecutor->setEntityCooldown(coord, {x, y}, defName, cooldownSeconds);
			});

			// Wire up ActionSystem to withdraw from harvestable resource pools. Returns how much
			// was actually taken plus whether the pool is now empty (so the tree gets removed).
			actionSystem.setDecrementResourceCallback(
				[this](const std::string& defName, float x, float y, uint32_t requested) -> ecs::ActionSystem::ResourceDraw {
					auto		   coord = engine::world::worldToChunk({x, y});
					const uint32_t removed = m_placementExecutor->decrementResourceCount(coord, {x, y}, defName, requested);
					const bool	   depleted = !m_placementExecutor->getResourceCount(coord, {x, y}, defName).has_value();
					return {removed, depleted};
				}
			);

			// The drop point picked in GameLoadingScene comes from the 2D river-channel
			// model, which can disagree with the runtime tile classification the navmesh
			// reads -- and, untouched, it can sit on a navmesh triangle BOUNDARY EDGE (off
			// the mesh by a few cm, which locateTriangle rejects) with trees/rocks placed
			// right on top of it. Either strands the colonist and makes the off-mesh
			// recovery snap fire at spawn. prepareLandingClearing() fixes it at the source:
			// snap to a walkable tile CENTER (solidly inside a walkable face, not on an
			// edge), remove the placed entities in a radius so there is an open clearing,
			// and return that as the guaranteed-clear, on-mesh spawn point.
			m_spawnPosition = prepareLandingClearing(m_spawnPosition);

			// The clearing center is the colony's home anchor. Store it once here, in the one
			// place that owns colony state for the session; every other consumer reads it from
			// m_colony.originPosition rather than keeping its own copy.
			m_colony.originPosition = m_spawnPosition;
			LOG_INFO(Game, "Colony origin set to (%.2f, %.2f)", m_colony.originPosition.x, m_colony.originPosition.y);

			// Keep the camera (set from the spawn in GameLoadingScene) in sync with the clearing.
			m_camera->setPosition({m_spawnPosition.x, m_spawnPosition.y});

			// Hand the colony origin to the AI: if a stranded colonist ever finds no walkable
			// navmesh face in range, the off-mesh recovery snaps it back to home. The engine
			// can't see app-level GameWorldState, so the origin is pushed across the boundary;
			// the AI holds it as a read of this single store, not an independently computed copy.
			ecsWorld->getSystem<ecs::AIDecisionSystem>().setColonyOrigin(m_colony.originPosition);

			// Spawn the initial colonist in the middle of the cleared, on-mesh clearing.
			spawnColonist(m_spawnPosition, "Bob");

			LOG_INFO(Game, "ECS initialized with 1 colonist");
		}

		/// Test whether a world-meter position sits on a water tile, using the SAME
		/// classification the navmesh reads (NavInputBuilder::extractWaterObstacles):
		/// a tile is water iff its surface is Water or its primary biome is water.
		/// An unloaded containing chunk is treated as water so callers keep searching
		/// for known-good land rather than dropping onto unknown ground.
		[[nodiscard]] bool isWaterAt(glm::vec2 worldMeters) const {
			const engine::world::WorldPosition pos{worldMeters.x, worldMeters.y};
			const engine::world::ChunkCoordinate coord = engine::world::worldToChunk(pos);
			const engine::world::Chunk*			chunk = m_chunkManager->getChunk(coord);
			if (chunk == nullptr || !chunk->isReady()) {
				return true;
			}
			const auto [localX, localY] = engine::world::worldToLocalTile(pos);
			const engine::world::TileData& tile = chunk->getTile(localX, localY);
			return tile.surface == engine::world::Surface::Water || engine::world::isWater(tile.primaryBiome);
		}

		/// Return the input position if it is already non-water; otherwise ring-search
		/// outward in tile-sized steps (up to ~64 m) and return the nearest non-water
		/// tile center. Falls back to the input unchanged when nothing walkable is
		/// found in range, so the caller can decide how to degrade.
		[[nodiscard]] glm::vec2 nearestWalkable(glm::vec2 worldMeters) const {
			if (!isWaterAt(worldMeters)) {
				return worldMeters;
			}

			constexpr float kStep	   = engine::world::kTileSize; // meters per tile
			constexpr int	kMaxRings  = 64;					  // ~64 m search radius
			for (int ring = 1; ring <= kMaxRings; ++ring) {
				glm::vec2 best{0.0F, 0.0F};
				float	  bestDistSq = std::numeric_limits<float>::max();
				bool	  found		 = false;
				for (int dy = -ring; dy <= ring; ++dy) {
					for (int dx = -ring; dx <= ring; ++dx) {
						// Only the perimeter of this ring; interior was covered earlier.
						if (std::abs(dx) != ring && std::abs(dy) != ring) {
							continue;
						}
						const glm::vec2 candidate{worldMeters.x + static_cast<float>(dx) * kStep,
												  worldMeters.y + static_cast<float>(dy) * kStep};
						if (isWaterAt(candidate)) {
							continue;
						}
						const float ddx	   = candidate.x - worldMeters.x;
						const float ddy	   = candidate.y - worldMeters.y;
						const float distSq = ddx * ddx + ddy * ddy;
						if (distSq < bestDistSq) {
							bestDistSq = distSq;
							best	   = candidate;
							found	   = true;
						}
					}
				}
				if (found) {
					return best;
				}
			}
			return worldMeters;
		}

		/// Radius (world meters) of the open clearing carved at the colonist's landing spot.
		/// Generous next to the 0.3 m agent radius so the spawn tile and its immediate
		/// neighbourhood are obstacle-free and the navmesh triangulates open walkable ground there.
		static constexpr float kLandingClearRadius = 2.5F;

		/// Turn the raw drop point into a guaranteed-clear, on-mesh spawn:
		///  1. snap to the nearest WALKABLE (non-water) tile, then to that tile's CENTER -- a tile
		///     center sits solidly inside a walkable navmesh face, never on the boundary edge that
		///     locateTriangle rejects (which is what made the off-mesh snap fire at spawn);
		///  2. clear the placed entities (trees/rocks) within kLandingClearRadius so nothing is
		///     triangulated as an obstacle on top of the spawn -- an open clearing.
		/// Returns the clearing center. Runs at ECS init, BEFORE the first navmesh build, so the
		/// removed obstacles never enter the mesh input and the colonist drops onto open ground.
		[[nodiscard]] glm::vec2 prepareLandingClearing(glm::vec2 rawSpawn) {
			// Step 1: nearest walkable land, then its tile center. kTileSize is 1 m, so flooring to
			// the tile and adding half a tile lands dead-center -- away from every tile/triangle edge.
			const glm::vec2 walkable = nearestWalkable(rawSpawn);
			const glm::vec2 center{std::floor(walkable.x) + 0.5F * engine::world::kTileSize,
								   std::floor(walkable.y) + 0.5F * engine::world::kTileSize};
			if (center != rawSpawn) {
				LOG_INFO(Game, "Landing: drop (%.1f, %.1f) -> walkable tile center (%.2f, %.2f)",
						 rawSpawn.x, rawSpawn.y, center.x, center.y);
			}

			// Step 2: carve the clearing.
			clearLandingArea(center, kLandingClearRadius);
			return center;
		}

		/// Remove every placed entity (trees, rocks, ...) within `radius` of `center` so the spawn
		/// drops into an open clearing. Entities near the radius can straddle a chunk boundary, so we
		/// gather from every chunk the clearing circle overlaps, not just the spawn chunk. After
		/// removing from the placement index (the same source the navmesh and renderer read), release
		/// each touched chunk's baked render mesh so it re-bakes WITHOUT the removed entities.
		void clearLandingArea(glm::vec2 center, float radius) {
			// Tile/meter are 1:1 (kTileSize == 1), so the spatial-index radius is the meter radius.
			std::unordered_set<engine::world::ChunkCoordinate> touchedChunks;
			const engine::world::ChunkCoordinate cMin =
				engine::world::worldToChunk({center.x - radius, center.y - radius});
			const engine::world::ChunkCoordinate cMax =
				engine::world::worldToChunk({center.x + radius, center.y + radius});

			size_t removed = 0;
			for (int32_t cy = cMin.y; cy <= cMax.y; ++cy) {
				for (int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
					const engine::world::ChunkCoordinate coord{cx, cy};
					const auto* index = m_placementExecutor->getChunkIndex(coord);
					if (index == nullptr) {
						continue;
					}
					// Copy out the (defName, position) of in-radius entities first: removeEntity
					// mutates the index, which would invalidate the queryRadius pointers mid-loop.
					std::vector<std::pair<std::string, glm::vec2>> victims;
					for (const auto* e : index->queryRadius(center, radius)) {
						victims.emplace_back(e->defName, e->position);
					}
					for (const auto& [defName, pos] : victims) {
						if (m_placementExecutor->removeEntity(coord, pos, defName)) {
							++removed;
						}
					}
					if (!victims.empty()) {
						touchedChunks.insert(coord);
					}
				}
			}

			// Re-bake the touched chunks so the cleared entities also disappear visually. The
			// renderer re-bakes any processed chunk missing from its cache on the next render.
			for (const engine::world::ChunkCoordinate& coord : touchedChunks) {
				m_entityRenderer->releaseBakedChunkCache(coord);
			}

			LOG_INFO(Game, "Landing: cleared %zu ent(s) within %.1f m of (%.2f, %.2f) across %zu chunk(s)",
					 removed, radius, center.x, center.y, touchedChunks.size());
		}

		/// Spawn a new colonist entity at the given position.
		ecs::EntityID spawnColonist(glm::vec2 newPosition, const std::string& newName) {
			auto entity = ecsWorld->createEntity();

			ecsWorld->addComponent<ecs::Position>(entity, ecs::Position{newPosition});
			ecsWorld->addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
			ecsWorld->addComponent<ecs::Velocity>(entity, ecs::Velocity{{0.0F, 0.0F}});
			ecsWorld->addComponent<ecs::MovementTarget>(entity, ecs::MovementTarget{{0.0F, 0.0F}, 2.0F, false});
			ecsWorld->addComponent<ecs::FacingDirection>(entity, ecs::FacingDirection{}); // Default: Down
			ecsWorld->addComponent<ecs::AnimationState>(entity, ecs::AnimationState{});	 // Walk-cycle phase
			ecsWorld->addComponent<ecs::Appearance>(entity, ecs::Appearance{"Colonist", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			ecsWorld->addComponent<ecs::Colonist>(entity, ecs::Colonist{newName});
			ecsWorld->addComponent<ecs::NeedsComponent>(entity, ecs::NeedsComponent::createDefault());

			// Roll static attributes (0-20), seeded per-entity so colonists vary
			// deterministically. Only Strength is consumed today: it sets hand-carry
			// capacity. The other attributes are placeholders for upcoming systems.
			ecs::Attributes attributes;
			std::mt19937   attrRng(static_cast<uint32_t>(entity));
			attributes.strength = std::uniform_real_distribution<float>(6.0F, 14.0F)(attrRng);
			ecsWorld->addComponent<ecs::Attributes>(entity, attributes);

			// Carry weight scales with Strength (an average colonist lands at the prior 35kg).
			auto inventory = ecs::Inventory::createForColonist();
			inventory.carryCapacityKg = ecs::Attributes::carryCapacityKg(attributes.strength);
			ecsWorld->addComponent<ecs::Inventory>(entity, std::move(inventory));
			ecsWorld->addComponent<ecs::Knowledge>(entity, ecs::Knowledge{});
			ecsWorld->addComponent<ecs::Memory>(entity, ecs::Memory{.owner = entity});
			ecsWorld->addComponent<ecs::Task>(entity, ecs::Task{});
			ecsWorld->addComponent<ecs::DecisionTrace>(entity, ecs::DecisionTrace{});
			ecsWorld->addComponent<ecs::Action>(entity, ecs::Action{});
			ecsWorld->addComponent<ecs::AgentRadius>(entity, ecs::AgentRadius{});

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

		/// Spawn a loose, haulable resource pile of `quantity` x `defName` at (x, y). A pile is capped
		/// at the item's own stackSize, so an over-cap drop splits into several piles scattered to
		/// distinct spots (within the 0.25 m pickup epsilon two piles would alias). No def / no
		/// itemProperties means unbounded -> a single pile. Each pile is a ResourceStack entity (not
		/// Packaged): immediately haulable. The SINGLE ground-pile drop path; ActionSystem (felling
		/// remainder) and ConstructionSystem (cancelled-site materials) both route through it.
		void dropResourcePiles(const std::string& defName, float x, float y, uint32_t quantity) {
			const auto*	   def = engine::assets::AssetRegistry::Get().getDefinition(defName);
			const uint32_t cap =
				(def != nullptr && def->itemProperties.has_value()) ? def->itemProperties->stackSize : UINT32_MAX;
			const auto piles = ecs::resourcePileDrops(cap, quantity, {x, y});
			for (const auto& [pos, qty] : piles) {
				auto entity = m_placementSystem->spawnEntity(defName, {pos.x, pos.y});
				ecsWorld->addComponent<ecs::ResourceStack>(entity, ecs::ResourceStack{qty});
			}
			LOG_INFO(Game, "Dropped %u x '%s' as %zu stacks at (%.1f, %.1f)", quantity, defName.c_str(), piles.size(), x, y);
		}

		/// Mark a structure's ECS blueprint for deconstruction. ConstructionSystem then emits a
		/// Deconstruct goal a colonist works down (or, for a not-yet-built site, removes it
		/// immediately and dumps its staged materials), and the deconstructed-completion callback
		/// does the topology removal. Returns false if the entity has no blueprint.
		bool markForDemolition(ecs::EntityID entity) {
			if (entity == ecs::kInvalidEntity) {
				return false;
			}
			auto* blueprint = ecsWorld->getComponent<ecs::StructureBlueprint>(entity);
			if (blueprint == nullptr) {
				return false;
			}
			blueprint->demolishing = true;
			return true;
		}

		/// Set the rooms-overlay active state and reflect it on the GameplayBar toggle
		/// button. The single entry point for the R hotkey and the button, so the two
		/// can never drift out of sync.
		void setRoomOverlayActive(bool active) {
			m_roomOverlay->setActive(active);
			if (gameUI) {
				gameUI->setRoomsOverlayActive(active);
			}
			LOG_INFO(Game, "Rooms overlay %s", active ? "ON" : "OFF");
		}

		/// Set the nav debug overlay active state. Plain hotkey toggle (N); no
		/// GameplayBar button mirror.
		void setNavOverlayActive(bool active) {
			m_navOverlay->setActive(active);
			LOG_INFO(Game, "Nav overlay %s", active ? "ON" : "OFF");
		}

		/// Set the vision debug overlay active state. Plain hotkey toggle (V).
		void setVisionOverlayActive(bool active) {
			m_visionOverlay->setActive(active);
			LOG_INFO(Game, "Vision overlay %s", active ? "ON" : "OFF");
		}

		/// Handle Demolish request from a foundation's info panel. Marks the foundation for
		/// deconstruction; a colonist tears it down over time (work-driven), and the
		/// deconstructed-completion callback removes the topology.
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

			// Walls block plain foundation demolition: removing the foundation alone would
			// orphan a hosted wall. The panel hides this button while walls stand and offers
			// Demolish building instead; this is the defensive guard for any other caller.
			if (constructionWorld.foundationHasWalls(foundationSel->id)) {
				LOG_WARNING(
					Game, "Cannot demolish foundation #%llu: walls still stand", static_cast<unsigned long long>(foundationSel->id)
				);
				gameUI->pushNotification("Walls still stand", "Use Demolish building", UI::ToastSeverity::Warning);
				return;
			}

			if (!markForDemolition(foundation->entity)) {
				LOG_WARNING(Game, "Cannot demolish foundation #%llu: no blueprint", static_cast<unsigned long long>(foundationSel->id));
				return;
			}

			LOG_INFO(Game, "Marked foundation #%llu for demolition", static_cast<unsigned long long>(foundationSel->id));
			gameUI->pushNotification("Marked for demolition", "Foundation", UI::ToastSeverity::Info);
			m_selectionSystem->clearSelection();
		}

		/// Handle Demolish building (the cascade) from a built foundation's panel: mark the
		/// foundation, every wall hosted on it, and every opening on those walls for
		/// deconstruction. ConstructionSystem's cascade gate then orders the actual teardown
		/// (openings -> walls -> foundation): each tier's Deconstruct goal stays Blocked until the
		/// tier it depends on is removed. No topology removal happens here; the
		/// deconstructed-completion callback owns that (single removal path).
		void handleDemolishBuilding() {
			const auto& sel = m_selectionSystem->current();
			auto*		foundationSel = std::get_if<world_sim::FoundationSelection>(&sel);
			if (foundationSel == nullptr) {
				LOG_WARNING(Game, "Cannot demolish building: no foundation selected");
				return;
			}

			auto&		constructionWorld = m_drawingSystem->world();
			const auto* foundation = constructionWorld.get(foundationSel->id);
			if (foundation == nullptr) {
				LOG_WARNING(
					Game, "Cannot demolish building: foundation #%llu not found", static_cast<unsigned long long>(foundationSel->id)
				);
				m_selectionSystem->clearSelection();
				return;
			}

			// Mark every opening on every wall, then every wall, then the foundation. Marking
			// order is immaterial (the cascade gate orders the teardown). Build the set of hosted
			// wall ids first, then walk the global openings list ONCE marking any opening whose
			// host segment is in that set: O(walls + openings), not O(walls * openings).
			const std::vector<engine::construction::SegmentId> segIds = constructionWorld.segmentsOnFoundation(foundationSel->id);
			std::unordered_set<engine::construction::SegmentId> wallIds(segIds.begin(), segIds.end());

			for (const engine::construction::SegmentId segId : segIds) {
				const auto* segment = constructionWorld.getSegment(segId);
				if (segment != nullptr && !markForDemolition(segment->entity)) {
					// A failed mark leaves the cascade gate blocked with no feedback; warn and keep
					// going so the rest of the building still gets marked.
					LOG_WARNING(Game, "Demolish building: wall segment #%llu has no blueprint", static_cast<unsigned long long>(segId));
				}
			}

			size_t markedOpenings = 0;
			for (const auto& opening : constructionWorld.openings()) {
				if (wallIds.count(opening.segment) == 0) {
					continue;
				}
				if (markForDemolition(opening.entity)) {
					markedOpenings++;
				} else {
					// A failed opening mark leaves its wall's deconstruct gate blocked; warn so the
					// stuck cascade is diagnosable instead of silently stalling.
					LOG_WARNING(Game, "Demolish building: opening #%llu has no blueprint", static_cast<unsigned long long>(opening.id));
				}
			}

			if (!markForDemolition(foundation->entity)) {
				LOG_WARNING(
					Game, "Demolish building: foundation #%llu has no blueprint", static_cast<unsigned long long>(foundationSel->id)
				);
			}

			LOG_INFO(
				Game,
				"Marked building for demolition: foundation #%llu, %zu wall(s), %zu opening(s)",
				static_cast<unsigned long long>(foundationSel->id),
				segIds.size(),
				markedOpenings
			);
			gameUI->pushNotification("Building marked for demolition", "Foundation, walls, and openings", UI::ToastSeverity::Info);
			m_selectionSystem->clearSelection();
		}

		/// Handle Demolish request from a wall segment's info panel. The SEGMENT, not the chain,
		/// is the demolition unit (design D6): a multi-segment wall keeps its other segments.
		/// Marks the segment for deconstruction; ConstructionSystem gates it behind any openings
		/// on it (those must deconstruct first), and the deconstructed-completion callback removes
		/// the topology (surfacing any opening entities).
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

			// Mark this segment's openings too, so the cascade can tear them down first; a wall's
			// Deconstruct goal stays Blocked while an opening sits on it. A failed opening mark would
			// leave the wall's gate stuck forever, so warn (and keep going).
			for (const auto& opening : constructionWorld.openings()) {
				if (opening.segment == wallSel->id && !markForDemolition(opening.entity)) {
					LOG_WARNING(
						Game,
						"Demolish wall segment #%llu: opening #%llu has no blueprint",
						static_cast<unsigned long long>(wallSel->id),
						static_cast<unsigned long long>(opening.id)
					);
				}
			}
			if (!markForDemolition(segment->entity)) {
				LOG_WARNING(Game, "Cannot demolish wall segment #%llu: no blueprint", static_cast<unsigned long long>(wallSel->id));
				return;
			}

			LOG_INFO(Game, "Marked wall segment #%llu for demolition", static_cast<unsigned long long>(wallSel->id));
			gameUI->pushNotification("Marked for demolition", "Wall segment", UI::ToastSeverity::Info);
			m_selectionSystem->clearSelection();
		}

		/// Handle Demolish request from an opening's info panel. The opening is its own demolition
		/// unit (independent of the host wall). Marks it for deconstruction; the
		/// deconstructed-completion callback removes the topology.
		void handleDemolishOpening() {
			const auto& sel = m_selectionSystem->current();
			auto*		openingSel = std::get_if<world_sim::OpeningSelection>(&sel);
			if (openingSel == nullptr) {
				LOG_WARNING(Game, "Cannot demolish: no opening selected");
				return;
			}

			auto&		constructionWorld = m_drawingSystem->world();
			const auto* opening = constructionWorld.getOpening(openingSel->id);
			if (opening == nullptr) {
				LOG_WARNING(Game, "Cannot demolish: opening #%llu not found", static_cast<unsigned long long>(openingSel->id));
				m_selectionSystem->clearSelection();
				return;
			}

			if (!markForDemolition(opening->entity)) {
				LOG_WARNING(Game, "Cannot demolish opening #%llu: no blueprint", static_cast<unsigned long long>(openingSel->id));
				return;
			}

			LOG_INFO(Game, "Marked opening #%llu for demolition", static_cast<unsigned long long>(openingSel->id));
			gameUI->pushNotification("Marked for demolition", "Opening", UI::ToastSeverity::Info);
			m_selectionSystem->clearSelection();
		}

		std::unique_ptr<engine::world::ChunkManager>	   m_chunkManager;
		std::unique_ptr<engine::world::WorldCamera>		   m_camera;
		glm::vec2										   m_spawnPosition{0.0F, 0.0F};
		// Planet forwarded from GameWorldState for the /api/state?what=landing readback.
		// Null when running in mock-world mode (no preloaded planet).
		std::shared_ptr<const worldgen::GeneratedWorld>    m_planet;
		double                                             m_landingLatDeg = 0.0;
		double                                             m_landingLonDeg = 0.0;
		// The colony: home anchor for the session. Single source of truth for the colony
		// origin (the cleared, on-mesh clearing center), set once at landing. Read by the
		// camera-home sync and pushed to AIDecisionSystem for the off-mesh recovery snap.
		ecs::Colony										   m_colony;
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
		std::unique_ptr<world_sim::RoomOverlay>		m_roomOverlay;
		std::unique_ptr<world_sim::NavOverlay>		m_navOverlay;
		std::unique_ptr<world_sim::VisionOverlay>	m_visionOverlay;

		// Dev/test command + state-readback surface (/api/dev, /api/state). Dev-only;
		// constructed after the systems above exist.
		std::unique_ptr<world_sim::DevCommandHandler> m_devHandler;

		// Entities queued for destruction, drained after ecsWorld->update() so we
		// never destroyEntity mid-view-iteration (deconstruct callback, demolish).
		std::vector<ecs::EntityID> m_pendingEntityRemoval;

		// Drops requested from inside the ActionSystem view loop (a colonist stows a held tool to
		// free its hands, or a fell drops its uncarried remainder). spawnEntity grows the component
		// pools, which would reallocate and dangle the live view's bound refs -- so the drop
		// callbacks ENQUEUE here and the real spawn happens at the drain below, after
		// ecsWorld->update(), next to the removal drain. `packaged` items spawn one entity with a
		// Packaged tag; resource piles route through dropResourcePiles (stack-capped split).
		struct PendingSpawn {
			std::string defName;
			float		x = 0.0F;
			float		y = 0.0F;
			uint32_t	quantity = 0; // resource-pile count; unused for packaged items
			bool		packaged = false;
			// Auto-place target for a packaged spawn (a crafted furniture box). When set, the
			// entity spawns AT this point with Packaged.targetPosition wired, so BuildGoalSystem
			// raises the place goal immediately (no manual [Place]). nullopt -> the player-driven
			// path (spawn in place, await a [Place] click) for a manually dropped packaged item.
			std::optional<glm::vec2> targetPosition;
		};
		std::vector<PendingSpawn> m_pendingSpawns;

		// Monotonic counter rotating the placement aim for crafted furniture so a run of boxes
		// from one station fans out (golden-angle stepped) instead of stacking on a single spot.
		std::uint32_t m_packagedSpawnSeq = 0;
	};

} // namespace

namespace world_sim::scenes {
	extern const world_sim::SceneInfo Game = {kSceneName, []() { return std::make_unique<GameScene>(); }};
} // namespace world_sim::scenes
