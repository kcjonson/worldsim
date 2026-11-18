// The main screens laid out at the window sizes and interface sizes the players have, linted clean, with a scrollbar
// only on what is meant to scroll. The splash and the loading scene aren't here: their updates bake the ground tiles
// and load the planet, which need the GPU and the quickstart planet; the view they show a failed load in is here on
// its own. The game scene needs both too, so its HUD and
// dialogs are the game's GameUI over a small world of their own.

#include "GameStartConfig.h"
#include "NewGameSetup.h"
#include "SceneTesting.test.h"
#include "UserSettings.h"
#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/WorldPointer.h"
#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/selection/SelectionTypes.h"
#include "scenes/shared/LoadFailureView.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/RecipeRegistry.h>
#include <assets/TraitRegistry.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/ShippedConfig.test.h>
#include <ecs/ThoughtTesting.test.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Needs.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/MoodSystem.h>
#include <ecs/systems/ThoughtSystem.h>
#include <ecs/systems/TimeSystem.h>
#include <theme/Theme.h>
#include <ui/components/Dialog.h>
#include <ui/components/TabBar.h>
#include <ui/components/Toast.h>
#include <ui/shapes/Text.h>
#include <utils/ResourcePath.h>
#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <algorithm>
#include <array>
#include <cmath>
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
		// Edges closer than this count as touching, as in the lint.
		constexpr float kOverlapTolerance = 0.5F;

		// What GameScene shows over the world, fed as GameScene feeds it, over a world of one colonist, a crafting
		// spot, a box and a few structures being torn down. The colonist works on one, carries some gear and
		// remembers what's around, so the dossier's tabs have rows. `open` opens something on it once it's built.
		// The colonist's thoughts, written here so the layout doesn't hang on the shipped thought ids.
		constexpr std::string_view kHudThoughts = R"(
  <thought id="slept_on_ground" label="Slept on the ground" importance="normal">
    <feeling is="miserable" mood="-10" hours="24"/>
    <line>My back hates me now.</line>
  </thought>
  <thought id="bottled_up" label="Bottled up" importance="normal" stacks="true">
    <feeling is="resigned" mood="-2" hours="24"/>
  </thought>
  <thought id="very_hungry" label="Very hungry" importance="normal">
    <feeling is="annoyed"/>
  </thought>
  <thought id="saw_something_new" label="Saw something new" importance="major">
    <feeling is="delighted"/>
  </thought>
)";

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

			[[nodiscard]] GameUI&				  hud() { return *gameUI; }
			[[nodiscard]] const Entities&		  entities() const { return spawned; }
			[[nodiscard]] ecs::World&			  ecsWorld() { return world; }
			[[nodiscard]] ecs::ThoughtEventTypeId thoughtType(std::string_view name) const { return thoughtTypes.type(name); }
			[[nodiscard]] DrawingSystem&		  drawing() { return *drawingSystem; }
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
					.onKeyGuideOpened = [this] { drawingSystem->toggleKeyGuide(); },
				});
				drawingSystem =
					std::make_unique<DrawingSystem>(DrawingSystem::Args{.world = &world, .camera = &camera, .navigation = nullptr});
				gameUI->showCatalogue(assets::RecipeRegistry::Get(), assets::ConstructionRegistry::Get(), drawingSystem->activeMaterial());
				gameUI->setMinimapContext({0.0F, 0.0F}, 45.0, 10.0, 8.0F);

				world.registerSystem<ecs::TimeSystem>();
				world.registerSystem<ecs::MoodSystem>(assets::MoodConfig::Get());
				for (const std::string& error : thoughtTypes.errors) {
					ADD_FAILURE() << error;
				}
				world.registerSystem<ecs::ThoughtSystem>(thoughtTypes.types, 7U);
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
				gameUI->setKeyGuide(drawingSystem->activeTool(), drawingSettings, drawingSystem->keyGuideCollapsed());
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

			// The Drawing Settings the key guide reads, as GameScene reads the player's.
			DrawingSettings drawingSettings{.showKeyGuide = true};

		  private:
			void spawnColonist() {
				const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
				spawned.colonist = world.createEntity();
				world.addComponent<ecs::Position>(spawned.colonist, ecs::Position{{0.0F, 0.0F}});
				world.addComponent<ecs::Colonist>(spawned.colonist, ecs::Colonist{"Ada Lovelace"});
				world.addComponent<ecs::NeedsComponent>(spawned.colonist, ecs::testing::shippedNeeds());
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

				// A few low needs, each a row of the mood breakdown.
				ecs::NeedsComponent& needs = *world.getComponent<ecs::NeedsComponent>(spawned.colonist);
				needs.get(ecs::NeedType::Hunger).value = 20.0F;
				needs.get(ecs::NeedType::Thirst).value = 45.0F;
				needs.get(ecs::NeedType::Hygiene).value = 10.0F;
				needs.get(ecs::NeedType::Recreation).value = 60.0F;

				// A held thought, a stack of two, and one with no mood effect, summed into the mood as a tick does.
				auto& thoughtSystem = world.getSystem<ecs::ThoughtSystem>();
				for (const char* type : {"slept_on_ground", "bottled_up", "bottled_up", "very_hungry"}) {
					thoughtSystem.create({.colonist = spawned.colonist, .eventType = thoughtTypes.type(type)});
				}
				world.getSystem<ecs::MoodSystem>().update(0.0F);

				spawned.newcomer = world.createEntity();
				world.addComponent<ecs::Position>(spawned.newcomer, ecs::Position{{2.0F, 0.0F}});
				world.addComponent<ecs::Colonist>(spawned.newcomer, ecs::Colonist{"Grace Hopper"});
				world.addComponent<ecs::NeedsComponent>(spawned.newcomer, ecs::testing::shippedNeeds());
				world.getSystem<ecs::MoodSystem>().startMood(spawned.newcomer);
				world.addComponent<ecs::Inventory>(spawned.newcomer, ecs::Inventory::createForColonist());
			}

			Open							 open;
			assets::TraitRegistry			 traits; // none: these colonists have no Traits
			ecs::testing::TestThoughtTypes	 thoughtTypes{kHudThoughts};
			ecs::World						 world;
			world::WorldCamera				 camera;
			world::ChunkManager				 chunks{std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{})};
			Entities						 spawned;
			std::unique_ptr<GameUI>			 gameUI;
			std::unique_ptr<DrawingSystem>	 drawingSystem;
			std::unique_ptr<PlacementSystem> placement;
			std::vector<FoundationTool>		 pickedFoundationTools;
		};

		constexpr const char* kFailureScene = "load_failure";
		constexpr std::size_t kFailureMessageCount = 12;

		// The view the splash and the loading scene show a failed load in, centred as they centre it, over more
		// messages than it shows, each long enough to wrap.
		class FailureScene final : public app::Scene {
		  public:
			[[nodiscard]] const char* name() const override { return kFailureScene; }
			void					  render(float /*alpha*/) override {}

			void onEnter(app::App& /*app*/) override {
				theme::apply(ui());
				std::vector<std::string> messages;
				for (std::size_t index = 0; index < kFailureMessageCount; ++index) {
					messages.push_back(
						std::format(
							"[needs] assets/config/colonists/needs/needs-{}.xml: <Need name=\"Hunger\"> seek must be a number, got "
							"\"abc\"; every threshold of a need is a number between 0 and 100, and a file with any error adds no "
							"need, so each need it gave is missing too and named by the check that every need is given",
							index
						)
					);
				}
				root()
					.addChild<ui::Component>({
						.size = {ui::percent(100.0F), ui::percent(100.0F)},
						.layout =
							{.display = ui::Display::Flex,
							 .flexDirection = ui::FlexDirection::Column,
							 .alignItems = ui::AlignItems::Center,
							 .justifyContent = ui::JustifyContent::Center},
					})
					.addChild<LoadFailureView>(
						{.id = "load_failure", .messages = std::move(messages), .instruction = "Press Esc to return to the menu"}
					);
			}
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

			// The key guide as it shows: its panel, or its chip while collapsed.
			[[nodiscard]] const ui::Component* keyGuideShown() const {
				const ui::Component* panel = shown("key_guide_panel");
				return panel != nullptr ? panel : shown("key_guide_chip");
			}

			// `component`'s children, in order.
			[[nodiscard]] std::vector<const ui::Component*> childrenOf(const ui::Component& component) const {
				std::vector<const ui::Component*> children;
				for (const ui::ComponentId id : component.getChildren()) {
					children.push_back(app->scenes().top()->ui().find(id));
				}
				return children;
			}

			// Where the key guide meets the HUD round it, at every window size and interface size: it sits over the
			// strip, centred on it, and overlaps nothing.
			[[nodiscard]] std::string keyGuideCrowding() {
				return everywhere([this](const WindowSize& window) {
					(void)app::lintAt(*app, *platform, window.width, window.height);
					std::vector<std::string> found;
					const ui::Component*	 guide = keyGuideShown();
					if (guide == nullptr) {
						found.emplace_back("no key guide");
						return found;
					}
					const Foundation::Rect box = guide->screenBounds();
					if (const ui::Component* strip = shown("construction_config_strip"); strip != nullptr) {
						const Foundation::Rect stripBox = strip->screenBounds();
						if (box.y + box.height > stripBox.y + kOverlapTolerance) {
							found.push_back(
								std::format("{} ends {:g} below the strip's top", guide->getId(), box.y + box.height - stripBox.y)
							);
						}
						const float offCentre = (box.x + (box.width * 0.5F)) - (stripBox.x + (stripBox.width * 0.5F));
						if (std::abs(offCentre) > kOverlapTolerance) {
							found.push_back(std::format("{} is {:g} off the strip's centre", guide->getId(), offCentre));
						}
					} else {
						found.emplace_back("no strip");
					}
					for (const char* id :
						 {"construction_config_strip",
						  "gameplay_bar",
						  "entity_panel",
						  "task_list",
						  "zoom_panel",
						  "colonist_list",
						  "right_stack",
						  "top_bar"}) {
						const ui::Component* other = shown(id);
						if (other == nullptr) {
							continue;
						}
						const Foundation::Rect near = other->screenBounds();
						const float			   across = std::min(box.x + box.width, near.x + near.width) - std::max(box.x, near.x);
						const float			   down = std::min(box.y + box.height, near.y + near.height) - std::max(box.y, near.y);
						if (across > kOverlapTolerance && down > kOverlapTolerance) {
							found.push_back(std::format("{} overlaps {} by {:g}x{:g}", guide->getId(), id, across, down));
						}
					}
					return found;
				});
			}
		};

		// Every helper on, for the tallest legend.
		DrawingSettings everyHelperOn() {
			return {
				.angleSteps = true,
				.guidesFromShape = true,
				.alignToExistingBuildings = true,
				.guidesFromWorldAxes = true,
				.guideCrossings = true,
				.commonLengths = true,
				.guideDistances = true,
				.angleReadout = true,
				.roundLengths = true,
				.showKeyGuide = true,
			};
		}

		// Each drawing tool, turned on in the HUD's drawing system; Add and Subtract on `floor`.
		struct DrawingToolOn {
			const char*														name;
			std::function<void(DrawingSystem&, construction::FoundationId)> activate;
		};

		std::vector<DrawingToolOn> everyDrawingTool() {
			return {
				{"Rectangle", [](DrawingSystem& drawing, construction::FoundationId) { drawing.activateRectangleTool(); }},
				{"Freeform", [](DrawingSystem& drawing, construction::FoundationId) { drawing.activateFoundationTool(); }},
				{"Wall", [](DrawingSystem& drawing, construction::FoundationId) { drawing.activateWallTool(); }},
				{"Door", [](DrawingSystem& drawing, construction::FoundationId) { drawing.activateOpeningTool("Door"); }},
				{"Add", [](DrawingSystem& drawing, construction::FoundationId floor) {
					 drawing.activateFoundationEditTool(floor, construction::FoundationEditMode::Add);
				 }},
			};
		}

		construction::FoundationId commitFloor(DrawingSystem& drawing) {
			const auto floor = drawing.world().commitFoundation(
				std::vector<Foundation::Vec2>{{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {0.0F, 4.0F}}, "Wood"
			);
			EXPECT_TRUE(floor.ok());
			return floor.id;
		}

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
		HudScene&  scene = openHud([](HudScene& opened) { opened.drawing().activateRectangleTool(); });
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
		HudScene&  scene = openHud();
		const auto floor = scene.drawing().world().commitFoundation(
			std::vector<Foundation::Vec2>{{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {0.0F, 4.0F}}, "Wood"
		);
		ASSERT_TRUE(floor.ok());
		for (const construction::FoundationEditMode mode :
			 {construction::FoundationEditMode::Add, construction::FoundationEditMode::Subtract}) {
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

	// Every tab, through the dialog's own tab bar. The panel is the height of its tallest tab that sets it, held on every
	// tab, and inside the cap; the tab scrolls only where the cap holds the panel shorter than that. Tasks and Log, lists
	// that grow with the colonist, fill the tab area the others set.
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
			{"dossier_gear", "gear_pack_row"},
			{"dossier_memory", "memory_row"},
			{"dossier_tasks", "dossier_task_row"},
			{"dossier_needs", "mood_breakdown_row"},
			{"dossier_log", "thought_log_block"}
		};
		// The list tabs, which don't set the height.
		static const std::array<std::string_view, 2> kListTabs{"dossier_tasks", "dossier_log"};
		// On the reference canvas, where the cap doesn't reach it, the tallest tab that sets the height fills the tab area exactly.
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
			if (std::ranges::find(kListTabs, std::string_view(tab)) == kListTabs.end()) {
				tallestTab = std::max(tallestTab, view->getSize().y);
			}
			if (const auto rows = kRowIds.find(tab); rows != kRowIds.end()) {
				ASSERT_GT(shownCount(rows->second), 0U) << "no rows on the " << tab << " tab";
			}
			if (std::string_view(tab) == "dossier_needs") {
				ASSERT_EQ(shownCount("mood_breakdown_need"), 4U) << "a row for each need costing something";
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
		EXPECT_NEAR(areaInside, tallestTab, 1.0F) << "the tab area against its tallest tab that sets the height";
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

	// A long log neither grows the dossier nor moves it: it opens at the size it opened at with a short one, and its Log
	// tab shows the newest page of notable thoughts until "Show older" adds the next.
	TEST_F(SceneLayout, TheColonistDossierIsTheSameSizeWithALongLogAndPagesIt) {
		HudScene& scene = openHud([](HudScene& opened) { opened.hud().showColonistDetails(opened.entities().colonist); });
		const ui::Ref<ui::Rectangle> panel = dossierPanel();
		ASSERT_NE(panel.get(), nullptr);
		const Foundation::Vec2 shortLog = panel.get()->getSize();
		press("dossier_close");

		// Each one a thought of its own: a major one with no mood effect is never held, so nothing folds.
		const std::size_t kSightings = kThoughtLogPage + 10;
		auto&			  thoughts = scene.ecsWorld().getSystem<ecs::ThoughtSystem>();
		for (std::size_t sighting = 0; sighting < kSightings; ++sighting) {
			thoughts.create({.colonist = scene.entities().colonist, .eventType = scene.thoughtType("saw_something_new")});
		}
		scene.hud().showColonistDetails(scene.entities().colonist);
		for (int tick = 0; tick < kModelFillFrames; ++tick) {
			frame();
		}
		const ui::Ref<ui::Rectangle> reopened = dossierPanel();
		ASSERT_NE(reopened.get(), nullptr);
		EXPECT_EQ(reopened.get()->getSize().x, shortLog.x);
		EXPECT_EQ(reopened.get()->getSize().y, shortLog.y) << "with a long log";

		press("tab_dossier_log");
		EXPECT_EQ(reopened.get()->getSize().y, shortLog.y) << "on the Log tab";
		EXPECT_EQ(shownCount("thought_log_block"), kThoughtLogPage);
		ui::Component* area = shown("dossier_tab_area");
		ASSERT_NE(area, nullptr);
		area->scrollTo({0.0F, 1.0e6F});
		press("thought_log_older");
		// The sightings, the night on the ground and the bottled-up stack; the hunger has no mood effect and isn't major.
		EXPECT_EQ(shownCount("thought_log_block"), kSightings + 2);
		EXPECT_EQ(shown("thought_log_older"), nullptr);
		EXPECT_EQ(reopened.get()->getSize().y, shortLog.y) << "after Show older";
		EXPECT_EQ(lintEverywhere(), "");
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

	TEST_F(SceneLayout, TheLoadFailureViewShowsTheFirstMessagesWrappedAndCountsTheRest) {
		app->scenes().add(kFailureScene, [] { return std::make_unique<FailureScene>(); });
		go(kFailureScene);
		EXPECT_EQ(shownCount("load_failure_message"), LoadFailureView::kShownMessages);
		const auto* more = static_cast<const ui::Text*>(shown("load_failure_more"));
		ASSERT_NE(more, nullptr);
		EXPECT_EQ(more->getText(), std::format("and {} more", kFailureMessageCount - LoadFailureView::kShownMessages));
		const ui::Component* messages = shown("load_failure_messages");
		ASSERT_NE(messages, nullptr);
		EXPECT_NEAR(messages->getSize().y, LoadFailureView::kMessagesMaxHeight, 0.01F);
		EXPECT_EQ(lintEverywhere(), "");
		EXPECT_EQ(unexpectedScrollbars({{.id = "load_failure_messages"}}), "");
	}

	// With every helper on (the tallest legend) and the task list open over the entity panel, for each tool, open and
	// collapsed: the HUD lints clean and the key guide meets nothing round it.
	TEST_F(SceneLayout, TheKeyGuideHoldsTogetherOpenAndCollapsedWithEachTool) {
		HudScene& scene = openHud([](HudScene& opened) { opened.drawingSettings = everyHelperOn(); });
		scene.hud().toggleDecisionInspector();
		const construction::FoundationId floor = commitFloor(scene.drawing());
		for (const DrawingToolOn& tool : everyDrawingTool()) {
			tool.activate(scene.drawing(), floor);
			for (const bool collapsed : {false, true}) {
				if (scene.drawing().keyGuideCollapsed() != collapsed) {
					scene.drawing().toggleKeyGuide();
				}
				frame();
				ASSERT_NE(shown("task_list"), nullptr);
				ASSERT_NE(shown(collapsed ? "key_guide_chip" : "key_guide_panel"), nullptr) << tool.name;
				EXPECT_EQ(lintEverywhere(), "") << tool.name << (collapsed ? ", collapsed" : ", open");
				EXPECT_EQ(keyGuideCrowding(), "") << tool.name << (collapsed ? ", collapsed" : ", open");
			}
		}
	}

	TEST_F(SceneLayout, TheKeyGuideShowsOnlyWhileAToolIsOnAndTheSettingIs) {
		HudScene& scene = openHud();
		EXPECT_EQ(shown("key_guide"), nullptr) << "no tool";
		scene.drawing().activateWallTool();
		frame();
		EXPECT_NE(shown("key_guide_panel"), nullptr);
		scene.drawingSettings.showKeyGuide = false;
		frame();
		EXPECT_EQ(shown("key_guide"), nullptr) << "the setting off";
		scene.drawingSettings.showKeyGuide = true;
		frame();
		EXPECT_NE(shown("key_guide_panel"), nullptr);
		scene.drawing().deactivate();
		frame();
		EXPECT_EQ(shown("key_guide"), nullptr) << "the tool off";

		scene.drawing().activateWallTool();
		scene.drawing().toggleKeyGuide();
		frame();
		const ui::Component* chip = shown("key_guide_chip");
		ASSERT_NE(chip, nullptr);
		const std::vector<const ui::Component*> chipText = childrenOf(*chip);
		ASSERT_EQ(chipText.size(), 1U);
		ASSERT_EQ(std::string_view(chipText[0]->typeName()), "Text");
		EXPECT_EQ(static_cast<const ui::Text*>(chipText[0])->getText(), "Keys ?");
		scene.drawingSettings.showKeyGuide = false;
		frame();
		EXPECT_EQ(shown("key_guide"), nullptr) << "collapsed, the setting off";
		EXPECT_EQ(shown("key_guide_chip"), nullptr) << "collapsed, the setting off";
	}

	// The panel's rows are the content's: a key cap for each key, dim text for the words among them, a glyph for each
	// legend row, and the foot; built again when the tool or the settings change while it is open.
	TEST_F(SceneLayout, TheKeyGuideBuildsItsRowsFromItsContent) {
		HudScene& scene = openHud([](HudScene& opened) {
			opened.drawingSettings = {.angleSteps = true, .commonLengths = true, .showKeyGuide = true};
			opened.drawing().activateWallTool();
		});
		frame();
		const auto expectBuiltFrom = [this](const KeyGuideContent& content, const std::string& when) {
			const ui::Component* keys = shown("key_guide_keys");
			ASSERT_NE(keys, nullptr) << when;
			const std::vector<const ui::Component*> keyCells = childrenOf(*keys);
			ASSERT_EQ(keyCells.size(), content.keyRows.size() * 2) << when;
			for (std::size_t row = 0; row < content.keyRows.size(); ++row) {
				const std::vector<const ui::Component*> parts = childrenOf(*keyCells[row * 2]);
				ASSERT_EQ(parts.size(), content.keyRows[row].keys.size()) << when << ", row " << row;
				for (std::size_t index = 0; index < parts.size(); ++index) {
					const KeyGuidePart& part = content.keyRows[row].keys[index];
					EXPECT_EQ(std::string_view(parts[index]->typeName()), part.chip ? "KeyCap" : "Text") << when << ", " << part.text;
				}
				ASSERT_EQ(std::string_view(keyCells[(row * 2) + 1]->typeName()), "Text") << when << ", row " << row;
				EXPECT_EQ(static_cast<const ui::Text*>(keyCells[(row * 2) + 1])->getText(), content.keyRows[row].action) << when;
			}
			const ui::Component* legend = shown("key_guide_legend");
			std::size_t			 glyphs = 0;
			if (legend != nullptr) {
				for (const ui::Component* child : childrenOf(*legend)) {
					glyphs += std::string_view(child->typeName()) == "LegendGlyphView" ? 1 : 0;
				}
			}
			EXPECT_EQ(glyphs, content.legendRows.size()) << when;
			const ui::Component* foot = shown("key_guide_foot");
			ASSERT_NE(foot, nullptr) << when;
			EXPECT_EQ(childrenOf(*foot).size(), content.foot.size()) << when;
		};
		expectBuiltFrom(keyGuideContent(ToolKind::Wall, scene.drawingSettings), "the wall tool");
		EXPECT_FALSE(keyGuideContent(ToolKind::Wall, scene.drawingSettings).legendRows.empty());

		scene.drawing().activateFoundationTool();
		frame();
		expectBuiltFrom(keyGuideContent(ToolKind::Foundation, scene.drawingSettings), "the Freeform tool");

		scene.drawingSettings = everyHelperOn();
		frame();
		expectBuiltFrom(keyGuideContent(ToolKind::Foundation, scene.drawingSettings), "every helper on");

		scene.drawingSettings = {.showKeyGuide = true};
		frame();
		expectBuiltFrom(keyGuideContent(ToolKind::Foundation, scene.drawingSettings), "every helper off");
		EXPECT_EQ(shown("key_guide_legend"), nullptr);
	}

	// Collapsed under one tool it stays collapsed under the next, and a click on the chip opens it.
	TEST_F(SceneLayout, TheKeyGuideStaysCollapsedAcrossToolsAndItsChipOpensIt) {
		HudScene& scene = openHud([](HudScene& opened) { opened.drawing().activateRectangleTool(); });
		scene.drawing().toggleKeyGuide();
		frame();
		ASSERT_NE(shown("key_guide_chip"), nullptr);
		const construction::FoundationId floor = commitFloor(scene.drawing());
		for (const DrawingToolOn& tool : everyDrawingTool()) {
			tool.activate(scene.drawing(), floor);
			frame();
			EXPECT_NE(shown("key_guide_chip"), nullptr) << tool.name;
			EXPECT_EQ(shown("key_guide_panel"), nullptr) << tool.name;
		}
		scene.drawing().deactivate();
		scene.drawing().activateWallTool();
		frame();
		EXPECT_NE(shown("key_guide_chip"), nullptr) << "after the tool went off and on";
		press("key_guide_chip");
		EXPECT_FALSE(scene.drawing().keyGuideCollapsed());
		frame();
		EXPECT_NE(shown("key_guide_panel"), nullptr);
		EXPECT_EQ(shown("key_guide_chip"), nullptr);
	}

	TEST_F(SceneLayout, TheKeyGuideStartsOpenInANewGameScene) {
		HudScene& scene = openHud([](HudScene& opened) { opened.drawing().activateWallTool(); });
		scene.drawing().toggleKeyGuide();
		frame();
		ASSERT_NE(shown("key_guide_chip"), nullptr);
		go(scenes::MainMenu.name);
		go(kHudScene);
		auto& again = *static_cast<HudScene*>(app->scenes().top());
		again.drawing().activateWallTool();
		frame();
		EXPECT_NE(shown("key_guide_panel"), nullptr);
		EXPECT_EQ(shown("key_guide_chip"), nullptr);
	}

	// The routing GameScene gives a left release: on the panel it is the interface's, so the tool places nothing; just
	// above it, it is the world's, and the tool places the point.
	TEST_F(SceneLayout, AClickOnTheKeyGuideMissesTheToolAndOneJustAboveItPlacesAPoint) {
		HudScene& scene = openHud([](HudScene& opened) { opened.drawing().activateFoundationTool(); });
		frame();
		const ui::Component* panel = shown("key_guide_panel");
		ASSERT_NE(panel, nullptr);
		const ui::Context&	   context = app->scenes().top()->ui();
		const float			   scale = app->uiLayoutScale();
		const Foundation::Vec2 window = context.getCanvas() * scale;
		const Foundation::Rect bounds = panel->screenBounds();
		const auto			   releaseAt = [&](glm::vec2 point) {
			const PointerTarget target =
				pointerTarget(point, static_cast<int>(window.x), static_cast<int>(window.y), scale, [&context](glm::vec2 uiPoint) {
					return context.hitTest(uiPoint).valid();
				});
			return drawingPointerAction(
				PointerInput::Release,
				foundation::MouseButton::Left,
				target,
				true,
				scene.drawing().activeTool(),
				scene.drawing().hasPendingPress()
			);
		};
		constexpr float kAbovePanel = 4.0F; // ui px over its top edge
		const glm::vec2 inside{(bounds.x + (bounds.width * 0.5F)) * scale, (bounds.y + (bounds.height * 0.5F)) * scale};
		const glm::vec2 above{inside.x, (bounds.y - kAbovePanel) * scale};
		EXPECT_NE(releaseAt(inside), PointerAction::DeliverClick);
		ASSERT_EQ(releaseAt(above), PointerAction::DeliverClick);
		scene.drawing().handleMouseMove(above.x, above.y, static_cast<int>(window.x), static_cast<int>(window.y), false);
		EXPECT_TRUE(scene.drawing().handleClick(above.x, above.y, static_cast<int>(window.x), static_cast<int>(window.y), false));
		EXPECT_EQ(scene.drawing().status().pointCount, 1);
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
