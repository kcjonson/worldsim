// The main screens laid out at the window sizes and interface sizes the players have, linted clean, with a scrollbar
// only on what is meant to scroll. The splash and the loading scene aren't here: their updates bake the ground tiles
// and load the planet, which need the GPU and the quickstart planet. The game scene needs both too, so its HUD and
// dialogs are the game's GameUI over a small world of their own.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTesting.test.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/RecipeRegistry.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Needs.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/TimeSystem.h>
#include <theme/Theme.h>
#include <ui/components/TabBar.h>
#include <ui/components/Toast.h>
#include <utils/ResourcePath.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace world_sim::testing {

	namespace {

		constexpr const char* kHudScene = "hud";
		// A quarter second at 60 Hz: past the task models' 0.2 s refresh.
		constexpr int kModelFillFrames = 15;

		// What GameScene shows over the world, fed as GameScene feeds it, over a world of one colonist, a crafting
		// spot, a box and a few structures being torn down. The colonist works on one, carries some gear and
		// remembers what's around, so the dossier's tabs have rows. `open` opens something on it once it's built.
		class HudScene final : public app::Scene {
		  public:
			struct Entities {
				ecs::EntityID colonist{0};
				ecs::EntityID station{0};
				ecs::EntityID box{0};
			};
			using Open = std::function<void(HudScene&)>;

			explicit HudScene(Open open)
				: open(std::move(open)) {}

			[[nodiscard]] const char* name() const override { return kHudScene; }

			[[nodiscard]] GameUI&		  hud() { return *gameUI; }
			[[nodiscard]] const Entities& entities() const { return spawned; }
			[[nodiscard]] DrawingSystem&  drawing() { return *drawingSystem; }

			void onEnter(app::App& /*app*/) override {
				theme::apply(ui());
				gameUI = std::make_unique<GameUI>(GameUI::Args{.root = root()});
				drawingSystem =
					std::make_unique<DrawingSystem>(DrawingSystem::Args{.world = &world, .camera = &camera, .navigation = nullptr});
				gameUI->showCatalogue(assets::RecipeRegistry::Get(), assets::ConstructionRegistry::Get(), drawingSystem->activeMaterial());
				gameUI->setMinimapContext({0.0F, 0.0F}, 45.0, 10.0, 8.0F);

				world.registerSystem<ecs::TimeSystem>();
				spawnColonist();
				placement = std::make_unique<PlacementSystem>(PlacementSystem::Args{
					.world = &world,
					.camera = &camera,
					.callbacks = {
						.onShowBuildMenu = [this](const std::vector<BuildMenuItem>& items) { gameUI->showBuildMenu(items); },
						.onHideBuildMenu = [this] { gameUI->hideBuildMenu(); }
					},
				});
				spawned.station = placement->spawnEntity("CraftingSpot", {4.0F, 0.0F});
				spawned.box = placement->spawnEntity("BasicBox", {-4.0F, 0.0F});

				if (open) {
					open(*this);
				}
			}

			void update(const app::FrameContext& frame) override {
				gameUI->setConstructionStatus(drawingSystem->status());
				gameUI->update(
					static_cast<float>(frame.dt),
					camera,
					chunks,
					world,
					assets::AssetRegistry::Get(),
					assets::RecipeRegistry::Get(),
					ColonistSelection{.entityId = spawned.colonist}
				);
			}

			void render(float /*alpha*/) override {}

			void onExit() override {
				gameUI.reset();
				ecs::GoalTaskRegistry::Get().clear();
			}

			void openBuildMenu() { placement->toggleBuildMenu(); }

		  private:
			void spawnColonist() {
				const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
				spawned.colonist = world.createEntity();
				world.addComponent<ecs::Position>(spawned.colonist, ecs::Position{{0.0F, 0.0F}});
				world.addComponent<ecs::Colonist>(spawned.colonist, ecs::Colonist{"Ada Lovelace"});
				world.addComponent<ecs::NeedsComponent>(spawned.colonist, ecs::NeedsComponent::createDefault());

				ecs::Inventory inventory = ecs::Inventory::createForColonist();
				inventory.leftHand = ecs::ItemStack{.defName = "AxePrimitive", .quantity = 1};
				for (const char* item : {"Berry", "Stick", "SmallStone", "PlantFiber", "Wood"}) {
					inventory.addItem(item, 3);
				}
				world.addComponent<ecs::Inventory>(spawned.colonist, std::move(inventory));

				ecs::Memory memory{.owner = spawned.colonist};
				for (int index = 0; index < 12; ++index) {
					const char*	   defName = index % 2 == 0 ? "Flora_BerryBush" : "Flora_TreeOak";
					const uint32_t defNameId = registry.getDefNameId(defName);
					memory.rememberWorldEntity({static_cast<float>(index) * 3.0F, 6.0F}, defNameId, registry.getCapabilityMask(defNameId));
				}
				world.addComponent<ecs::Memory>(spawned.colonist, std::move(memory));

				// Structures part torn down, each a Deconstruct goal with work left; the colonist works on the first.
				ecs::EntityID worked{0};
				for (int index = 0; index < 8; ++index) {
					const ecs::EntityID		structure = world.createEntity();
					ecs::StructureBlueprint blueprint;
					blueprint.workTotal = 100.0F;
					blueprint.workDone = 25.0F;
					world.addComponent<ecs::StructureBlueprint>(structure, blueprint);
					ecs::GoalTask goal;
					goal.type = ecs::TaskType::Deconstruct;
					goal.owner = ecs::GoalOwner::ConstructionGoalSystem;
					goal.destinationEntity = structure;
					goal.destinationPosition = {static_cast<float>(index) * 5.0F, -8.0F};
					ecs::GoalTaskRegistry::Get().createGoal(std::move(goal));
					if (index == 0) {
						worked = structure;
					}
				}
				ecs::Task task;
				task.type = ecs::TaskType::Deconstruct;
				task.buildBlueprintEntityId = static_cast<uint64_t>(worked);
				world.addComponent<ecs::Task>(spawned.colonist, task);
			}

			Open							 open;
			ecs::World						 world;
			world::WorldCamera				 camera;
			world::ChunkManager				 chunks{std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{})};
			Entities						 spawned;
			std::unique_ptr<GameUI>			 gameUI;
			std::unique_ptr<DrawingSystem>	 drawingSystem;
			std::unique_ptr<PlacementSystem> placement;
		};

		class SceneLayout : public SceneTest {
		  protected:
			void SetUp() override {
				SceneTest::SetUp();
				// The menu's backdrop and Quick Start read the shipped planet; here, as on CI, there is none.
				ASSERT_FALSE(Foundation::findResource(kQuickstartPlanetResource).has_value());
				NewGameSetup::Reset();
			}

			void TearDown() override {
				SceneTest::TearDown();
				ecs::GoalTaskRegistry::Get().clear();
			}

			// The HUD over its world, the station and the box spawned, run long enough for its 5 Hz models to fill.
			HudScene& openHud(HudScene::Open open = {}) {
				app->scenes().add(kHudScene, [open] { return std::make_unique<HudScene>(open); });
				go(kHudScene);
				for (int tick = 0; tick < kModelFillFrames; ++tick) {
					frame();
				}
				auto* scene = static_cast<HudScene*>(app->scenes().top());
				EXPECT_NE(scene->entities().station, ecs::EntityID{0});
				EXPECT_NE(scene->entities().box, ecs::EntityID{0});
				return *scene;
			}
		};

		// The settings list needs a canvas taller than the reference's 1000 ui px, and fits on the next one checked.
		constexpr float kSettingsListFitsFrom = 1200.0F;

	} // namespace

	TEST_F(SceneLayout, TheMainMenuHoldsTogether) {
		go(scenes::MainMenu.name);
		ASSERT_NE(shown("menu_new_game"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, SettingsHoldTogetherWithOnlyTheListScrollingWhereItCantFit) {
		go(scenes::Settings.name);
		ASSERT_NE(shown("settings_list"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({{.id = "settings_list", .belowCanvasHeight = kSettingsListFitsFrom}}), "");
	}

	TEST_F(SceneLayout, ScenarioSelectHoldsTogether) {
		go(scenes::ScenarioSelect.name);
		ASSERT_NE(shown("scenario_cards"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, PartySelectHoldsTogether) {
		go(scenes::PartySelect.name);
		ASSERT_NE(shown("party_roster"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	// Configuring, before a world is made: generating and reviewing need worldgen's run and the globe's GL.
	TEST_F(SceneLayout, TheWorldCreatorHoldsTogether) {
		go(scenes::WorldCreator.name);
		ASSERT_NE(shown("wc_parameters"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	TEST_F(SceneLayout, TheHudHoldsTogether) {
		openHud();
		ASSERT_NE(shown("gameplay_bar"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	// The build menu, both task lists and a stack of toasts, everything that grows over the play area at once.
	TEST_F(SceneLayout, TheHudHoldsTogetherWithItsPopupsOpen) {
		HudScene& scene = openHud([](HudScene& opened) {
			opened.openBuildMenu();
			for (const ui::ToastSeverity severity :
				 {ui::ToastSeverity::Critical,
				  ui::ToastSeverity::Warning,
				  ui::ToastSeverity::Info,
				  ui::ToastSeverity::Warning,
				  ui::ToastSeverity::Info}) {
				opened.hud().pushNotification(
					"Ada Lovelace is starving", "She has nothing to eat within reach of the colony.", severity, 0.0F
				);
			}
		});
		press("tasks_header");
		scene.hud().toggleDecisionInspector();
		frame();
		ASSERT_TRUE(scene.hud().isBuildMenuVisible());
		ASSERT_TRUE(scene.hud().isGlobalTaskListExpanded());
		const ui::Component* globalTasks = shown("tasks_scroll");
		ASSERT_NE(globalTasks, nullptr);
		ASSERT_FALSE(globalTasks->getChildren().empty());
		ASSERT_NE(shown("task_list"), nullptr);
		const ui::Component* toasts = shown("toast_stack");
		ASSERT_NE(toasts, nullptr);
		ASSERT_EQ(toasts->getChildren().size(), 5U);
		const ui::Component* queue = shown("task_list_content");
		ASSERT_NE(queue, nullptr);
		ASSERT_FALSE(queue->getChildren().empty());
		EXPECT_EQ(lintEverywhere(), "");
		// The colony's task list is a fixed-height box whose rows scroll.
		EXPECT_EQ(unexpectedScrollbars({{.id = "tasks_scroll"}}), "");
		// Still open after every window size and interface size, so none drops out of the later ones unnoticed.
		EXPECT_TRUE(scene.hud().isBuildMenuVisible());
		EXPECT_TRUE(scene.hud().isGlobalTaskListExpanded());
		EXPECT_NE(shown("task_list"), nullptr);
		EXPECT_NE(shown("tasks_scroll"), nullptr);
		EXPECT_NE(shown("toast_stack"), nullptr);
	}

	TEST_F(SceneLayout, TheHudHoldsTogetherWithTheWallToolOn) {
		openHud([](HudScene& opened) { opened.drawing().activateWallTool(); });
		frame();
		ASSERT_NE(shown("construction_config_strip"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	// Every tab, through the dialog's own tab bar.
	TEST_F(SceneLayout, TheColonistDossierHoldsTogetherWithOnlyItsTabScrolling) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showColonistDetails(opened.entities().colonist); });
		ASSERT_TRUE(scene.hud().isColonistDetailsVisible());
		static constexpr std::array<const char*, 8> kTabs{
			"dossier_bio",
			"dossier_needs",
			"dossier_skills",
			"dossier_social",
			"dossier_gear",
			"dossier_memory",
			"dossier_tasks",
			"dossier_log"
		};
		// The tabs fed from the colonist's gear, memory and known work, each by the id its rows share.
		static const std::map<std::string_view, std::string_view> kRowIds{
			{"dossier_gear", "gear_pack_row"}, {"dossier_memory", "memory_row"}, {"dossier_tasks", "dossier_task_row"}
		};
		for (const char* tab : kTabs) {
			press(std::string("tab_") + tab);
			const auto* bar = static_cast<const ui::TabBar*>(shown("dossier_tabs"));
			ASSERT_NE(bar, nullptr);
			ASSERT_EQ(bar->getSelectedId(), tab);
			ASSERT_NE(shown("dossier_tab_area"), nullptr);
			if (const auto rows = kRowIds.find(tab); rows != kRowIds.end()) {
				ASSERT_GT(shownCount(rows->second), 0U) << "no rows on the " << tab << " tab";
			}
			EXPECT_EQ(lintEverywhere(), "") << "on the " << tab << " tab";
			EXPECT_EQ(unexpectedScrollbars({{.id = "dossier_tab_area"}}), "") << "on the " << tab << " tab";
			EXPECT_TRUE(scene.hud().isColonistDetailsVisible()) << "after the sizes on the " << tab << " tab";
		}
	}

	TEST_F(SceneLayout, TheCraftingDialogHoldsTogether) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showCraftingDialog(opened.entities().station, "CraftingSpot"); });
		ASSERT_TRUE(scene.hud().isCraftingDialogVisible());
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
		EXPECT_TRUE(scene.hud().isCraftingDialogVisible());
	}

	TEST_F(SceneLayout, TheStorageDialogHoldsTogether) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showStorageConfigDialog(opened.entities().box, "BasicBox"); });
		ASSERT_TRUE(scene.hud().isStorageConfigVisible());
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
		EXPECT_TRUE(scene.hud().isStorageConfigVisible());
	}

	TEST_F(SceneLayout, ThePauseMenuHoldsTogether) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showPauseMenu(); });
		ASSERT_TRUE(scene.hud().isPauseMenuVisible());
		ASSERT_NE(shown("pause_menu"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
		EXPECT_TRUE(scene.hud().isPauseMenuVisible());
	}

} // namespace world_sim::testing
