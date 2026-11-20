// Game Loading Scene - Pre-loads world chunks and entities with progress bar
// Prevents asset "pop-in" by ensuring all initial content is ready before gameplay.
//
// Always plays on a generated planet: the pending GameStartConfig either carries
// a world from the creator flow, or (Quick Start / direct scene jump) the scene
// loads the prebuilt quickstart planet shipped with the game (built by the
// `quickstart-planet` CMake target). It is never generated at runtime.

#include "GameBootstrap.h"
#include "GameStartConfig.h"
#include "GameWorldState.h"
#include "SceneTypes.h"
#include "UserSettings.h"
#include "scenes/game/dev/DevJson.h"
#include "scenes/shared/LandingSiteSurvey.h"
#include "scenes/shared/LoadFailureView.h"
#include "scenes/shared/SalalUi.h"
#include "scenes/shared/Starfield.h"
#include <theme/Theme.h>

#include <application/App.h>
#include <assets/AssetRegistry.h>
#include <assets/WorkConfig.h>
#include <assets/placement/AsyncChunkProcessor.h>
#include <assets/placement/BiomeProvisions.h>
#include <assets/placement/CanopyCover.h>
#include <assets/placement/PlacementExecutor.h>
#include <ecs/ColonistRules.h>
#include <ecs/GoalTaskRegistry.h>
#include <graphics/Color.h>
#include <primitives/Primitives.h>
#include <theme/Keys.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkCoordinate.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/GeneratedWorldSampler.h>
#include <world/rendering/ChunkRenderer.h>
#include <world/rendering/EntityRenderer.h>
#include <world/rendering/GroundTileBank.h>

#include <worldgen/data/Biome.h>
#include <worldgen/io/PlanetIO.h>
#include <worldgen/sampling/LandingSite.h>

#include <ui/components/ProgressBar.h>
#include <ui/shapes/Text.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

namespace {

	constexpr const char* kSceneName = "gameloading";
	constexpr float		  kPixelsPerMeter = 8.0F;
	constexpr int		  kTargetChunks = 9; // 3×3 grid (center + 8 adjacent)

	namespace tokens = world_sim::tokens;
	namespace family = ui::tokens::font::family;

	/// Loading phases
	enum class LoadingPhase { BakingGround, PreparingPlanet, ChoosingSite, Initializing, Failed, LoadingChunks, PlacingEntities, Complete, Cancelling };

	class GameLoadingScene : public app::Scene {
	  public:
		void onEnter(app::App& app) override {
			owner = &app;
			LOG_INFO(Game, "GameLoadingScene - Entering");
			theme::apply(ui());
			build();

			phase = LoadingPhase::BakingGround;
			progress = 0.0F;
			chunksLoaded = 0;
			chunksProcessed = 0;
			failureMessages.clear();
			asyncProcessor.reset();

			// Direct scene jumps (debug API, --scene=game) have no pending
			// config; treat them as Quick Start.
			startConfig = world_sim::GameStartConfig::Take();
			if (!startConfig) {
				startConfig = std::make_unique<world_sim::GameStartConfig>();
				startConfig->source = world_sim::GameStartConfig::Source::QuickStart;
			}

			// Create the world state that will be transferred to GameScene
			worldState = std::make_unique<world_sim::GameWorldState>();
			statusMessage = "Initializing...";
		}

		void update(const app::FrameContext& frame) override {
			// Check for ESC to cancel loading or return from error
			if (theme::key(frame.input, foundation::Key::Escape).pressed()) {
				if (phase == LoadingPhase::Failed) {
					LOG_INFO(Game, "GameLoadingScene - Returning to menu from a failed load");
					owner->scenes().switchTo(world_sim::scenes::MainMenu.name);
					return;
				}
				if (phase != LoadingPhase::Cancelling && phase != LoadingPhase::Complete) {
					LOG_INFO(Game, "GameLoadingScene - Cancel requested");
					phase = LoadingPhase::Cancelling;
					updateStatusText("Cancelling...");
				}
			}

			switch (phase) {
				case LoadingPhase::BakingGround:
					bakeGround();
					break;

				case LoadingPhase::PreparingPlanet:
					preparePlanet();
					break;

				case LoadingPhase::ChoosingSite:
					chooseSite();
					break;

				case LoadingPhase::Initializing:
					initializeWorldSystems();
					break;

				case LoadingPhase::Failed:
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
			showProgress();
		}

		void render(float /*alpha*/) override {
			const ui::Theme& theme = ui().getTheme();
			Renderer::Primitives::clear(theme.get(tokens::color::bgVoid));
			const float screenW = Renderer::Primitives::PercentWidth(100.0F);
			const float screenH = Renderer::Primitives::PercentHeight(100.0F);
			world_sim::renderStarfield(theme, static_cast<int>(screenW), static_cast<int>(screenH), 5U, true);
		}

		void onExit() override {
			LOG_INFO(Game, "GameLoadingScene - Exiting");
			siteSurvey.cancel();
			asyncProcessor.reset();
			// Note: worldState is moved to GameWorldState::SetPending() before exit
		}

		std::string exportState() const override {
			std::ostringstream oss;
			oss << R"({"scene":"gameloading","progress":)" << progress << R"(,"failed":)"
				<< (phase == LoadingPhase::Failed ? "true" : "false") << R"(,"errors":)" << world_sim::jsonStrings(failureMessages) << "}";
			return oss.str();
		}

		const char* name() const override { return kSceneName; }

	  private:
		app::App* owner = nullptr;

		// In the splash's idiom, centred: a mono kicker over a display title, then the status line and
		// its percentage over the bar, which a failed load replaces with the failure view.
		void build() {
			const ui::Theme& theme = ui().getTheme();
			const ui::Style	 mono = world_sim::text(
				 theme,
				 {.family = family::mono,
				  .size = tokens::fontSize::fsXs,
				  .color = tokens::color::text,
				  .tracking = tokens::letterSpacing::lsWide,
				  .transform = Foundation::TextTransform::Uppercase}
			 );
			column = root().addChild<ui::Component>({
				.id = "loading",
				.size = {ui::percent(100.0F), ui::percent(100.0F)},
				.layout = {
					.display = ui::Display::Flex,
					.flexDirection = ui::FlexDirection::Column,
					.gap = theme.get(tokens::spacing::space3),
					.alignItems = ui::AlignItems::Center,
					.justifyContent = ui::JustifyContent::Center
				},
			});
			column.addChild<ui::Text>({
				.id = "loading_kicker",
				.style = world_sim::text(
					theme,
					{.family = family::mono,
					 .size = tokens::fontSize::fs2xs,
					 .color = tokens::color::accent,
					 .tracking = tokens::letterSpacing::lsWider}
				),
				.text = "// EXPEDITION    DESCENT",
			});
			column.addChild<ui::Text>({
				.id = "loading_title",
				.margin = {0.0F, 0.0F, theme.get(tokens::spacing::space6), 0.0F},
				.style = world_sim::text(
					theme,
					{.family = family::display,
					 .size = tokens::fontSize::fs3xl,
					 .color = tokens::color::textBright,
					 .tracking = tokens::letterSpacing::lsWide}
				),
				.text = "Making Planetfall",
			});
			status = column.addChild<ui::Component>({
				.id = "loading_status",
				.size = {420.0F, ui::kAuto},
				.layout = {.display = ui::Display::Flex, .justifyContent = ui::JustifyContent::SpaceBetween},
			});
			statusText = status.addChild<ui::Text>({.id = "loading_message", .style = mono, .text = statusMessage});
			ui::Style percent = mono;
			percent.color = theme.get(tokens::color::accentBright);
			percentText = status.addChild<ui::Text>({.id = "loading_percent", .style = percent, .text = "0%"});
			bar = column.addChild<ui::ProgressBar>(
				{.id = "loading_bar", .size = {420.0F, ui::kAuto}, .value = 0.0F, .tone = ui::Tone::Accent}
			);
		}

		// The status line, its percentage and the bar from the phase's progress.
		void showProgress() {
			if (ui::Text* line = statusText.get()) {
				if (line->getText() != statusMessage) {
					line->setText(statusMessage);
				}
			}
			if (ui::Text* shown = percentText.get()) {
				shown->setText(std::to_string(static_cast<int>(progress * 100.0F)) + "%");
			}
			if (ui::ProgressBar* meter = bar.get()) {
				meter->setValue(std::clamp(progress, 0.0F, 1.0F));
			}
		}

		// Every way a load fails ends here: the messages are logged, kept for the state view, and shown in place
		// of the status line and the bar until Esc returns to the menu.
		void fail(std::vector<std::string> messages) {
			world_sim::logLoadFailure("GameLoadingScene", messages);
			failureMessages = std::move(messages);
			phase = LoadingPhase::Failed;
			if (ui::Component* row = status.get()) {
				row->setVisible(false);
			}
			if (ui::ProgressBar* meter = bar.get()) {
				meter->setVisible(false);
			}
			column.addChild<world_sim::LoadFailureView>(
				{.id = "loading_failure", .messages = failureMessages, .instruction = "Press Esc to return to the menu"}
			);
		}

		/// Before phase 0: the ground tiles, which the splash bakes; a --scene jump skips it, so they're baked here,
		/// behind the same validation gate, a variant a frame so the bar moves.
		void bakeGround() {
			const world_sim::GroundBakeStep step = world_sim::stepGroundBake();
			if (!step.done) {
				const worldview::GroundTileBank& bank = world_sim::groundTiles();
				progress = 0.25F * static_cast<float>(bank.bakedVariants()) / static_cast<float>(std::max(1, bank.totalVariants()));
				updateStatusText("Baking ground tiles...");
				return;
			}
			if (!step.errors.empty()) {
				fail(step.errors);
				return;
			}
			phase = LoadingPhase::PreparingPlanet;
		}

		/// Phase 0: ensure we have a planet to land on.
		/// Creator flow already provides one; Quick Start loads the prebuilt
		/// planet shipped with the game. It is never generated at runtime — the
		/// main menu disables Quick Start when the file is absent, so a missing
		/// file here is a packaging error, not a recoverable player state.
		void preparePlanet() {
			if (startConfig->world) {
				phase = LoadingPhase::Initializing;
				progress = 0.3F;
				return;
			}

			updateStatusText("Loading planet...");
			auto resolved = Foundation::findResource(world_sim::kQuickstartPlanetResource);
			if (!resolved) {
				// Best-effort: show the primary expected path (exe-relative) so the user
				// knows exactly what's missing, even though findResource searched several roots.
				const std::string expectedPath = (Foundation::getExecutableDir() / world_sim::kQuickstartPlanetResource).string();
				fail(
					{"No pre-generated planet found at '" + expectedPath +
					 "'. Bake it (target quickstart-planet) or junction planets/ from the main checkout."}
				);
				return;
			}

			auto planet = worldgen::loadPlanet(*resolved);
			if (!planet) {
				fail({"The quickstart planet at '" + resolved->string() + "' failed to load; the log names why."});
				return;
			}

			LOG_INFO(Game, "GameLoadingScene - Loaded quickstart planet (n=%u)", planet->params.gridSubdivision);
			progress = 0.3F;
			adoptQuickstartPlanet(std::move(planet));
		}

		// Quick Start lands on the first suggested site with its biome preference, found on the survey's worker.
		void adoptQuickstartPlanet(std::shared_ptr<const worldgen::GeneratedWorld> planet) {
			startConfig->world = std::move(planet);
			siteSurvey.start(
				startConfig->world, assets::biomeProvisionsFromDefinitions(assets::AssetRegistry::Get()), worldgen::quickStartSiteQuery()
			);
			phase = LoadingPhase::ChoosingSite;
			updateStatusText("Choosing a landing site...");
		}

		void chooseSite() {
			std::optional<std::vector<worldgen::LandingSite>> sites = siteSurvey.takeSites();
			if (!sites) {
				return;
			}
			siteSurvey.cancel();
			if (sites->empty()) {
				fail({"The quickstart planet has no landing site with fresh water, food, wood, and a livable climate."});
				return;
			}
			startConfig->landing = sites->front();
			LOG_DEBUG(
				Game,
				"GameLoadingScene - Quickstart landing site lat=%.2f lon=%.2f",
				startConfig->landing.origin.latDeg,
				startConfig->landing.origin.lonDeg
			);
			phase = LoadingPhase::Initializing;
		}

		/// Phase 1: Initialize world systems
		void initializeWorldSystems() {
			LOG_INFO(Game, "GameLoadingScene - Initializing world systems");

			// Load work configuration first. Goals are process-wide too; a new game starts with none.
			updateStatusText("Loading configuration...");
			ecs::GoalTaskRegistry::Get().clear();
			std::vector<std::string> configErrors = assets::loadWorkConfig("assets/config");
			ecs::ColonistRulesLoad colonistRules = ecs::loadColonistRules("assets/config");
			configErrors.insert(configErrors.end(), colonistRules.errors.begin(), colonistRules.errors.end());
			worldState->colonistRules = std::move(colonistRules.rules);
			if (!configErrors.empty()) {
				fail(std::move(configErrors));
				return;
			}
			LOG_INFO(Game, "Configuration loaded successfully");
			// The drawing settings' defaults are the construction config's, loaded just now.
			world_sim::UserSettings::Get().load(owner->settings());

			// Create world sampler and chunk manager from the generated planet;
			// the landing origin maps to the 2D world origin
			const worldgen::LatLon origin = startConfig->landing.origin;
			auto sampler = std::make_unique<world::GeneratedWorldSampler>(startConfig->world, origin.latDeg, origin.lonDeg);
			worldState->planet = startConfig->world;
			worldState->landing = startConfig->landing;
			worldState->party = std::move(startConfig->party);
			worldState->chunkManager =
				std::make_unique<world::ChunkManager>(std::move(sampler), assets::canopyCoverFromDefinitions(assets::AssetRegistry::Get()));

			// Only load 3×3 grid (center + 8 adjacent) - chunks are large!
			worldState->chunkManager->setLoadRadius(1);
			worldState->chunkManager->setUnloadRadius(2);

			// The drop the landing resolution chose, beside its water when it found one.
			const worldgen::SpawnSite& spawn = worldState->landing.spawn;
			LOG_DEBUG(
				Game,
				"GameLoadingScene - Spawn (%.1f, %.1f) water=%d at %.1f m",
				spawn.xMeters,
				spawn.yMeters,
				static_cast<int>(spawn.water),
				static_cast<double>(spawn.waterDistanceMeters)
			);

			// Create camera centered on the spawn point
			worldState->camera = std::make_unique<world::WorldCamera>();
			worldState->camera->setPanSpeed(200.0F);
			worldState->camera->setPosition({static_cast<float>(spawn.xMeters), static_cast<float>(spawn.yMeters)});

			// Create renderers
			worldState->renderer = std::make_unique<worldview::ChunkRenderer>(kPixelsPerMeter, world_sim::groundTiles());
			worldState->entityRenderer = std::make_unique<worldview::EntityRenderer>(kPixelsPerMeter);

			// Initialize placement executor
			auto& assetRegistry = assets::AssetRegistry::Get();
			worldState->placementExecutor = std::make_unique<assets::PlacementExecutor>(assetRegistry);
			worldState->placementExecutor->initialize();

			LOG_INFO(Game, "PlacementExecutor initialized with %zu entity types", worldState->placementExecutor->getSpawnOrder().size());

			// Move to next phase
			phase = LoadingPhase::LoadingChunks;
			updateStatusText("Generating terrain...");
		}

		/// Phase 2: Load chunks (generation runs on workers; keep polling until ready)
		void loadChunks() {
			// ChunkManager::update() starts loads for the grid around the camera
			// and integrates finished generation workers each call. No movers exist
			// yet; the camera sits on the spawn, so the colony's chunk loads here.
			worldState->chunkManager->update(worldState->camera->position(), {});

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
				asyncProcessor = std::make_unique<assets::AsyncChunkProcessor>(*worldState->placementExecutor, worldState->processedChunks);

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
			worldState->chunkManager->update(worldState->camera->position(), {});
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

			updateStatusText("Placing entities...");

			// Check if all tasks are complete (every loaded chunk processed and
			// nothing in flight; chunks still generating haven't launched yet)
			if (!asyncProcessor->hasPending() && worldState->processedChunks.size() >= worldState->chunkManager->loadedChunkCount()) {
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
			owner->scenes().switchTo(world_sim::scenes::Game.name);
		}

		/// Cancel loading - waits for async tasks to complete with UI feedback
		void cancelLoading() {
			siteSurvey.cancel();
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
			owner->scenes().switchTo(world_sim::scenes::MainMenu.name);
		}

		void updateStatusText(const std::string& text) { statusMessage = text; }

		// Loading state
		LoadingPhase			 phase = LoadingPhase::Initializing;
		float					 progress = 0.0F;
		int						 chunksLoaded = 0;
		int						 chunksProcessed = 0;
		std::vector<std::string> failureMessages; // why the load failed, once phase is Failed

		// Finds Quick Start's landing site off the main thread
		world_sim::LandingSiteSurvey siteSurvey;

		// Async chunk processor (shared implementation)
		std::unique_ptr<assets::AsyncChunkProcessor> asyncProcessor;

		// World state being built (transferred to GameScene when complete)
		std::unique_ptr<world_sim::GameWorldState> worldState;

		// How this game starts (planet + landing site)
		std::unique_ptr<world_sim::GameStartConfig> startConfig;

		// Loader status line, shown by the status text
		std::string				 statusMessage;
		ui::Ref<ui::Component>	 column;
		ui::Ref<ui::Component>	 status;
		ui::Ref<ui::Text>		 statusText;
		ui::Ref<ui::Text>		 percentText;
		ui::Ref<ui::ProgressBar> bar;
	};

} // namespace

// Export scene info for registry
namespace world_sim::scenes {
	extern const world_sim::SceneInfo GameLoading = {kSceneName, []() { return std::make_unique<GameLoadingScene>(); }};
} // namespace world_sim::scenes
