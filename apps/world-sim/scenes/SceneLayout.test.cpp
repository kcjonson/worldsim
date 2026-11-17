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
#include <assets/TraitRegistry.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Needs.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/MoodSystem.h>
#include <ecs/systems/TimeSystem.h>
#include <theme/Theme.h>
#include <ui/components/Dialog.h>
#include <ui/components/TabBar.h>
#include <ui/components/Toast.h>
#include <utils/ResourcePath.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
				ecs::EntityID newcomer{0}; // empty-handed, remembering nothing: a shorter dossier
				ecs::EntityID station{0};
				ecs::EntityID box{0};
			};
			using Open = std::function<void(HudScene&)>;

			explicit HudScene(Open open)
				: open(std::move(open)) {}

			[[nodiscard]] const char* name() const override { return kHudScene; }

			[[nodiscard]] GameUI&		  hud() { return *gameUI; }
			[[nodiscard]] const Entities& entities() const { return spawned; }
			[[nodiscard]] ecs::World&	  ecsWorld() { return world; }
			[[nodiscard]] DrawingSystem&  drawing() { return *drawingSystem; }
			// What the strip's tool picker asked for, in order.
			[[nodiscard]] const std::vector<FoundationTool>& foundationToolPicks() const { return pickedFoundationTools; }

			void onEnter(app::App& /*app*/) override {
				theme::apply(ui());
				gameUI = std::make_unique<GameUI>(GameUI::Args{
					.root = root(),
					.traits = traits,
					.onFoundationToolSelected =
						[this](FoundationTool tool) {
							pickedFoundationTools.push_back(tool);
							drawingSystem->pickFoundationTool(tool);
						},
				});
				drawingSystem =
					std::make_unique<DrawingSystem>(DrawingSystem::Args{.world = &world, .camera = &camera, .navigation = nullptr});
				gameUI->showCatalogue(assets::RecipeRegistry::Get(), assets::ConstructionRegistry::Get(), drawingSystem->activeMaterial());
				gameUI->setMinimapContext({0.0F, 0.0F}, 45.0, 10.0, 8.0F);

				world.registerSystem<ecs::TimeSystem>();
				world.registerSystem<ecs::MoodSystem>(assets::MoodConfig::Get());
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
				world.getSystem<ecs::MoodSystem>().startMood(spawned.colonist);

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

				spawned.newcomer = world.createEntity();
				world.addComponent<ecs::Position>(spawned.newcomer, ecs::Position{{2.0F, 0.0F}});
				world.addComponent<ecs::Colonist>(spawned.newcomer, ecs::Colonist{"Grace Hopper"});
				world.addComponent<ecs::NeedsComponent>(spawned.newcomer, ecs::NeedsComponent::createDefault());
				world.getSystem<ecs::MoodSystem>().startMood(spawned.newcomer);
				world.addComponent<ecs::Inventory>(spawned.newcomer, ecs::Inventory::createForColonist());
			}

			Open							 open;
			assets::TraitRegistry			 traits; // none: these colonists have no Traits
			ecs::World						 world;
			world::WorldCamera				 camera;
			world::ChunkManager				 chunks{std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{})};
			Entities						 spawned;
			std::unique_ptr<GameUI>			 gameUI;
			std::unique_ptr<DrawingSystem>	 drawingSystem;
			std::unique_ptr<PlacementSystem> placement;
			std::vector<FoundationTool>		 pickedFoundationTools;
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

			// The open dossier's panel.
			[[nodiscard]] ui::Ref<ui::Rectangle> dossierPanel() const {
				const auto* dossier = static_cast<const ui::Dialog*>(shown("colonist_dossier"));
				return dossier != nullptr ? dossier->panel() : ui::Ref<ui::Rectangle>{};
			}
		};

		// The prototype modal's cap, which the dossier's panel keeps to.
		constexpr float kDossierMaxWidthShare = 0.92F;
		constexpr float kDossierMaxHeightShare = 0.86F;

		// The settings list fits a canvas the reference's height at the default size, and scrolls only on shorter ones.
		constexpr auto kSettingsListFitsFrom = static_cast<float>(theme::uiScaleConfig().referenceHeight);

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
		ASSERT_NE(shown("interface_size_choice"), nullptr);
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

	// The foundation tools' strip carries the tool picker beside the material cards.
	TEST_F(SceneLayout, TheHudHoldsTogetherWithEachFoundationToolOn) {
		HudScene& scene = openHud([](HudScene& opened) { opened.drawing().activateRectangleTool(); });
		frame();
		ASSERT_NE(shown("config_strip_tools"), nullptr);
		ASSERT_NE(shown("config_strip_tool_Rectangle"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
		scene.drawing().activateFoundationTool();
		frame();
		ASSERT_NE(shown("config_strip_tools"), nullptr);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	// A card fires the pick with its tool, and the picked card follows the tool on.
	TEST_F(SceneLayout, ThePickerPicksItsToolAndMarksTheOneOn) {
		HudScene& scene = openHud([](HudScene& opened) { opened.drawing().activateRectangleTool(); });
		const auto borderOf = [this](std::string_view id) {
			const ui::Component* card = shown(id);
			return card != nullptr ? card->getStyle().borderWidth.value_or(0.0F) : 0.0F;
		};
		frame();
		EXPECT_GT(borderOf("config_strip_tool_Rectangle"), borderOf("config_strip_tool_Freeform"));
		press("config_strip_tool_Freeform");
		EXPECT_EQ(scene.foundationToolPicks(), std::vector<FoundationTool>{FoundationTool::Freeform});
		EXPECT_EQ(scene.drawing().status().foundationTool, FoundationTool::Freeform);
		frame();
		EXPECT_GT(borderOf("config_strip_tool_Freeform"), borderOf("config_strip_tool_Rectangle"));
		press("config_strip_tool_Rectangle");
		EXPECT_EQ(scene.foundationToolPicks().back(), FoundationTool::Rectangle);
		EXPECT_TRUE(scene.drawing().isRectangleTool());
	}

	// Add and Subtract stay freeform, so the strip shows no tool picker.
	TEST_F(SceneLayout, AFoundationEditShowsNoToolPicker) {
		HudScene& scene = openHud();
		const auto floor = scene.drawing().world().commitFoundation(
			std::vector<Foundation::Vec2>{{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {0.0F, 4.0F}}, "Wood"
		);
		ASSERT_TRUE(floor.ok());
		for (const construction::FoundationEditMode mode : {construction::FoundationEditMode::Add, construction::FoundationEditMode::Subtract}) {
			scene.drawing().activateFoundationEditTool(floor.id, mode);
			frame();
			ASSERT_NE(shown("construction_config_strip"), nullptr);
			EXPECT_EQ(shown("config_strip_tools"), nullptr);
			EXPECT_EQ(shown("config_strip_tool_Rectangle"), nullptr);
		}
	}

	TEST_F(SceneLayout, TheHudHoldsTogetherWithTheWallToolOn) {
		openHud([](HudScene& opened) { opened.drawing().activateWallTool(); });
		frame();
		ASSERT_NE(shown("construction_config_strip"), nullptr);
		EXPECT_EQ(shown("config_strip_tools"), nullptr) << "the wall tool has no tool picker";
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({}), "");
	}

	// Every tab, through the dialog's own tab bar. The panel is its tallest tab's height, held on every tab, and inside
	// the cap; the tab scrolls only where the cap holds the panel shorter than that.
	TEST_F(SceneLayout, TheColonistDossierHoldsItsSizeOnEveryTabWithOnlyItsTabScrolling) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showColonistDetails(opened.entities().colonist); });
		ASSERT_TRUE(scene.hud().isColonistDetailsVisible());
		const ui::Ref<ui::Rectangle> panel = dossierPanel();
		ASSERT_NE(panel.get(), nullptr);
		const float held = panel.get()->getSize().y;
		EXPECT_LT(held, kDossierMaxHeightShare * app->scenes().top()->ui().getCanvas().y) << "pinned to the cap on the reference";
		const ExpectedScrollbar tabScrolls{.id = "dossier_tab_area", .belowCanvasHeight = held / kDossierMaxHeightShare};
		const auto				panelSizes = [this, &panel] {
			 return everywhere([this, &panel](const WindowSize& window) {
				 (void)app::lintAt(*app, *platform, window.width, window.height);
				 const Foundation::Vec2	  canvas = app->scenes().top()->ui().getCanvas();
				 const Foundation::Vec2	  size = panel.get()->getSize();
				 std::vector<std::string> lines{std::format("{:g}x{:g}", size.x, size.y)};
				 if (size.x > (kDossierMaxWidthShare * canvas.x) + 0.5F || size.y > (kDossierMaxHeightShare * canvas.y) + 0.5F) {
					 lines.push_back(std::format("past the cap on a {:g}x{:g} canvas", canvas.x, canvas.y));
				 }
				 return lines;
			 });
		};
		std::string									firstSizes;
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
		// On the reference canvas, where the cap doesn't reach it, the tallest tab fills the tab area exactly.
		float areaInside = 0.0F;
		float tallestTab = 0.0F;
		for (const char* tab : kTabs) {
			press(std::string("tab_") + tab);
			const auto* bar = static_cast<const ui::TabBar*>(shown("dossier_tabs"));
			ASSERT_NE(bar, nullptr);
			ASSERT_EQ(bar->getSelectedId(), tab);
			const ui::Component* area = shown("dossier_tab_area");
			ASSERT_NE(area, nullptr);
			ASSERT_EQ(area->getChildren().size(), 1U);
			const ui::Component* view = app->scenes().top()->ui().find(area->getChildren().front());
			ASSERT_NE(view, nullptr);
			areaInside = area->getSize().y - area->getLayout().padding.vertical();
			tallestTab = std::max(tallestTab, view->getSize().y);
			if (const auto rows = kRowIds.find(tab); rows != kRowIds.end()) {
				ASSERT_GT(shownCount(rows->second), 0U) << "no rows on the " << tab << " tab";
			}
			const std::string sizes = panelSizes();
			EXPECT_EQ(sizes.find("past the cap"), std::string::npos) << sizes;
			if (firstSizes.empty()) {
				firstSizes = sizes;
			} else {
				EXPECT_EQ(sizes, firstSizes) << "the panel moved on the " << tab << " tab";
			}
			EXPECT_EQ(lintEverywhere(), "") << "on the " << tab << " tab";
			EXPECT_EQ(unexpectedScrollbars({tabScrolls}), "") << "on the " << tab << " tab";
			EXPECT_TRUE(scene.hud().isColonistDetailsVisible()) << "after the sizes on the " << tab << " tab";
		}
		EXPECT_NEAR(areaInside, tallestTab, 1.0F) << "the tab area against its tallest tab";
	}

	// A pack that fills under the open dossier grows its Gear tab and leaves the panel as it opened. Then the entity
	// panel's Details again while it's open, for that colonist and then for another: the content follows to the Bio tab at
	// the top, the panel keeps the size it opened with. Closed through its own button and opened again, it measures afresh.
	TEST_F(SceneLayout, TheColonistDossierHoldsItsSizeAsItsDataGrowsAndWhenOpenedAgainWhileOpen) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showColonistDetails(opened.entities().colonist); });
		const ui::Ref<ui::Rectangle> panel = dossierPanel();
		ASSERT_NE(panel.get(), nullptr);
		const Foundation::Vec2 opened = panel.get()->getSize();
		const auto			   settle = [this] {
			for (int tick = 0; tick < kModelFillFrames; ++tick) {
				frame();
			}
		};
		const auto title = [this] {
			const auto* dossier = static_cast<const ui::Dialog*>(shown("colonist_dossier"));
			return dossier != nullptr ? dossier->getTitle() : std::string{};
		};

		press("tab_dossier_gear");
		const std::size_t rows = shownCount("gear_pack_row");
		ecs::Inventory&	  pack = *scene.ecsWorld().getComponent<ecs::Inventory>(scene.entities().colonist);
		for (const char* item : {"AxePrimitive", "Granite", "Basalt", "Sandstone", "RiverCobble"}) {
			ASSERT_EQ(pack.addItem(item, 1), 1U) << item;
		}
		settle();
		ASSERT_GT(shownCount("gear_pack_row"), rows) << "a fuller pack grows the Gear tab, so a re-measure would change the height";
		EXPECT_EQ(panel.get()->getSize().x, opened.x);
		EXPECT_EQ(panel.get()->getSize().y, opened.y) << "with a fuller pack";
		ui::Component* area = shown("dossier_tab_area");
		ASSERT_NE(area, nullptr);
		area->scrollTo({0.0F, 1.0e6F});
		ASSERT_GT(area->getScroll().y, 0.0F) << "the grown Gear tab scrolls in the held size";

		scene.hud().showColonistDetails(scene.entities().colonist);
		settle();
		EXPECT_EQ(panel.get()->getSize().x, opened.x);
		EXPECT_EQ(panel.get()->getSize().y, opened.y) << "for the same colonist";
		const auto* bar = static_cast<const ui::TabBar*>(shown("dossier_tabs"));
		ASSERT_NE(bar, nullptr);
		EXPECT_EQ(bar->getSelectedId(), "dossier_bio");
		area = shown("dossier_tab_area");
		ASSERT_NE(area, nullptr);
		EXPECT_EQ(area->getScroll().y, 0.0F);

		scene.hud().showColonistDetails(scene.entities().newcomer);
		settle();
		EXPECT_EQ(title(), "Grace Hopper");
		EXPECT_EQ(panel.get()->getSize().x, opened.x);
		EXPECT_EQ(panel.get()->getSize().y, opened.y) << "for another colonist";
		EXPECT_EQ(lintEverywhere(), "");

		press("dossier_close");
		EXPECT_FALSE(scene.hud().isColonistDetailsVisible());
		scene.hud().showColonistDetails(scene.entities().newcomer);
		settle();
		const ui::Ref<ui::Rectangle> reopened = dossierPanel();
		ASSERT_NE(reopened.get(), nullptr);
		EXPECT_EQ(title(), "Grace Hopper");
		EXPECT_LT(reopened.get()->getSize().y, opened.y) << "the newcomer's own, shorter dossier";
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
